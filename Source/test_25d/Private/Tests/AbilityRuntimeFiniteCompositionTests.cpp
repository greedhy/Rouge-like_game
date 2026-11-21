// 调用链：真实原型细图 -> 服务器配方补丁 -> 同一执行器 -> 新增普通有限节点/实际物理投递。
// 不上传图、不改共享DA；新增节点不能借原型helper合同逃离普通有限额度。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityNormalizationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "Actor/MyProjectile.h"
#include "PureMass/PureMassSubsystem.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityRuntimeFiniteCompositionTests
{
    struct FStage : FAbilityGraphTestFixture
    {
        int32 Victim = INDEX_NONE;
        UPrototypeCastContext* Context = nullptr;
        UAbilityGraphInstance* Run = nullptr;

        FStage()
        {
            Target->Destroy();
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 200.f;
            Type->Speed = 0.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            Type->XPReward = 0;
            Victim = Mass->SpawnMonster(Type, FVector(50, 0, 0));
            Context = NewObject<UPrototypeCastContext>(World);
            Context->Caster = Source;
            Context->Level = 1;
            Context->CastId = FGuid::NewGuid();
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            Context->DamageParams.BaseDamage = 25.f;
            Context->DamageParams.DamageType = FMyGameplayTags::Get().Damage_Magic;
            FString Error;
            Context->CaptureSourceLife(Error);
        }

        ~FStage()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        UAbilityGraphDefinition* Base(FString& Error, bool bField = true)
        {
            auto* Parameters = NewObject<UAbilityPrototype>(World);
            Parameters->Kind = bField ? EAbilityPrototypeKind::Field : EAbilityPrototypeKind::Burst;
            Parameters->Cooldown = 0.f;
            Parameters->Radius = bField ? 100.f : 20.f;
            Parameters->Duration = bField ? 10.f : .1f;
            Parameters->Period = .1f;
            Parameters->Damage = bField ? 1.f : 25.f;
            Parameters->DamagePerLevel = 0.f;
            Parameters->DamageType = FMyGameplayTags::Get().Damage_Magic;
            if (!Parameters->IsValidConfiguration())
            {
                Error = TEXT("Finite overlay fixture must first have a valid original prototype configuration.");
                return nullptr;
            }
            return AbilityPrototypeFineCompiler::Compile(World, Parameters, TEXT("LegacyMixed.v1"), Error);
        }

        bool Start(UAbilityGraphDefinition* Definition, FString& Error)
        {
            FAbilityNodeInput Input;
            Input.SourceToken = SourceToken;
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            Input.Direction = FVector::ForwardVector;
            Run = NewObject<UAbilityGraphInstance>(World);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            return Run->StartWithInput(Definition, Input, Options, Error);
        }

        float Health() const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Victim, View) ? View.Health : -1.f;
        }
    };

    /** 普通片段由真实策略构成，其自身不申请原型兼容或持续合同。 */
    UAbilityGraphDefinition* StatusBranch(UObject* Outer)
    {
        auto* Branch = NewObject<UAbilityGraphDefinition>(Outer);
        Branch->SchemaVersion = 2;
        Branch->CompatibilityProfile = TEXT("UnifiedV2");
        const auto Impact = FGuid::NewGuid();
        const auto Resolve = FGuid::NewGuid();
        const auto State = FGuid::NewGuid();
        Branch->EntryNodeId = Impact;
        Branch->Nodes = {{Impact, TEXT("Event.ImpactFact"), NewObject<UAbilityImpactFactNodeConfig>(Branch)},
                         {Resolve, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(Branch)},
                         {State, TEXT("Status.Petrify"), NewObject<UAbilityPetrifyNodeConfig>(Branch)}};
        Branch->Edges = {{Impact, Resolve, TEXT("Completed")}, {Resolve, State, TEXT("TargetFound")}};
        return Branch;
    }

    bool Append(FAutomationTestBase& Test, FStage& F, UAbilityGraphDefinition& Base, FName Type, FName Port,
                UAbilityGraphDefinition* Branch, UAbilityGraphDefinition*& Compiled)
    {
        const auto* Target = Base.Nodes.FindByPredicate(
            [&](const auto& Node)
            {
                return Node.TypeId == Type;
            });
        if (!Test.TestNotNull(TEXT("Actual fine attachment axis exists"), Target))
        {
            return false;
        }
        FAbilityRuntimeRecipeContribution Contribution;
        Contribution.SourceId = TEXT("Rune.FiniteTest");
        FAbilityRuntimeRecipePatch Patch;
        Patch.TargetNodeId = Target->NodeId;
        Patch.Operation = EAbilityRecipePatchOperation::AppendBranch;
        Patch.OutputPort = Port;
        Patch.BranchGraph = Branch;
        Contribution.Patches.Add(Patch);
        FString Error;
        const bool bResult = AbilityRuntimeRecipes::Compile(F.World, &Base, {Contribution}, Compiled, Error);
        Test.TestTrue(TEXT("Ordinary finite fragment compiles inside original prototype execution: ") + Error, bResult);
        return bResult;
    }
} // namespace AbilityRuntimeFiniteCompositionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeFiniteAppendInvocationLimit,
    "test_25d.UnifiedGA.DynamicMage.RuntimeFinite.AppendedPayloadsKeepAggregateSixtyFourCalls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFiniteAppendInvocationLimit::RunTest(const FString&)
{
    using namespace AbilityRuntimeFiniteCompositionTests;
    FStage F;
    FString Error;
    auto* Base = F.Base(Error);
    if (!TestNotNull(TEXT("Actual prototype fine base: ") + Error, Base))
    {
        return false;
    }
    const auto Before = AbilityRuntimeRecipes::Fingerprint(*Base);
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!Append(*this, F, *Base, TEXT("Effect.PrototypeDamageBatch"), TEXT("DamageResolved"), StatusBranch(F.World),
                Compiled))
    {
        return false;
    }
    TestEqual(TEXT("Source definition remains immutable"), AbilityRuntimeRecipes::Fingerprint(*Base), Before);
    if (!TestTrue(TEXT("Actual combined graph starts: ") + Error, F.Start(Compiled, Error)))
    {
        return false;
    }
    for (int32 Frame = 0; Frame < 22; ++Frame)
    {
        ++GFrameCounter;
        F.Run->Advance(.1f);
    }
    TestTrue(TEXT("Twenty-second three-node payload exhausts the aggregate finite64 ledger"), F.Run->IsFinished());
    TestEqual(TEXT("Inserted payloads cannot consume unlimited prototype-helper calls"), F.Run->GetEndReason(),
              EAbilityGraphEndReason::BudgetExceeded);
    TestEqual(TEXT("All22 root pulses commit before their independent finite payload admission"), F.Health(), 178.f);
    TestEqual(TEXT("Inserted state nodes do not become logical prototype spawns"), F.Context->Spawned, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeFiniteAppendActiveLimit,
    "test_25d.UnifiedGA.DynamicMage.RuntimeFinite.AppendedAsyncNodesKeepAggregateActiveThirtyTwo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFiniteAppendActiveLimit::RunTest(const FString&)
{
    using namespace AbilityRuntimeFiniteCompositionTests;
    FStage F;
    FString Error;
    auto* Base = F.Base(Error);
    if (!TestNotNull(TEXT("Actual fine base: ") + Error, Base))
    {
        return false;
    }
    auto* Branch = NewObject<UAbilityGraphDefinition>(F.World);
    Branch->SchemaVersion = 2;
    Branch->CompatibilityProfile = TEXT("UnifiedV2");
    auto* Delay = NewObject<UAbilityDelayNodeConfig>(Branch);
    Delay->DelaySeconds = 10.f;
    Branch->EntryNodeId = FGuid::NewGuid();
    Branch->Nodes = {{Branch->EntryNodeId, TEXT("Control.DelayOrigin"), Delay}};
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!Append(*this, F, *Base, TEXT("Effect.PrototypeDamageBatch"), TEXT("Completed"), Branch, Compiled))
    {
        return false;
    }
    if (!TestTrue(TEXT("Actual combined asynchronous graph starts: ") + Error, F.Start(Compiled, Error)))
    {
        return false;
    }
    for (int32 Frame = 0; Frame < 33; ++Frame)
    {
        ++GFrameCounter;
        F.Run->Advance(.1f);
    }
    TestTrue(TEXT("Thirty-third independent delay cannot inherit helper active128"), F.Run->IsFinished());
    TestEqual(TEXT("Finite asynchronous branch keeps ordinary active32"), F.Run->GetEndReason(),
              EAbilityGraphEndReason::BudgetExceeded);
    TestEqual(TEXT("Root33 still settles before its new branch hits the ordinary active gate"), F.Health(), 167.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeFiniteSequentialPhysics,
    "test_25d.UnifiedGA.DynamicMage.RuntimeFinite.SequentialUsesInvocationScopedActualPhysicalAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFiniteSequentialPhysics::RunTest(const FString&)
{
    using namespace AbilityRuntimeFiniteCompositionTests;
    FStage F;
    FString Error;
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 200.f;
    Type->Speed = 0.f;
    Type->XPReward = 0;
    const int32 A = Mass->SpawnMonster(Type, FVector(300, 0, 0));
    const int32 B = Mass->SpawnMonster(Type, FVector(600, 0, 0));
    // 移除近处的Field夹具目标；真实两跳只允许300/600处的原生命。
    Mass->RemoveMonster(F.Victim);
    auto* Base = F.Base(Error, false);
    if (!TestNotNull(TEXT("Actual Burst lifecycle base: ") + Error, Base))
    {
        return false;
    }
    auto* Branch = NewObject<UAbilityGraphDefinition>(F.World);
    Branch->SchemaVersion = 2;
    Branch->CompatibilityProfile = TEXT("UnifiedV2");
    auto* Sequential = NewObject<UAbilityPropagationNodeConfig>(Branch);
    Sequential->Delivery.ProjectileClass = AMyProjectile::StaticClass();
    Sequential->Delivery.Speed = 300.f;
    Sequential->Delivery.MaxDistance = 1000.f;
    Sequential->Delivery.TimeoutSeconds = 5.f;
    Sequential->Delivery.CollisionRadius = 8.f;
    Sequential->MaxTargets = 2;
    Sequential->JumpRadius = 350.f;
    Sequential->JumpDelay = .15f;
    Branch->EntryNodeId = FGuid::NewGuid();
    Branch->Nodes = {{Branch->EntryNodeId, TEXT("Propagation.Sequential"), Sequential}};
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!Append(*this, F, *Base, TEXT("Control.PrototypeLifetime"), TEXT("Completed"), Branch, Compiled))
    {
        return false;
    }
    if (!TestTrue(TEXT("Actual lifecycle plus sequential branch starts: ") + Error, F.Start(Compiled, Error)))
    {
        return false;
    }
    int32 Hits = 0;
    F.Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Hits +=
                Event.NodeType == TEXT("Propagation.Sequential") && Event.OutputPort == TEXT("DamageResolved") ? 1 : 0;
        });
    for (int32 Frame = 0; Frame < 450 && !F.Run->IsFinished(); ++Frame)
    {
        TArray<TWeakObjectPtr<AMyProjectile>> Projectiles;
        for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
        {
            Projectiles.Add(*It);
        }
        for (auto Projectile : Projectiles)
        {
            if (Projectile.IsValid() && !Projectile->IsActorBeingDestroyed())
            {
                Projectile->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
            }
        }
        ++GFrameCounter;
        F.Run->Advance(.01f);
    }
    TestEqual(TEXT("A prototype host can launch two actual bounded sequential projectiles"), Hits, 2);
    TestEqual(TEXT("Finite physical admission does not change the root end contract"), F.Run->GetEndReason(),
              EAbilityGraphEndReason::Completed);
    FPureMassView View;
    TestTrue(TEXT("First original target survives"), Mass->Resolve(A, View));
    TestEqual(TEXT("First receives the original25 payload exactly once"), View.Health, 175.f);
    TestTrue(TEXT("Second original target survives"), Mass->Resolve(B, View));
    TestEqual(TEXT("Second receives the original25 payload exactly once"), View.Health, 175.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeFiniteRawSpawnGuard,
    "test_25d.UnifiedGA.DynamicMage.RuntimeFinite.RawMultiActorActionsCannotBypassPhysicalAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFiniteRawSpawnGuard::RunTest(const FString&)
{
    using namespace AbilityRuntimeFiniteCompositionTests;
    FEditorScriptExecutionGuard ScriptGuard;
    for (bool bPrototypeHost : {true, false})
    {
        for (bool bSummon : {false, true})
        {
            for (int32 Count : {33, 65})
            {
                FStage F;
                FString Error;
                UAbilityGraphDefinition* Base = nullptr;
                FName AttachType;
                if (bPrototypeHost)
                {
                    Base = F.Base(Error, false);
                    AttachType = TEXT("Control.PrototypeLifetime");
                }
                else
                {
                    Base = NewObject<UAbilityGraphDefinition>(F.World);
                    Base->SchemaVersion = 2;
                    Base->CompatibilityProfile = TEXT("UnifiedV2");
                    Base->EntryNodeId = FGuid::NewGuid();
                    auto* Delay = NewObject<UAbilityDelayNodeConfig>(Base);
                    Delay->DelaySeconds = .1f;
                    Base->Nodes = {{Base->EntryNodeId, TEXT("Control.DelayOrigin"), Delay}};
                    AttachType = TEXT("Control.DelayOrigin");
                }
                if (!TestNotNull(TEXT("Actual host base must be available: ") + Error, Base))
                {
                    return false;
                }
                auto* Branch = NewObject<UAbilityGraphDefinition>(F.World);
                Branch->SchemaVersion = 2;
                Branch->CompatibilityProfile = TEXT("UnifiedV2");
                Branch->EntryNodeId = FGuid::NewGuid();
                if (bSummon)
                {
                    auto* Settings = NewObject<UAbilitySummonNodeConfig>(Branch);
                    Settings->Count = Count;
                    Settings->Interval = .01f;
                    Settings->MinionClasses.Add(ACharacter::StaticClass());
                    for (int32 Index = 0; Index < Count; ++Index)
                    {
                        Settings->PreparedLocations.Add(FVector(200 + Index * 20, 200, 0));
                    }
                    Branch->Nodes = {{Branch->EntryNodeId, TEXT("Spawn.Summon"), Settings}};
                }
                else
                {
                    auto* Settings = NewObject<UAbilityActorProjectileNodeConfig>(Branch);
                    Settings->Settings.ProjectileClass = AMyProjectile::StaticClass();
                    Settings->Settings.Count = Count;
                    Settings->Settings.bLimitCountToLevel = false;
                    Branch->Nodes = {{Branch->EntryNodeId, TEXT("Delivery.ActorProjectiles"), Settings}};
                }
                TArray<FString> Validation;
                if (!TestTrue(TEXT("Raw registered action is a valid original formal definition"),
                              Branch->ValidateForAuthoring(Validation)))
                {
                    AddInfo(FString::Join(Validation, TEXT(";")));
                    return false;
                }
                FAbilityRuntimeRecipeContribution Rune;
                Rune.SourceId = TEXT("Rune.RawSpawnProbe");
                FAbilityRuntimeRecipePatch Patch;
                Patch.TargetTypeId = AttachType;
                Patch.Operation = EAbilityRecipePatchOperation::AppendBranch;
                Patch.OutputPort = TEXT("Completed");
                Patch.BranchGraph = Branch;
                Rune.Patches.Add(Patch);
                const auto BaseHash = AbilityRuntimeRecipes::Fingerprint(*Base);
                UAbilityGraphDefinition* Compiled = nullptr;
                const bool bAccepted = AbilityRuntimeRecipes::Compile(F.World, Base, {Rune}, Compiled, Error);
                TestEqual(TEXT("Admission checks never mutate the original formal base"),
                          AbilityRuntimeRecipes::Fingerprint(*Base), BaseHash);
                if (!bAccepted)
                {
                    TestNull(TEXT("Fail-closed compile publishes no partial graph"), Compiled);
                    TestFalse(TEXT("Fail-closed admission explains the missing managed contract"), Error.IsEmpty());
                    continue;
                }
                if (!TestTrue(TEXT("Accepted fragment starts through the actual shared executor"),
                              F.Start(Compiled, Error)))
                {
                    return false;
                }
                int32 Peak = 0;
                for (int32 Frame = 0; Frame < Count + 5; ++Frame)
                {
                    ++GFrameCounter;
                    F.Run->Advance(.05f);
                    int32 Live = 0;
                    if (bSummon)
                    {
                        for (TActorIterator<APawn> It(F.World); It; ++It)
                        {
                            Live +=
                                !It->IsActorBeingDestroyed() && It->FindComponentByClass<UAbilitySummonedPawnLease>()
                                    ? 1
                                    : 0;
                        }
                    }
                    else
                    {
                        for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
                        {
                            Live += It->IsActorBeingDestroyed() ? 0 : 1;
                        }
                    }
                    Peak = FMath::Max(Peak, Live);
                }
                const FString Label =
                    FString::Printf(TEXT("%s %s count%d"), bPrototypeHost ? TEXT("Prototype") : TEXT("Bounded"),
                                    bSummon ? TEXT("PreparedSummon") : TEXT("ActorProjectiles"), Count);
                TestTrue(Label + TEXT(" cannot have more than32 actual concurrent actors"), Peak <= 32);
                TestTrue(Label + TEXT(" cannot produce more than64 actual managed physical starts"),
                         F.Run->GetPhysicalSpawnCountTotal() <= 64);
                TestTrue(Label + TEXT(" actual actors must all be represented in physical admission"),
                         F.Run->GetPhysicalSpawnCountTotal() >= static_cast<uint64>(Peak));
            }
        }
    }
    return true;
}

namespace AbilityRuntimeFiniteCompositionTests
{
    /** 同一真实普通时钟分别作为旧Bounded基图和新增片段，避免拿helper实现自身作期望值。 */
    UAbilityGraphDefinition* RepeatGraph(UObject* Outer, int32 Count)
    {
        auto* Result = NewObject<UAbilityGraphDefinition>(Outer);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        auto* Settings = NewObject<UAbilityRepeatNodeConfig>(Result);
        Settings->Count = Count;
        Settings->Period = .1f;
        Settings->Duration = 1.f;
        Settings->bEmitImmediately = false;
        Result->EntryNodeId = FGuid::NewGuid();
        Result->Nodes = {{Result->EntryNodeId, TEXT("Control.Repeat"), Settings}};
        return Result;
    }

    UAbilityGraphDefinition* ClockDefinition(FAutomationTestBase& Test, FStage& F, bool bOverlay, int32 Count,
                                             bool bFrameLimit)
    {
        auto* Clock = RepeatGraph(F.World, Count);
        if (!bOverlay)
        {
            if (bFrameLimit)
            {
                Clock->Budget.MaxEventsPerTick = 1;
            }
            else
            {
                Clock->Budget.MaxEvents = 4;
            }
            return Clock;
        }
        FString Error;
        auto* Base = F.Base(Error, false);
        if (!Test.TestNotNull(TEXT("Actual prototype lifecycle base: ") + Error, Base))
        {
            return nullptr;
        }
        // 先配置尚未发布的基础定义；Compile输出带内容封印，不能再改其预算绕过快照只读合同。
        if (bFrameLimit)
        {
            Base->RuntimeFiniteBudget.MaxEventsPerTick = 1;
        }
        else
        {
            Base->RuntimeFiniteBudget.MaxEvents = 4;
        }
        UAbilityGraphDefinition* Compiled = nullptr;
        if (!Append(Test, F, *Base, TEXT("Control.PrototypeLifetime"), TEXT("Completed"), Clock, Compiled))
        {
            return nullptr;
        }
        return Compiled;
    }
} // namespace AbilityRuntimeFiniteCompositionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeFiniteEvents,
    "test_25d.UnifiedGA.DynamicMage.RuntimeFinite.RepeatedPublicationKeepsOrdinaryAccumulatedEventBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFiniteEvents::RunTest(const FString&)
{
    using namespace AbilityRuntimeFiniteCompositionTests;
    for (bool bOverlay : {false, true})
    {
        FStage F;
        auto* Definition = ClockDefinition(*this, F, bOverlay, 6, false);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Actual repeated-publication definition starts: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        auto Pulses = MakeShared<int32>(0);
        F.Run->OnEvent.AddLambda(
            [Pulses](const auto& Event)
            {
                *Pulses += Event.NodeType == TEXT("Control.Repeat") && Event.OutputPort == TEXT("Pulse") ? 1 : 0;
            });
        if (bOverlay)
        {
            ++GFrameCounter;
            F.Run->Advance(.11f); // 原生命周期到期后才启动新时钟，本帧不推进新调用的Age。
        }
        for (int32 Frame = 0; Frame < 5; ++Frame)
        {
            ++GFrameCounter;
            F.Run->Advance(.1f);
        }
        TestEqual(FString(bOverlay ? TEXT("Overlay") : TEXT("Original Bounded")) +
                      TEXT(" accepts exactly four pulse events"),
                  *Pulses, 4);
        TestEqual(TEXT("Fifth publication fails even though the clock used only one invocation"), F.Run->GetEndReason(),
                  EAbilityGraphEndReason::BudgetExceeded);
        TestTrue(TEXT("Event-limit stop is a real terminal cleanup"), F.Run->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeFiniteFrameQuota,
    "test_25d.UnifiedGA.DynamicMage.RuntimeFinite.ActualFrameQuotaDoesNotResetAcrossAdvanceZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFiniteFrameQuota::RunTest(const FString&)
{
    using namespace AbilityRuntimeFiniteCompositionTests;
    for (bool bOverlay : {false, true})
    {
        FStage F;
        auto* Definition = ClockDefinition(*this, F, bOverlay, 3, true);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Actual frame-limited clock starts: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        auto Pulses = MakeShared<int32>(0);
        F.Run->OnEvent.AddLambda(
            [Pulses](const auto& Event)
            {
                *Pulses += Event.NodeType == TEXT("Control.Repeat") && Event.OutputPort == TEXT("Pulse") ? 1 : 0;
            });
        if (bOverlay)
        {
            ++GFrameCounter;
            F.Run->Advance(.11f);
        }
        ++GFrameCounter;
        F.Run->Advance(.31f);
        TestEqual(TEXT("One actual frame consumes one queued pulse from the catch-up batch"), *Pulses, 1);
        F.Run->Advance(0.f);
        TestEqual(TEXT("Same-frame Advance0 cannot consume the second pulse"), *Pulses, 1);
        ++GFrameCounter;
        F.Run->Advance(0.f);
        TestEqual(TEXT("A new actual frame receives one new finite event slot"), *Pulses, 2);
        ++GFrameCounter;
        F.Run->Advance(0.f);
        TestEqual(TEXT("Third frame consumes the remaining pulse"), *Pulses, 3);
        ++GFrameCounter;
        F.Run->Advance(0.f);
        TestEqual(TEXT("Final terminal event is independently consumed and cleans the clock"), F.Run->GetEndReason(),
                  EAbilityGraphEndReason::Completed);
        TestTrue(TEXT("No asynchronous invocation remains after the final event"), F.Run->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeFiniteDepth, "test_25d.UnifiedGA.DynamicMage.RuntimeFinite.DepthEightIsLocalAndNarrowerBudgetIsValidated",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFiniteDepth::RunTest(const FString&)
{
    using namespace AbilityRuntimeFiniteCompositionTests;
    FStage F;
    FString Error;
    auto* Base = F.Base(Error, false);
    if (!TestNotNull(TEXT("Actual lifecycle base: ") + Error, Base))
    {
        return false;
    }
    auto* Branch = NewObject<UAbilityGraphDefinition>(F.World);
    Branch->SchemaVersion = 2;
    Branch->CompatibilityProfile = TEXT("UnifiedV2");
    FGuid Previous;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        auto* Config = NewObject<UAbilityOriginalPortNodeConfig>(Branch);
        Config->bRequestChild = false;
        const auto Node = FGuid::NewGuid();
        Branch->Nodes.Add({Node, TEXT("Event.OriginalOrigin"), Config});
        if (Previous.IsValid())
        {
            Branch->Edges.Add({Previous, Node, TEXT("Forward")});
        }
        else
        {
            Branch->EntryNodeId = Node;
        }
        Previous = Node;
    }
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!Append(*this, F, *Base, TEXT("Control.PrototypeLifetime"), TEXT("Completed"), Branch, Compiled) ||
        !TestTrue(TEXT("Actual depth8 finite branch starts: ") + Error, F.Start(Compiled, Error)))
    {
        return false;
    }
    ++GFrameCounter;
    F.Run->Advance(.11f);
    TestEqual(TEXT("Host helpers do not inflate the branch's own legal depth8"), F.Run->GetEndReason(),
              EAbilityGraphEndReason::Completed);
    auto* Narrowed = DuplicateObject(Compiled, F.World);
    Narrowed->RuntimeFiniteBudget.MaxDepth = 7;
    TArray<FString> Validation;
    TestFalse(TEXT("A depth8 branch cannot claim a finite depth7 declaration"),
              Narrowed->ValidateForAuthoring(Validation));
    TestTrue(TEXT("Finite budget changes are bound to the immutable semantic fingerprint"),
             AbilityRuntimeRecipes::Fingerprint(*Narrowed) != AbilityRuntimeRecipes::Fingerprint(*Compiled));
    return true;
}
#endif
