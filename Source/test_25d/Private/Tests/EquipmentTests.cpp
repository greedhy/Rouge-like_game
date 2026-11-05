#include "Equipment/EquipmentSlots.h"
#include "Misc/AutomationTest.h"
#include "Equipment/EquipmentComponent.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NativeGameplayTags.h"
#include "Equipment/EquipmentSoulPickup.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Player/MyPlayerController.h"
#include "Components/SceneComponent.h"
#include "Engine/CurveTable.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "Tests/EquipmentTestEffects.h"
#include "Backpack/WorldPickupComponent.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TestEquipmentSlot, "Equipment.Test.Slot");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TestEquipmentSlot2, "Equipment.Test.Slot2");

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentOwnershipTest, "test_25d.Equipment.ComponentOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentOwnershipTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    AMyPlayerState* State = World->SpawnActor<AMyPlayerState>();
    TestNotNull(TEXT("PlayerState owns equipment"), State->FindComponentByClass<UEquipmentComponent>());
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

namespace EquipmentTests
{
struct FFixture
{
 UWorld* World;
 AMyPlayerState* State;
 APawn* Pawn;
 UEquipmentComponent* E;
 UBackpackComponent* B;
 UAbilitySystemComponent* ASC;
 UBackpackItemDefinitions* Catalog;
 FPrimaryAssetId Armor{FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("TestArmor"))};
 FPrimaryAssetId Seven{FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("TestSeven"))};
 FPrimaryAssetId Health{FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("TestHealth"))};
 FFixture()
 {
  World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
  GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
  State = World->SpawnActor<AMyPlayerState>(); Pawn = World->SpawnActor<APawn>(); Pawn->SetPlayerState(State);
  E = State->GetEquipmentComponent(); B = State->GetBackpackComponent(); ASC = State->GetAbilitySystemComponent();
  ASC->InitAbilityActorInfo(State, Pawn); ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
  ASC->SetNumericAttributeBase(UMyAttributeSet::GetArmorAttribute(), 10.f);
  ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
  ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
  E->AvailableSlots.AddTag(TAG_TestEquipmentSlot); E->AvailableSlots.AddTag(TAG_TestEquipmentSlot2);
  Catalog = NewObject<UBackpackItemDefinitions>(State);
  Add(Armor, UEquipmentTestArmor::StaticClass()); Add(Seven, UEquipmentTestArmorSeven::StaticClass()); Add(Health, UEquipmentTestHealth::StaticClass());
  B->SetItemDefinitions(Catalog); B->Initialize(); B->SetCapacity(3); E->ReconcileEffects();
 }
 void Add(FPrimaryAssetId Id, TSubclassOf<UEquipmentGameplayEffect> Effect)
 {
  auto& Def = Catalog->Items.AddDefaulted_GetRef(); Def.ItemDefinitionId = Id; Def.bEquippable = true;
  Def.bStackable = false; Def.MaxStack = 1; Def.AllowedEquipmentSlots = E->AvailableSlots; Def.EquipEffect = Effect;
 }
 bool Equip(int32 I, FGameplayTag Tag = TAG_TestEquipmentSlot)
 { return E->TryEquip(I, Tag, B->GetInventoryModel()->GetSlots()[I].ItemInstanceId, B->GetRevision(), E->Revision); }
 bool Unequip(FGameplayTag Tag = TAG_TestEquipmentSlot)
 {
  const auto* Slot = E->Slots.FindByPredicate([&](const FEquipmentSlot& S){ return S.SlotTag == Tag; });
  return Slot && E->TryUnequip(Tag, Slot->ItemInstanceId, B->GetRevision(), E->Revision);
 }
 float Value() const { return ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()); }
 ~FFixture() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentIndependentTest, "test_25d.Equipment.IndependentEffects", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentIndependentTest::RunTest(const FString& Parameters)
{
 EquipmentTests::FFixture F;
 F.B->TryAddItem(F.Armor, 2);
 TestTrue(TEXT("Distinct same-type identities"), F.B->GetInventoryModel()->GetSlots()[0].ItemInstanceId != F.B->GetInventoryModel()->GetSlots()[1].ItemInstanceId);
 TestTrue(TEXT("Equip first"), F.Equip(0)); TestTrue(TEXT("Equip second"), F.Equip(1, TAG_TestEquipmentSlot2));
 TestEqual(TEXT("Independent identical effects"), F.Value(), 20.f);
 TestTrue(TEXT("Remove only first"), F.Unequip()); TestEqual(TEXT("Second survives"), F.Value(), 15.f);
 TestTrue(TEXT("Remove second"), F.Unequip(TAG_TestEquipmentSlot2)); TestEqual(TEXT("Base restored"), F.Value(), 10.f);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentSwapTest, "test_25d.Equipment.SwapAndStaleRequests", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentSwapTest::RunTest(const FString& Parameters)
{
 EquipmentTests::FFixture F; F.B->SetCapacity(1); F.B->TryAddItem(F.Armor, 1);
 const auto Id = F.B->GetInventoryModel()->GetSlots()[0].ItemInstanceId;
 const int32 OldRevision = F.B->GetRevision();
 TestTrue(TEXT("Equip"), F.Equip(0)); F.B->TryAddItem(F.Seven, 1);
 TestFalse(TEXT("Full backpack prevents unequip"), F.Unequip());
 TestFalse(TEXT("Stale request rejected"), F.E->TryEquip(0, TAG_TestEquipmentSlot, Id, OldRevision, 0));
 TestTrue(TEXT("Full backpack swap uses source slot"), F.Equip(0));
 TestEqual(TEXT("Seven replaces five"), F.Value(), 17.f);
 TestEqual(TEXT("Old instance returned unchanged"), F.B->GetInventoryModel()->GetSlots()[0].ItemInstanceId, Id);
 
 F.ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda([&](UAbilitySystemComponent* ASC, const FGameplayEffectSpec&, FActiveGameplayEffectHandle H){ ASC->RemoveActiveGameplayEffect(H); });
 TestFalse(TEXT("Rejected new effect preserves old equipment"), F.Equip(0));
 TestEqual(TEXT("Old effect preserved"), F.Value(), 17.f);
 TestEqual(TEXT("New item not consumed"), F.B->GetInventoryModel()->GetSlots()[0].ItemInstanceId, Id);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentRestoreTest, "test_25d.Equipment.PersistenceAndReconcile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentRestoreTest::RunTest(const FString& Parameters)
{
 EquipmentTests::FFixture F; F.B->TryAddItem(F.Armor, 1); F.B->TryAddItem(F.Seven, 1);
 TestTrue(TEXT("Equip five"), F.Equip(0)); TestTrue(TEXT("Equip seven"), F.Equip(1, TAG_TestEquipmentSlot2));
 TestEqual(TEXT("Acceptance 22"), F.Value(), 22.f);
 const auto Saved = F.State->ExportBackpackSnapshot();
 if (!TestEqual(TEXT("Two equipped records required for restore test"), Saved.Equipment.Num(), 2)) return false;
 bool bSawPartialRestore = false;
 auto Listener = F.B->InventoryModel->OnInventoryUpdated.AddLambda([&](const TArray<FInventorySlot>&){
  const auto During = F.State->ExportBackpackSnapshot();
  TSet<FGuid> Seen;
  for (const auto& Item : During.Slots) if (!Item.IsEmpty()) Seen.Add(Item.ItemInstanceId);
  for (const auto& Item : During.Equipment) if (Seen.Contains(Item.ItemInstanceId)) bSawPartialRestore = true;
 });
 TestTrue(TEXT("Repeat reconcile"), F.E->ReconcileEffects()); TestEqual(TEXT("No duplicate effects"), F.Value(), 22.f);
 TestTrue(TEXT("Restore combined snapshot"), F.State->ImportBackpackSnapshot(Saved)); TestEqual(TEXT("Restore exactly once"), F.Value(), 22.f);
 auto Bad = Saved; Bad.Slots[0].ItemDefinitionId = F.Armor; Bad.Slots[0].Quantity = 1; Bad.Slots[0].ItemInstanceId = Saved.Equipment[0].ItemInstanceId;
 TestFalse(TEXT("Duplicate inventory/equipment ID rejected"), F.State->ImportBackpackSnapshot(Bad)); TestEqual(TEXT("Rejected snapshot unchanged"), F.Value(), 22.f);
 F.ASC->RemoveActiveEffects(FGameplayEffectQuery());
 TestTrue(TEXT("External removal marks reconciliation needed"), F.E->bNeedsReconcile);
 TestTrue(TEXT("Ordinary unequip repairs missing effects first"), F.Unequip());
 TestEqual(TEXT("Remaining rebuilt bonus"), F.Value(), 17.f);
 TestTrue(TEXT("Put first back"), F.Equip(0)); TestEqual(TEXT("Rebuilt bonuses"), F.Value(), 22.f);
 TestTrue(TEXT("Remove first"), F.Unequip());
 TestTrue(TEXT("Restore when an equipped item is in backpack"), F.State->ImportBackpackSnapshot(Saved));
 TestFalse(TEXT("Observers never see half a restore"), bSawPartialRestore);
 F.B->InventoryModel->OnInventoryUpdated.Remove(Listener);
 TestTrue(TEXT("Remove first again"), F.Unequip()); TestEqual(TEXT("Acceptance 17"), F.Value(), 17.f);
 TestTrue(TEXT("Remove second"), F.Unequip(TAG_TestEquipmentSlot2)); TestEqual(TEXT("Acceptance 10"), F.Value(), 10.f);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentResourcesTest, "test_25d.Equipment.ResourcesAndUseGuard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentResourcesTest::RunTest(const FString& Parameters)
{
 EquipmentTests::FFixture F; F.B->TryAddItem(F.Health, 1);
 TestFalse(TEXT("Equipment cannot be consumed"), F.B->TryUseItemForPawn(0, F.Pawn));
 TestTrue(TEXT("Equip health"), F.Equip(0));
 TestEqual(TEXT("No free healing"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
 F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 140.f);
 const auto Saved = F.State->ExportBackpackSnapshot();
 TestTrue(TEXT("Restore health equipment"), F.State->ImportBackpackSnapshot(Saved));
 TestEqual(TEXT("Restore does not transiently clamp health"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 140.f);
 TestTrue(TEXT("Unequip health"), F.Unequip());
 TestEqual(TEXT("Clamp lower maximum"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
 TestTrue(TEXT("Re-equip"), F.Equip(0)); TestEqual(TEXT("Cannot heal by swapping"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentPickupTest, "test_25d.Equipment.PickupReentryAndIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentPickupTest::RunTest(const FString& Parameters)
{
 EquipmentTests::FFixture F;
 auto* Other = F.World->SpawnActor<AMyPlayerState>(); Other->GetBackpackComponent()->SetItemDefinitions(F.Catalog); Other->GetBackpackComponent()->Initialize();
 auto* Actor = F.World->SpawnActor<AActor>();
 auto* Pickup = NewObject<UWorldPickupComponent>(Actor); Actor->AddInstanceComponent(Pickup); Pickup->RegisterComponent();
 Pickup->ItemDefinitions = F.Catalog; Pickup->ConfigurePickup(F.Armor, 1, true); Pickup->ItemInstanceId = FGuid::NewGuid();
 const auto Id = Pickup->ItemInstanceId;
 bool bStolen = false;
 auto Handle = F.B->OnBackpackStateChanged.AddLambda([&](){ bStolen = Pickup->TryPickup(Other); });
 TestTrue(TEXT("First player picks up"), Pickup->TryPickup(F.State));
 F.B->OnBackpackStateChanged.Remove(Handle);
 TestFalse(TEXT("Synchronous callback cannot double claim"), bStolen);
 TestEqual(TEXT("Identity survives pickup"), F.B->GetInventoryModel()->GetSlots()[0].ItemInstanceId, Id);
 TestTrue(TEXT("Second player stays empty"), Other->GetBackpackComponent()->GetInventoryModel()->GetSlots()[0].IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentAuthoredAssetsTest, "test_25d.Equipment.AuthoredTableAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentAuthoredAssetsTest::RunTest(const FString& Parameters)
{
 EquipmentTests::FFixture F;
 // Match the real player: Channel0 contains derived-attribute Override modifiers.
 UClass* SecondaryClass = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_SecondaryAttributes.GE_SecondaryAttributes_C"));
 if (!TestNotNull(TEXT("Real player initialization GE"), SecondaryClass)) return false;
 auto Context = F.ASC->MakeEffectContext(); Context.AddSourceObject(F.Pawn);
 auto Spec = F.ASC->MakeOutgoingSpec(SecondaryClass, 1.f, Context);
 if (!TestTrue(TEXT("Initialization spec"), Spec.IsValid())) return false;
 const auto Initialization = F.ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
 if (!TestTrue(TEXT("Initialization GE active"), Initialization.IsValid())) return false;
 F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()));
 F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute()));
 auto* Catalog = LoadObject<UBackpackItemDefinitions>(nullptr, TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
 auto* Curves = LoadObject<UCurveTable>(nullptr, TEXT("/Game/Equipment/CT_EquipmentStats.CT_EquipmentStats"));
 if (!TestNotNull(TEXT("Equipment catalog"), Catalog) || !TestNotNull(TEXT("Stats curve table"), Curves)) return false;
 if (!TestNotNull(TEXT("Metadata DataTable"), Catalog->EquipmentTable.Get())) return false;
 F.B->SetItemDefinitions(Catalog);
 const auto Left = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Left"));
 const auto Right = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Right"));
 const auto Chest = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Chest"));
 F.E->AvailableSlots.AddTag(Left); F.E->AvailableSlots.AddTag(Right); F.E->AvailableSlots.AddTag(Chest);
 struct FExample { const TCHAR* Name; FGameplayTag Slot; FGameplayAttribute Attribute; float Amount; };
 const FExample Examples[] = {
  {TEXT("SapphireRing"), Left, UMyAttributeSet::GetMaxManaAttribute(), 25.f},
  {TEXT("RubyRing"), Right, UMyAttributeSet::GetMaxHealthAttribute(), 50.f},
  {TEXT("IronGuardChest"), Chest, UMyAttributeSet::GetArmorAttribute(), 7.f}};
 for (const auto& Example : Examples)
 {
  const FPrimaryAssetId Id(FPrimaryAssetType(TEXT("BackpackItem")), FName(Example.Name));
  const auto* Def = Catalog->FindDefinition(Id);
  if (!TestNotNull(Example.Name, Def)) return false;
  TestNotNull(TEXT("Imported texture referenced"), Def->Icon.Get());
  TestTrue(TEXT("Icon uses EquipmentIcon directory"), Def->Icon && Def->Icon->GetPathName().StartsWith(TEXT("/Game/Assets/EquipmentIcon/")));
  TestFalse(TEXT("Equipment not stackable"), Def->bStackable);
  const float Before = F.ASC->GetNumericAttribute(Example.Attribute);
  const float HealthBefore = F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
  const float ManaBefore = F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
  TestTrue(TEXT("Add actual table equipment"), F.B->TryAddItem(Id, 1).bSucceeded);
  TestTrue(TEXT("Apply actual GE asset"), F.Equip(0, Example.Slot));
  TestEqual(TEXT("Curve table determines bonus"), F.ASC->GetNumericAttribute(Example.Attribute), Before + Example.Amount);
  TestEqual(TEXT("Equipment does not heal"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), HealthBefore);
  TestEqual(TEXT("Equipment does not refill mana"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), ManaBefore);
  TestTrue(TEXT("Remove actual GE asset"), F.Unequip(Example.Slot));
  TestEqual(TEXT("Bonus removed"), F.ASC->GetNumericAttribute(Example.Attribute), Before);
  // Reset inventory through the same combined snapshot path used by travel.
  auto Empty = F.State->ExportBackpackSnapshot(); for (auto& Slot : Empty.Slots) Slot.Clear();
  TestTrue(TEXT("Reset test inventory"), F.State->ImportBackpackSnapshot(Empty));
 }
 auto* Curve = Curves->FindCurve(TEXT("IronGuardChest_Armor"), TEXT("EquipmentTest"));
 if (!TestNotNull(TEXT("Editable armor curve"), Curve)) return false;
 const float Original = Curve->Eval(1.f);
 Curve->UpdateOrAddKey(1.f, 13.f);
 const FPrimaryAssetId ChestId(FPrimaryAssetType(TEXT("BackpackItem")), TEXT("IronGuardChest"));
 F.B->TryAddItem(ChestId, 1);
 const float Before = F.Value();
 TestTrue(TEXT("Equip after table edit"), F.Equip(0, Chest));
 TestEqual(TEXT("No GE rewrite required for balance changes"), F.Value(), Before + 13.f);
 const float OldResilience = F.ASC->GetNumericAttribute(UMyAttributeSet::GetResilienceAttribute());
 F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetResilienceAttribute(), OldResilience + 4.f);
 TestEqual(TEXT("Derived base recalculates while equipment bonus survives"), F.Value(), Before + 14.f);
 F.Unequip(Chest);
 Curve->UpdateOrAddKey(1.f, Original); // In-memory only; never save test edits.
 auto EmptyRings = F.State->ExportBackpackSnapshot(); for (auto& Item : EmptyRings.Slots) Item.Clear();
 TestTrue(TEXT("Clear fixture for two real rings"), F.State->ImportBackpackSnapshot(EmptyRings));
 const FPrimaryAssetId RingId(FPrimaryAssetType(TEXT("BackpackItem")), TEXT("SapphireRing"));
 const float ManaBase = F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute());
 TestTrue(TEXT("Add two real rings"), F.B->TryAddItem(RingId, 2).bSucceeded);
 TestTrue(TEXT("Equip left ring"), F.Equip(0, Left));
 TestTrue(TEXT("Equip right ring"), F.Equip(1, Right));
 TestEqual(TEXT("Two rings stack after base override"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute()), ManaBase + 50.f);
 TestTrue(TEXT("Remove left ring independently"), F.Unequip(Left));
 TestEqual(TEXT("Right ring survives"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute()), ManaBase + 25.f);
 TestTrue(TEXT("Remove right ring"), F.Unequip(Right));
 TestEqual(TEXT("Mana maximum restored"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute()), ManaBase);
 TestNotNull(TEXT("Existing health potion remains available"), Catalog->FindDefinition(FPrimaryAssetId(FPrimaryAssetType(TEXT("BackpackItem")), TEXT("HealthPotion"))));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentSoulPickupTest, "test_25d.Equipment.SoulPickupAuthorityAndCapacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentSoulPickupTest::RunTest(const FString& Parameters)
{
 EquipmentTests::FFixture F;
 auto* Item = F.World->SpawnActor<AEquipmentSoulPickup>();
 Item->Pickup->ItemDefinitions = F.Catalog;
 Item->Pickup->ConfigurePickup(F.Armor,1,true);
 Item->Pickup->ItemInstanceId = FGuid::NewGuid();
 const auto Id = Item->Pickup->ItemInstanceId;
 TestEqual(TEXT("Interactive equipment only"), Item->Pickup->GetPickupMode(), EPickupMode::Interact);
 TestTrue(TEXT("Soul effect assigned"), Item->Soul->GetAsset() && Item->Soul->GetAsset()->GetName()==TEXT("NS_Soul"));
 TestTrue(TEXT("Pickup effect assigned"), Item->PickupEffect && Item->PickupEffect->GetName()==TEXT("NS_SoulPickup"));
 TestTrue(TEXT("Replicated pickup"), Item->GetIsReplicated());
 int32 Events=0; Item->Pickup->OnPickedUp.AddLambda([&](){++Events;});
 auto* PawnRoot = NewObject<USceneComponent>(F.Pawn); F.Pawn->AddInstanceComponent(PawnRoot); F.Pawn->SetRootComponent(PawnRoot); PawnRoot->RegisterComponent();
 F.Pawn->SetActorLocation(FVector(1000,0,0));
 TestFalse(TEXT("Remote request cannot pick up far soul"),Item->Pickup->TryPickup(F.State));
 F.Pawn->SetActorLocation(FVector::ZeroVector);
 F.B->SetCapacity(0);
 TestFalse(TEXT("Full backpack retains world equipment"), Item->Pickup->TryPickup(F.State));
 TestEqual(TEXT("No success VFX on failed pickup"), Events, 0);
 TestEqual(TEXT("Failed pickup retains quantity"),Item->Pickup->Quantity,1);
 F.B->SetCapacity(1);
 TestTrue(TEXT("Near player can pick up"),Item->Pickup->TryPickup(F.State));
 TestEqual(TEXT("Exactly one collection event"),Events,1);
 TestEqual(TEXT("Original instance in backpack"),F.B->GetInventoryModel()->GetSlots()[0].ItemInstanceId,Id);
 TestFalse(TEXT("No duplicate pickup"),Item->Pickup->TryPickup(F.State));
 TestTrue(TEXT("Picked equipment can be equipped"),F.Equip(0));
 TestEqual(TEXT("GE applied after pickup"),F.Value(),15.f);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentChannelValidationTest, "test_25d.Equipment.RejectWrongEvaluationChannel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentChannelValidationTest::RunTest(const FString& Parameters)
{
 const auto* Settings = GetDefault<UGameplayAbilitiesDeveloperSettings>();
 if (!TestTrue(TEXT("Channels enabled by project settings"), Settings->bAllowGameplayModEvaluationChannels)) return false;
 auto* Effect = NewObject<UEquipmentTestArmor>();
 TestTrue(TEXT("Equipment channel accepted"), Effect->IsSafeEquipmentEffect(1.f));
 Effect->Modifiers[0].EvaluationChannelSettings.SetEvaluationChannel(EGameplayModEvaluationChannel::Channel0);
 TestFalse(TEXT("Base channel rejected even when other GE settings are valid"), Effect->IsSafeEquipmentEffect(1.f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentTenSlotsTest, "test_25d.Equipment.TenSlots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentTenSlotsTest::RunTest(const FString&)
{
 EquipmentTests::FFixture F;
 TestEqual(TEXT("Ten ordered standard slots"),EquipmentSlots::All().Num(),10);
 int32 Elements=0;
 F.B->TryAddItem(F.Armor,1);
 for (const auto& Info:EquipmentSlots::All())
 {
  TestTrue(TEXT("Legacy component defaults extended"),F.E->AvailableSlots.HasTagExact(Info.Tag));
  Elements+=Info.bElement ? 1 : 0;

  TestTrue(TEXT("Equip standard slot"),F.Equip(0,Info.Tag));
  TestTrue(TEXT("Independent bonus"),FMath::IsNearlyEqual(F.Value(),15.f));
  auto Snapshot=F.State->ExportBackpackSnapshot();
  TestTrue(TEXT("Restore new slot identity"),F.State->ImportBackpackSnapshot(Snapshot));
  TestTrue(TEXT("No duplicate effect"),FMath::IsNearlyEqual(F.Value(),15.f));
  TestTrue(TEXT("Unequip"),F.Unequip(Info.Tag));
  TestTrue(TEXT("Restored armor"),FMath::IsNearlyEqual(F.Value(),10.f));

 }
 TestEqual(TEXT("Separate element group"),Elements,3);
 TestEqual(TEXT("Armor retains saved Chest identity"),EquipmentSlots::All()[3].Tag.ToString(),FString(TEXT("Equipment.Slot.Chest")));
 return true;
}
#endif




