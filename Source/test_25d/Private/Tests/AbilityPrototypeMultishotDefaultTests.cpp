// 调用链：真实Compile入口/旧控制 -> container/同深度单弹Scope -> 真实Mass接触 -> 各自原hooks。
// 不使用testlocal注册，明确验证制作出口的builtin环境和原逻辑/物理预算。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMultishotDefaultCompilesInProductionEnvironment,
    "test_25d.UnifiedGA.DynamicMage.FineDefaults.MultishotCompileUsesBuiltinAndEachPelletOwnHooks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMultishotDefaultCompilesInProductionEnvironment::RunTest(const FString&)
{
    for (const bool bFine : {false, true})
    {
        FEditorScriptExecutionGuard Guard;
        FAbilityGraphTestFixture F;
        F.Target->Destroy();
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = 200.f;
        Type->Radius = 2.f;
        Type->HalfHeight = 10.f;
        Type->XPReward = 0;
        const int32 Victim = Mass->SpawnMonster(Type, FVector(200, 0, 0));
        auto* Root = NewObject<UAbilityPrototype>(F.World);
        Root->Kind = EAbilityPrototypeKind::Multishot;
        Root->Count = 3;
        Root->SpreadDegrees = 0.f;
        Root->Speed = 1000.f;
        Root->Range = 400.f;
        Root->Duration = 2.f;
        Root->Damage = 5.f;
        Root->DamagePerLevel = 0.f;
        Root->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Root->Cooldown = 0.f;
        for (const bool bExpiry : {false, true})
        {
            auto* Child = NewObject<UAbilityPrototype>(F.World);
            Child->Kind = EAbilityPrototypeKind::Burst;
            Child->Damage = bExpiry ? 2.f : 3.f;
            Child->DamagePerLevel = 0.f;
            Child->DamageType = Root->DamageType;
            Child->Cooldown = 0.f;
            Child->Radius = bExpiry ? 20.f : 1.f;
            Child->Duration = .1f;
            if (bExpiry)
            {
                Root->OnExpire = Child;
            }
            else
            {
                Root->OnHit = Child;
            }
        }
        FString Error;
        auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(F.World, Root, TEXT("LegacyMixed.v1"), Error)
                                 : UAbilityLegacyGraphCompiler::Compile(F.World, Root, TEXT("LegacyMixed.v1"), Error);
        if (!TestNotNull(FString(bFine ? TEXT("Actual production fine Compile: ") : TEXT("Original control: ")) + Error,
                         Definition))
        {
            return false;
        }
        auto* Context = NewObject<UPrototypeCastContext>(F.World);
        Context->Caster = F.Source;
        Context->Level = 1;
        Context->CastId = FGuid::NewGuid();
        Context->DamageParams.SourceAbilitySystemComponent = F.ASC;
        Context->CaptureSourceLife(Error);
        FAbilityNodeInput Input;
        Input.SourceToken = Context->GetSourceLife();
        Input.Context = Context;
        Input.DamageParams = Context->DamageParams;
        Input.Direction = FVector::ForwardVector;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Definition->CompatibilityProfile;
        Options.BudgetPolicy = Definition->BudgetPolicy;
        if (!TestTrue(TEXT("Default actually starts in builtin environment: ") + Error,
                      Run->StartWithInput(Definition, Input, Options, Error)))
        {
            return false;
        }
        TestEqual(TEXT("Container and three single pellets each reserve once"), Context->Spawned, 4);
        ++GFrameCounter;
        Run->Advance(.3f);
        FPureMassView View;
        if (TestTrue(TEXT("Real target remains alive"), Mass->Resolve(Victim, View)))
        {
            TestEqual(TEXT("Only three pellets own root5/hit3/expiry2; container has no hooks"), View.Health, 170.f);
        }
        TestEqual(TEXT("No duplicate container hooks enter logical budget"), Context->Spawned, 10);
        Run->Cancel();
    }
    return true;
}
#endif
