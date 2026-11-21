// 测试链：实际Follow目录/可信Append.Status.Petrify -> Contact:DamageResult -> 原生命resolver -> 单目标状态。
// 复用AbilityPrototypeSustainedPayloadTests中的目录/启动helper；不以Period→Area替代后伤事实，也不复制执行器。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "PureMass/PureMassSubsystem.h"
#include "Misc/AutomationTest.h"

namespace AbilityPrototypeSustainedPayloadTests
{
    // 同模块的既有实际目录/SourceLife helper；定义由原325测试文件维护。
    const FAbilityRuntimeDeveloperDefinition* Find(FAbilityActualMageFrontendFixture& F, bool bOrbit, bool bActor,
                                                   FString& Error);
    UAbilityGraphInstance* Start(FAbilityActualMageFrontendFixture& F, const FAbilityRuntimeDeveloperDefinition& Row,
                                 UAbilityGraphDefinition& Definition, FString& Error);
} // namespace AbilityPrototypeSustainedPayloadTests

namespace AbilityFollowDamageResultRuntimeTests
{
    struct FFollow : FAbilityActualMageFrontendFixture
    {
        UAbilityGraphInstance* Run = nullptr;
        const FAbilityRuntimeDeveloperDefinition* Row = nullptr;
        int32 MassId = INDEX_NONE;
        float Period = 0.f;
        int32 DamageFacts = 0;
        int32 StatusActions = 0;

        /** 明确取消本测试图，再让原fixture撤预览/销毁世界，避免观察者捕获已退出的栈状态。 */
        ~FFollow()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        /** 高血量仍能观察10级别伤害；不能用1e9导致float舍入把真实小额伤害吞掉。 */
        bool Prepare(bool bAppend, FString& Error)
        {
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100000.f);
            auto* EnemyASC = Target->GetAbilitySystemComponent();
            EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100000.f);
            EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100000.f);
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100000.f;
            Type->Speed = 0.f;
            Type->Radius = 2.f;
            Type->XPReward = 0;
            MassId = Sim->SpawnMonster(Type, FVector(0, 100, 0));
            Row = AbilityPrototypeSustainedPayloadTests::Find(*this, false, false, Error);
            if (!Row || !Developer->BeginPreview(Row->DefinitionId, Error))
            {
                return false;
            }
            const auto* Entry = Row->Base.DefaultGraph->Nodes.FindByPredicate(
                [&](const FAbilityGraphNode& Node)
                {
                    return Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
                });
            const auto* Scope = Entry ? Cast<UAbilityPrototypeScopeNodeConfig>(Entry->Config) : nullptr;
            const int32 Sink = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
                [](const FAbilityGraphNode& Node)
                {
                    return Node.TypeId == TEXT("Effect.PrototypeDamageBatch");
                });
            if (!Scope || !Scope->Parameters || Sink == INDEX_NONE)
            {
                Error = TEXT("Actual Follow default has no independent root damage owner.");
                return false;
            }
            Period = Scope->Parameters->Period;
            if (bAppend)
            {
                FAbilityRuntimeDeveloperEdit Edit;
                Edit.DefinitionId = Row->DefinitionId;
                Edit.ModifierId = TEXT("Append.Status.Petrify");
                Edit.NodeIndex = Sink;
                Edit.Value = 2.f;
                Edit.ExpectedBaseHash = Row->BaseHash;
                Edit.ExpectedRevision = Developer->GetDeveloperState().Revision;
                if (!Developer->ApplyEdit(Edit, Error))
                {
                    return false;
                }
            }
            FAbilityRuntimeRecipeSnapshot Accepted;
            if (!Developer->CreateCastSnapshot(Row->Base.AbilityTag, World, Accepted, Error))
            {
                return false;
            }
            if (bAppend)
            {
                // 检查真正可信工厂的事实转换边，不允许另接Area工厂而仍把本测试命名后伤resolver。
                const FGuid OwnerId = Row->Base.DefaultGraph->Nodes[Sink].NodeId;
                const bool bActualResolver = Accepted.Graph->Edges.ContainsByPredicate(
                    [&](const FAbilityGraphEdge& Edge)
                    {
                        const auto* Node = Accepted.Graph->Nodes.FindByPredicate(
                            [&](const FAbilityGraphNode& Candidate)
                            {
                                return Candidate.NodeId == Edge.To;
                            });
                        return Edge.From == OwnerId && Edge.OutputPort == TEXT("Contact") && Node &&
                               Node->TypeId == TEXT("Target.ResolveDamageTarget");
                    });
                if (!bActualResolver)
                {
                    Error = TEXT(
                        "Trusted AppendStatus did not connect its Contact damage result to the original-life resolver.");
                    return false;
                }
            }
            Run = AbilityPrototypeSustainedPayloadTests::Start(*this, *Row, *Accepted.Graph, Error);
            if (!Run)
            {
                return false;
            }
            Run->OnEvent.AddLambda(
                [this](const FAbilityGraphEvent& Event)
                {
                    // Contact是实际逐目标GE后的同步事实；原DamageResolved只为已连接的旧OnHit孩子收集/发布。
                    if (Event.NodeType == TEXT("Effect.PrototypeDamageBatch") && Event.OutputPort == TEXT("Contact") &&
                        Event.Input.bDamageAttempted)
                    {
                        ++DamageFacts;
                    }
                    if (Event.NodeType == TEXT("Status.Petrify") && Event.OutputPort == TEXT("Completed"))
                    {
                        ++StatusActions;
                    }
                });
            return true;
        }

        /** 每次推进真实Follow时钟，GFrameCounter区分每周期，保留原同帧额度合同。 */
        void Pulse()
        {
            ++GFrameCounter;
            Run->Advance(Period);
        }
        float ActorHealth() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
        float MassHealth() const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(MassId, View) ? View.Health : -1.f;
        }
        bool MassPetrified() const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(MassId, View) && View.bPetrified;
        }
        float Mana() const
        {
            return PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        }
    };
} // namespace AbilityFollowDamageResultRuntimeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFollowDamageResult325,
                                 "test_25d.DynamicMage.PrototypeSustained.FollowDamageResultStatus325ActualPeriods",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFollowDamageResult325::RunTest(const FString&)
{
    using namespace AbilityFollowDamageResultRuntimeTests;
    FVector Original = FVector::ZeroVector;
    for (const bool bAppend : {false, true})
    {
        FFollow F;
        FString Error;
        if (!TestTrue(TEXT("Actual Follow preview/accepted graph prepares through the trusted factory"),
                      F.Prepare(bAppend, Error)))
        {
            AddError(Error);
            return false;
        }
        for (int32 Index = 0; Index < 325 && !F.Run->IsFinished(); ++Index)
        {
            F.Pulse();
        }
        TestFalse(TEXT("Original Follow root survives all325 real paid periods"), F.Run->IsFinished());
        // 原默认没有Contact消费者，批动作不会为观察而额外发布事实；实际伤害由下方HP对照验证。
        // 可信Append新增Contact消费者后，两个原生命每周期必须各发布一次真实后伤事实。
        TestEqual(TEXT("Damage facts are published exactly when the actual Contact branch consumes them"),
                  F.DamageFacts, bAppend ? 650 : 0);
        TestTrue(TEXT("Both original backends still lose actual HP"),
                 F.ActorHealth() < 100000.f && F.MassHealth() < 100000.f);
        if (!bAppend)
        {
            Original = FVector(F.ActorHealth(), F.MassHealth(), F.Mana());
        }
        else
        {
            TestEqual(TEXT("Post-damage state leaves original Actor damage unchanged"), F.ActorHealth(),
                      static_cast<float>(Original.X));
            TestEqual(TEXT("Post-damage state leaves original Mass damage unchanged"), F.MassHealth(),
                      static_cast<float>(Original.Y));
            TestEqual(TEXT("Post-damage state preserves exact original upkeep"), F.Mana(),
                      static_cast<float>(Original.Z));
            TestEqual(TEXT("The real resolver applies both independent states on every period"), F.StatusActions, 650);
            TestTrue(TEXT("Actor and Mass have the actual authored post-damage status"),
                     F.IsPetrified() && F.MassPetrified());
        }
        F.Run->Cancel();
        const auto Events = F.Run->GetAcceptedEventCount();
        const FVector After(F.ActorHealth(), F.MassHealth(), F.Mana());
        F.Pulse();
        TestEqual(TEXT("Cancelled post-damage graph has no late events"), F.Run->GetAcceptedEventCount(), Events);
        TestTrue(TEXT("Cancelled post-damage graph has no late damage or upkeep"),
                 After.Equals(FVector(F.ActorHealth(), F.MassHealth(), F.Mana()), .001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFollowDamageResultLives,
                                 "test_25d.DynamicMage.PrototypeSustained.FollowDamageResultOriginalLivesAndSourceGate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFollowDamageResultLives::RunTest(const FString&)
{
    using namespace AbilityFollowDamageResultRuntimeTests;
    for (const bool bSourceRetired : {false, true})
    {
        FFollow F;
        FString Error;
        if (!TestTrue(TEXT("Actual post-damage status factory compiles before life changes"), F.Prepare(true, Error)))
        {
            AddError(Error);
            return false;
        }
        F.Pulse();
        TestTrue(TEXT("Both first original lives receive damage and post-damage state"),
                 F.IsPetrified() && F.MassPetrified());
        if (bSourceRetired)
        {
            auto* Next = F.World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Next->Tags = {TEXT("Player")};
            F.PlayerASC->InitAbilityActorInfo(F.State, Next);
            const FVector Before(F.ActorHealth(), F.MassHealth(), F.Mana());
            const auto Events = F.Run->GetAcceptedEventCount();
            F.Pulse();
            TestEqual(TEXT("Retired original source closes its actual post-damage graph"), F.Run->GetEndReason(),
                      EAbilityGraphEndReason::SourceInvalid);
            TestEqual(TEXT("Retired source cannot publish a late resolver action"), F.Run->GetAcceptedEventCount(),
                      Events);
            TestTrue(TEXT("Retired source cannot pay or damage either backend again"),
                     Before.Equals(FVector(F.ActorHealth(), F.MassHealth(), F.Mana()), .001));
        }
        else
        {
            const auto Transform = F.Target->GetActorTransform();
            F.Target->EnableDungeonPooling();
            F.Target->SleepForDungeonPool(false);
            TestTrue(TEXT("Actual pool wake starts a new target life"),
                     F.Target->WakeFromDungeonPool(Transform, NAME_None));
            const float WokenHealth = F.ActorHealth();
            const float SurvivingMassHealth = F.MassHealth();
            F.Pulse();
            TestFalse(TEXT("Original-life resolver cannot apply old state to the woken Actor"), F.IsPetrified());
            TestEqual(TEXT("Original damage query also preserves the original Actor life"), F.ActorHealth(),
                      WokenHealth);
            TestTrue(TEXT("The other valid Mass life still receives its normal damage/state"),
                     F.MassHealth() < SurvivingMassHealth && F.MassPetrified());
            TestFalse(TEXT("One replaced target cannot terminate the valid source Follow root"), F.Run->IsFinished());
        }
        F.Run->Cancel();
    }
    return true;
}
#endif
