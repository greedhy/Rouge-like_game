#include "Misc/AutomationTest.h"
#include "Player/SwordShieldConfig.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Game/MyGameModeBase.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"
#include "Character/SwordShieldPlayer.h"
#include "Components/StaticMeshComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwordGeometryTest,"test_25d.SwordShield.GeometryAndCombo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSwordGeometryTest::RunTest(const FString&)
{
 FSwordStrike Strike;
 TestTrue(TEXT("In front inside range"),Strike.Contains(FVector::ZeroVector,FVector::ForwardVector,FVector(120,0,0)));
 TestFalse(TEXT("Behind is safe"),Strike.Contains(FVector::ZeroVector,FVector::ForwardVector,FVector(-80,0,0)));
 TestFalse(TEXT("Beyond range is safe"),Strike.Contains(FVector::ZeroVector,FVector::ForwardVector,FVector(161,0,0)));
 TestFalse(TEXT("Other floor is safe"),Strike.Contains(FVector::ZeroVector,FVector::ForwardVector,FVector(80,0,150)));
 Strike.Angle=360;
 TestTrue(TEXT("Spin includes rear"),Strike.Contains(FVector::ZeroVector,FVector::ForwardVector,FVector(-80,0,0)));
 TestEqual(TEXT("Continue combo within window"),USwordShieldConfig::ResolveComboIndex(1,10,10.5,.9f),1);
 TestEqual(TEXT("Reset after delay"),USwordShieldConfig::ResolveComboIndex(2,10,11,.9f),0);
 TestEqual(TEXT("Invalid combo index resets"),USwordShieldConfig::ResolveComboIndex(5,10,10.1,.9f),0);
 TestFalse(TEXT("Missing animation rejected"),Strike.Validate());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwordGuardTest,"test_25d.SwordShield.GuardEffectIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSwordGuardTest::RunTest(const FString&)
{
 auto* W=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 auto* P=W->SpawnActor<AEnemyCharacter>();auto* ASC=P->GetAbilitySystemComponent();ASC->InitAbilityActorInfo(P,P);
 ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(P));
 ASC->SetNumericAttributeBase(UMyAttributeSet::GetBlockChanceAttribute(),23);
 auto Spec=ASC->MakeOutgoingSpec(USwordGuardEffect::StaticClass(),1,ASC->MakeEffectContext());
 auto Handle=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
 TestTrue(TEXT("Guard status applied"),ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
 TestEqual(TEXT("Guard leaves base chance intact"),ASC->GetNumericAttribute(UMyAttributeSet::GetBlockChanceAttribute()),23.f);
 ASC->SetNumericAttributeBase(UMyAttributeSet::GetBlockChanceAttribute(),35);
 ASC->RemoveActiveGameplayEffect(Handle);
 TestFalse(TEXT("Guard status removed"),ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
 TestEqual(TEXT("Removing guard preserves changed base stat"),ASC->GetNumericAttribute(UMyAttributeSet::GetBlockChanceAttribute()),35.f);
 GEngine->DestroyWorldContext(W);W->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwordAssetsTest,"test_25d.SwordShield.AuthoredAssets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSwordAssetsTest::RunTest(const FString&)
{
 auto* Config=LoadObject<USwordShieldConfig>(nullptr,TEXT("/Game/BluePrints/Characters/SwordShield/DA_SwordShield.DA_SwordShield"));
 if(TestNotNull(TEXT("SwordShield config"),Config)) TestTrue(TEXT("Valid complete configuration"),Config->Validate());
 const auto* Player=GetDefault<ASwordShieldPlayer>();
 TestTrue(TEXT("Sword matches source blueprint quaternion, not opposite UE roll"),
  Player->SwordVisual->GetRelativeRotation().Quaternion().Equals(FQuat(FVector::ForwardVector,PI/2),.001f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwordGuardDamageTest,"test_25d.SwordShield.GuardRealDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSwordGuardDamageTest::RunTest(const FString&)
{
 FEditorScriptExecutionGuard Script;
 auto* W=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 W->SetGameInstance(NewObject<UGameInstance>(GEngine));FURL URL;URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));W->SetGameMode(URL);
 auto* Mode=CastChecked<AMyGameModeBase>(W->GetAuthGameMode());
 Mode->CharacterClassInfo=LoadObject<UCharacterClassInfo>(nullptr,TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
 auto* Source=W->SpawnActor<AEnemyCharacter>();auto* Target=W->SpawnActor<AEnemyCharacter>();
 Source->Tags={FName("Enemy")};Target->Tags={FName("Player")};
 for(auto* Actor:{Source,Target})
 {
  auto* ASC=Actor->GetAbilitySystemComponent();ASC->InitAbilityActorInfo(Actor,Actor);ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
  ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),1000);ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),1000);
 }
 auto* ASC=Target->GetAbilitySystemComponent();
 // Reproduce the player's pre-existing overriding secondary-stat effect.
 auto* Baseline=NewObject<UGameplayEffect>(W);Baseline->DurationPolicy=EGameplayEffectDurationType::Infinite;
 FGameplayModifierInfo Modifier;Modifier.Attribute=UMyAttributeSet::GetBlockChanceAttribute();Modifier.ModifierOp=EGameplayModOp::Override;Modifier.ModifierMagnitude=FScalableFloat(0);Baseline->Modifiers.Add(Modifier);
 auto BaseHandle=ASC->ApplyGameplayEffectToSelf(Baseline,1,ASC->MakeEffectContext());
 FDamageEffectParams Params;Params.WorldContextObject=Source;Params.SourceAbilitySystemComponent=Source->GetAbilitySystemComponent();Params.TargetAbilitySystemComponent=ASC;
 Params.DamageGameplayEffectClass=LoadClass<UGameplayEffect>(nullptr,TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C"));Params.BaseDamage=100;Params.DamageType=FMyGameplayTags::Get().Damage_Physics;
 auto Spec=ASC->MakeOutgoingSpec(USwordGuardEffect::StaticClass(),1,ASC->MakeEffectContext());auto Guard=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
 const auto Context=UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
 TestTrue(TEXT("Guard marks real hit blocked despite existing override"),UMyAbilitySystemLibrary::IsBlockedHit(Context));
 TestEqual(TEXT("Guard halves real damage"),ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),950.f);
 ASC->RemoveActiveGameplayEffect(Guard);
 UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
 TestEqual(TEXT("Damage returns to full after guard ends"),ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),850.f);
 TestTrue(TEXT("Other effect survives guard removal"),ASC->GetActiveGameplayEffect(BaseHandle)!=nullptr);
 GEngine->DestroyWorldContext(W);W->DestroyWorld(false);return true;
}
#endif
