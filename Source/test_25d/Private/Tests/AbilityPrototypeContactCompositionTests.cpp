// 调用链：真实细节点默认图 -> 删除/旁路某个接触动作 -> 同一图执行器 -> Actor GE/Slow/Niagara。
// 不更改原 DA、不激活嵌套 GA；检查每目标接触状态和表现确实由可替换图节点拥有。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "PureMass/PureMassRuntime.h"
#include "Components/CapsuleComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Script.h"
#include "MyGameplayTags.h"

namespace AbilityPrototypeContactCompositionTests
{
    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        UAbilityGraphInstance* Run = nullptr;

        FWorld()
        {
            Prepare(*Target);
        }

        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        /** 与已有真实 Actor 接触夹具使用相同 overlap/LOS 协议。 */
        static void Prepare(AEnemyCharacter& Actor)
        {
            Actor.GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            Actor.GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            Actor.GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Actor.GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Actor.GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
            Actor.GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        }

        AEnemyCharacter* Other()
        {
            auto* Actor = World->SpawnActor<AEnemyCharacter>();
            Actor->Tags = {TEXT("Enemy")};
            Actor->SetActorLocation(FVector(220, 0, 0));
            auto* TargetASC = Actor->GetAbilitySystemComponent();
            TargetASC->InitAbilityActorInfo(Actor, Actor);
            TargetASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
            Prepare(*Actor);
            return Actor;
        }

        UBatch6FieldPrototype* Parameters(EAbilityPrototypeKind Kind)
        {
            auto* Result = NewObject<UBatch6FieldPrototype>(World);
            Result->Kind = Kind;
            Result->Damage = 25.f;
            Result->DamagePerLevel = 0.f;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Cooldown = 0.f;
            Result->Radius = 350.f;
            Result->Duration = .5f;
            Result->Period = .1f;
            return Result;
        }

        /** GE实际扣10血；测试不能用一个失效的 DamageParams 空转来冒充动作替换。 */
        bool Start(UAbilityGraphDefinition* Definition, FString& Error)
        {
            auto* Context = NewObject<UPrototypeCastContext>(World);
            Context->Caster = Source;
            Context->CastId = FGuid::NewGuid();
            Context->Level = 1;
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            Context->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            if (!Context->CaptureSourceLife(Error))
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Context->GetSourceLife();
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            Input.Direction = FVector::ForwardVector;
            Run = NewObject<UAbilityGraphInstance>(World);
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            return Run->StartWithInput(Definition, Input, Options, Error);
        }
    };

    /** 删除实际图动作，并把其入边连到原后继；不改帧中的原始状态/特效参数。 */
    int32 RemoveAndBypass(UAbilityGraphDefinition& Graph, FName Type)
    {
        TArray<FGuid> Removed;
        for (const auto& Node : Graph.Nodes)
        {
            if (Node.TypeId == Type)
            {
                Removed.Add(Node.NodeId);
            }
        }
        for (FGuid Id : Removed)
        {
            const auto Before = Graph.Edges;
            for (const auto& Incoming : Before)
            {
                if (Incoming.To != Id)
                {
                    continue;
                }
                for (const auto& Outgoing : Before)
                {
                    if (Outgoing.From == Id)
                    {
                        FAbilityGraphEdge Edge;
                        Edge.From = Incoming.From;
                        Edge.OutputPort = Incoming.OutputPort;
                        Edge.To = Outgoing.To;
                        Graph.Edges.Add(Edge);
                    }
                }
            }
            Graph.Edges.RemoveAll(
                [Id](const auto& Edge)
                {
                    return Edge.From == Id || Edge.To == Id;
                });
            Graph.Nodes.RemoveAll(
                [Id](const auto& Node)
                {
                    return Node.NodeId == Id;
                });
        }
        return Removed.Num();
    }

    /** 实际稳定 Niagara 资源；不创建一个测试粒子资产替代项目效果。 */
    UNiagaraSystem* ImpactSystem()
    {
        const auto* Catalog = LoadObject<UAbilityInfo>(
            nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"));
        if (!Catalog)
        {
            return nullptr;
        }
        for (const auto& Row : Catalog->AbilityInformation)
        {
            const auto* Ability = Row.Ability ? Cast<UPrototypeAbility>(Row.Ability->GetDefaultObject()) : nullptr;
            if (Ability && Ability->Prototype && Ability->Prototype->LoopVFX)
            {
                return Ability->Prototype->LoopVFX;
            }
        }
        return nullptr;
    }
} // namespace AbilityPrototypeContactCompositionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineContactSlowIsReplaceable,
    "test_25d.UnifiedGA.DynamicMage.FineContact.RemovingSlowNodePreservesDamageWithoutIntrinsicSlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineContactSlowIsReplaceable::RunTest(const FString&)
{
    using namespace AbilityPrototypeContactCompositionTests;
    for (const bool bRemoveSlow : {false, true})
    {
        FWorld F;
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Field);
        Parameters->SlowMultiplier = .5f;
        Parameters->SlowDuration = 2.f;
        FString Error;
        auto* Definition = AbilityPrototypeFineCompiler::Compile(F.World, Parameters, TEXT("LegacyMixed.v1"), Error);
        if (!TestNotNull(TEXT("Real fine field compiles: ") + Error, Definition))
        {
            return false;
        }
        if (bRemoveSlow)
        {
            TestEqual(TEXT("Default exposes one real replaceable per-contact Slow action"),
                      RemoveAndBypass(*Definition, TEXT("Status.PrototypeSlow")), 1);
        }
        if (!TestTrue(TEXT("Actual field runs with the selected contact body"), F.Start(Definition, Error)))
        {
            AddInfo(Error);
            return false;
        }
        ++GFrameCounter;
        F.Run->Advance(.11f);
        TestEqual(TEXT("Removing only state preserves the one actual damage action"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  90.f);
        TestEqual(TEXT("Slow belongs to the graph action, not a hidden intrinsic DamageBatch branch"),
                  UBatch6SlowComponent::GetMultiplier(F.Target), bRemoveSlow ? 1.f : .5f);
        TestEqual(TEXT("Graph edits never mutate the original slow parameter"), Parameters->SlowMultiplier, .5f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineContactImpactIsReplaceable,
    "test_25d.UnifiedGA.DynamicMage.FineContact.ImpactNodeOwnsEachRealContactAndCanBeRemoved",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineContactImpactIsReplaceable::RunTest(const FString&)
{
    using namespace AbilityPrototypeContactCompositionTests;
    auto* System = ImpactSystem();
    if (!TestNotNull(TEXT("Existing stable Niagara system"), System))
    {
        return false;
    }
    for (const bool bRemoveImpact : {false, true})
    {
        FWorld F;
        auto* Other = F.Other();
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Burst);
        Parameters->ImpactVFX = System;
        Parameters->ImpactVFXScale = FVector(4, 5, 6);
        FString Error;
        auto* Definition = AbilityPrototypeFineCompiler::Compile(F.World, Parameters, TEXT("LegacyMixed.v1"), Error);
        if (!TestNotNull(TEXT("Real fine burst compiles: ") + Error, Definition))
        {
            return false;
        }
        if (bRemoveImpact)
        {
            TestEqual(TEXT("Default exposes one reusable contact presentation action"),
                      RemoveAndBypass(*Definition, TEXT("Presentation.PrototypeImpact")), 1);
        }
        if (!TestTrue(TEXT("Contact composition starts"), F.Start(Definition, Error)))
        {
            AddInfo(Error);
            return false;
        }
        int32 Impacts = 0;
        for (TActorIterator<APureMassRuntime> It(F.World); It; ++It)
        {
            Impacts += It->GetPresentationStats().Impacts;
        }
        TestEqual(TEXT("Actual presentation host receives exactly one event per genuine target contact"), Impacts,
                  bRemoveImpact ? 0 : 2);
        int32 RealEffects = 0;
        for (TObjectIterator<UNiagaraComponent> It; It; ++It)
        {
            if (It->GetWorld() == F.World && It->GetAsset() == System &&
                (It->GetComponentLocation().Equals(F.Target->GetActorLocation()) ||
                 It->GetComponentLocation().Equals(Other->GetActorLocation())) &&
                It->GetComponentScale().Equals(FVector(4, 5, 6)))
            {
                ++RealEffects;
            }
        }
        TestEqual(TEXT("Contact graph produces actual Niagara instances with the frozen scale"), RealEffects,
                  bRemoveImpact ? 0 : 2);
        TestEqual(TEXT("Presentation removal never removes real damage to A"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  90.f);
        TestEqual(TEXT("Presentation removal never removes real damage to B"),
                  Other->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
        F.Run->Cancel();
        ++GFrameCounter;
        F.Run->Advance(2.f);
        int32 AfterCancel = 0;
        for (TActorIterator<APureMassRuntime> It(F.World); It; ++It)
        {
            AfterCancel += It->GetPresentationStats().Impacts;
        }
        TestEqual(TEXT("Cancel closes contact presentation without a fake late finish impact"), AfterCancel, Impacts);
    }
    return true;
}
#endif
