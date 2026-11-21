// 调用链：Automation -> 只读原型细节点编译 -> 同一 GraphInstance -> 原 Mass 属性/死亡/表现宿主。
// 验证合法长场、原逻辑预算、真实 hook 次序与表现寿命；不以 Legacy 宏改名或预制组合代替拆分。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassRuntime.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeFineCompilerTests
{
    /** 所有参数均为本次测试的私有对象，不写原目录/GA/CDO/DA。 */
    UAbilityPrototype* Burst(UObject* Outer, float Radius, float Damage, FGameplayTag Type)
    {
        auto* Result = NewObject<UAbilityPrototype>(Outer);
        Result->Kind = EAbilityPrototypeKind::Burst;
        Result->Cooldown = 0.f;
        Result->Radius = Radius;
        Result->Damage = Damage;
        Result->DamagePerLevel = 0.f;
        Result->DamageType = Type;
        Result->Duration = .1f;
        Result->Period = .1f;
        return Result;
    }

    int32 Enemy(FAbilityGraphTestFixture& F, FVector Position, float Health,
                const TArray<FGameplayTag>& Immunities = {})
    {
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = Health;
        Type->XPReward = 0;
        Type->Radius = 2.f;
        Type->HalfHeight = 10.f;
        Type->Speed = 0.f;
        for (auto Tag : Immunities)
        {
            Type->Resistances.Add(Tag, 100.f);
        }
        return Mass->SpawnMonster(Type, Position);
    }

    float Health(FAbilityGraphTestFixture& F, int32 Id)
    {
        FPureMassView View;
        return F.World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View) ? View.Health : -1.f;
    }

    UPrototypeCastContext* Context(FAbilityGraphTestFixture& F)
    {
        auto* Result = NewObject<UPrototypeCastContext>(F.World);
        Result->Caster = F.Source;
        Result->CastId = FGuid::NewGuid();
        Result->Level = 1;
        Result->DamageParams.SourceAbilitySystemComponent = F.ASC;
        Result->DamageParams.WorldContextObject = F.Source;
        Result->DamageParams.DamageType = FMyGameplayTags::Get().Damage_Magic;
        FString Error;
        Result->CaptureSourceLife(Error);
        return Result;
    }

    /** 确实调用新编译器；旧宏配置即使换 TypeId 也不能通过这条检查。 */
    UAbilityGraphDefinition* Compile(FAutomationTestBase& Test, FAbilityGraphTestFixture& F,
                                     const UAbilityPrototype* Root)
    {
        FString Error;
        auto* Graph = AbilityPrototypeFineCompiler::Compile(F.World, Root, TEXT("LegacyMixed.v1"), Error);
        if (!Test.TestNotNull(TEXT("Actual fine default recipe compiles"), Graph))
        {
            Test.AddInfo(Error);
            return nullptr;
        }
        bool bFine = true;
        for (const auto& Node : Graph->Nodes)
        {
            bFine &= Test.TestFalse(TEXT("Default has no whole legacy runtime TypeId"),
                                    Node.TypeId.ToString().StartsWith(TEXT("Legacy.")));
            bFine &= Test.TestFalse(TEXT("Renaming a legacy macro does not count as decomposition"),
                                    Node.Config && Node.Config->IsA<UAbilityLegacyNodeConfig>());
        }
        return bFine ? Graph : nullptr;
    }

    /** 同一个生产图执行器；预算策略由编译结果显式声明，不在测试中偷偷放宽 Bounded。 */
    bool Start(UAbilityGraphInstance* Run, UAbilityGraphDefinition* Graph, FAbilityGraphTestFixture& F,
               UPrototypeCastContext* Cast, FString& Error)
    {
        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        Input.Direction = FVector::ForwardVector;
        Input.Context = Cast;
        Input.Level = 1.f;
        Input.DamageParams = Cast->DamageParams;
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Graph->CompatibilityProfile;
        Options.BudgetPolicy = Graph->BudgetPolicy;
        Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
        return Run->StartWithInput(Graph, Input, Options, Error);
    }

    /** 读取真实目录引用的稳定 Niagara 资源；不生成临时粒子对象作为复制替身。 */
    UNiagaraSystem* ExistingLoopSystem()
    {
        for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                                  TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"),
                                  TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo")})
        {
            if (auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path))
            {
                for (const auto& Row : Catalog->AbilityInformation)
                {
                    const auto* Ability =
                        Row.Ability ? Cast<UPrototypeAbility>(Row.Ability->GetDefaultObject()) : nullptr;
                    if (Ability && Ability->Prototype && Ability->Prototype->LoopVFX)
                    {
                        return Ability->Prototype->LoopVFX;
                    }
                }
            }
        }
        return nullptr;
    }
} // namespace AbilityPrototypeFineCompilerTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFineFieldSixHundredPulses,
                                 "test_25d.UnifiedGA.DynamicMage.FineCompiler.FieldSixtySecondsSixHundredPulses",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineFieldSixHundredPulses::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineCompilerTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    const int32 Target = Enemy(F, FVector(50, 0, 0), 10000.f);
    auto* Config = NewObject<UBatch6FieldPrototype>(F.World);
    Config->Kind = EAbilityPrototypeKind::Field;
    Config->Cooldown = 0.f;
    Config->Duration = 60.f;
    Config->Period = .1f;
    Config->Radius = 100.f;
    Config->Damage = 1.f;
    Config->DamagePerLevel = 0.f;
    Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
    auto* Graph = Compile(*this, F, Config);
    if (!Graph)
    {
        return false;
    }
    auto* Cast = Context(F);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Finite field starts through actual fine graph"), Start(Run, Graph, F, Cast, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Original finite Field has no initial pulse"), Health(F, Target), 10000.f);
    for (int32 Frame = 0; Frame < 240; ++Frame)
    {
        ++GFrameCounter;
        Run->Advance(.25f);
    }
    TestEqual(TEXT("Complete legal 60/.1 field delivers all 600 real pulses"), Health(F, Target), 9400.f);
    TestEqual(TEXT("Helper query/damage/cadence calls do not consume original logical spawn budget"), Cast->Spawned, 1);
    TestTrue(TEXT("Finite field actually ends at its complete duration"), Run->IsFinished());
    TestEqual(TEXT("Field completes rather than silently hitting helper budget"), Run->GetEndReason(),
              EAbilityGraphEndReason::Completed);
    ++GFrameCounter;
    Run->Advance(1.f);
    TestEqual(TEXT("No pulse occurs after normal end"), Health(F, Target), 9400.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFineLogicalBudget,
                                 "test_25d.UnifiedGA.DynamicMage.FineCompiler.LogicalBudgetExcludesHelperCalls",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineLogicalBudget::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineCompilerTests;
    EAbilityGraphEndReason OriginalReason = EAbilityGraphEndReason::Failed;
    for (const bool bFine : {false, true})
    {
        FAbilityGraphTestFixture F;
        F.Target->Destroy();
        TArray<int32> Targets;
        for (int32 Index = 0; Index < 70; ++Index)
        {
            Targets.Add(Enemy(F, FVector(100 + Index * 10, 0, 0), 100.f));
        }
        auto* Config = Burst(F.World, 1000.f, 1.f, FMyGameplayTags::Get().Damage_Magic);
        Config->OnHit = Burst(F.World, 1.f, 2.f, FMyGameplayTags::Get().Damage_Magic);
        FString Error;
        auto* Graph = bFine ? Compile(*this, F, Config)
                            : UAbilityLegacyGraphCompiler::Compile(F.World, Config, TEXT("LegacyMixed.v1"), Error);
        if (!TestNotNull(TEXT("Actual original/fine crowded default compiles: ") + Error, Graph))
        {
            return false;
        }
        auto* Cast = Context(F);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        if (!TestTrue(TEXT("Crowded root with actual OnHit fragments is accepted"), Start(Run, Graph, F, Cast, Error)))
        {
            AddInfo(Error);
            return false;
        }
        for (int32 Frame = 0; Frame < 64 && !Run->IsFinished(); ++Frame)
        {
            ++GFrameCounter;
            Run->Advance(.25f);
        }
        TestEqual(TEXT("Original root plus exactly 63 children use the original 64 logical budget"), Cast->Spawned, 64);
        for (int32 Index = 0; Index < Targets.Num(); ++Index)
        {
            TestEqual(FString::Printf(TEXT("%s target %d retains root/accepted-child damage"),
                                      bFine ? TEXT("Fine") : TEXT("Original"), Index),
                      Health(F, Targets[Index]), Index < 63 ? 97.f : 99.f);
        }
        TestTrue(TEXT("The actual finite graph reaches a terminal state"), Run->IsFinished());
        if (!bFine)
        {
            OriginalReason = Run->GetEndReason();
            AddInfo(FString::Printf(
                TEXT("Actual LegacyGraph crowded control: EndReason=%d, ContextSpawned=%d, Cancelled=%d"),
                static_cast<int32>(OriginalReason), Cast->Spawned, Cast->bCancelled));
            TestTrue(TEXT("Original logical admission is not an internal helper budget exception"),
                     OriginalReason != EAbilityGraphEndReason::BudgetExceeded);
        }
        else
        {
            TestEqual(TEXT("Fine preserves the measured original rejection stage and terminal reason"),
                      Run->GetEndReason(), OriginalReason);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFineHooksActualOrder,
                                 "test_25d.UnifiedGA.DynamicMage.FineCompiler.FieldAndTrapActualHookOrder",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineHooksActualOrder::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineCompilerTests;
    const auto& Tags = FMyGameplayTags::Get();
    for (const bool bTrap : {false, true})
    {
        FAbilityGraphTestFixture F;
        F.Target->Destroy();
        Enemy(F, FVector(0, 0, 0), 1000.f);
        Enemy(F, FVector(5, 0, 0), 1000.f);
        const int32 HitOnly = Enemy(F, FVector(15, 0, 0), 1.f, {Tags.Damage_Fire, Tags.Damage_Water});
        const int32 PeriodOnly =
            bTrap ? INDEX_NONE : Enemy(F, FVector(50, 0, 0), 1.f, {Tags.Damage_Thunder, Tags.Damage_Water});
        const int32 ExpireOnly = Enemy(F, FVector(150, 0, 0), 1.f, {Tags.Damage_Thunder, Tags.Damage_Fire});
        UAbilityPrototype* Config =
            bTrap ? NewObject<UAbilityPrototype>(F.World) : NewObject<UBatch6FieldPrototype>(F.World);
        Config->Kind = bTrap ? EAbilityPrototypeKind::Trap : EAbilityPrototypeKind::Field;
        Config->Cooldown = 0.f;
        Config->Radius = 10.f;
        Config->Duration = bTrap ? 1.f : .2f;
        Config->Period = .1f;
        Config->ArmDelay = .1f;
        Config->Damage = 1.f;
        Config->DamagePerLevel = 0.f;
        Config->DamageType = Tags.Damage_Magic;
        Config->OnHit = Burst(F.World, 20.f, 1.f, Tags.Damage_Thunder);
        Config->OnExpire = Burst(F.World, 200.f, 1.f, Tags.Damage_Water);
        if (!bTrap)
        {
            Config->OnPeriod = Burst(F.World, 100.f, 1.f, Tags.Damage_Fire);
        }
        auto* Graph = Compile(*this, F, Config);
        if (!Graph)
        {
            return false;
        }
        TArray<int32> ActualDeaths;
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        const auto Handle = Mass->OnMonsterRemoved.AddLambda(
            [&](int32 Id, FName, EPureMassRemovalReason Reason)
            {
                if (Reason == EPureMassRemovalReason::Killed)
                {
                    ActualDeaths.Add(Id);
                }
            });
        ON_SCOPE_EXIT
        {
            Mass->OnMonsterRemoved.Remove(Handle);
        };
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        if (!TestTrue(TEXT("Finite multi-target recipe starts"), Start(Run, Graph, F, Context(F), Error)))
        {
            AddInfo(Error);
            return false;
        }
        ++GFrameCounter;
        Run->Advance(bTrap ? .11f : .25f);
        // 不让后阶段“代杀”前阶段哨兵：它们分别只承受 Thunder、Fire、Water。
        const int32 HitIndex = ActualDeaths.IndexOfByKey(HitOnly);
        const int32 PeriodIndex = ActualDeaths.IndexOfByKey(PeriodOnly);
        const int32 ExpireIndex = ActualDeaths.IndexOfByKey(ExpireOnly);
        TestTrue(TEXT("Real OnHit initial action settles within triggering frame"), HitIndex != INDEX_NONE);
        TestTrue(TEXT("Real OnExpire action settles within triggering frame"), ExpireIndex != INDEX_NONE);
        TestTrue(TEXT("Actual OnHit damage occurs before actual OnExpire damage"),
                 HitIndex != INDEX_NONE && ExpireIndex > HitIndex);
        if (!bTrap)
        {
            TestTrue(TEXT("Real OnPeriod initial action settles within the same frame"), PeriodIndex != INDEX_NONE);
            TestTrue(TEXT("Every pulse's hit actions precede the first OnPeriod action"),
                     HitIndex != INDEX_NONE && PeriodIndex > HitIndex);
            TestTrue(TEXT("Final OnPeriod action precedes OnExpire"),
                     PeriodIndex != INDEX_NONE && ExpireIndex > PeriodIndex);
        }
        TestEqual(TEXT("Each branch-specific actual victim dies exactly once"), ActualDeaths.Num(), bTrap ? 2 : 3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFinePresentationFullLifetime,
                                 "test_25d.UnifiedGA.DynamicMage.FineCompiler.PresentationHostFullDuration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFinePresentationFullLifetime::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineCompilerTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    const int32 Target = Enemy(F, FVector(50, 0, 0), 200.f);
    auto* Config = Burst(F.World, 100.f, 17.f, FMyGameplayTags::Get().Damage_Magic);
    Config->Duration = .5f;
    Config->LoopVFX = ExistingLoopSystem();
    auto* Graph = Compile(*this, F, Config);
    if (!Graph)
    {
        return false;
    }
    if (!TestNotNull(TEXT("Actual directory supplies a stable existing loop system"), Config->LoopVFX.Get()))
    {
        return false;
    }
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Fine damage and presentation start together"), Start(Run, Graph, F, Context(F), Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Actual initial damage is applied once"), Health(F, Target), 183.f);
    APureMassRuntime* Host = nullptr;
    int32 Hosts = 0;
    for (TActorIterator<APureMassRuntime> It(F.World); It; ++It)
    {
        if (It->GetOwner() == F.Source)
        {
            if (!It->HasActorBegunPlay())
            {
                It->DispatchBeginPlay();
            }
            Host = *It;
            ++Hosts;
        }
    }
    if (!TestNotNull(TEXT("Existing replicated presentation host is actually created"), Host))
    {
        return false;
    }
    TestEqual(TEXT("One logical burst owns one presentation host"), Hosts, 1);
    TestTrue(TEXT("Host retains original replicated ownership"), Host->GetIsReplicated());
    bool bStableSystem = false;
    for (auto* Component : TInlineComponentArray<UNiagaraComponent*>(Host))
    {
        bStableSystem |= Component->GetAsset() == Config->LoopVFX;
    }
    TestTrue(TEXT("Stable Niagara system reference reaches actual presentation component"), bStableSystem);
    TestEqual(TEXT("Presentation BeginPlay does not execute a second legacy Burst"), Health(F, Target), 183.f);
    ++GFrameCounter;
    Run->Advance(.25f);
    TestTrue(TEXT("Presentation remains owned after immediate logical damage ends"),
             IsValid(Host) && !Host->IsFinished());
    TestFalse(TEXT("Burst does not discard its original duration"), Run->IsFinished());
    ++GFrameCounter;
    Run->Advance(.26f);
    TestTrue(TEXT("Normal duration closes the actual presentation owner"), !IsValid(Host) || Host->IsFinished());
    TestTrue(TEXT("Finite default eventually completes"), Run->IsFinished());
    TestEqual(TEXT("Lifetime branch never repeats initial burst damage"), Health(F, Target), 183.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeLongFieldOptionalAdmission,
    "test_25d.UnifiedGA.DynamicMage.LogicalAdmission.LongFieldRejectsOptionalChildrenWithoutStoppingDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeLongFieldOptionalAdmission::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FAbilityGraphTestFixture F;
        F.Target->Destroy();
        const int32 Victim = Enemy(F, FVector(50, 0, 0), 10000.f);
        auto* Parameters = NewObject<UBatch6FieldPrototype>(F.World);
        Parameters->Kind = EAbilityPrototypeKind::Field;
        Parameters->Cooldown = 0.f;
        Parameters->Radius = 100.f;
        Parameters->Duration = 60.f;
        Parameters->Period = .1f;
        Parameters->Damage = 1.f;
        Parameters->DamagePerLevel = 0.f;
        Parameters->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Parameters->SlowMultiplier = 1.f;
        Parameters->OnPeriod = Burst(F.World, 100.f, 2.f, Parameters->DamageType);
        FString Error;
        auto* Graph = bFine ? Compile(*this, F, Parameters)
                            : UAbilityLegacyGraphCompiler::Compile(F.World, Parameters, TEXT("LegacyMixed.v1"), Error);
        if (!TestNotNull(TEXT("Complete original/fine long field compiles: ") + Error, Graph))
        {
            return false;
        }
        auto* Cast = Context(F);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        if (!TestTrue(TEXT("Original/fine finite field is accepted"), Start(Run, Graph, F, Cast, Error)))
        {
            return false;
        }
        for (int32 Frame = 0; Frame < 240; ++Frame)
        {
            ++GFrameCounter;
            Run->Advance(.25f);
        }
        TestEqual(TEXT("All600 field pulses remain real after only63 optional children are admitted"),
                  Health(F, Victim), 9274.f);
        TestEqual(TEXT("Original root plus63 optional children retain original total logical64"), Cast->Spawned, 64);
        TestFalse(TEXT("Pre-refused optional emission does not cancel shared context"), Cast->bCancelled);
        TestTrue(TEXT("Original full duration actually ends"), Run->IsFinished());
        TestEqual(TEXT("Optional admission rejection preserves normal field completion"), Run->GetEndReason(),
                  EAbilityGraphEndReason::Completed);
    }
    return true;
}
#endif
