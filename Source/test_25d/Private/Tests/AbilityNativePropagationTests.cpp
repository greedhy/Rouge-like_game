// 测试链：实际FireBolt基础图 -> Compile ReplaceNode -> 原Notify/一次装备准备 -> 真实逐跳原class火箭。
// 首击致死和混合目标仍使用原外部payload；测试不激活第二个GA，也不手工伪造命中。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityNativeFireBoltProfileTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "Actor/MyProjectile.h"
#include "Components/CapsuleComponent.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "PureMass/PureMassSubsystem.h"
#include "Engine/LatentActionManager.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include "test_25d/test_25d.h"

namespace AbilityNativePropagationTests
{
    FAbilityRuntimeRecipeContribution Contribution(UObject* Outer)
    {
        FAbilityRuntimeRecipeContribution Result;
        Result.SourceId = TEXT("Rune.NativeFirePropagation");
        FAbilityRuntimeRecipePatch Patch;
        Patch.Operation = EAbilityRecipePatchOperation::ReplaceNode;
        Patch.TargetTypeId = TEXT("Delivery.ProjectileAwait");
        Patch.ReplacementTypeId = TEXT("Propagation.Sequential");
        auto* Settings = NewObject<UAbilityPropagationNodeConfig>(Outer);
        Settings->bUseNativeLaunchParameters = true;
        Settings->MaxTargets = 3;
        Settings->JumpDelay = .1f;
        Settings->JumpRadius = 450.f;
        Settings->DamageFalloff = .5f;
        Patch.ReplacementConfig = Settings;
        Result.Patches.Add(Patch);
        return Result;
    }

    void Initialize(AMyProjectile* Shot)
    {
        if (!Shot->IsActorInitialized())
        {
            Shot->InitializeComponents();
            Shot->PostInitializeComponents();
        }
        if (!Shot->HasActorBegunPlay())
        {
            Shot->DispatchBeginPlay();
        }
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
        bool Install(UClass* Class, FString& Error)
        {
            Ability->ConfigureNativePropagationClass(Class);
            auto* Base = Ability->BuildRuntimeBaseGraph(World, Error);
            UAbilityGraphDefinition* Compiled = nullptr;
            const auto Patch = Contribution(World);
            if (!AbilityRuntimeRecipes::Compile(World, Base, {Patch}, Compiled, Error))
            {
                return false;
            }
            auto* Recipes = NewObject<UAbilityRuntimeRecipeComponent>(Source);
            Source->AddInstanceComponent(Recipes);
            Recipes->RegisterComponent();
            return Recipes->RegisterBase(FMyGameplayTags::Get().Abilities_Fire_FireBolt, Base, Error) &&
                   Recipes->SetContribution(FMyGameplayTags::Get().Abilities_Fire_FireBolt, Patch, Error);
        }
    };

    /** 推进真实BP Delay/原移动组件；先观察接触，再推进同一图时钟，区分接触边界和下一Advance。 */
    void PhysicalFrame(UWorld* World, TSet<TWeakObjectPtr<AMyProjectile>>& Seen)
    {
        ++GFrameCounter;
        World->GetLatentActionManager().BeginFrame();
        World->GetLatentActionManager().ProcessLatentActions(nullptr, .01f);
        World->GetTimerManager().Tick(.01f);
        for (auto* Shot : Shots(World))
        {
            Seen.Add(Shot);
            Initialize(Shot);
            Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
        }
    }
} // namespace AbilityNativePropagationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePropagationNotifyFrame,
                                 "test_25d.DynamicMage.NativePropagation.RealNotifyKeepsNativeLaunchFrame",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativePropagationNotifyFrame::RunTest(const FString&)
{
    using namespace AbilityNativePropagationTests;
    auto* BP = LoadClass<AMyProjectile>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/BP_FireBolt.BP_FireBolt_C"));
    if (!TestNotNull(TEXT("Real original BP projectile loads"), BP))
    {
        return false;
    }
    for (UClass* Class : {AMyProjectile::StaticClass(), BP})
    {
        FFixture F;
        F.Target->Destroy();
        F.Gear();
        FString Error;
        if (!TestTrue(TEXT("Actual FireBolt base graph compiles delivery replacement"), F.Install(Class, Error)))
        {
            AddInfo(Error);
            return false;
        }
        TestTrue(TEXT("Actual ASC ability activates"), F.ASC->TryActivateAbility(F.Handle));
        TestEqual(TEXT("No physical launch before real original Notify"), Shots(F.World).Num(), 0);
        const FVector PointAim(3000, 200, 100);
        F.Ability->SpawnProjectiles(PointAim, {}, false, 0.f, nullptr);
        const auto Launched = Shots(F.World);
        if (!TestEqual(TEXT("Level1 plus one equipment extra produces two actual native arrows"), Launched.Num(), 2))
        {
            continue;
        }
        auto* Defaults = Class->GetDefaultObject<AMyProjectile>();
        for (auto* Shot : Launched)
        {
            Initialize(Shot);
            TestEqual(TEXT("Replacement keeps original actual projectile class"), Shot->GetClass(), Class);
            TestEqual(TEXT("Replacement reads original native initial speed"), Shot->ProjectileMovement->InitialSpeed,
                      Defaults->ProjectileMovement->InitialSpeed);
            TestEqual(TEXT("Replacement reads native collision radius"), Shot->Sphere->GetScaledSphereRadius(),
                      Defaults->Sphere->GetScaledSphereRadius());
            TestEqual(TEXT("Equipment damage is prepared exactly once"), Shot->DamageEffectParams.BaseDamage, 12.5f);
            TestEqual(TEXT("Equipment DOT preparation is not repeated"), Shot->DamageEffectParams.DebuffDamage, 4.f);
            const auto Aim = Shot->ProjectileMovement->HomingTargetComponent;
            TestTrue(TEXT("Point aim remains configured without a live Actor target"),
                     Aim.IsValid() && Aim->GetComponentLocation().Equals(PointAim));
            TestTrue(TEXT("Each native arrow remains externally managed"), Shot->IsGraphDeliveryManaged());
        }
        TestEqual(TEXT("Notify commits the original ability cost once"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
        TestEqual(TEXT("Both propagation branches belong to one original world execution"),
                  F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePropagationMixedPayload,
                                 "test_25d.DynamicMage.NativePropagation.LethalMassActorMassUsesRealExternalPayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativePropagationMixedPayload::RunTest(const FString&)
{
    using namespace AbilityNativePropagationTests;
    FFixture F;
    F.Target->SetActorLocation(FVector(600, 0, 0));
    if (!F.Target->IsActorInitialized())
    {
        F.Target->InitializeComponents();
        F.Target->PostInitializeComponents();
    }
    F.Target->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    F.Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Fragile = NewObject<UPureMassMonsterType>(F.World);
    Fragile->Health = 10.f;
    Fragile->XPReward = 0;
    auto* Healthy = NewObject<UPureMassMonsterType>(F.World);
    Healthy->Health = 100.f;
    Healthy->XPReward = 0;
    const int32 A = Mass->SpawnMonster(Fragile, FVector(300, 0, 0));
    const int32 C = Mass->SpawnMonster(Healthy, FVector(900, 0, 0));
    FString Error;
    if (!TestTrue(TEXT("Native FireBolt delivery is replaced in the real default graph"),
                  F.Install(AMyProjectile::StaticClass(), Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("Real caster activates"), F.ASC->TryActivateAbility(F.Handle));
    F.Ability->SpawnProjectiles(FVector(300, 0, 0), {}, false, 0.f, nullptr);
    TestEqual(TEXT("One original native arrow starts this chain"), Shots(F.World).Num(), 1);
    F.World->SetBegunPlay(true);
    TSet<TWeakObjectPtr<AMyProjectile>> Seen;
    bool bLethalAtPhysicalBoundary = false;
    bool bActorAtPhysicalBoundary = false;
    bool bObservedRealDelay = false;
    int32 FirstHitFrame = -1;
    int32 SecondSpawnFrame = -1;
    FPureMassView View;
    for (int32 Frame = 0; Frame < 600; ++Frame)
    {
        const bool bFirstAliveBeforePhysical = Mass->Resolve(A, View);
        const float ActorHealthBeforePhysical = TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        PhysicalFrame(F.World, Seen);
        if (FirstHitFrame < 0 && bFirstAliveBeforePhysical && !Mass->Resolve(A, View))
        {
            FirstHitFrame = Frame;
            bLethalAtPhysicalBoundary = true;
            bObservedRealDelay = Shots(F.World).IsEmpty();
        }
        if (FirstHitFrame >= 0 && SecondSpawnFrame < 0 && Seen.Num() >= 2)
        {
            SecondSpawnFrame = Frame;
        }
        bActorAtPhysicalBoundary |=
            TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()) < ActorHealthBeforePhysical;
        // 前面的观测早于本帧图时钟；外部Hit→ResolveImpact→InputDamage必须已在真实接触边界消费。
        F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->Tick(.01f);
        if (FirstHitFrame >= 0 && F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount() == 0)
        {
            break;
        }
    }
    TestTrue(TEXT("First real Mass impact kills before the next graph Advance"), bLethalAtPhysicalBoundary);
    TestTrue(TEXT("First lethal impact does not immediately conjure another projectile"), bObservedRealDelay);
    TestTrue(TEXT("Next native projectile starts only after the configured short delay"),
             FirstHitFrame >= 0 && SecondSpawnFrame - FirstHitFrame >= 9);
    TestTrue(TEXT("Actor payload is consumed inside a physical contact boundary"), bActorAtPhysicalBoundary);
    TestEqual(TEXT("One physical native fire arrow per target life"), Seen.Num(), 3);
    TestEqual(TEXT("Actor receives one hop of cumulative falloff"),
              TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 87.5f);
    if (TestTrue(TEXT("Final Mass target remains alive"), Mass->Resolve(C, View)))
    {
        TestEqual(TEXT("Final external payload consumes two hop falloffs once"), View.Health, 93.75f);
    }
    TestEqual(TEXT("Finite mixed propagation completes the original root"),
              F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePropagationSharedPhysicalBudget,
                                 "test_25d.DynamicMage.NativePropagation.BranchesShareOriginalPhysicalBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativePropagationSharedPhysicalBudget::RunTest(const FString&)
{
    using namespace AbilityNativePropagationTests;
    FFixture F;
    F.Target->Destroy();
    F.Gear();
    F.Ability->ConfigureNativePropagationClass(AMyProjectile::StaticClass());
    FString Error;
    auto* Base = F.Ability->BuildRuntimeBaseGraph(F.World, Error);
    if (Base)
    {
        // 测试预算必须在编译前进入基础定义；已封存的运行快照不能再由调用者改写。
        Base->Budget.MaxInvocations = 16;
    }
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!TestTrue(TEXT("Actual default graph compiles its replacement"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {Contribution(F.World)}, Compiled, Error)))
    {
        AddInfo(Error);
        return false;
    }
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 1000.f;
    Type->XPReward = 0;
    Mass->SpawnMonster(Type, FVector(300, 0, 0));
    Mass->SpawnMonster(Type, FVector(600, 0, 0));
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = FVector(60, 0, 0);
    Input.Direction = FVector::ForwardVector;
    Input.EventCenter = FVector(300, 0, 0);
    Input.DamageParams.BaseDamage = 25.f;
    Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Fire;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Compiled single root starts both actual branches"), Run->StartWithInput(Compiled, Input, {}, Error));
    TestEqual(TEXT("Both initial physical launches consume the same original root"), Shots(F.World).Num(), 2);
    int32 RemainingReservations = 0;
    while (RemainingReservations < 32 && Run->ReservePhysicalSpawn())
    {
        ++RemainingReservations;
    }
    TestEqual(TEXT("Two physical launches leave exactly fourteen cumulative root reservations"), RemainingReservations,
              14);
    TSet<TWeakObjectPtr<AMyProjectile>> Seen;
    F.World->SetBegunPlay(true);
    for (int32 Frame = 0; Frame < 200 && !Run->IsFinished(); ++Frame)
    {
        PhysicalFrame(F.World, Seen);
        Run->Advance(.01f);
    }
    TestTrue(TEXT("A child hop cannot replace or reset its exhausted root budget"), Run->IsFinished());
    TestEqual(TEXT("Neither branch creates an unreserved third physical projectile"), Seen.Num(), 2);
    TestFalse(TEXT("Completing or cancelling delivery does not refund cumulative root spawns"),
              Run->ReservePhysicalSpawn());
    Run->Cancel();
    return true;
}
#endif
