// 测试链：真实原生配置 -> 运行时细节点图 -> 实际Actor/Mass扣血、替换payload、同步取消与池生命。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaFragmentNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatchCombatNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "PureMass/PureMassSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace AbilityNativeFragmentTests
{
    /** 临时世界保留实际目标查询和GE；只把伤害GE替换为确定性10点，避免随机曲线干扰。 */
    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityGraphInstance* Run = nullptr;
        UAbilityNovaNodeConfig* Settings = nullptr;
        UAbilityNovaNodeContext* Binding = nullptr;
        FAbilityNodeInput Input;
        UPureMassSubsystem* Mass = nullptr;
        int32 MassId = INDEX_NONE;

        FFixture()
        {
            Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),
                                                                         100.f);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 0;
            MassId = Mass->SpawnMonster(Type, FVector(200.f, 0.f, 0.f));
            Settings = NewObject<UAbilityNovaNodeConfig>(World);
            Settings->Radius = 300.f;
            Settings->bIncludeMass = true;
            Binding = NewObject<UAbilityNovaNodeContext>(World);
            Input.SourceToken = SourceToken;
            Input.Origin = FVector::ZeroVector;
            Input.Context = Binding;
            Input.DamageParams.SourceAbilitySystemComponent = ASC;
            Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            Input.DamageParams.DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Magic"));
            Input.DamageParams.BaseDamage = 10.f;
            Run = NewObject<UAbilityGraphInstance>(World);
        }
        ~FFixture()
        {
            Run->Cancel();
        }
        float Health() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
        float MassHealth() const
        {
            FPureMassView View;
            return Mass->Resolve(MassId, View) ? View.Health : 0.f;
        }
        bool Start(UAbilityGraphDefinition* Definition, FString& Error)
        {
            return Run->StartWithInput(Definition, Input, {}, Error);
        }
    };
} // namespace AbilityNativeFragmentTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNovaFragments,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFragments.NovaDefaultAndReplaceablePayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeNovaFragments::RunTest(const FString&)
{
    using namespace AbilityNativeFragmentTests;
    FFixture F;
    auto* Graph = AbilityNovaFragments::Build(F.World, *F.Settings);
    int32 Reports = 0;
    int32 Results = 0;
    F.Binding->BeforeActorDamage = [&](AActor*)
    {
        ++Reports;
        TestEqual(TEXT("Original attack report precedes every payload"), F.Health(), 100.f);
    };
    F.Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Results += Event.OutputPort == TEXT("DamageResolved") ? 1 : 0;
        });
    FString Error;
    TestTrue(TEXT("Fine default is a valid graph"), F.Start(Graph, Error));
    TestEqual(TEXT("Actor takes one real GE"), F.Health(), 90.f);
    TestEqual(TEXT("Mass takes one equivalent payload"), F.MassHealth(), 90.f);
    TestEqual(TEXT("One original attack report"), Reports, 1);
    TestEqual(TEXT("Each actual target reports a result"), Results, 2);
    TestEqual(TEXT("Default completes after presentation"), F.Run->GetEndReason(), EAbilityGraphEndReason::Completed);

    FFixture Replaced;
    Graph = AbilityNovaFragments::Build(Replaced.World, *Replaced.Settings);
    auto* Damage = NewObject<UAbilityDamageBatchNodeConfig>(Graph);
    Damage->Damage = 27.f;
    Damage->DamagePerLevel = 0.f;
    Damage->DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Magic"));
    Graph->Nodes[1].TypeId = TEXT("Effect.DamageBatch");
    Graph->Nodes[1].Config = Damage;
    TestTrue(TEXT("Alternate payload starts with unchanged native selection"), Replaced.Start(Graph, Error));
    TestEqual(TEXT("Replaced Actor payload still applies exactly one GE"), Replaced.Health(), 90.f);
    TestEqual(TEXT("Mass observes alternate fixed damage, not original Nova plus replacement"), Replaced.MassHealth(),
              73.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNovaLifeAndCancel,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFragments.NovaFrozenLifeAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeNovaLifeAndCancel::RunTest(const FString&)
{
    using namespace AbilityNativeFragmentTests;
    FFixture F;
    F.Binding->BeforeActorDamage = [&](AActor*)
    {
        F.Target->EnableDungeonPooling();
    };
    FString Error;
    auto* Graph = AbilityNovaFragments::Build(F.World, *F.Settings);
    TestTrue(TEXT("Original report callback runs inside the real query"), F.Start(Graph, Error));
    TestEqual(TEXT("Callback cannot rebind captured target to pooled replacement life"), F.Health(), 100.f);
    TestEqual(TEXT("Unrelated captured Mass life still receives payload"), F.MassHealth(), 90.f);

    FFixture Cancelled;
    Cancelled.Binding->BeforeActorDamage = [&Cancelled](AActor*)
    {
        Cancelled.Run->Cancel();
    };
    Graph = AbilityNovaFragments::Build(Cancelled.World, *Cancelled.Settings);
    Cancelled.Start(Graph, Error);
    TestEqual(TEXT("Synchronous source callback cancels before damage"), Cancelled.Health(), 100.f);
    TestEqual(TEXT("Cancelled branch cannot apply Mass payload"), Cancelled.MassHealth(), 100.f);
    TestEqual(TEXT("Cancellation is not normal completion"), Cancelled.Run->GetEndReason(),
              EAbilityGraphEndReason::Cancelled);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNovaScalarRecipe,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFragments.NovaRuntimeDamageScalar",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 真实编译器修改独立payload的主伤害参数，不能只改UI数值或在原伤害后追加第二次结算。 */
bool FNativeNovaScalarRecipe::RunTest(const FString&)
{
    using namespace AbilityNativeFragmentTests;
    FFixture F;
    auto* Base = AbilityNovaFragments::Build(F.World, *F.Settings);
    FAbilityRuntimeRecipeContribution Rune;
    Rune.SourceId = TEXT("NativeNovaDamageRune");
    FAbilityRuntimeRecipePatch Patch;
    Patch.TargetTypeId = TEXT("Effect.NovaDamage");
    Patch.Operation = EAbilityRecipePatchOperation::DamageScale;
    Patch.Scalar = .5f;
    Rune.Patches.Add(Patch);
    UAbilityGraphDefinition* Compiled = nullptr;
    FString Error;
    if (!TestTrue(TEXT("Actual native damage fragment accepts its typed scalar"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {Rune}, Compiled, Error)))
    {
        AddInfo(Error);
        return false;
    }
    int32 ActorEffects = 0;
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    const auto Applied = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
        {
            ++ActorEffects;
            TestEqual(TEXT("Real Actor spec carries same scaled damage as Mass"),
                      Spec.GetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.Magic")), false), 5.f);
        });
    TestTrue(TEXT("Compiled rune is executed by same graph"), F.Start(Compiled, Error));
    TestEqual(TEXT("Actor receives exactly one real GE"), F.Health(), 90.f);
    TestEqual(TEXT("Mass receives the scaled damage exactly once"), F.MassHealth(), 95.f);
    TestEqual(TEXT("Actor has no second hidden payload"), ActorEffects, 1);
    TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Applied);
    return true;
}
#endif
