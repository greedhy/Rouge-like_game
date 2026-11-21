// 测试链：原生细图编译阶段倍率 -> 同一真实宿主 -> target-authored Period/source-authored Final -> SBC及HP。
#include "AbilityNativePhaseScalarTestTypes.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(NativeThunderScalarDamageTag, "Damage.Thunder");

UAbilityNativeThunderScalarEffect::UAbilityNativeThunderScalarEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FSetByCallerFloat Value;
    Value.DataTag = GetDamageTag();
    auto& Modifier = Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetIncomingDamageAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Value);
}

FGameplayTag UAbilityNativeThunderScalarEffect::GetDamageTag()
{
    return NativeThunderScalarDamageTag;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteFragments.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace AbilityNativePhaseScalarTests
{
    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityElectrocuteContext* Binding = nullptr;
        UAbilityGraphInstance* Run = nullptr;
        FAbilityNodeInput Input;
        UAbilityGraphDefinition* Definition = nullptr;
        FFixture()
        {
            auto* Config = NewObject<UAbilityElectrocuteNodeConfig>(World);
            Config->bTraceFromWeapon = false;
            Config->MaxAdditionalTargets = 0;
            Definition = AbilityElectrocuteFragments::Build(World, *Config);
            auto* TargetASC = Target->GetAbilitySystemComponent();
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
            Binding = NewObject<UAbilityElectrocuteContext>(World);
            Binding->AimActor = Target;
            Input.Context = Binding;
            Input.SourceToken = SourceToken;
            Input.LiveTargetToken = TargetToken;
            Input.Origin = Target->GetActorLocation();
            Input.DamageParams.SourceAbilitySystemComponent = ASC;
            Input.DamageParams.DamageGameplayEffectClass = UAbilityNativeThunderScalarEffect::StaticClass();
            Input.DamageParams.DamageType = UAbilityNativeThunderScalarEffect::GetDamageTag();
            Input.DamageParams.BaseDamage = 13.f;
            Input.DamageParams.DebuffChance = 0.f;
            Input.DamageParams.DebuffDamage = 8.f;
            Input.DamageParams.DebuffFrequency = .5f;
            Input.DamageParams.DebuffDuration = 2.f;
            Run = NewObject<UAbilityGraphInstance>(World);
        }
        ~FFixture()
        {
            Run->Cancel();
        }
        bool Start(FString& Error)
        {
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            const bool bStarted = Run->StartWithInput(Definition, Input, Options, Error);
            World->GetTimerManager().Tick(0.f);
            return bStarted;
        }
        void Period()
        {
            ++GFrameCounter;
            World->GetTimerManager().Tick(.11f);
            Run->Advance(.11f);
        }
        float Health() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
    };

    bool Exercise(FAutomationTestBase& Test, FName ScaledType)
    {
        for (const bool bModified : {false, true})
        {
            FFixture F;
            FString Error;
            if (bModified)
            {
                FAbilityRuntimeRecipeContribution Rune;
                Rune.SourceId = TEXT("Rune.NativePhaseOnly");
                FAbilityRuntimeRecipePatch Patch;
                Patch.TargetTypeId = ScaledType;
                Patch.Operation = EAbilityRecipePatchOperation::DamageScale;
                Patch.Scalar = 2.f;
                Rune.Patches.Add(Patch);
                UAbilityGraphDefinition* Compiled = nullptr;
                if (!Test.TestTrue(TEXT("A typed rune scales exactly one native phase"),
                                   AbilityRuntimeRecipes::Compile(F.World, F.Definition, {Rune}, Compiled, Error)))
                {
                    Test.AddInfo(Error);
                    return false;
                }
                F.Definition = Compiled;
            }
            int32 Costs = 0;
            int32 Cooldowns = 0;
            int32 Periods = 0;
            int32 Finals = 0;
            float PeriodMagnitude = 0.f;
            float FinalMagnitude = 0.f;
            F.Binding->CommitPeriodCost = [&]()
            {
                ++Costs;
                return true;
            };
            F.Binding->CommitEndCooldown = [&]()
            {
                ++Cooldowns;
                Test.TestEqual(TEXT("Actual source-authored final precedes cooldown"), Finals, 1);
            };
            auto* TargetASC = F.Target->GetAbilitySystemComponent();
            const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                {
                    const float Magnitude =
                        Spec.GetSetByCallerMagnitude(UAbilityNativeThunderScalarEffect::GetDamageTag(), false);
                    const auto* Instigator = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
                    if (Instigator == TargetASC)
                    {
                        ++Periods;
                        PeriodMagnitude = Magnitude;
                    }
                    if (Instigator == F.ASC)
                    {
                        ++Finals;
                        FinalMagnitude = Magnitude;
                    }
                });
            Test.TestTrue(TEXT("Native fine graph starts with actual SBC damage dependency"), F.Start(Error));
            Test.AddInfo(Error);
            F.Period();
            const float ExpectedPeriod = bModified && ScaledType == TEXT("Effect.ElectrocutePeriod") ? 26.f : 13.f;
            const float ExpectedFinal = bModified && ScaledType == TEXT("Effect.ElectrocuteFinal") ? 26.f : 13.f;
            Test.TestEqual(TEXT("Target-authored period is consumed exactly once"), Periods, 1);
            Test.TestEqual(TEXT("Only the requested period magnitude changes"), PeriodMagnitude, ExpectedPeriod);
            Test.TestEqual(TEXT("Actual health reflects the period SBC"), F.Health(), 1000.f - ExpectedPeriod);
            F.Binding->RequestCompletion();
            F.Run->FlushPublishedEvents(true);
            Test.TestEqual(TEXT("Source-authored final executes once"), Finals, 1);
            Test.TestEqual(TEXT("Only the requested final magnitude changes"), FinalMagnitude, ExpectedFinal);
            Test.TestEqual(TEXT("Actual health reflects both independently configured phases"), F.Health(),
                           1000.f - ExpectedPeriod - ExpectedFinal);
            Test.TestEqual(TEXT("Phase modifiers never duplicate upkeep"), Costs, 1);
            Test.TestEqual(TEXT("Phase modifiers never duplicate cooldown"), Cooldowns, 1);
            TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
        }
        return true;
    }
} // namespace AbilityNativePhaseScalarTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePeriodScalar,
                                 "test_25d.DynamicMage.NativeScalars.PeriodMultiplierKeepsFinalAndFees",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativePeriodScalar::RunTest(const FString&)
{
    return AbilityNativePhaseScalarTests::Exercise(*this, TEXT("Effect.ElectrocutePeriod"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFinalScalar,
                                 "test_25d.DynamicMage.NativeScalars.FinalMultiplierKeepsPeriodAndFees",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeFinalScalar::RunTest(const FString&)
{
    return AbilityNativePhaseScalarTests::Exercise(*this, TEXT("Effect.ElectrocuteFinal"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePhaseCumulativeScalars,
                                 "test_25d.DynamicMage.NativeScalars.CumulativeMainAndDOTAreConsumedOnce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativePhaseCumulativeScalars::RunTest(const FString&)
{
    using namespace AbilityNativePhaseScalarTests;
    FFixture F;
    F.Input.DamageScale = 2.f;
    F.Input.DebuffDamageScale = .25f;
    int32 Costs = 0;
    int32 Cooldowns = 0;
    int32 Periods = 0;
    int32 Finals = 0;
    F.Binding->CommitPeriodCost = [&]()
    {
        ++Costs;
        return true;
    };
    F.Binding->CommitEndCooldown = [&]()
    {
        ++Cooldowns;
    };
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
        {
            TestEqual(TEXT("Actual GE receives cumulative main scale once"),
                      Spec.GetSetByCallerMagnitude(UAbilityNativeThunderScalarEffect::GetDamageTag(), false), 26.f);
            const auto* Instigator = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
            if (Instigator == TargetASC)
            {
                ++Periods;
                TestEqual(TEXT("Period still has no hidden Final debuff payload"),
                          Spec.GetSetByCallerMagnitude(FMyGameplayTags::Get().Debuff_Damage, false, -999.f), -999.f);
            }
            else if (Instigator == F.ASC)
            {
                ++Finals;
                TestEqual(TEXT("Full Final preserves its independent cumulative DOT parameter"),
                          Spec.GetSetByCallerMagnitude(FMyGameplayTags::Get().Debuff_Damage, false), 2.f);
            }
        });
    FString Error;
    TestTrue(TEXT("Cumulative payload starts in the same actual native graph"), F.Start(Error));
    AddInfo(Error);
    F.Period();
    TestEqual(TEXT("Actual period removes26 health"), F.Health(), 974.f);
    F.Binding->RequestCompletion();
    F.Run->FlushPublishedEvents(true);
    TestEqual(TEXT("Actual final removes exactly26 additional health"), F.Health(), 948.f);
    TestEqual(TEXT("One target-authored period remains"), Periods, 1);
    TestEqual(TEXT("One source-authored final remains"), Finals, 1);
    TestEqual(TEXT("Original unscaled main params remain untouched"), F.Input.DamageParams.BaseDamage, 13.f);
    TestEqual(TEXT("Original unscaled DOT params remain untouched"), F.Input.DamageParams.DebuffDamage, 8.f);
    TestEqual(TEXT("Cumulative scalars do not alter period payment count"), Costs, 1);
    TestEqual(TEXT("Cumulative scalars do not alter cooldown count"), Cooldowns, 1);
    TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
    return true;
}
#endif
