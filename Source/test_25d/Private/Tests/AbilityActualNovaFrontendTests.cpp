// 测试链：实际Nova目录/同一PlayerState ASC -> 正式ASC输入与零编辑预览T输入 -> 原Commit/报告/原GE -> 自然冷却。
// 不RegisterBase、不注入替换图，不替换GA/GE依赖；此为权威输入整合，不声称真实客户端网络/鼠标已验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Ability/MyArcaneNova.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActualNovaZeroEditPreview,
                                 "test_25d.DynamicMage.NativePreview.ActualNovaSameASCDefaultAndZeroEditPreview",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActualNovaZeroEditPreview::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    if (!TestNotNull(TEXT("Original damage coefficient assets are installed"), F.DamageDefaults))
    {
        return false;
    }
    FString Error;
    const auto* Row = F.Definition(
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneNova/GA_ArcaneNova.GA_ArcaneNova_C"), Error);
    if (!TestNotNull(TEXT("Actual original Nova class exists in the real Mage catalog"), Row))
    {
        AddInfo(Error);
        return false;
    }
    auto* Outside = F.EnemyAt(FVector(9000, 0, 0));
    const auto* SameASC = F.PlayerASC;
    float OldDamage = 0.f;
    float OldCost = 0.f;
    for (const bool bPreview : {false, true})
    {
        const auto Slot = FMyGameplayTags::Get().InputTag_DeveloperRecipe;
        if (bPreview)
        {
            if (!TestTrue(TEXT("Same actual ASC begins zero-edit Nova preview"),
                          F.Developer->BeginPreview(Row->DefinitionId, Error)))
            {
                AddInfo(Error);
                return false;
            }
            F.Handle = F.Developer->GetPreviewHandle();
        }
        else
        {
            FGameplayAbilitySpec Spec(Row->Base.AbilityClass, 1, INDEX_NONE, F.State);
            Spec.DynamicAbilityTags.AddTag(Slot);
            Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
            F.Handle = F.PlayerASC->GiveAbility(Spec);
        }
        auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        auto* Ability = Spec ? Cast<UMyArcaneNova>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestNotNull(TEXT("The actual asset's original native frontend instance exists"), Ability) ||
            !TestNotNull(TEXT("Actual original cost effect remains configured"), Ability->GetCostGameplayEffect()) ||
            !TestNotNull(TEXT("Actual original cooldown effect remains configured"),
                         Ability->GetCooldownGameplayEffect()))
        {
            return false;
        }
        F.PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
        const auto* Cost = Ability->GetCostGameplayEffect();
        const auto* Cooldown = Ability->GetCooldownGameplayEffect();
        int32 Costs = 0;
        int32 Cooldowns = 0;
        int32 DamageEffects = 0;
        int32 Reports = 0;
        FString FrozenAtCommit;
        const auto SourceHook = F.PlayerASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Effect, FActiveGameplayEffectHandle)
            {
                Costs += Effect.Def == Cost ? 1 : 0;
                Cooldowns += Effect.Def == Cooldown ? 1 : 0;
                if (Effect.Def == Cost)
                {
                    FrozenAtCommit = Ability->GetRuntimeRecipeSnapshot().ContentHash;
                }
            });
        const auto TargetHook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
            {
                ++DamageEffects;
                TestEqual(TEXT("Original cost is already committed before the real Nova damage"), Costs, 1);
                TestEqual(TEXT("Original cooldown is already committed before the real Nova damage"), Cooldowns, 1);
            });
        const auto ReportHook = F.PlayerASC->OffensiveAttackTargeted.AddLambda(
            [&](UGameplayAbility*, AActor* Target)
            {
                Reports += Target == F.Target ? 1 : 0;
            });
        ON_SCOPE_EXIT
        {
            F.PlayerASC->OnGameplayEffectAppliedDelegateToSelf.Remove(SourceHook);
            TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(TargetHook);
            F.PlayerASC->OffensiveAttackTargeted.Remove(ReportHook);
        };
        TestFalse(TEXT("Default preview never uses a synthetic no-op source"),
                  F.Developer->HasContributions(Row->Base.AbilityTag));
        AddInfo(
            TEXT("ActualFrontendClass=") + GetPathNameSafe(Row->Base.AbilityClass.Get()) +
            TEXT(
                "; Gate=original authority ASC/T input; TargetData/Notify=not in original Nova frontend; GE=original; Network=unverified"));
        FMath::RandInit(812493);
        if (bPreview)
        {
            F.Key(IE_Pressed); // 原T入口，ServerOnly可在真实权威ASC直接激活，无客户端包夹具。
            F.Key(IE_Released);
        }
        else
        {
            F.PlayerASC->AbilityInputTagPressed(Slot);
            F.PlayerASC->AbilityInputTagHeld(Slot);
            F.PlayerASC->AbilityInputTagReleased(Slot);
        }
        TestTrue(TEXT("Both casts use the same actual PlayerState ASC"), SameASC == F.PlayerASC);
        TestFalse(TEXT("Original immediate Nova frontend ends after its actual graph"), Ability->IsActive());
        TestEqual(TEXT("Actual input commits cost once"), Costs, 1);
        TestEqual(TEXT("Actual input commits cooldown once"), Cooldowns, 1);
        TestEqual(TEXT("Original attack-report callback occurs once for the actual Actor"), Reports, 1);
        TestEqual(TEXT("One original GE damages the captured Actor, with no hidden fallback"), DamageEffects, 1);
        const float Damage = 10000.f - TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        const float CostValue = 10000.f - F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        TestTrue(TEXT("Actual original GE removes real Actor health"), Damage > 0.f);
        TestTrue(TEXT("Actual original cost consumes real Mana"), CostValue > 0.f);
        TestEqual(TEXT("Outside actual query remains untouched"),
                  Outside->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  10000.f);
        if (bPreview)
        {
            TestEqual(TEXT("Zero-edit preview freezes the exact selected real default at original Commit"),
                      FrozenAtCommit, Row->BaseHash);
            TestEqual(TEXT("Zero-edit default preserves actual original GE damage"), Damage, OldDamage);
            TestEqual(TEXT("Zero-edit default preserves actual original mana cost"), CostValue, OldCost);
        }
        else
        {
            TestTrue(TEXT("Ordinary original entry does not freeze an artificial dynamic source"),
                     FrozenAtCommit.IsEmpty());
            OldDamage = Damage;
            OldCost = CostValue;
        }
        TestTrue(TEXT("Original cooldown tags naturally clear before a second cast"), F.WaitReady(*Ability));
        if (bPreview)
        {
            const float SpentMana = F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
            TestTrue(TEXT("Closing only this exact preview succeeds"), F.Developer->EndPreview(Error));
            TestEqual(TEXT("Closing preview does not refund or alter real committed mana"),
                      F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), SpentMana);
        }
        else
        {
            F.PlayerASC->ClearAbility(F.Handle);
        }
    }
    return true;
}
#endif
