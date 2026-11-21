// 测试链：实际目录BeginPreview -> 可信参数/分支事务 -> accepted snapshot -> 同一执行器实际时序/碰撞/状态。
// 不手写替代基图；夹具只控制确定性空间输入，真实鼠标/网络另由PIE报告证明。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityProjectileMotion.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeDistributionNodes.h"
#include "PureMass/PureMassSubsystem.h"
#include "UObject/UnrealType.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace AbilityRuntimeCrossAxisCombinationTests
{
    struct FScenario : FAbilityActualMageFrontendFixture
    {
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Context = nullptr;
        int32 Neighbor = INDEX_NONE;

        /** 目标保持真实ASC/GE，增加生命只为避免默认伤害遮蔽独立状态断言。 */
        FScenario()
        {
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),
                                                                         1000000.f);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),
                                                                         1000000.f);
        }
        ~FScenario()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        /** 轻量目标无AI/移动，所有实际命中仍经原Mixed查询与状态服务。 */
        int32 Mass(FVector Position)
        {
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 10000.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            Type->Speed = 0.f;
            Type->XPReward = 0;
            return Neighbor = Sim->SpawnMonster(Type, Position);
        }

        /** 所有轴和分支都经过当前版本检查；测试不得直接修改已接受snapshot或原DA。 */
        bool Edit(const FAbilityRuntimeDeveloperDefinition& Row, int32 Index, FName Modifier, float Value,
                  FString& Error, FName Fragment = NAME_None, FName Port = NAME_None)
        {
            FAbilityRuntimeDeveloperEdit Request;
            Request.DefinitionId = Row.DefinitionId;
            Request.ExpectedBaseHash = Row.BaseHash;
            Request.ExpectedRevision = Developer->GetDeveloperState().Revision;
            Request.NodeIndex = Index;
            Request.ModifierId = Modifier;
            Request.Value = Value;
            Request.FragmentId = Fragment;
            Request.OutputPortId = Port;
            Request.Radius = 350.f;
            return Developer->ApplyEdit(Request, Error);
        }

        /** 原PlayerState生命进入原型上下文；可选目标证明仅用于实际Homing，不上传目标地址到解析器。 */
        bool Start(const FAbilityRuntimeDeveloperDefinition& Row, FString& Error, bool bAim = false)
        {
            FAbilityRuntimeRecipeSnapshot Accepted;
            if (!Developer->CreateCastSnapshot(Row.Base.AbilityTag, World, Accepted, Error))
            {
                return false;
            }
            Context = NewObject<UPrototypeCastContext>(World);
            Context->Caster = Pawn;
            Context->CastId = FGuid::NewGuid();
            Context->Level = 1;
            Context->DamageParams.SourceAbilitySystemComponent = PlayerASC;
            const auto* Defaults = Row.Base.AbilityClass->GetDefaultObject<UPrototypeAbility>();
            const auto* Property = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("DamageEffectClass"));
            Context->DamageParams.DamageGameplayEffectClass =
                Property ? Cast<UClass>(Property->GetObjectPropertyValue_InContainer(Defaults)) : nullptr;
            if (!Context->CaptureSourceLife(Error))
            {
                return false;
            }
            FAbilityNodeInput In;
            In.SourceToken = Context->GetSourceLife();
            In.Context = Context;
            In.Direction = FVector::ForwardVector;
            In.DamageParams = Context->DamageParams;
            if (bAim)
            {
                FAbilityTargetToken TargetLife;
                if (!AbilityExecutionIdentity::CaptureTarget(In.SourceToken, FPureMassCombatTarget::FromActor(Target),
                                                             TargetLife, Error))
                {
                    return false;
                }
                In.LiveTargetToken = TargetLife;
            }
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Accepted.Graph->CompatibilityProfile;
            Options.BudgetPolicy = Accepted.Graph->BudgetPolicy;
            Run = NewObject<UAbilityGraphInstance>(World);
            return Run->StartWithInput(Accepted.Graph, In, Options, Error) &&
                   !(Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
        }

        /** 推进真实空间/碰撞调度；不手工发布Contact或状态事件。 */
        void Advance(float Delta)
        {
            ++GFrameCounter;
            Run->Advance(Delta);
        }
        /** 生产ASC更换Avatar使旧SourceToken失效，不伪造外部取消或新源身份。 */
        void RetireSource()
        {
            auto* Next = World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Next->Tags = {TEXT("Player")};
            PlayerASC->InitAbilityActorInfo(State, Next);
        }
        bool MassPetrified() const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Neighbor, View) && View.bPetrified;
        }
    };

    /** 默认导出中的节点位置只在同一BaseHash内使用，端口不依靠数组顺序推断。 */
    int32 Index(const FAbilityRuntimeDeveloperDefinition& Row, FName Type)
    {
        return Row.Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == Type;
            });
    }

    /** 同时验证正常触发、前摇取消与源退休；纯payload由可信区域工厂生成而非测试手写。 */
    bool Temporal(FAutomationTestBase& Test, bool bTrap)
    {
        for (int32 Mode = 0; Mode < 3; ++Mode)
        {
            FScenario F;
            F.Mass(FVector(0, 180, 0));
            FString Error;
            const FString Path = bTrap ? TEXT("/Game/PureMass/Abilities/GA_Prototype_Trap.GA_Prototype_Trap_C")
                                       : TEXT("/Game/PureMass/Abilities/GA_Prototype_Delay.GA_Prototype_Delay_C");
            const auto* Row = F.Definition(Path, Error);
            if (!Test.TestNotNull(TEXT("Exact actual temporal directory row is available"), Row) ||
                !Test.TestTrue(TEXT("Actual temporal preview begins"),
                               F.Developer->BeginPreview(Row->DefinitionId, Error)))
            {
                Test.AddError(Error);
                return false;
            }
            const int32 Root = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
                [&](const FAbilityGraphNode& Node)
                {
                    return Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
                });
            const int32 Clock = Index(*Row, bTrap ? TEXT("Control.PrototypeTrap") : TEXT("Control.PrototypeDelay"));
            if (!Test.TestTrue(TEXT("Actual separate temporal clock exists"), Clock != INDEX_NONE) ||
                !Test.TestTrue(TEXT("Actual arm/delay edit is accepted"),
                               F.Edit(*Row, Root, TEXT("Set.ArmDelay"), .2f, Error)) ||
                !Test.TestTrue(TEXT("Trusted area payload attaches to the actual temporal fact port"),
                               F.Edit(*Row, Clock, TEXT("Append.Registered"), 2.f, Error, TEXT("Area.Petrify"),
                                      bTrap ? TEXT("Action") : TEXT("Expired"))) ||
                !Test.TestTrue(TEXT("Accepted temporal snapshot executes with real source proof"),
                               F.Start(*Row, Error)))
            {
                Test.AddError(Error);
                return false;
            }
            F.Advance(.19f);
            Test.TestFalse(TEXT("Added Actor state does not bypass actual arm/delay time"), F.IsPetrified());
            Test.TestFalse(TEXT("Added Mass state does not bypass actual arm/delay time"), F.MassPetrified());
            if (Mode == 1)
            {
                F.Run->Cancel();
            }
            else if (Mode == 2)
            {
                F.RetireSource();
            }
            F.Advance(.02f);
            Test.TestEqual(TEXT("Actual temporal event applies Actor state only on normal execution"), F.IsPetrified(),
                           Mode == 0);
            Test.TestEqual(TEXT("Actual temporal event applies Mass state only on normal execution"), F.MassPetrified(),
                           Mode == 0);
            if (Mode == 2)
            {
                Test.TestEqual(TEXT("Actual source retirement closes before expiry payload"), F.Run->GetEndReason(),
                               EAbilityGraphEndReason::SourceInvalid);
            }
            F.Run->Cancel();
            const auto Count = F.Run->GetAcceptedEventCount();
            F.Advance(10.f);
            Test.TestEqual(TEXT("No cancelled timer/clock can publish another temporal effect"),
                           F.Run->GetAcceptedEventCount(), Count);
            Test.TestEqual(TEXT("Actual shared base graph is unchanged by the new combination"),
                           AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph), Row->BaseHash);
        }
        return true;
    }
} // namespace AbilityRuntimeCrossAxisCombinationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActualTrapRuntimePayload,
                                 "test_25d.DynamicMage.CrossAxis.ActualTrapParserPayloadAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActualTrapRuntimePayload::RunTest(const FString&)
{
    return AbilityRuntimeCrossAxisCombinationTests::Temporal(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActualDelayRuntimePayload,
                                 "test_25d.DynamicMage.CrossAxis.ActualDelayParserPayloadAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActualDelayRuntimePayload::RunTest(const FString&)
{
    return AbilityRuntimeCrossAxisCombinationTests::Temporal(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActualScatterMotionState,
                                 "test_25d.DynamicMage.CrossAxis.ActualScatterMotionAndStateParserCombination",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActualScatterMotionState::RunTest(const FString&)
{
    using namespace AbilityRuntimeCrossAxisCombinationTests;
    for (const bool bHoming : {false, true})
    {
        for (int32 Mode = 0; Mode < (bHoming ? 5 : 3); ++Mode)
        {
            FScenario F;
            F.Target->SetActorLocation(bHoming ? FVector(160, 100, 0) : FVector(150, 0, 0));
            F.Mass(FVector(300, 0, 0));
            FString Error;
            const auto* Row =
                F.Definition(TEXT("/Game/PureMass/Abilities/GA_Prototype_Multishot.GA_Prototype_Multishot_C"), Error);
            if (!TestNotNull(TEXT("Exact actual Mixed Scatter directory row exists"), Row) ||
                !TestTrue(TEXT("Actual Scatter preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
            {
                AddError(Error);
                return false;
            }
            const int32 Root = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
                [&](const FAbilityGraphNode& Node)
                {
                    return Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
                });
            const int32 Flight = Index(*Row, TEXT("Delivery.PrototypeFlight"));
            const int32 Distribution = Index(*Row, TEXT("Distribution.PrototypeMultishot"));
            if (!TestTrue(TEXT("Actual default exposes independent single-pellet motion/target distribution"),
                          Flight != INDEX_NONE && Distribution != INDEX_NONE) ||
                !TestTrue(TEXT("Actual Scatter count edit is accepted"),
                          F.Edit(*Row, Root, TEXT("Set.Count"), 3.f, Error)) ||
                !TestTrue(TEXT("Actual Scatter fan angle edit is accepted"),
                          F.Edit(*Row, Root, TEXT("Set.Spread"), bHoming ? 30.f : 0.f, Error)) ||
                !TestTrue(TEXT("Actual selected flight motion is accepted"),
                          F.Edit(*Row, Flight, TEXT("Set.Motion"),
                                 float(bHoming ? EAbilityProjectileMotion::LegacyTurnHoming
                                               : EAbilityProjectileMotion::Linear),
                                 Error)) ||
                !TestTrue(TEXT("Actual flight speed is accepted"),
                          F.Edit(*Row, Flight, TEXT("Set.Speed"), 600.f, Error)) ||
                !TestTrue(TEXT("Actual flight range is accepted"),
                          F.Edit(*Row, Flight, TEXT("Set.Range"), 600.f, Error)) ||
                !TestTrue(TEXT("Actual flight turn rate is accepted"),
                          F.Edit(*Row, Flight, TEXT("Set.HomingTurnRate"), 720.f, Error)) ||
                !TestTrue(TEXT("Actual impact policy is accepted"),
                          F.Edit(*Row, Flight, TEXT("Set.ImpactPolicy"),
                                 float(bHoming ? EAbilityProjectileImpactPolicy::StopFirst
                                               : EAbilityProjectileImpactPolicy::Pierce),
                                 Error)) ||
                !TestTrue(TEXT("Actual per-pellet impact count is accepted"),
                          F.Edit(*Row, Flight, TEXT("Set.ImpactLimit"), bHoming ? 1.f : 2.f, Error)) ||
                (bHoming && !TestTrue(TEXT("Only explicit policy routes the original selected life to homing pellets"),
                                      F.Edit(*Row, Distribution, TEXT("Set.PelletTargetPolicy"), 1.f, Error))) ||
                !TestTrue(TEXT("Trusted TargetSet state attaches to actual physical contacts"),
                          F.Edit(*Row, Flight, TEXT("Append.Registered"), 2.f, Error, TEXT("Target.Petrify"),
                                 TEXT("Contact"))) ||
                !TestTrue(TEXT("The same accepted parser snapshot starts all three real pellets"),
                          F.Start(*Row, Error, bHoming && Mode != 3)))
            {
                AddError(Error);
                return false;
            }
            TestEqual(TEXT("Real Scatter container and three independently admitted pellet scopes exist"),
                      F.Context->Spawned, 4);
            int32 CompletedPellets = 0;
            int32 PelletContacts = 0;
            F.Run->OnEvent.AddLambda(
                [&](const FAbilityGraphEvent& Event)
                {
                    const auto* Frame = Cast<UAbilityPrototypeParameterFrame>(Event.Input.ParameterFrame.Get());
                    if (Event.NodeType == TEXT("Delivery.PrototypeFlight") && Frame && Frame->GetLogicalDepth() == 0 &&
                        Frame->IsSingleProjectile())
                    {
                        CompletedPellets += Event.OutputPort == TEXT("Completed") ? 1 : 0;
                        PelletContacts += Event.OutputPort == TEXT("Contact") ? 1 : 0;
                    }
                });
            if (Mode == 1)
            {
                F.Run->Cancel();
            }
            else if (Mode == 2)
            {
                F.RetireSource();
            }
            else if (Mode == 4)
            {
                // 原token已经进入三个实际Flight；改变lease不能使任何pellet重捕复用后的Actor。
                F.Target->EnableDungeonPooling();
            }
            for (int32 Step = 0; Step < 80 && !F.Run->IsFinished(); ++Step)
            {
                F.Advance(.025f);
            }
            TestEqual(TEXT("Actual homing/piercing Scatter contacts apply independent Actor state"), F.IsPetrified(),
                      Mode == 0);
            TestEqual(TEXT("Only normal Piercing reaches the second real Mass life"), F.MassPetrified(),
                      Mode == 0 && !bHoming);
            if (Mode == 2)
            {
                TestEqual(TEXT("Retired original source suppresses every pending pellet payload"),
                          F.Run->GetEndReason(), EAbilityGraphEndReason::SourceInvalid);
            }
            if (Mode == 3 || Mode == 4)
            {
                // 只断言原三个单弹结束；原DA独立OnExpire孩子仍可拥有自己的正常寿命。
                TestEqual(TEXT("Missing or retired original target closes all three actual homing pellets"),
                          CompletedPellets, 3);
                TestEqual(TEXT("Missing or retired original life never authorizes an implicit replacement target"),
                          PelletContacts, 0);
            }
            if (Mode == 0)
            {
                TestEqual(TEXT("Actual physical contacts follow the selected per-pellet policy"), PelletContacts,
                          bHoming ? 3 : 6);
            }
            F.Run->Cancel();
            TestEqual(TEXT("All three physical pellet leases are released"), F.Run->GetActivePhysicalSpawnCount(), 0);
            TestEqual(TEXT("Dynamic motion/state never rewrites the original Multishot definition"),
                      AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph), Row->BaseHash);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActualPelletTargetPolicyAtomic,
                                 "test_25d.DynamicMage.CrossAxis.PelletTargetPolicyTransactionsAreAtomic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActualPelletTargetPolicyAtomic::RunTest(const FString&)
{
    using namespace AbilityRuntimeCrossAxisCombinationTests;
    FScenario F;
    FString Error;
    const auto* Row =
        F.Definition(TEXT("/Game/PureMass/Abilities/GA_Prototype_Multishot.GA_Prototype_Multishot_C"), Error);
    if (!TestNotNull(TEXT("Actual Scatter directory row exists for policy transactions"), Row) ||
        !TestTrue(TEXT("Actual Scatter preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
    {
        AddError(Error);
        return false;
    }
    const int32 Distribution = Index(*Row, TEXT("Distribution.PrototypeMultishot"));
    if (!TestTrue(TEXT("Actual distribution exposes its explicit target policy"), Distribution != INDEX_NONE) ||
        !TestTrue(TEXT("Explicit OriginalSelectedLife policy is accepted"),
                  F.Edit(*Row, Distribution, TEXT("Set.PelletTargetPolicy"), 1.f, Error)))
    {
        AddError(Error);
        return false;
    }
    const auto Accepted = F.Developer->GetDeveloperState();
    for (const float Invalid : {-1.f, .5f, 2.f, std::numeric_limits<float>::quiet_NaN()})
    {
        TestFalse(TEXT("Noninteger/out-of-range/nonfinite target policies are rejected transactionally"),
                  F.Edit(*Row, Distribution, TEXT("Set.PelletTargetPolicy"), Invalid, Error));
        TestEqual(TEXT("Invalid target policy leaves accepted revision unchanged"),
                  F.Developer->GetDeveloperState().Revision, Accepted.Revision);
        TestEqual(TEXT("Invalid target policy leaves accepted graph identity unchanged"),
                  F.Developer->GetDeveloperState().ContentHash, Accepted.ContentHash);
    }
    TestTrue(TEXT("Explicit None restores the original no-target-routing policy"),
             F.Edit(*Row, Distribution, TEXT("Set.PelletTargetPolicy"), 0.f, Error));
    FAbilityRuntimeRecipeSnapshot Snapshot;
    if (TestTrue(TEXT("The reset snapshot is available"),
                 F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Snapshot, Error)))
    {
        const auto* Config = Cast<UAbilityPrototypeMultishotNodeConfig>(Snapshot.Graph->Nodes[Distribution].Config);
        if (TestNotNull(TEXT("Actual reset retains the exact distribution strategy"), Config))
        {
            TestEqual(TEXT("Only the original None policy is restored"), Config->PelletTargetPolicy,
                      EAbilityPelletTargetPolicy::None);
        }
    }
    return true;
}
#endif
