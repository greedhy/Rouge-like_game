#include "Misc/AutomationTest.h"
#include "LifecycleUIProbe.h"
#include "UObject/GarbageCollection.h"
#include "UI/WidgetController/OverlayWidgetController.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Script.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLifecycleUIBindingsTest,"test_25d.Lifecycle.UIBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLifecycleUIBindingsTest::RunTest(const FString&)
{
 FEditorScriptExecutionGuard Guard;
 UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* PS=World->SpawnActor<AMyPlayerState>();
 auto* ASC=Cast<UMyAbilitySystemComponent>(PS->GetAbilitySystemComponent());
 auto* AS=Cast<UMyAttributeSet>(PS->GetAttributeSet());
 auto* Overlay=NewObject<UOverlayWidgetController>();
 auto* Spell=NewObject<USpellMenuWidgetController>();
 const FWidgetControllerParams Params(nullptr,PS,ASC,AS);
 Overlay->SetWidgetControllerParams(Params);Spell->SetWidgetControllerParams(Params);
 Overlay->BindCallbacksToDependencies();Spell->BindCallbacksToDependencies();
 auto& Health=ASC->GetGameplayAttributeValueChangeDelegate(AS->GetHealthAttribute());
 TestTrue(TEXT("Health subscription tracks UObject receiver, rather than raw this"),Health.IsBoundToObject(Overlay));
 TestTrue(TEXT("Level subscription tracks receiver"),PS->OnLevelChangedDelegate.IsBoundToObject(Overlay));
 TestTrue(TEXT("Spell status subscription tracks receiver"),ASC->AbilityStatusChanged.IsBoundToObject(Spell));
 TestTrue(TEXT("Spell points subscription tracks receiver"),PS->OnSpellPointsChangedDelegate.IsBoundToObject(Spell));
 // RemoveAll must detach even while receiver is alive. This safely detects raw lambdas
 // without broadcasting through a freed pointer in the editor process.
 Health.RemoveAll(Overlay);PS->OnLevelChangedDelegate.RemoveAll(Overlay);
 ASC->AbilityStatusChanged.RemoveAll(Spell);PS->OnSpellPointsChangedDelegate.RemoveAll(Spell);
 TestFalse(TEXT("Health detached"),Health.IsBound());
 TestFalse(TEXT("Level detached"),PS->OnLevelChangedDelegate.IsBound());
 TestFalse(TEXT("Spell status detached"),ASC->AbilityStatusChanged.IsBound());
 TestFalse(TEXT("Spell points detached"),PS->OnSpellPointsChangedDelegate.IsBound());
 // Rebind twice and verify a real UI multicast fires exactly once.
 Overlay->BindCallbacksToDependencies();Overlay->BindCallbacksToDependencies();
 if(Health.IsBoundToObject(Overlay))
 {
  auto* Probe=NewObject<ULifecycleUIProbe>();Probe->AddToRoot();
  Overlay->OnHealthChanged.AddDynamic(Probe,&ULifecycleUIProbe::Health);
  FOnAttributeChangeData Change;Change.NewValue=73.f;
  Health.Broadcast(Change);
  TestEqual(TEXT("Repeated Bind dispatches exactly once"),Probe->Calls,1);
  auto* OtherPS=World->SpawnActor<AMyPlayerState>();
  auto* OtherASC=Cast<UMyAbilitySystemComponent>(OtherPS->GetAbilitySystemComponent());
  auto* OtherAS=Cast<UMyAttributeSet>(OtherPS->GetAttributeSet());
  Overlay->SetWidgetControllerParams(FWidgetControllerParams(nullptr,OtherPS,OtherASC,OtherAS));
  Overlay->BindCallbacksToDependencies();
  Health.Broadcast(Change);
  TestEqual(TEXT("Old dependency detached on parameter replacement"),Probe->Calls,1);
  auto& OtherHealth=OtherASC->GetGameplayAttributeValueChangeDelegate(OtherAS->GetHealthAttribute());
  OtherHealth.Broadcast(Change);
  TestEqual(TEXT("New dependency dispatches"),Probe->Calls,2);
  TWeakObjectPtr<UOverlayWidgetController> Old=Overlay;Overlay=nullptr;
  CollectGarbage(RF_NoFlags);
  TestFalse(TEXT("Old controller actually collected"),Old.IsValid());
  OtherHealth.Broadcast(Change);
  TestEqual(TEXT("No old UI callback after real GC"),Probe->Calls,2);
  Probe->RemoveFromRoot();
 }
 // Clean up unsafe legacy callbacks without invoking them.
 Health.Clear();PS->OnLevelChangedDelegate.Clear();ASC->AbilityStatusChanged.Clear();PS->OnSpellPointsChangedDelegate.Clear();
 GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return true;
}
#endif
