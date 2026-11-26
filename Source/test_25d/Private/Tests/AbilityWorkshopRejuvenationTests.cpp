// 测试链：工作台目录 -> 服务器授予临时 Spec -> 玩家 T 键 -> GAS Commit -> 统一图 -> 原属性与护盾。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/WorkshopRejuvenationAbilities.h"
#include "PureMass/PureMassIncomingDamageEffect.h"

namespace
{
    /** 使用正式目录和试装租约授予技能，避免临时夹具替换原生技能的费用和默认图。 */
    bool BeginRejuvenationPreview(FAbilityActualMageFrontendFixture& Fixture, const TCHAR* Path, FString& Error)
    {
        const auto* Definition = Fixture.Definition(Path, Error);
        if (!Definition)
        {
            Error = FString::Printf(TEXT("Missing rejuvenation catalog definition: %s. %s"), Path, *Error);
            return false;
        }
        Fixture.Developer->ServerBeginCardPreview(Definition->DefinitionId, FGuid::NewGuid());
        return Fixture.Developer->GetPreviewHandle().IsValid();
    }

    /** 复用第四页的卡片事务与草稿令牌，验证修改进入下一次施法快照。 */
    bool ApplyRejuvenationCard(FAbilityActualMageFrontendFixture& Fixture, FName Card, FString& Error)
    {
        auto& Developer = *Fixture.Developer;
        const auto& State = Developer.GetDeveloperState();
        return Developer.PreviewCard(Card, State.PreviewSessionId, State.Revision, Error) &&
               Developer.ApplyGraphDraft(State.DraftToken, State.Revision, Error);
    }

    /** 原输入和服务器直接施法的调用方式，与工作台试玩相同。 */
    void CastRejuvenationPreview(FAbilityActualMageFrontendFixture& Fixture)
    {
        Fixture.Key(IE_Pressed);
        Fixture.Key(IE_Released);
        Fixture.Step(.05f);
    }

    /** 由敌人的真实伤害 GE 触发玩家的元属性，读取减伤后的实际生命值。 */
    void DamageRejuvenationCaster(FAbilityActualMageFrontendFixture& Fixture, float Amount)
    {
        auto* SourceASC = Fixture.Target->GetAbilitySystemComponent();
        auto Spec = SourceASC->MakeOutgoingSpec(UPureMassIncomingDamageEffect::StaticClass(), 1.f,
                                                SourceASC->MakeEffectContext());
        Spec.Data->SetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(), Amount);
        SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, Fixture.PlayerASC);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopTidalMendCast, "test_25d.UnifiedGA.WorkshopRejuvenation.TidalMendCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 实际施放三次恢复，校验开始、间隔、整次费用、生命上限。 */
bool FWorkshopTidalMendCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Tidal Mend uses the real catalog trial"),
                  BeginRejuvenationPreview(Fixture, TEXT("/Script/test_25d.WorkshopTidalMend"), Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto Mana = UMyAttributeSet::GetManaAttribute();
    Fixture.PlayerASC->SetNumericAttributeBase(Health, 100.f);
    Fixture.PlayerASC->SetNumericAttributeBase(Mana, 100.f);
    const auto* EnemyASC = Fixture.Target->GetAbilitySystemComponent();
    const float InitialEnemyHealth = EnemyASC->GetNumericAttribute(Health);
    CastRejuvenationPreview(Fixture);
    TestEqual(TEXT("The first pulse applies immediately"), Fixture.PlayerASC->GetNumericAttribute(Health), 110.f);
    TestEqual(TEXT("The cast pays once"), Fixture.PlayerASC->GetNumericAttribute(Mana), 88.f);
    Fixture.Step(.38f);
    TestEqual(TEXT("The second pulse waits for its cadence"), Fixture.PlayerASC->GetNumericAttribute(Health), 110.f);
    Fixture.Step(.25f);
    TestEqual(TEXT("The second pulse applies ten health"), Fixture.PlayerASC->GetNumericAttribute(Health), 120.f);
    Fixture.Step(.63f);
    TestEqual(TEXT("The third pulse applies ten health"), Fixture.PlayerASC->GetNumericAttribute(Health), 130.f);
    Fixture.Step(5.1f);
    TestEqual(TEXT("The graph never emits a fourth pulse"), Fixture.PlayerASC->GetNumericAttribute(Health), 130.f);
    TestEqual(TEXT("Follow-up pulses have no additional cost"), Fixture.PlayerASC->GetNumericAttribute(Mana), 88.f);
    TestEqual(TEXT("The enemy is neither healed nor damaged"), EnemyASC->GetNumericAttribute(Health),
              InitialEnemyHealth);

    if (!TestTrue(TEXT("The trial accepts independent cadence and heal amount cards"),
                  ApplyRejuvenationCard(Fixture, TEXT("ExtraTides"), Error) &&
                      ApplyRejuvenationCard(Fixture, TEXT("DeepMend"), Error)))
    {
        AddInfo(Error);
        return false;
    }
    Fixture.PlayerASC->SetNumericAttributeBase(Health, 9966.f);
    CastRejuvenationPreview(Fixture);
    TestEqual(TEXT("The amount card gives fifteen health immediately"), Fixture.PlayerASC->GetNumericAttribute(Health),
              9981.f);
    Fixture.Step(.36f);
    TestEqual(TEXT("The period card advances the second pulse before the old interval"),
              Fixture.PlayerASC->GetNumericAttribute(Health), 9996.f);
    Fixture.Step(.33f);
    TestEqual(TEXT("Restoration is capped by the caster's real maximum"),
              Fixture.PlayerASC->GetNumericAttribute(Health), 10000.f);
    TestEqual(TEXT("The second cast has committed normally"), Fixture.PlayerASC->GetNumericAttribute(Mana), 76.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopFrostAegisCast, "test_25d.UnifiedGA.WorkshopRejuvenation.FrostAegisCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 只净化匹配状态；匹配与不匹配均保护施法者，护盾按实际时钟结束。 */
bool FWorkshopFrostAegisCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Frost Aegis uses the real catalog trial"),
                  BeginRejuvenationPreview(Fixture, TEXT("/Script/test_25d.WorkshopFrostAegis"), Error)))
    {
        AddInfo(Error);
        return false;
    }
    auto* Attributes = CastChecked<UMyAttributeSet>(Fixture.State->GetAttributeSet());
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    Fixture.PlayerASC->SetNumericAttributeBase(Health, 100.f);
    if (!TestTrue(TEXT("The player has a real Chilled exposure on the current avatar"),
                  Attributes->ApplyElementalExposure(EAbilityElement::Ice, 1.f)))
    {
        return false;
    }
    TestEqual(TEXT("The test began with Chilled"), Attributes->GetElementalExposure(), EAbilityExposure::Chilled);
    CastRejuvenationPreview(Fixture);
    TestEqual(TEXT("The exact Chilled state is removed"), Attributes->GetElementalExposure(), EAbilityExposure::None);
    DamageRejuvenationCaster(Fixture, 40.f);
    TestEqual(TEXT("The barrier absorbs thirty of forty damage"), Fixture.PlayerASC->GetNumericAttribute(Health), 90.f);

    Fixture.Step(7.1f);
    if (!TestTrue(TEXT("A different elemental effect is active"),
                  Attributes->ApplyElementalExposure(EAbilityElement::Water, 1.f)))
    {
        return false;
    }
    CastRejuvenationPreview(Fixture);
    TestEqual(TEXT("A different exposure stays active"), Attributes->GetElementalExposure(), EAbilityExposure::Wet);
    DamageRejuvenationCaster(Fixture, 10.f);
    TestEqual(TEXT("The barrier is still granted without matching exposure"),
              Fixture.PlayerASC->GetNumericAttribute(Health), 90.f);
    Fixture.Step(4.1f);
    DamageRejuvenationCaster(Fixture, 10.f);
    TestEqual(TEXT("The unused barrier expires through the shared timer"),
              Fixture.PlayerASC->GetNumericAttribute(Health), 80.f);

    Fixture.Step(3.1f);
    if (!TestTrue(TEXT("The trial accepts a specific substitute for the cleanse target"),
                  ApplyRejuvenationCard(Fixture, TEXT("WarmAegisCleanse"), Error)) ||
        !TestTrue(TEXT("Burning is present before the edited cast"),
                  Attributes->ApplyElementalExposure(EAbilityElement::Fire, 1.f) &&
                      Attributes->GetElementalExposure() == EAbilityExposure::Burning))
    {
        AddInfo(Error);
        return false;
    }
    CastRejuvenationPreview(Fixture);
    TestEqual(TEXT("The edited cast clears Burning"), Attributes->GetElementalExposure(), EAbilityExposure::None);
    DamageRejuvenationCaster(Fixture, 30.f);
    TestEqual(TEXT("The status change preserves the original barrier capacity"),
              Fixture.PlayerASC->GetNumericAttribute(Health), 80.f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
