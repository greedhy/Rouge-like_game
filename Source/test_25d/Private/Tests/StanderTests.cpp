#include "Misc/AutomationTest.h"
#include "Player/StanderCombatConfig.h"
#include "AbilitySystem/Ability/StanderAbility.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "MyAbilityTypes.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanderPhaseTest,"test_25d.Stander.DashPhase",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FStanderPhaseTest::RunTest(const FString&)
{
 auto* C=NewObject<UStanderCombatConfig>();C->DashStart=.2f;C->DashEnd=.6f;
 TestEqual(TEXT("Windup never moves"),C->DashFraction(.1f),0.f);
 TestTrue(TEXT("Halfway through displacement"),FMath::IsNearlyEqual(C->DashFraction(.4f),.5f,1.e-6f));
 TestEqual(TEXT("Late frame never overshoots"),C->DashFraction(5.f),1.f);
 C->DashEnd=C->DashStart;TestEqual(TEXT("Bad interval does not move"),C->DashFraction(.4f),0.f);
 TestFalse(TEXT("Missing animations are rejected"),C->ValidateStander());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanderCounterTest,"test_25d.Stander.CounterRequiresBlockedDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FStanderCounterTest::RunTest(const FString&)
{
 FGameplayEffectContextHandle Context(new FMyGameplayEffectContext());
 FGameplayEffectSpec Spec(GetDefault<UGameplayEffect>(),Context,1.f);
 const auto Damage=FMyGameplayTags::Get().Damage_Physics;
 Spec.SetSetByCallerMagnitude(Damage,30.f);
 TestFalse(TEXT("Ordinary unblocked hit cannot counter"),UStanderAbility::IsCounterDamage(Spec));
 UMyAbilitySystemLibrary::SetIsBlockedHit(Context,true);
 TestTrue(TEXT("Blocked damage can counter"),UStanderAbility::IsCounterDamage(Spec));
 Spec.SetSetByCallerMagnitude(Damage,0.f);
 TestFalse(TEXT("Zero damage or cooldown effect cannot counter"),UStanderAbility::IsCounterDamage(Spec));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanderAssetsTest,"test_25d.Stander.AuthoredAssets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FStanderAssetsTest::RunTest(const FString&)
{
 auto* C=LoadObject<UStanderCombatConfig>(nullptr,TEXT("/Game/BluePrints/Characters/Stander/DA_Stander.DA_Stander"));
 if(TestNotNull(TEXT("Stander configuration"),C)) TestTrue(TEXT("All attacks and dash timing valid"),C->ValidateStander());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanderImpulseTest,"test_25d.Stander.DeathImpulse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FStanderImpulseTest::RunTest(const FString&)
{
 auto* C=NewObject<UStanderCombatConfig>();
 TestTrue(TEXT("Default independent impulses valid"),C->ValidateDeathImpulses());
 TestEqual(TEXT("Forward rotates but lift stays vertical"),C->WorldDeathImpulse(FVector(100,0,300),FVector(0,1,0)),FVector(0,100,300));
 const FVector PlayerLocation(100,50,0);
 const FVector RearTarget(-100,50,60);
 const FVector SpinForward=(RearTarget-PlayerLocation).GetSafeNormal2D();
 TestEqual(TEXT("Spin rear victim flies outward with configured vertical lift"),C->WorldDeathImpulse(C->SpinDeathImpulse,SpinForward),FVector(-C->SpinDeathImpulse.X,-C->SpinDeathImpulse.Y,C->SpinDeathImpulse.Z));
 C->ComboDeathImpulses.RemoveAt(0);TestFalse(TEXT("Missing combo impulse rejected"),C->ValidateDeathImpulses());
 FGameplayEffectContextHandle Context(new FMyGameplayEffectContext());FHitResult Hit;Hit.Item=123;Context.AddHitResult(Hit);
 UMyAbilitySystemLibrary::SetIsBlockedHit(Context,true);
 FGameplayEffectSpec Fake(GetDefault<UGameplayEffect>(),Context,1.f);Fake.SetSetByCallerMagnitude(FMyGameplayTags::Get().Damage_Physics,30);
 TestFalse(TEXT("Arbitrary effect cannot impersonate a Mass attacker via Item"),UStanderAbility::IsMassCounterDamage(Fake));
 FGameplayEffectSpec Real(GetDefault<UPureMassIncomingDamageEffect>(),Context,1.f);Real.SetSetByCallerMagnitude(FMyGameplayTags::Get().Damage_Physics,30);
 TestTrue(TEXT("Typed blocked Mass hit accepted for entity resolution"),UStanderAbility::IsMassCounterDamage(Real));
 Real.SetSetByCallerMagnitude(FMyGameplayTags::Get().Damage_Physics,0);
 TestFalse(TEXT("Zero damage Mass hit rejected"),UStanderAbility::IsMassCounterDamage(Real));
 return true;
}
#endif

