// 调用链：清空模拟重载后的模块标签缓存 -> FMyGameplayTags::Get -> 从引擎标签树恢复伤害映射。
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MyGameplayTags.h"
#include "Templates/UnrealTemplate.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayTagModuleReloadTest, "test_25d.EnemyCombat.DamageTagsAfterModuleReload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGameplayTagModuleReloadTest::RunTest(const FString&)
{
    const FMyGameplayTags Before = FMyGameplayTags::Get();
    const TGuardValue<FMyGameplayTags> SimulatedReload(FMyGameplayTags::GameplayTags, FMyGameplayTags());
    const auto& Restored = FMyGameplayTags::Get();
    TestTrue(TEXT("The editor's existing tag tree supplies the new module's physical damage tag"),
             Restored.Damage_Physics.IsValid() && Restored.Damage_Physics == Before.Damage_Physics);
    TestEqual(TEXT("All original damage types still resolve their resistance after reload"),
              Restored.DamageTypesToResistance.Num(), Before.DamageTypesToResistance.Num());
    TestTrue(TEXT("Physical damage maps to physical resistance"),
             Restored.DamageTypesToResistance.FindRef(Before.Damage_Physics).IsValid() &&
                 Restored.DamageTypesToResistance.FindRef(Before.Damage_Physics) == Before.Attributes_Resistance_Physics);
    TestTrue(TEXT("Damage auxiliary tags and death XP remain usable"),
             Restored.Debuff_Chance.IsValid() && Restored.Attributes_Meta_IncomingXP.IsValid());
    return true;
}
#endif
