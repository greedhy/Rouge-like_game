// 测试链：实际FireBolt ASC激活/Notify -> 运行时Forked替换 -> 原class真实PM回执 -> 独立InputDamage。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityNativeFireBoltProfileTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityForkedPropagationNodes.h"
#include "Actor/MyProjectile.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "PureMass/PureMassSubsystem.h"
#include "Engine/LatentActionManager.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include "UObject/UObjectIterator.h"
#include "UObject/StrongObjectPtr.h"
#include "test_25d/test_25d.h"
#include "Misc/AutomationTest.h"

namespace AbilityNativeForkedPropagationTests
{
    /** 生产配方替换同一真实投递轴；不直接调用伤害，也不另授予一个完整GA。 */
    FAbilityRuntimeRecipeContribution Contribution(UObject* Outer)
    {
        FAbilityRuntimeRecipeContribution Result;
        Result.SourceId = TEXT("Rune.NativeForkedFire");
        FAbilityRuntimeRecipePatch Patch;
        Patch.Operation = EAbilityRecipePatchOperation::ReplaceNode;
        Patch.TargetTypeId = TEXT("Delivery.ProjectileAwait");
        Patch.ReplacementTypeId = TEXT("Propagation.Forked");
        auto* Config = NewObject<UAbilityForkedPropagationNodeConfig>(Outer);
        Config->bUseNativeLaunchParameters = true;
        Config->MaxTargets = 3;
        Config->MaxDepth = 1;
        Config->BranchesPerTarget = 2;
        Config->JumpDelay = .1f;
        Config->JumpRadius = 450.f;
        Config->DamageFalloff = .5f;
        Patch.ReplacementConfig = Config;
        Result.Patches.Add(Patch);
        return Result;
    }

    TArray<AMyProjectile*> Shots(UWorld* World)
    {
        TArray<AMyProjectile*> Result;
        for (TActorIterator<AMyProjectile> It(World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed())
            {
                Result.Add(*It);
            }
        }
        return Result;
    }

    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        FGameplayAbilitySpecHandle Handle;
        UAbilityNativeFireBoltProfileTest* Ability = nullptr;
        FFixture()
        {
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityNativeFireBoltProfileTest::StaticClass(), 1));
            Ability = CastChecked<UAbilityNativeFireBoltProfileTest>(
                ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
            if (!Target->IsActorInitialized())
            {
                Target->InitializeComponents();
                Target->PostInitializeComponents();
            }
            Target->SetActorLocation(FVector(600, -80, 0));
            Target->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
            Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),
                                                                         100.f);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        }
        ~FFixture()
        {
            World->SetBegunPlay(false);
            ASC->CancelAllAbilities();
            ASC->ClearAllAbilities();
        }
        void Gear()
        {
            auto* Effect = NewObject<UUniqueEquipmentGameplayEffect>(World);
            Effect->ProjectileRule.AbilityTag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
            Effect->ProjectileRule.ExtraProjectiles = FScalableFloat(1.f);
            Effect->ProjectileRule.DamageMultiplier = FScalableFloat(.5f);
            FGameplayEffectSpec Spec(Effect, ASC->MakeEffectContext(), 1.f);
            ASC->ApplyGameplayEffectSpecToSelf(Spec);
        }
        UAbilityGraphDefinition* Compile(UClass* ProjectileClass, FString& Error, int32 MaxInvocations = 0)
        {
            Ability->ConfigureNativePropagationClass(ProjectileClass);
            auto* Base = Ability->BuildRuntimeBaseGraph(World, Error);
            if (Base && MaxInvocations > 0)
            {
                // 预算属于待编译的私有输入；已发布配方必须保持封印，不能在Start前临时修改。
                Base->Budget.MaxInvocations = MaxInvocations;
            }
            UAbilityGraphDefinition* Compiled = nullptr;
            return Base && AbilityRuntimeRecipes::Compile(World, Base, {Contribution(World)}, Compiled, Error)
                       ? Compiled
                       : nullptr;
        }
        bool Install(UClass* ProjectileClass, FString& Error)
        {
            Ability->ConfigureNativePropagationClass(ProjectileClass);
            auto* Base = Ability->BuildRuntimeBaseGraph(World, Error);
            auto* Recipes = NewObject<UAbilityRuntimeRecipeComponent>(Source);
            Source->AddInstanceComponent(Recipes);
            Recipes->RegisterComponent();
            return Base && Recipes->RegisterBase(FMyGameplayTags::Get().Abilities_Fire_FireBolt, Base, Error) &&
                   Recipes->SetContribution(FMyGameplayTags::Get().Abilities_Fire_FireBolt, Contribution(World), Error);
        }
        bool Fire(FString& Error)
        {
            if (!Install(AMyProjectile::StaticClass(), Error) || !ASC->TryActivateAbility(Handle))
            {
                return false;
            }
            Ability->SpawnProjectiles(FVector(300, 0, 0), {}, false, 0.f, nullptr);
            World->SetBegunPlay(true);
            return true;
        }
        int32 Mass(FVector Position, float Health)
        {
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = Health;
            Type->XPReward = 0;
            Type->Speed = 0.f;
            return Sim->SpawnMonster(Type, Position);
        }
        float MassHealth(int32 Id) const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View) ? View.Health : -1.f;
        }
        float ActorHealth() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
        int32 Roots() const
        {
            return World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount();
        }
        /** 只推进原PM/延迟和图所属世界；调用者可在物理边界、图Advance之前检查真实GE。 */
        void Physical(TSet<TWeakObjectPtr<AMyProjectile>>& Seen)
        {
            ++GFrameCounter;
            World->GetLatentActionManager().BeginFrame();
            World->GetLatentActionManager().ProcessLatentActions(nullptr, .01f);
            World->GetTimerManager().Tick(.01f);
            for (auto* Shot : Shots(World))
            {
                Seen.Add(Shot);
                if (!Shot->IsActorInitialized())
                {
                    Shot->InitializeComponents();
                    Shot->PostInitializeComponents();
                }
                if (!Shot->HasActorBegunPlay())
                {
                    Shot->DispatchBeginPlay();
                }
                Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
            }
        }
        void Step(TSet<TWeakObjectPtr<AMyProjectile>>& Seen)
        {
            Physical(Seen);
            World->GetSubsystem<UAbilityGraphWorldSubsystem>()->Tick(.01f);
        }
    };
} // namespace AbilityNativeForkedPropagationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNativeForkedNotifyGear, "test_25d.DynamicMage.NativeForked.NotifyPreservesNativeClassAndOneEquipmentPreparation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeForkedNotifyGear::RunTest(const FString&)
{
    using namespace AbilityNativeForkedPropagationTests;
    auto* BP = LoadClass<AMyProjectile>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/BP_FireBolt.BP_FireBolt_C"));
    if (!TestNotNull(TEXT("Actual native FireBolt Blueprint class loads"), BP))
    {
        return false;
    }
    for (UClass* Class : {AMyProjectile::StaticClass(), BP})
    {
        FFixture F;
        F.Gear();
        FString Error;
        if (!TestTrue(TEXT("Original native frontend accepts the runtime Forked replacement"), F.Install(Class, Error)))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("Original ASC activation succeeds"), F.ASC->TryActivateAbility(F.Handle));
        TestEqual(TEXT("Forked owns no physical launch before the actual original Notify"), Shots(F.World).Num(), 0);
        const FVector Aim(3000, 200, 100);
        F.Ability->SpawnProjectiles(Aim, {}, false, 0.f, nullptr);
        const auto Launched = Shots(F.World);
        TestEqual(TEXT("One level arrow plus one equipment arrow keeps the original native count"), Launched.Num(), 2);
        for (auto* Shot : Launched)
        {
            auto* Defaults = Class->GetDefaultObject<AMyProjectile>();
            TestEqual(TEXT("Every fork retains the actual native class"), Shot->GetClass(), Class);
            TestEqual(TEXT("Native class speed is not replaced by prototype defaults"),
                      Shot->ProjectileMovement->InitialSpeed, Defaults->ProjectileMovement->InitialSpeed);
            TestEqual(TEXT("Native collision shape is preserved"), Shot->Sphere->GetScaledSphereRadius(),
                      Defaults->Sphere->GetScaledSphereRadius());
            TestEqual(TEXT("Equipment primary damage preparation runs exactly once"),
                      Shot->DamageEffectParams.BaseDamage, 12.5f);
            TestEqual(TEXT("Equipment DOT preparation runs exactly once"), Shot->DamageEffectParams.DebuffDamage, 4.f);
            TestTrue(TEXT("Native point aim is retained before any BFS contact"),
                     Shot->ProjectileMovement->HomingTargetComponent.IsValid() &&
                         Shot->ProjectileMovement->HomingTargetComponent->GetComponentLocation().Equals(Aim));
            TestTrue(TEXT("Original projectile uses exactly one external payload owner"),
                     Shot->IsGraphDeliveryManaged());
        }
        TestEqual(TEXT("One original Commit pays the skill cost once"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
        TestEqual(TEXT("Both fork roots are calls inside one original graph execution"), F.Roots(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeForkedLethalMixed,
                                 "test_25d.DynamicMage.NativeForked.LethalMassBranchesToActorAndMassAfterRealDelay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeForkedLethalMixed::RunTest(const FString&)
{
    using namespace AbilityNativeForkedPropagationTests;
    FFixture F;
    const int32 A = F.Mass(FVector(300, 0, 0), 10.f);
    const int32 C = F.Mass(FVector(600, 80, 0), 100.f);
    FString Error;
    if (!TestTrue(TEXT("Actual FireBolt activates and sends its original Notify"), F.Fire(Error)))
    {
        AddError(Error);
        return false;
    }
    TSet<TWeakObjectPtr<AMyProjectile>> Seen;
    int32 FirstHit = INDEX_NONE;
    int32 SecondLaunch = INDEX_NONE;
    bool bActorPayloadInsideContact = false;
    bool bNoImmediateBranch = false;
    for (int32 Frame = 0; Frame < 1000; ++Frame)
    {
        const bool bFirstAlive = F.MassHealth(A) > 0.f;
        const float BeforeActor = F.ActorHealth();
        F.Physical(Seen);
        if (bFirstAlive && F.MassHealth(A) < 0.f)
        {
            FirstHit = Frame;
            bNoImmediateBranch = Shots(F.World).IsEmpty();
        }
        if (FirstHit != INDEX_NONE && SecondLaunch == INDEX_NONE && Seen.Num() >= 2)
        {
            SecondLaunch = Frame;
        }
        bActorPayloadInsideContact |= F.ActorHealth() < BeforeActor;
        F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->Tick(.01f);
        if (FirstHit != INDEX_NONE && F.Roots() == 0)
        {
            break;
        }
    }
    TestTrue(TEXT("Actual first Mass contact is lethal"), FirstHit != INDEX_NONE);
    TestTrue(TEXT("No branch is conjured during the first contact itself"), bNoImmediateBranch);
    TestTrue(TEXT("The first real sibling flight waits the explicit short delay"),
             FirstHit != INDEX_NONE && SecondLaunch - FirstHit >= 9);
    TestTrue(TEXT("Native Actor damage is consumed at the physical contact boundary"), bActorPayloadInsideContact);
    TestEqual(TEXT("One actual arrow reaches each admitted original life"), Seen.Num(), 3);
    TestEqual(TEXT("Actor sibling consumes depth-one falloff once"), F.ActorHealth(), 87.5f);
    TestEqual(TEXT("Mass sibling consumes the same depth-one falloff once"), F.MassHealth(C), 87.5f);
    TestEqual(TEXT("All finite native branches release the original graph"), F.Roots(), 0);
    TestEqual(TEXT("No native projectile remains after terminal contacts"), Shots(F.World).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeForkedBudgetAndSourceRetirement,
                                 "test_25d.DynamicMage.NativeForked.SharedBudgetAndSourceRetirementCloseAllDeliveries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeForkedBudgetAndSourceRetirement::RunTest(const FString&)
{
    using namespace AbilityNativeForkedPropagationTests;
    {
        FFixture F;
        F.Gear();
        F.Mass(FVector(300, 0, 0), 10.f);
        FString Error;
        auto* Definition = F.Compile(AMyProjectile::StaticClass(), Error, 16);
        if (!TestNotNull(TEXT("Real native graph compiles before the shared-budget boundary"), Definition))
        {
            AddError(Error);
            return false;
        }
        FAbilityNodeInput Payload;
        Payload.SourceToken = F.SourceToken;
        Payload.Origin = FVector(60, 0, 0);
        Payload.Direction = FVector::ForwardVector;
        Payload.EventCenter = FVector(300, 0, 0);
        Payload.DamageParams.BaseDamage = 25.f;
        Payload.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Fire;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        if (!TestTrue(TEXT("One root admits both equipment-native deliveries"),
                      Run->StartWithInput(Definition, Payload, {}, Error)))
        {
            AddError(Error);
            return false;
        }
        TestEqual(TEXT("Both initial deliveries belong to the same root"), Shots(F.World).Num(), 2);
        int32 Remaining = 0;
        while (Remaining < 32 && Run->ReservePhysicalSpawn())
        {
            ++Remaining;
        }
        TestEqual(TEXT("Two actual launches leave fourteen root reservations"), Remaining, 14);
        TSet<TWeakObjectPtr<AMyProjectile>> Seen;
        F.World->SetBegunPlay(true);
        for (int32 Frame = 0; Frame < 600 && !Run->IsFinished(); ++Frame)
        {
            F.Physical(Seen);
            Run->Advance(.01f);
        }
        TestTrue(TEXT("A later child cannot reset the exhausted root budget"), Run->IsFinished());
        TestEqual(TEXT("The root reports the physical budget failure explicitly"), Run->GetEndReason(),
                  EAbilityGraphEndReason::BudgetExceeded);
        TestEqual(TEXT("No unreserved third arrow was created"), Seen.Num(), 2);
        TestEqual(TEXT("Failure releases every original physical lease"), Run->GetActivePhysicalSpawnCount(), 0);
        TestEqual(TEXT("Failure closes every owned native projectile"), Shots(F.World).Num(), 0);
    }
    {
        FFixture F;
        const int32 Id = F.Mass(FVector(300, 0, 0), 100.f);
        FString Error;
        if (!TestTrue(TEXT("Source retirement case reaches the real Notify"), F.Fire(Error)))
        {
            AddError(Error);
            return false;
        }
        F.Source->EnableDungeonPooling();
        F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->Tick(.01f);
        TestEqual(TEXT("Source new life immediately closes the old root"), F.Roots(), 0);
        TestEqual(TEXT("Old native deliveries are removed before a late physical hit"), Shots(F.World).Num(), 0);
        TestEqual(TEXT("New source life never authorizes the old arrow payload"), F.MassHealth(Id), 100.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeForkedOldLifeAndReceipt,
                                 "test_25d.DynamicMage.NativeForked.OldReceiptAndQueuedActorLifeCannotBeReused",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeForkedOldLifeAndReceipt::RunTest(const FString&)
{
    using namespace AbilityNativeForkedPropagationTests;
    FFixture F;
    const int32 A = F.Mass(FVector(300, 0, 0), 10.f);
    F.Mass(FVector(600, 80, 0), 100.f);
    FString Error;
    if (!TestTrue(TEXT("Original native root reaches Notify before receipt/life changes"), F.Fire(Error)))
    {
        AddError(Error);
        return false;
    }
    const auto Initial = Shots(F.World);
    UAbilityDeliveryInstance* InitialDelivery = nullptr;
    for (TObjectIterator<UAbilityDeliveryInstance> It; It; ++It)
    {
        if (Initial.Num() == 1 && It->GetProjectile() == Initial[0])
        {
            InitialDelivery = *It;
            break;
        }
    }
    if (!TestNotNull(TEXT("First actual projectile has a real managed delivery owner"), InitialDelivery))
    {
        return false;
    }
    const TStrongObjectPtr<UAbilityDeliveryInstance> KeepFirst(InitialDelivery);
    TSet<TWeakObjectPtr<AMyProjectile>> Seen;
    for (int32 Frame = 0; Frame < 600 && F.MassHealth(A) > 0.f; ++Frame)
    {
        F.Step(Seen);
    }
    if (!TestEqual(TEXT("First real contact is completed before manipulating later life"), F.MassHealth(A), -1.f))
    {
        return false;
    }
    const auto OldResult = InitialDelivery->GetResult();
    TestEqual(TEXT("The old receipt is a genuine physical Hit"), OldResult.Outcome, EAbilityDeliveryOutcome::Hit);
    const int32 ShotsBefore = Shots(F.World).Num();
    InitialDelivery->OnFinished.Broadcast(OldResult);
    InitialDelivery->OnFinished.Broadcast(OldResult);
    TestEqual(TEXT("Duplicate terminal receipts never schedule another immediate branch"), Shots(F.World).Num(),
              ShotsBefore);
    F.Target->EnableDungeonPooling();
    F.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    for (int32 Frame = 0; Frame < 1000 && F.Roots() > 0; ++Frame)
    {
        F.Step(Seen);
    }
    TestEqual(TEXT("Old queued Actor token never damages the new pool life"), F.ActorHealth(), 100.f);
    TestEqual(TEXT("Queued target-life failure closes the original root"), F.Roots(), 0);
    TestEqual(TEXT("Queued failure leaves no projectile owned by the old invocation"), Shots(F.World).Num(), 0);
    return true;
}
#endif
