// 调用链：Automation真实ASC/装备GE → 共享角色接触动作 → Mass伤害结果；不修改用户技能资产。
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueEquipmentMeleeConversionTest,
                                 "test_25d.Equipment.Unique.MeleeConversionAndRemoval",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 真实抗性差异证明转换已进入伤害通路；同时守住原接触次数、范围与配置不变。
bool FUniqueEquipmentMeleeConversionTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture Fixture;
    Fixture.Target->Destroy();
    const auto& Tags = FMyGameplayTags::Get();
    const FGameplayTag Attack = FGameplayTag::RequestGameplayTag(TEXT("Abilities.StanderWeapons.Sword_Shield.Attack1"));
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    Type->Resistances.Add(Tags.Damage_Physics, 50.f);
    Type->Resistances.Add(Tags.Damage_Fire, 0.f);
    const int32 Front = Mass->SpawnMonster(Type, FVector(120, 0, 0));
    const int32 Rear = Mass->SpawnMonster(Type, FVector(-120, 0, 0));
    const int32 Far = Mass->SpawnMonster(Type, FVector(350, 0, 0));

    auto* Equipment = NewObject<UUniqueEquipmentGameplayEffect>(Fixture.World);
    Equipment->MeleeRule.AbilityTag = Attack;
    Equipment->MeleeRule.DamageTypeOverride = Tags.Damage_Fire;
    Equipment->MeleeRule.DamageMultiplier = FScalableFloat(1.8f);
    FGameplayEffectSpec Spec(Equipment, Fixture.ASC->MakeEffectContext(), 1.f);
    const auto Handle = Fixture.ASC->ApplyGameplayEffectSpecToSelf(Spec);
    TestTrue(TEXT("Equipment rule is active"), Handle.IsValid());

    auto* Settings = NewObject<UAbilityCharacterStrikeNodeConfig>(Fixture.World);
    Settings->Profile = EAbilityCharacterStrikeProfile::WeaponMixed;
    Settings->EquipmentAbilityTag = Attack;
    Settings->Strike.Damage = 20.f;
    Settings->Strike.Radius = 180.f;
    Settings->Strike.Angle = 90.f;
    FDamageEffectParams Damage;
    Damage.DamageType = Tags.Damage_Physics;
    int32 Hits = 0;
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.AfterDamage = [&Hits](const FAbilityTargetToken&, const FVector&, const FAbilityDamageResult&)
    {
        ++Hits;
        return true;
    };
    auto Strike = [&]()
    {
        AbilityCharacterActions::ExecuteStrike(Fixture.SourceToken, Damage, *Settings, FVector::ZeroVector,
                                               FVector::ForwardVector, Callbacks);
    };
    FPureMassView View;
    Strike();
    Mass->Resolve(Front, View);
    TestTrue(TEXT("Fire conversion bypasses physical resistance and deals 36"),
             FMath::IsNearlyEqual(View.Health, 64.f));
    TestEqual(TEXT("One contact remains one hit"), Hits, 1);
    Mass->Resolve(Rear, View);
    TestEqual(TEXT("Conversion does not extend the angle"), View.Health, 100.f);
    Mass->Resolve(Far, View);
    TestEqual(TEXT("Conversion does not extend the reach"), View.Health, 100.f);
    TestEqual(TEXT("Shared config damage unchanged"), Settings->Strike.Damage, 20.f);
    TestEqual(TEXT("Caller damage type unchanged"), Damage.DamageType, Tags.Damage_Physics);

    Settings->EquipmentAbilityTag = Tags.Abilities_Fire_FireBolt;
    Strike();
    Mass->Resolve(Front, View);
    TestTrue(TEXT("Other ability uses original physical damage 10"), FMath::IsNearlyEqual(View.Health, 54.f));
    Settings->EquipmentAbilityTag = Attack;
    Fixture.ASC->RemoveActiveGameplayEffect(Handle);
    Strike();
    Mass->Resolve(Front, View);
    TestTrue(TEXT("Removing equipment restores physical damage 10"), FMath::IsNearlyEqual(View.Health, 44.f));
    return true;
}
#endif
