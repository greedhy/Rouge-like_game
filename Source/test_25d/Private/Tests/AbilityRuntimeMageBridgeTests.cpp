// 调用链：ASC PreActivate 冻结 -> 原 FireBolt 发射通知 -> 动态内存图 -> Mass 真实血量。
// 无动态修正的原发射路径由 Traditional.NativeAndGraphProjectileParity 独立回归。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityRuntimeMageBridgeTestTypes.h"
#include "AbilityGraphTestAbility.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Ability/MyArcaneNova.h"
#include "AbilitySystem/Ability/MyOrbitingFireBoltAbility.h"
#include "Player/StanderWeaponActionConfig.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "PureMass/PureMassSubsystem.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"
#include "Tests/DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"

namespace AbilityRuntimeMageBridgeTests
{
    /** 新图只在内存中创建；Query/Delay/Damage 均经生产注册表执行。 */
    UAbilityGraphDefinition* MakePayload(UObject* Outer, bool bDelayed)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Outer);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Query;
        Query.NodeId = FGuid::NewGuid();
        Query.TypeId = TEXT("Query.Shape");
        auto* Shape = NewObject<UAbilityQueryNodeConfig>(Graph);
        Shape->Query.Radius = 250.f;
        Query.Config = Shape;
        FAbilityGraphNode Damage;
        Damage.NodeId = FGuid::NewGuid();
        Damage.TypeId = TEXT("Effect.Damage");
        auto* Effect = NewObject<UAbilityDamageNodeConfig>(Graph);
        Effect->Damage = 17.f;
        Effect->DamagePerLevel = 0.f;
        Effect->DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Fire"));
        Damage.Config = Effect;
        Graph->Nodes = {Query, Damage};
        Graph->EntryNodeId = Query.NodeId;
        FAbilityGraphEdge Edge;
        Edge.From = Query.NodeId;
        Edge.To = Damage.NodeId;
        Edge.OutputPort = TEXT("TargetFound");
        Graph->Edges.Add(Edge);
        if (bDelayed)
        {
            FAbilityGraphNode Delay;
            Delay.NodeId = FGuid::NewGuid();
            Delay.TypeId = TEXT("Control.DelayOrigin");
            auto* Config = NewObject<UAbilityDelayNodeConfig>(Graph);
            Config->DelaySeconds = .25f;
            Delay.Config = Config;
            Graph->Nodes.Insert(Delay, 0);
            Edge.From = Delay.NodeId;
            Edge.To = Query.NodeId;
            Edge.OutputPort = TEXT("Completed");
            Graph->Edges.Add(Edge);
            Graph->EntryNodeId = Delay.NodeId;
        }
        return Graph;
    }

    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityRuntimeRecipeComponent* Recipes = nullptr;
        UPureMassSubsystem* Mass = nullptr;
        int32 Id = INDEX_NONE;
        FGameplayAbilitySpecHandle Handle;
        UAbilityRuntimeFireBoltBridgeTest* Ability = nullptr;
        FFixture()
        {
            Target->Destroy();
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            Recipes = NewObject<UAbilityRuntimeRecipeComponent>(Source);
            Source->AddInstanceComponent(Recipes);
            Recipes->RegisterComponent();
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 0;
            Id = Mass->SpawnMonster(Type, FVector(100, 0, 0));
            Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityRuntimeFireBoltBridgeTest::StaticClass(), 1));
            Ability = CastChecked<UAbilityRuntimeFireBoltBridgeTest>(
                ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        }
        ~FFixture()
        {
            ASC->CancelAllAbilities();
            ASC->ClearAllAbilities();
        }
        bool Install(FGameplayTag Tag, bool bDelayed, FString& Error)
        {
            if (!Recipes->RegisterBase(Tag, MakePayload(World, bDelayed), Error))
            {
                return false;
            }
            FAbilityRuntimeRecipeContribution Contribution;
            Contribution.SourceId = TEXT("BridgeTest");
            // 空补丁来源显式选择默认内存配方，不预制组合 GA。
            return Recipes->SetContribution(Tag, Contribution, Error);
        }
        float Health() const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : 0.f;
        }
        int32 ProjectileCount() const
        {
            int32 Count = 0;
            for (TActorIterator<AMyProjectile> It(World); It; ++It)
            {
                ++Count;
            }
            return Count;
        }
    };
} // namespace AbilityRuntimeMageBridgeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeFireBoltNotifyBridge,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFrontends.FireBoltNotifyUsesFrozenRecipe",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFireBoltNotifyBridge::RunTest(const FString&)
{
    AbilityRuntimeMageBridgeTests::FFixture F;
    FString Error;
    if (!TestTrue(TEXT("Install runtime-only payload"),
                  F.Install(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Fire.FireBolt")), false, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("Original FireBolt GAS frontend activates"), F.ASC->TryActivateAbility(F.Handle));
    TestNotNull(TEXT("Native frontend freezes recipe before notify"),
                F.Ability->GetRuntimeRecipeSnapshot().Graph.Get());
    F.Ability->SpawnProjectiles(FVector(100, 0, 0), {}, false, 0.f, nullptr);
    TestEqual(TEXT("Notify runs alternate graph's actual Mass payload"), F.Health(), 83.f);
    TestEqual(TEXT("Dynamic notify does not also invoke old projectile action"), F.ProjectileCount(), 0);
    F.Ability->SpawnProjectiles(FVector(100, 0, 0), {}, false, 0.f, nullptr);
    TestEqual(TEXT("Duplicate native release cannot start another recipe root or repeat payload"), F.Health(), 83.f);
    TestEqual(TEXT("Actual release commits its original cost exactly once"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeFireBoltIndependentBridge,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFrontends.FireBoltGraphSurvivesNormalEnd",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFireBoltIndependentBridge::RunTest(const FString&)
{
    AbilityRuntimeMageBridgeTests::FFixture F;
    FString Error;
    TestTrue(TEXT("Install delayed runtime-only payload"),
             F.Install(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Fire.FireBolt")), true, Error));
    TestTrue(TEXT("Original frontend starts"), F.ASC->TryActivateAbility(F.Handle));
    F.Ability->SpawnProjectiles(FVector(100, 0, 0), {}, false, 0.f, nullptr);
    auto* Owner = F.World->GetSubsystem<UAbilityGraphWorldSubsystem>();
    TestEqual(TEXT("Delayed graph is adopted by existing world owner"), Owner->GetActiveExecutionCount(), 1);
    F.Ability->EndAbility(F.Handle, F.ASC->AbilityActorInfo.Get(), F.Ability->GetCurrentActivationInfo(), true, false);
    TestEqual(TEXT("No early payload at normal frontend end"), F.Health(), 100.f);
    Owner->Tick(.26f);
    TestEqual(TEXT("Independent cast effect retains frozen graph after normal end"), F.Health(), 83.f);
    TestEqual(TEXT("Completed graph releases world ownership"), Owner->GetActiveExecutionCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeNativeFreezeError,
    "test_25d.UnifiedGA.DynamicMage.NativeFrontends.InvalidFrozenRecipeRejectsCommitAndNotify",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeNativeFreezeError::RunTest(const FString&)
{
    AbilityRuntimeMageBridgeTests::FFixture F;
    FString Error;
    TestTrue(TEXT("Install first identity"),
             F.Install(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Fire.FireBolt")), false, Error));
    TestTrue(TEXT("Install competing identity"),
             F.Install(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Prototype.Burst")), false, Error));
    F.ASC->TryActivateAbility(F.Handle);
    TestFalse(TEXT("Competing frozen identities have an explicit error"), F.Ability->GetRuntimeRecipeError().IsEmpty());
    TestFalse(TEXT("Invalid frozen recipe cannot commit"),
              F.Ability->CommitAbility(F.Handle, F.ASC->AbilityActorInfo.Get(), F.Ability->GetCurrentActivationInfo()));
    F.Ability->SpawnProjectiles(FVector(100, 0, 0), {}, false, 0.f, nullptr);
    TestEqual(TEXT("Late notify cannot fall back to unmodified damage"), F.ProjectileCount(), 0);
    TestEqual(TEXT("Invalid recipe has no payload"), F.Health(), 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePrototypeFrontendBridge,
                                 "test_25d.UnifiedGA.DynamicMage.Frontend.PrototypeVersionsUseFrozenRecipe",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimePrototypeFrontendBridge::RunTest(const FString&)
{
    const TCHAR* Paths[] = {
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Burst.GA_Prototype_Burst_C"),
        TEXT("/Game/PureMass/Abilities/GA_Prototype_Burst.GA_Prototype_Burst_C")};
    for (const TCHAR* Path : Paths)
    {
        AbilityRuntimeMageBridgeTests::FFixture F;
        auto* Class = LoadClass<UPrototypeAbility>(nullptr, Path);
        if (!TestNotNull(Path, Class))
        {
            continue;
        }
        FString Error;
        TestTrue(TEXT("Install delayed runtime graph for original identity"),
                 F.Install(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Prototype.Burst")), true, Error));
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
        F.ASC->AbilityActorInfo->PlayerController = F.World->SpawnActor<AAbilityGraphRemoteTestController>();
        const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(Class, 1));
        auto* Ability = CastChecked<UPrototypeAbility>(F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        FPredictionKey Key;
        Key.Current = 217;
        F.ASC->CallServerTryActivateAbility(Handle, false, Key);
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Item = INDEX_NONE;
        Hit->HitResult.ImpactPoint = FVector(100, 0, 0);
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        // 实际 Burst 资产可配置为自身释放；只有仍等待的原前端才提交真实 TargetData。
        if (Ability->IsActive())
        {
            F.ASC->ServerSetReplicatedTargetData(Handle,
                                                 Ability->GetCurrentActivationInfo().GetActivationPredictionKey(), Data,
                                                 FGameplayTag(), FPredictionKey());
        }
        TestEqual(FString(Path) + TEXT(" dynamic delay prevents early hit"), F.Health(), 100.f);
        auto* Owner = F.World->GetSubsystem<UAbilityGraphWorldSubsystem>();
        Owner->Tick(.26f);
        TestEqual(FString(Path) + TEXT(" actual runtime replacement reaches Mass payload"), F.Health(), 83.f);
        TestEqual(FString(Path) + TEXT(" preserves original root cost once"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                  1000.f - Ability->Prototype->ManaCost);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeNovaOrbitFrontendBridge,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFrontends.NovaAndOrbitUseFrozenRecipe",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeNovaOrbitFrontendBridge::RunTest(const FString&)
{
    const TCHAR* Paths[] = {
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneNova/GA_ArcaneNova.GA_ArcaneNova_C"),
        TEXT(
            "/Game/BluePrints/AbilitySystem/Player/Ability/PassiveSpell/OrbitingFireBolt/GA_OrbitingFireBolt.GA_OrbitingFireBolt_C")};
    const TCHAR* Tags[] = {TEXT("Abilities.Arcane.ArcaneNova"), TEXT("Abilities.Passive.OrbitingFireBolt")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Paths); ++Index)
    {
        AbilityRuntimeMageBridgeTests::FFixture F;
        auto* Class = LoadClass<UMyGameplayAbility>(nullptr, Paths[Index]);
        if (!TestNotNull(Paths[Index], Class))
        {
            continue;
        }
        FString Error;
        TestTrue(TEXT("Install generated replacement for original native identity"),
                 F.Install(FGameplayTag::RequestGameplayTag(Tags[Index]), false, Error));
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
        const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(Class, 1));
        TestTrue(TEXT("Actual native frontend activates"), F.ASC->TryActivateAbility(Handle));
        TestEqual(FString(Paths[Index]) + TEXT(" runs replacement payload at original action time"), F.Health(), 83.f);
        F.ASC->CancelAbilityHandle(Handle);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeShardBeamNotifyBridge,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFrontends.ShardsAndBeamKeepNotifyGate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeShardBeamNotifyBridge::RunTest(const FString&)
{
    for (const bool bShards : {true, false})
    {
        AbilityRuntimeMageBridgeTests::FFixture F;
        auto* Config = LoadObject<UStanderWeaponActionConfig>(
            nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_Attack1.DA_Attack1"));
        auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander"));
        if (!TestNotNull(TEXT("Compatible montage configuration"), Config) ||
            !TestNotNull(TEXT("Compatible mesh for real production animation task"), Mesh))
        {
            return false;
        }
        F.Source->GetMesh()->SetSkeletalMeshAsset(Mesh);
        F.Source->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        F.Source->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
        F.ASC->RefreshAbilityActorInfo();
        F.ASC->AbilityActorInfo->PlayerController = F.World->SpawnActor<AAbilityGraphRemoteTestController>();
        const auto Tag = FGameplayTag::RequestGameplayTag(bShards ? TEXT("Abilities.Arcane.ArcaneShards")
                                                                  : TEXT("Abilities.Thunder.Electrocute"));
        FString Error;
        TestTrue(TEXT("Install typed replacement before original activation"), F.Install(Tag, false, Error));
        const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(
            bShards ? UAbilityRuntimeShardBridgeTest::StaticClass() : UAbilityRuntimeBeamBridgeTest::StaticClass(), 1));
        auto* Ability = F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance();
        if (bShards)
        {
            CastChecked<UAbilityRuntimeShardBridgeTest>(Ability)->ShardMontage = Config->Montage;
        }
        else
        {
            CastChecked<UAbilityRuntimeBeamBridgeTest>(Ability)->ConfigureMontage(Config->Montage);
        }
        FPredictionKey Key;
        Key.Current = 219;
        F.ASC->CallServerTryActivateAbility(Handle, false, Key);
        if (!TestTrue(TEXT("Original frontend is active before target/notify"), Ability->IsActive()))
        {
            continue;
        }
        if (bShards)
        {
            F.ASC->InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Handle,
                                         Ability->GetCurrentActivationInfo().GetActivationPredictionKey());
        }
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Item = INDEX_NONE;
        Hit->HitResult.ImpactPoint = FVector(100, 0, 0);
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        F.ASC->ServerSetReplicatedTargetData(Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                             Data, FGameplayTag(), FPredictionKey());
        TestTrue(TEXT("Original montage task is actually running"),
                 F.Source->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Config->Montage));
        TestEqual(TEXT("Dynamic replacement does not bypass Notify gate"), F.Health(), 100.f);
        const auto EventTag = FGameplayTag::RequestGameplayTag(bShards ? TEXT("Event.Montage.AracaneShards")
                                                                       : TEXT("Event.Montage.Electrocute"));
        FGameplayEventData Payload;
        Payload.EventTag = EventTag;
        F.ASC->HandleGameplayEvent(EventTag, &Payload);
        TestEqual(TEXT("Actual ASC Notify executes replacement once"), F.Health(), 83.f);
        if (bShards)
        {
            TestEqual(TEXT("Synchronous replacement still attempts original shard cost after first effect"),
                      F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
        }
        F.ASC->HandleGameplayEvent(EventTag, &Payload);
        TestEqual(TEXT("Repeated Notify cannot duplicate replacement"), F.Health(), 83.f);
        F.ASC->CancelAbilityHandle(Handle);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeFireBoltCommitFailure,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFrontends.FireBoltCommitFailureBlocksRelease",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFireBoltCommitFailure::RunTest(const FString&)
{
    AbilityRuntimeMageBridgeTests::FFixture F;
    FString Error;
    TestTrue(TEXT("Install runtime payload"),
             F.Install(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Fire.FireBolt")), false, Error));
    TestTrue(TEXT("Original frontend accepts initial resource state"), F.ASC->TryActivateAbility(F.Handle));
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 0.f);
    F.Ability->SpawnProjectiles(FVector(100, 0, 0), {}, false, 0.f, nullptr);
    TestEqual(TEXT("Failed actual commit cannot run a frozen release"), F.Health(), 100.f);
    TestEqual(TEXT("Failed commit cannot create old fallback projectile"), F.ProjectileCount(), 0);
    TestEqual(TEXT("Failed original cost does not make mana negative"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeNativeActualObservation,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFrontends.ActualReleaseRecordsCostAndDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeNativeActualObservation::RunTest(const FString&)
{
    AbilityRuntimeMageBridgeTests::FFixture F;
    auto* State = F.World->SpawnActor<AMyPlayerState>();
    State->Profession = EPlayerProfession::Mage;
    auto* Pawn = F.World->SpawnActor<ADungeonPlayerRunTestPawn>();
    Pawn->AttachState(State);
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
    auto* Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
    if (!TestNotNull(TEXT("Actual player's recipe owner exposes bounded observations"), Developer))
    {
        return false;
    }
    const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Fire.FireBolt"));
    FString Error;
    TestTrue(TEXT("Register generated payload for actual player"),
             Developer->RegisterBase(Tag, AbilityRuntimeMageBridgeTests::MakePayload(F.World, false), Error));
    FAbilityRuntimeRecipeContribution Contribution;
    Contribution.SourceId = TEXT("ObservationIntegration");
    TestTrue(TEXT("Enable runtime recipe"), Developer->SetContribution(Tag, Contribution, Error));
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityRuntimeFireBoltBridgeTest::StaticClass(), 1));
    auto* Ability =
        CastChecked<UAbilityRuntimeFireBoltBridgeTest>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    TestTrue(TEXT("Actual frontend accepts player input activation"), ASC->TryActivateAbility(Handle));
    Ability->SpawnProjectiles(FVector(100, 0, 0), {}, false, 0.f, nullptr);
    TestEqual(TEXT("Observed release really changes health"), F.Health(), 83.f);
    TestEqual(TEXT("Observed release really pays cost"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              90.f);
    int32 Costs = 0;
    int32 Hits = 0;
    int32 Ends = 0;
    FGuid RootId;
    for (const auto& Record : Developer->GetObservations())
    {
        TestEqual(TEXT("Actual frontend observation retains selected identity"), Record.AbilityTag, Tag);
        if (!RootId.IsValid())
        {
            RootId = Record.RootCastId;
        }
        TestEqual(TEXT("Cost and effect observations share original cast identity"), Record.RootCastId, RootId);
        if (Record.Kind == EAbilityRuntimeObservationKind::Cost && Record.bCostSucceeded &&
            Record.ResourceBefore == 100.f && Record.ResourceAfter == 90.f)
        {
            ++Costs;
        }
        if (Record.Stage == TEXT("DamageResolved") && Record.bActualDamageKnown && Record.ActualDamage == 17.f)
        {
            ++Hits;
        }
        Ends += Record.Kind == EAbilityRuntimeObservationKind::GraphFinished ? 1 : 0;
    }
    TestEqual(TEXT("Production commit, not synthetic UI record, is captured once"), Costs, 1);
    TestEqual(TEXT("Real payload receipt is visible once"), Hits, 1);
    TestEqual(TEXT("Synchronous graph end is visible once"), Ends, 1);
    ASC->ClearAbility(Handle);
    return true;
}
#endif
