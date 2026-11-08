#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Widget/EquipmentPanel.h"
#include "Backpack/InventoryModel.h"
#include "Backpack/ItemDefinition.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentUIEligibilityTest, "test_25d.Equipment.UI.SelectionEligibility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentUIEligibilityTest::RunTest(const FString& Parameters)
{
    const FGameplayTag Left = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Left"));
    const FGameplayTag Right = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Right"));
    UInventoryModel* Model = NewObject<UInventoryModel>();
    UBackpackItemDefinitions* Definitions = NewObject<UBackpackItemDefinitions>();
    FBackpackItemDefEntry Def;
    Def.ItemDefinitionId = FPrimaryAssetId(TEXT("Item"), TEXT("UITestRing"));
    Def.bEquippable = true;
    Def.bStackable = false;
    Def.MaxStack = 1;
    Def.AllowedEquipmentSlots.AddTag(Left);
    Definitions->Items.Add(Def);
    Model->SetItemDefinitions(Definitions);
    Model->InitializeSlots(2);
    TestFalse(TEXT("No model"), UEquipmentPanel::CanEquipToSlot(nullptr, 0, Left));
    TestFalse(TEXT("No selection"), UEquipmentPanel::CanEquipToSlot(Model, INDEX_NONE, Left));
    TestFalse(TEXT("Empty selection"), UEquipmentPanel::CanEquipToSlot(Model, 0, Left));
    Model->TryAddItem(Def.ItemDefinitionId, 1);
    TestTrue(TEXT("Compatible selected equipment enables equip"), UEquipmentPanel::CanEquipToSlot(Model, 0, Left));
    TestFalse(TEXT("Different slot remains disabled"), UEquipmentPanel::CanEquipToSlot(Model, 0, Right));
    TestFalse(TEXT("Stale index remains disabled"), UEquipmentPanel::CanEquipToSlot(Model, 20, Left));
    return true;
}
#include "UI/ViewModel/MVVM_Backpack.h"
#include "Backpack/ReplicatedInventory.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentUIDetailsTest, "test_25d.Equipment.UI.SavedRollDetails",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentUIDetailsTest::RunTest(const FString& Parameters)
{
    UInventoryModel* Model = NewObject<UInventoryModel>();
    UBackpackItemDefinitions* Definitions = NewObject<UBackpackItemDefinitions>();
    FBackpackItemDefEntry Def;
    Def.ItemDefinitionId = FPrimaryAssetId(TEXT("Item"), TEXT("UIDetails"));
    Def.DisplayName = FText::FromString(TEXT("Ring"));
    Definitions->Items.Add(Def);
    Model->SetItemDefinitions(Definitions);
    Model->InitializeSlots(1);
    UMVVM_Backpack* VM = NewObject<UMVVM_Backpack>();
    VM->Initialize(Model);
    TestTrue(TEXT("No selection has no description"), VM->GetSelectedItemDescription().IsEmpty());
    FReplicatedInventorySlot Slot;
    Slot.ItemDefinitionId = Def.ItemDefinitionId;
    Slot.Quantity = 1;
    Slot.ItemInstanceId = FGuid::NewGuid();
    Model->ApplyReplicatedSlots({Slot}, 1);
    VM->SelectSlot(0);
    TestEqual(TEXT("Legacy name stays unchanged"), VM->GetSelectedItemName().ToString(), FString(TEXT("Ring")));
    TestTrue(TEXT("Legacy roll has no random description"), VM->GetSelectedItemDescription().IsEmpty());
    Slot.RollData.GenerationVersion = 1;
    Slot.RollData.ItemLevel = 17;
    Slot.RollData.RarityId = TEXT("Rare");
    Slot.RollData.RarityName = FText::FromString(TEXT("Rare"));
    FEquipmentRolledAffix Affix;
    Affix.AffixId = TEXT("Strong");
    Affix.DisplayName = FText::FromString(TEXT("Strong"));
    FEquipmentRolledStat Stat;
    Stat.StatTag = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Stat.Armor"));
    Stat.Value = 23;
    Affix.Stats.Add(Stat);
    Slot.RollData.Affixes.Add(Affix);
    Model->ApplyReplicatedSlots({Slot}, 1);
    TestTrue(TEXT("Saved prefix included"), VM->GetSelectedItemName().ToString().Contains(TEXT("Strong")));
    const FString Description = VM->GetSelectedItemDescription().ToString();
    TestTrue(TEXT("Saved rarity included"), Description.Contains(TEXT("Rare")));
    TestTrue(TEXT("Saved level included"), Description.Contains(TEXT("17")));
    TestTrue(TEXT("Saved affix value included"), Description.Contains(TEXT("23")));
    TestTrue(TEXT("Saved affix name included"), Description.Contains(TEXT("Strong")));
    TestTrue(TEXT("Reading UI does not change saved roll"), Model->GetSlots()[0].RollData == Slot.RollData);
    VM->ResetSelection();
    TestTrue(TEXT("Cleared selection clears details"), VM->GetSelectedItemDescription().IsEmpty());
    return true;
}
#endif
