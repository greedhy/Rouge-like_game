// 调用链：Automation真实ASC/装备Spec与部件Tag → 共享近战动作 → Mass扣血及局部快照断言。
// 规则只使用临时对象；不修改生产GE、GA或动作配置，真实GA接线由对应资产集成测试覆盖。
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "Equipment/EquipmentMeleeModifiers.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "Engine/CurveTable.h"
#include "Misc/AutomationTest.h"
#include "MyGameplayTags.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "UObject/Script.h"
#include <limits>

namespace UniqueEquipmentMeleeSetTests
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(HelmTag, "Test.Equipment.Unique.ThunderHelm");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(MailTag, "Test.Equipment.Unique.ThunderMail");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(MailChildTag, "Test.Equipment.Unique.ThunderMail.Child");

    // 使用生产剑盾旋转斩的身份，不用测试Tag掩盖拼写错误；目标动作仍走共享接触实现。
    FGameplayTag AttackTag()
    {
        return FGameplayTag::RequestGameplayTag(TEXT("Abilities.StanderWeapons.Sword_Shield.Skill7_Around"));
    }

    // 曲线横轴是装备Spec等级；技能等级不参与本层解析。
    FScalableFloat Curve(UCurveTable* Table, FName Name, float First, float Second)
    {
        auto& Row = Table->AddRichCurve(Name);
        Row.AddKey(1.f, First);
        Row.AddKey(2.f, Second);
        FScalableFloat Value(1.f);
        Value.Curve.CurveTable = Table;
        Value.Curve.RowName = Name;
        return Value;
    }

    // 只有头盔携带技能规则；胸甲提供另一个部件Tag，不再次应用技能倍率。
    UUniqueEquipmentGameplayEffect* Helm(FAbilityGraphTestFixture& Fixture)
    {
        auto* Effect = NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World);
        auto* Table = NewObject<UCurveTable>(Effect);
        auto& Rule = Effect->MeleeRule;
        Rule.AbilityTag = AttackTag();
        Rule.DamageTypeOverride = FMyGameplayTags::Get().Damage_Thunder;
        Rule.DamageMultiplier = Curve(Table, TEXT("Single"), 1.6f, 1.8f);
        Rule.SetDamageMultiplier = Curve(Table, TEXT("Set"), 2.3f, 2.6f);
        Rule.RequiredSetTags.AddTag(HelmTag);
        Rule.RequiredSetTags.AddTag(MailTag);
        return Effect;
    }

    // 标签随真实Active GE生命周期撤销；不使用LooseTag伪装装备部件。
    FActiveGameplayEffectHandle Apply(FAbilityGraphTestFixture& Fixture, UUniqueEquipmentGameplayEffect* Effect,
                                      FGameplayTag Piece, float Level = 1.f)
    {
        FGameplayEffectSpec Spec(Effect, Fixture.ASC->MakeEffectContext(), Level);
        Spec.DynamicGrantedTags.AddTag(Piece);
        return Fixture.ASC->ApplyGameplayEffectSpecToSelf(Spec);
    }

    FEquipmentMeleeSnapshot InitialSnapshot()
    {
        FEquipmentMeleeSnapshot Snapshot;
        Snapshot.Damage = 20.f;
        Snapshot.DamageType = FMyGameplayTags::Get().Damage_Physics;
        return Snapshot;
    }
} // namespace UniqueEquipmentMeleeSetTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueMeleeSetValidation, "test_25d.Equipment.Unique.MeleeSetValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 新字段默认值兼容旧暗金；不允许半配置套装、无技能身份或不可逆倍率进入装备事务。
bool FUniqueMeleeSetValidation::RunTest(const FString&)
{
    using namespace UniqueEquipmentMeleeSetTests;
    FEquipmentMeleeRule Rule;
    TestTrue(TEXT("Old empty melee rules remain valid"), Rule.IsValid(1.f));
    Rule.RequiredSetTags.AddTag(HelmTag);
    Rule.RequiredSetTags.AddTag(MailTag);
    TestFalse(TEXT("Set identities require a skill identity"), Rule.IsValid(1.f));
    Rule.RequiredSetTags.Reset();
    Rule.SetDamageMultiplier = FScalableFloat(2.3f);
    TestFalse(TEXT("Empty rule cannot hide a nondefault set multiplier"), Rule.IsValid(1.f));
    Rule.AbilityTag = AttackTag();
    Rule.DamageTypeOverride = FMyGameplayTags::Get().Damage_Thunder;
    Rule.DamageMultiplier = FScalableFloat(1.6f);
    TestFalse(TEXT("Active rule with no set requires neutral set multiplier"), Rule.IsValid(1.f));
    Rule.SetDamageMultiplier = FScalableFloat(1.f);
    TestTrue(TEXT("Old single-piece melee rules remain valid"), Rule.IsValid(1.f));
    Rule.RequiredSetTags.AddTag(HelmTag);
    TestFalse(TEXT("Single part cannot define a set"), Rule.IsValid(1.f));
    Rule.RequiredSetTags.AddTag(HelmTag);
    TestFalse(TEXT("Adding the same tag twice still cannot define a set"), Rule.IsValid(1.f));
    Rule.RequiredSetTags.AddTag(MailTag);
    Rule.SetDamageMultiplier = FScalableFloat(2.3f);
    TestTrue(TEXT("Two distinct parts and a finite bounded factor are valid"), Rule.IsValid(1.f));
    for (float Invalid :
         {0.f, -1.f, 0.01f, 10.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        Rule.SetDamageMultiplier = FScalableFloat(Invalid);
        TestFalse(TEXT("Invalid set damage factor is rejected before equip"), Rule.IsValid(1.f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueMeleeSetLifecycle, "test_25d.Equipment.Unique.MeleeSetLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 观察真实Mass扣血，验证套装替代、Spec等级、精确标签与拆装/抑制，不以辅助函数返回true冒充生效。
bool FUniqueMeleeSetLifecycle::RunTest(const FString&)
{
    using namespace UniqueEquipmentMeleeSetTests;
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture Fixture;
    Fixture.Target->Destroy();
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Health = 1000.f;
    Type->XPReward = 0;
    Type->Resistances.Add(FMyGameplayTags::Get().Damage_Physics, 50.f);
    Type->Resistances.Add(FMyGameplayTags::Get().Damage_Thunder, 0.f);
    const int32 Target = Mass->SpawnMonster(Type, FVector(120, 0, 0));
    auto* Settings = NewObject<UAbilityCharacterStrikeNodeConfig>(Fixture.World);
    Settings->Profile = EAbilityCharacterStrikeProfile::WeaponMixed;
    Settings->EquipmentAbilityTag = AttackTag();
    Settings->Strike.Damage = 20.f;
    Settings->Strike.Radius = 180.f;
    Settings->Strike.Angle = 360.f;
    FDamageEffectParams Damage;
    Damage.DamageType = FMyGameplayTags::Get().Damage_Physics;
    const auto StrikeDamage = [&]()
    {
        FPureMassView Before;
        FPureMassView After;
        Mass->Resolve(Target, Before);
        AbilityCharacterActions::ExecuteStrike(Fixture.SourceToken, Damage, *Settings, FVector::ZeroVector,
                                               FVector::ForwardVector, {});
        Mass->Resolve(Target, After);
        return Before.Health - After.Health;
    };
    auto* Effect = Helm(Fixture);
    auto HelmHandle = Apply(Fixture, Effect, HelmTag, 2.f);
    TestTrue(TEXT("Rule-bearing equipment is active at equipment level two"), HelmHandle.IsValid());
    TestTrue(TEXT("Single piece evaluates normal multiplier at Spec level two"),
             FMath::IsNearlyEqual(StrikeDamage(), 36.f, .01f));
    const auto Duplicate = Apply(Fixture, NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World), HelmTag);
    TestTrue(TEXT("Two copies of one part cannot complete the set"), FMath::IsNearlyEqual(StrikeDamage(), 36.f, .01f));
    const auto Descendant = Apply(Fixture, NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World), MailChildTag);
    TestTrue(TEXT("A descendant tag does not satisfy an exact part identity"),
             FMath::IsNearlyEqual(StrikeDamage(), 36.f, .01f));
    auto MailHandle = Apply(Fixture, NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World), MailTag);
    TestTrue(TEXT("Set replaces normal factor with equipment-level set curve"),
             FMath::IsNearlyEqual(StrikeDamage(), 52.f, .01f));
    MailHandle = Fixture.ASC->SetActiveGameplayEffectInhibit(MoveTemp(MailHandle), true, false);
    TestTrue(TEXT("Inhibiting the supporting piece breaks the set"), FMath::IsNearlyEqual(StrikeDamage(), 36.f, .01f));
    MailHandle = Fixture.ASC->SetActiveGameplayEffectInhibit(MoveTemp(MailHandle), false, false);
    TestTrue(TEXT("Re-enabling the supporting piece restores the set"),
             FMath::IsNearlyEqual(StrikeDamage(), 52.f, .01f));
    HelmHandle = Fixture.ASC->SetActiveGameplayEffectInhibit(MoveTemp(HelmHandle), true, false);
    TestTrue(TEXT("Inhibiting rule owner restores original physical resistance"),
             FMath::IsNearlyEqual(StrikeDamage(), 10.f, .01f));
    HelmHandle = Fixture.ASC->SetActiveGameplayEffectInhibit(MoveTemp(HelmHandle), false, false);
    Fixture.ASC->RemoveActiveGameplayEffect(MailHandle);
    TestTrue(TEXT("Removing the supporting piece restores the normal skill variant"),
             FMath::IsNearlyEqual(StrikeDamage(), 36.f, .01f));
    Fixture.ASC->RemoveActiveGameplayEffect(HelmHandle);
    TestTrue(TEXT("Removing rule owner restores original damage even with another matching part tag"),
             FMath::IsNearlyEqual(StrikeDamage(), 10.f, .01f));
    Fixture.ASC->RemoveActiveGameplayEffect(Duplicate);
    Fixture.ASC->RemoveActiveGameplayEffect(Descendant);

    // 同次挥击只在开始时读取套装；首目标伤害回调卸件，不能改变余下目标的既定数值。
    HelmHandle = Apply(Fixture, Effect, HelmTag, 2.f);
    MailHandle = Apply(Fixture, NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World), MailTag);
    const int32 SecondTarget = Mass->SpawnMonster(Type, FVector(100, 60, 0));
    FPureMassView BeforeFirst;
    FPureMassView BeforeSecond;
    Mass->Resolve(Target, BeforeFirst);
    Mass->Resolve(SecondTarget, BeforeSecond);
    int32 Hits = 0;
    bool bRemovedDuringFirstHit = false;
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.AfterDamage = [&](const FAbilityTargetToken&, const FVector&, const FAbilityDamageResult&)
    {
        ++Hits;
        if (Hits == 1)
        {
            bRemovedDuringFirstHit = Fixture.ASC->RemoveActiveGameplayEffect(MailHandle);
        }
        return true;
    };
    AbilityCharacterActions::ExecuteStrike(Fixture.SourceToken, Damage, *Settings, FVector::ZeroVector,
                                           FVector::ForwardVector, Callbacks);
    FPureMassView AfterFirst;
    FPureMassView AfterSecond;
    Mass->Resolve(Target, AfterFirst);
    Mass->Resolve(SecondTarget, AfterSecond);
    TestTrue(TEXT("Supporting piece is removed during the first resolved hit"), bRemovedDuringFirstHit);
    TestEqual(TEXT("The same strike still hits each target exactly once"), Hits, 2);
    TestTrue(TEXT("First target keeps the set snapshot regardless of query order"),
             FMath::IsNearlyEqual(BeforeFirst.Health - AfterFirst.Health, 52.f, .01f));
    TestTrue(TEXT("Second target keeps the set snapshot after mid-strike removal"),
             FMath::IsNearlyEqual(BeforeSecond.Health - AfterSecond.Health, 52.f, .01f));
    TestTrue(TEXT("The next strike sees the broken set"), FMath::IsNearlyEqual(StrikeDamage(), 36.f, .01f));
    Fixture.ASC->RemoveActiveGameplayEffect(HelmHandle);
    TestEqual(TEXT("Shared authored action remains unchanged"), Settings->Strike.Damage, 20.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueMeleeSetConflict, "test_25d.Equipment.Unique.MeleeSetConflictAtomicity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 套装不改变原有冲突处理；整条动作失败时保留输入快照，避免容器遍历顺序决定元素或部分倍率。
bool FUniqueMeleeSetConflict::RunTest(const FString&)
{
    using namespace UniqueEquipmentMeleeSetTests;
    FAbilityGraphTestFixture Fixture;
    auto* Effect = Helm(Fixture);
    Effect->MeleeRule.HitEffect = NewObject<UNiagaraSystem>(Fixture.World);
    Apply(Fixture, Effect, HelmTag);
    Apply(Fixture, NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World), MailTag);
    auto* Other = NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World);
    Other->MeleeRule.AbilityTag = AttackTag();
    Other->MeleeRule.DamageTypeOverride = FMyGameplayTags::Get().Damage_Fire;
    Other->MeleeRule.DamageMultiplier = FScalableFloat(1.2f);
    auto Conflict = Apply(Fixture, Other, HelmTag);
    auto Snapshot = InitialSnapshot();
    TestFalse(TEXT("A different elemental conversion still rejects the whole action"),
              EquipmentMeleeModifiers::Resolve(Fixture.ASC, AttackTag(), Snapshot));
    TestEqual(TEXT("Rejected element does not partially change input damage"), Snapshot.Damage, 20.f);
    TestEqual(TEXT("Rejected element does not partially change input type"), Snapshot.DamageType,
              FMyGameplayTags::Get().Damage_Physics);
    TestNull(TEXT("Rejected element does not leak another rule's hit effect"), Snapshot.HitEffect);
    Fixture.ASC->RemoveActiveGameplayEffect(Conflict);

    auto* DifferentEffect = NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World);
    DifferentEffect->MeleeRule.AbilityTag = AttackTag();
    DifferentEffect->MeleeRule.DamageTypeOverride = FMyGameplayTags::Get().Damage_Thunder;
    DifferentEffect->MeleeRule.HitEffect = NewObject<UNiagaraSystem>(Fixture.World);
    Conflict = Apply(Fixture, DifferentEffect, HelmTag);
    TestFalse(TEXT("Two different nonempty hit effects still conflict"),
              EquipmentMeleeModifiers::Resolve(Fixture.ASC, AttackTag(), Snapshot));
    TestEqual(TEXT("Effect conflict keeps original input damage"), Snapshot.Damage, 20.f);
    TestNull(TEXT("Effect conflict keeps original visual"), Snapshot.HitEffect);
    Fixture.ASC->RemoveActiveGameplayEffect(Conflict);

    Snapshot = InitialSnapshot();
    TestTrue(TEXT("Removing conflicting equipment allows the complete set to resolve"),
             EquipmentMeleeModifiers::Resolve(Fixture.ASC, AttackTag(), Snapshot));
    TestTrue(TEXT("Only the surviving set rule applies its level-one set multiplier"),
             FMath::IsNearlyEqual(Snapshot.Damage, 46.f, .01f));
    TestEqual(TEXT("Resolved set restores its intended element"), Snapshot.DamageType,
              FMyGameplayTags::Get().Damage_Thunder);
    TestTrue(TEXT("Resolved set uses its intended effect"), Snapshot.HitEffect == Effect->MeleeRule.HitEffect);
    return true;
}
#endif
