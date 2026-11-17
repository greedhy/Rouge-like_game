// 测试链：已结算伤害 -> Actor 的两个实际 GAS Spec -> 状态重叠、逃脱及韧性恢复。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Ability/EnemyCombatReactionAbilities.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "PureMass/PureMassSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyReactionIndependenceTest, "test_25d.EnemyCombat.Reactions.IndependentStates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemyReactionIndependenceTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Enemy = F.Target;
    auto* ASC = Enemy->GetAbilitySystemComponent();
    Enemy->InitializeCombatReactions();
    const auto Hitstun = EnemyCombatReactionTags::Hitstun.GetTag();
    const auto Broken = EnemyCombatReactionTags::PoiseBroken.GetTag();
    const auto Poise = UMyAttributeSet::GetPoiseAttribute();
    const auto MaxPoise = UMyAttributeSet::GetMaxPoiseAttribute();
    const float Capacity = ASC->GetNumericAttribute(MaxPoise);

    Enemy->ApplyCombatDamageReaction(Capacity, FVector::ZeroVector);
    TestTrue(TEXT("Direct damage enters short hitstun"), ASC->HasMatchingGameplayTag(Hitstun));
    TestTrue(TEXT("Poise zero activates the independently owned break"), ASC->HasMatchingGameplayTag(Broken));
    TestEqual(TEXT("Break starts with empty poise"), ASC->GetNumericAttribute(Poise), 0.f);
    for (int32 Frame = 0; Frame < 7; ++Frame)
    {
        ++GFrameCounter;
        F.World->Tick(LEVELTICK_All, .05f);
    }
    TestFalse(TEXT("Hitstun naturally ends first"), ASC->HasMatchingGameplayTag(Hitstun));
    TestTrue(TEXT("The break retains its own lock"), ASC->HasMatchingGameplayTag(Broken));
    const float Recovering = ASC->GetNumericAttribute(Poise);
    TestTrue(TEXT("Recovering poise is positive and below maximum"), Recovering > 0.f && Recovering < Capacity);
    Enemy->ApplyCombatDamageReaction(Capacity * 2.f, FVector::ZeroVector);
    TestEqual(TEXT("Incoming damage never subtracts recovering poise"), ASC->GetNumericAttribute(Poise), Recovering);
    for (int32 Frame = 0; Frame < 40; ++Frame)
    {
        ++GFrameCounter;
        F.World->Tick(LEVELTICK_All, .05f);
    }
    TestFalse(TEXT("Break ends at full poise"), ASC->HasMatchingGameplayTag(Broken));
    TestEqual(TEXT("Recovered poise is capped at maximum"), ASC->GetNumericAttribute(Poise), Capacity);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyReactionProfileTest, "test_25d.EnemyCombat.Reactions.ProfileAndEscape",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemyReactionProfileTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Target->InitializeCombatReactions();
    F.Source->LootRank = EEnemyLootRank::Boss;
    F.Source->InitializeCombatReactions();
    auto* Normal = F.Target->GetAbilitySystemComponent();
    auto* Boss = F.Source->GetAbilitySystemComponent();
    const auto Hitstun = EnemyCombatReactionTags::Hitstun.GetTag();
    const auto Broken = EnemyCombatReactionTags::PoiseBroken.GetTag();
    const auto MaxPoise = UMyAttributeSet::GetMaxPoiseAttribute();
    TestTrue(TEXT("Boss has more poise"), Boss->GetNumericAttribute(MaxPoise) > Normal->GetNumericAttribute(MaxPoise));
    F.Source->ApplyCombatDamageReaction(Boss->GetNumericAttribute(MaxPoise), FVector(0, 0, 400));
    TestFalse(TEXT("Boss ignores ordinary hitstun"), Boss->HasMatchingGameplayTag(Hitstun));
    TestTrue(TEXT("Boss can still be broken"), Boss->HasMatchingGameplayTag(Broken));
    for (int32 Hit = 0; Hit < F.Target->GetCombatReactionSettings().HitLimit; ++Hit)
    {
        F.Target->ApplyCombatDamageReaction(1.f, FVector::ZeroVector);
    }
    TestFalse(TEXT("Hit limit immediately ends normal hitstun"), Normal->HasMatchingGameplayTag(Hitstun));
    F.Target->ApplyCombatDamageReaction(1.f, FVector::ZeroVector);
    TestFalse(TEXT("Escape grace prevents immediate reentry"), Normal->HasMatchingGameplayTag(Hitstun));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyReactionDamageReceiptTest, "test_25d.EnemyCombat.Reactions.AuthorityDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemyReactionDamageReceiptTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* ASC = F.Target->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    F.Target->InitializeCombatReactions();
    const auto Poise = UMyAttributeSet::GetPoiseAttribute();
    const float Start = ASC->GetNumericAttribute(Poise);
    FDamageEffectParams Damage;
    Damage.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
    Damage.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
    Damage.BaseDamage = 15.f;
    const auto Result = AbilityDamageService::Apply(F.SourceToken, F.TargetToken, Damage);
    TestEqual(TEXT("Actor receipt reports actual health damage"), Result.ActualDamage.Get(-1.f), 15.f);
    TestEqual(TEXT("Real GE drives poise through the normal damage entry"), ASC->GetNumericAttribute(Poise), Start - 15.f);
    TestTrue(TEXT("Real GE activated hitstun on the correct actor"),
             ASC->HasMatchingGameplayTag(EnemyCombatReactionTags::Hitstun));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMassFodderHealthTest, "test_25d.EnemyCombat.Mass.OneHealthAnyPositiveDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMassFodderHealthTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 1.f;
    const int32 Id = Mass->SpawnMonster(Type, FVector::ZeroVector);
    if (!TestTrue(TEXT("Spawned real Mass entity"), Id > 0))
    {
        return false;
    }
    FPureMassDamage Damage;
    Damage.Source = F.Source;
    TestEqual(TEXT("Zero damage leaves it alive"), Mass->ApplyDamage(Id, Damage), 0.f);
    Damage.Amount = .01f;
    TestEqual(TEXT("Positive fractional damage consumes its entire health"), Mass->ApplyDamage(Id, Damage), 1.f);
    TestEqual(TEXT("Death removes the old combat target"), Mass->ApplyDamage(Id, Damage), 0.f);
    return true;
}
#endif
