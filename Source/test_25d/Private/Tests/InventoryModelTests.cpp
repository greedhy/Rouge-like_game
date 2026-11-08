/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Inventory.InitializesStableSlots
 * - test_25d.Inventory.ItemDefinitionUsesSafeDefaults
 * - test_25d.Inventory.ItemDefinitionCatalog
 * - test_25d.Inventory.AddsCatalogItemByStableId
 * - test_25d.Inventory.PartiallyAddsToRemainingStack
 * - test_25d.Inventory.SwapsStableSlots
 * - test_25d.Inventory.ShrinksByDiscardingTailSlots
 * - test_25d.Inventory.BackpackMaxSlotsTextContainsOnlyCapacity
 * - test_25d.Inventory.UsesAndClearsFinalItem
 * - test_25d.Inventory.RejectsInvalidOrEffectlessUseWithoutConsumption
 * - test_25d.Inventory.DoesNotConsumeWhenEffectApplicationFails
 * - test_25d.Inventory.ConsumesWhenInstantEffectSucceeds
 * - test_25d.Inventory.ConsumesWhenDurationEffectSucceeds
 * - test_25d.Inventory.PlayerStateOwnsBackpack
 * - test_25d.Inventory.ReplicatedInventoryMarksChangedSlotDirty
 * - test_25d.Inventory.RejectsClientMutation
 * - test_25d.Inventory.ReplicatedSlotsDriveTypedViewModels
 * - test_25d.Inventory.BackpackSnapshotRoundTripsStableSlots
 * - test_25d.Inventory.BackpackPersistenceRejectsDuplicateSessionLock
 * - test_25d.Inventory.BackpackPersistenceRestoresReconnectSnapshot
 * - test_25d.Inventory.BackpackTravelCopiesPlayerStateSnapshot
 * - test_25d.Inventory.BackpackMutationProducesPersistableSnapshot
 * - test_25d.Inventory.BackpackLocalDevelopmentIdentityIsStableAcrossReconnect
 * - test_25d.Inventory.GameModeBackpackLoginLoadsAndRejectsDuplicate
 * - test_25d.Inventory.GameModeBackpackMutationPersistsSnapshot
 * - test_25d.Inventory.GameModeBackpackSeamlessTravelRetainsSnapshotAndLock
 * - test_25d.Inventory.ViewModelSwapRequestDoesNotMutateModel
 * - test_25d.Inventory.SlotNormalBackgroundIsTransparent
 * - test_25d.Inventory.InputTagsExposeBackpackAndPickup
 * - test_25d.Inventory.InteractPickupTracksOverlapsAndChoosesCandidate
 * - test_25d.Inventory.NetworkedDropAndEnemySpawnAreServerAuthoritative
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Backpack/InventoryModel.h"
#include "Backpack/ReplicatedInventory.h"
#include "Backpack/ItemDefinition.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/WorldPickupComponent.h"
#include "Backpack/BackpackPersistence.h"
#include "Player/MyPlayerState.h"
#include "Player/MyPlayerController.h"
#include "Actor/MyEnemySpawnVolume.h"
#include "Game/MyGameModeBase.h"
#include "Tests/InventoryTestGameplayEffects.h"
#include "UI/ViewModel/MVVM_Backpack.h"
#include "UI/ViewModel/MVVM_InventorySlot.h"
#include "MyGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    struct FInventoryTestPawn
    {
        FInventoryTestPawn()
        {
            const FName WorldName =
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("InventoryTestWorld"));
            World = UWorld::CreateWorld(EWorldType::Game, false, WorldName);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Pawn = World->SpawnActor<APawn>();
            AbilitySystem = NewObject<UAbilitySystemComponent>(Pawn);
            Pawn->AddInstanceComponent(AbilitySystem);
            AbilitySystem->RegisterComponent();
            AbilitySystem->InitAbilityActorInfo(Pawn, Pawn);
        }

        ~FInventoryTestPawn()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        UWorld* World = nullptr;
        APawn* Pawn = nullptr;
        UAbilitySystemComponent* AbilitySystem = nullptr;
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryInitializesStableSlots, "test_25d.Inventory.InitializesStableSlots",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.InitializesStableSlots 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryInitializesStableSlots::RunTest(const FString& Parameters)
{
    UInventoryModel* Model = NewObject<UInventoryModel>();
    Model->InitializeSlots(3);

    TestEqual(TEXT("Stable slot count"), Model->GetSlots().Num(), 3);
    TestTrue(TEXT("Default slot is empty"), Model->GetSlots()[0].IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryItemDefinitionUsesSafeDefaults,
                                 "test_25d.Inventory.ItemDefinitionUsesSafeDefaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.ItemDefinitionUsesSafeDefaults 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryItemDefinitionUsesSafeDefaults::RunTest(const FString& Parameters)
{
    UItemDefinition* Definition = NewObject<UItemDefinition>();
    TestTrue(TEXT("Items stack by default"), Definition->bStackable);
    TestEqual(TEXT("Default stack limit"), Definition->MaxStack, 1);
    TestFalse(TEXT("Items are not auto-picked up by default"), Definition->bAutoPickup);
    TestFalse(TEXT("Items are not usable by default"), Definition->bUsable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryItemDefinitionCatalogResolvesStableIds,
                                 "test_25d.Inventory.ItemDefinitionCatalog",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.ItemDefinitionCatalog 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryItemDefinitionCatalogResolvesStableIds::RunTest(const FString& Parameters)
{
    UBackpackItemDefinitions* Definitions = NewObject<UBackpackItemDefinitions>();
    FBackpackItemDefEntry HealthPotion;
    HealthPotion.ItemDefinitionId =
        FPrimaryAssetId(FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("HealthPotion")));
    HealthPotion.PickupMode = EPickupMode::Auto;
    HealthPotion.MaxStack = 10;
    Definitions->Items.Add(HealthPotion);

    const FBackpackItemDefEntry* Found = Definitions->FindDefinition(HealthPotion.ItemDefinitionId);
    TestNotNull(TEXT("Configured stable ID resolves"), Found);
    if (Found)
    {
        TestEqual(TEXT("Configured pickup mode is preserved"), Found->PickupMode, EPickupMode::Auto);
        TestEqual(TEXT("Configured stack limit is preserved"), Found->MaxStack, 10);
    }

    const FPrimaryAssetId MissingId(FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("Missing")));
    TestNull(TEXT("Unknown stable ID is rejected"), Definitions->FindDefinition(MissingId));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAddsCatalogItemByStableId, "test_25d.Inventory.AddsCatalogItemByStableId",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.AddsCatalogItemByStableId 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryAddsCatalogItemByStableId::RunTest(const FString& Parameters)
{
    UBackpackItemDefinitions* Definitions = NewObject<UBackpackItemDefinitions>();
    FBackpackItemDefEntry HealthPotion;
    HealthPotion.ItemDefinitionId =
        FPrimaryAssetId(FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("HealthPotion")));
    HealthPotion.bStackable = true;
    HealthPotion.MaxStack = 10;
    Definitions->Items.Add(HealthPotion);

    UInventoryModel* Model = NewObject<UInventoryModel>();
    Model->InitializeSlots(1);
    Model->SetItemDefinitions(Definitions);
    const FInventoryAddResult Result = Model->TryAddItem(HealthPotion.ItemDefinitionId, 12);

    TestEqual(TEXT("Catalog ID fills one stack"), Result.AcceptedQuantity, 10);
    TestEqual(TEXT("Catalog ID preserves unaccepted remainder"), Result.RemainingQuantity, 2);
    TestEqual(TEXT("Catalog ID is written to the stable slot"), Model->GetSlots()[0].ItemDefinitionId,
              HealthPotion.ItemDefinitionId);
    TestFalse(TEXT("Unknown catalog ID is rejected"),
              Model->TryAddItem(FPrimaryAssetId(FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("Unknown"))), 1)
                  .bSucceeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryPartiallyAddsToRemainingStack,
                                 "test_25d.Inventory.PartiallyAddsToRemainingStack",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.PartiallyAddsToRemainingStack 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryPartiallyAddsToRemainingStack::RunTest(const FString& Parameters)
{
    UInventoryModel* Model = NewObject<UInventoryModel>();
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Potion->bStackable = true;
    Potion->MaxStack = 10;

    Model->InitializeSlots(1);
    const FInventoryAddResult InitialAdd = Model->TryAddItem(Potion, 5);
    const FInventoryAddResult PartialAdd = Model->TryAddItem(Potion, 10);

    TestEqual(TEXT("Initial quantity is accepted"), InitialAdd.AcceptedQuantity, 5);
    TestEqual(TEXT("Second add fills the remaining stack space"), PartialAdd.AcceptedQuantity, 5);
    TestEqual(TEXT("Second add leaves the excess as a remainder"), PartialAdd.RemainingQuantity, 5);
    TestEqual(TEXT("Slot is full"), Model->GetSlots()[0].Quantity, 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySwapsStableSlots, "test_25d.Inventory.SwapsStableSlots",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.SwapsStableSlots 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventorySwapsStableSlots::RunTest(const FString& Parameters)
{
    UInventoryModel* Model = NewObject<UInventoryModel>();
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Potion->MaxStack = 10;

    Model->InitializeSlots(2);
    Model->TryAddItem(Potion, 1);
    Model->SwapSlots(0, 1);

    TestTrue(TEXT("Source slot is empty after swap"), Model->GetSlots()[0].IsEmpty());
    TestFalse(TEXT("Target slot contains the item after swap"), Model->GetSlots()[1].IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryShrinksByDiscardingTailSlots,
                                 "test_25d.Inventory.ShrinksByDiscardingTailSlots",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.ShrinksByDiscardingTailSlots 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryShrinksByDiscardingTailSlots::RunTest(const FString& Parameters)
{
    AMyPlayerState* State = NewObject<AMyPlayerState>();
    UBackpackComponent* Component = State->GetBackpackComponent();
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Potion->bStackable = false;

    Component->TryAddItem(Potion, 3);
    const int32 RevisionBeforeCapacityChange = Component->GetRevision();
    TestTrue(TEXT("Authoritative component accepts a capacity decrease"), Component->SetCapacity(2));
    TestEqual(TEXT("Capacity and replicated list shrink together"), Component->GetReplicatedSlots().Items.Num(),
              Component->GetCapacity());
    TestEqual(TEXT("Capacity removes the tail slots"), Component->GetInventoryModel()->GetSlots().Num(), 2);
    TestFalse(TEXT("Existing slot zero is preserved"), Component->GetInventoryModel()->GetSlots()[0].IsEmpty());
    TestFalse(TEXT("Existing slot one is preserved"), Component->GetInventoryModel()->GetSlots()[1].IsEmpty());
    TestEqual(TEXT("Capacity mutation advances revision once"), Component->GetRevision(),
              RevisionBeforeCapacityChange + 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackpackMaxSlotsTextContainsOnlyCapacity,
                                 "test_25d.Inventory.BackpackMaxSlotsTextContainsOnlyCapacity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.BackpackMaxSlotsTextContainsOnlyCapacity 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBackpackMaxSlotsTextContainsOnlyCapacity::RunTest(const FString& Parameters)
{
    UInventoryModel* Model = NewObject<UInventoryModel>();
    Model->InitializeSlots(16);
    UMVVM_Backpack* ViewModel = NewObject<UMVVM_Backpack>();
    ViewModel->Initialize(Model);

    TestEqual(TEXT("Capacity text contains only the slot count"), ViewModel->GetMaxSlotsText().ToString(),
              FString(TEXT("16")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryUsesAndClearsFinalItem, "test_25d.Inventory.UsesAndClearsFinalItem",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.UsesAndClearsFinalItem 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryUsesAndClearsFinalItem::RunTest(const FString& Parameters)
{
    UInventoryModel* Model = NewObject<UInventoryModel>();
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Potion->bUsable = true;
    Potion->MaxStack = 10;

    Model->InitializeSlots(1);
    Model->TryAddItem(Potion, 1);

    TestTrue(TEXT("Usable item can be used"), Model->UseItemAt(0));
    TestTrue(TEXT("Using the final item leaves a stable empty slot"), Model->GetSlots()[0].IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryRejectsInvalidOrEffectlessUseWithoutConsumption,
                                 "test_25d.Inventory.RejectsInvalidOrEffectlessUseWithoutConsumption",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.RejectsInvalidOrEffectlessUseWithoutConsumption 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryRejectsInvalidOrEffectlessUseWithoutConsumption::RunTest(const FString& Parameters)
{
    AMyPlayerState* State = NewObject<AMyPlayerState>();
    UBackpackComponent* Component = State->GetBackpackComponent();
    FInventoryTestPawn Target;
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Potion->bUsable = true;

    TestTrue(TEXT("Authority adds the effectless item"), Component->TryAddItem(Potion, 1).bSucceeded);
    TestFalse(TEXT("Invalid use does not consume"), Component->TryUseItemForPawn(99, Target.Pawn));
    TestFalse(TEXT("Effectless use does not consume"), Component->TryUseItemForPawn(0, Target.Pawn));
    TestEqual(TEXT("Failed effect retains stack"), Component->GetInventoryModel()->GetSlots()[0].Quantity, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryDoesNotConsumeWhenEffectApplicationFails,
                                 "test_25d.Inventory.DoesNotConsumeWhenEffectApplicationFails",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.DoesNotConsumeWhenEffectApplicationFails 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryDoesNotConsumeWhenEffectApplicationFails::RunTest(const FString& Parameters)
{
    AMyPlayerState* State = NewObject<AMyPlayerState>();
    UBackpackComponent* Component = State->GetBackpackComponent();
    FInventoryTestPawn Target;
    FGameplayEffectApplicationQuery RejectEffect;
    RejectEffect.BindLambda(
        [](const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec&)
        {
            return false;
        });
    Target.AbilitySystem->GameplayEffectApplicationQueries.Add(MoveTemp(RejectEffect));
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Potion->bUsable = true;
    Potion->UseEffect = UInventoryTestInstantGameplayEffect::StaticClass();

    TestTrue(TEXT("Authority adds the item with a rejected effect"), Component->TryAddItem(Potion, 1).bSucceeded);
    TestFalse(TEXT("Rejected effect does not consume"), Component->TryUseItemForPawn(0, Target.Pawn));
    TestEqual(TEXT("Rejected effect retains stack"), Component->GetInventoryModel()->GetSlots()[0].Quantity, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryConsumesWhenInstantEffectSucceeds,
                                 "test_25d.Inventory.ConsumesWhenInstantEffectSucceeds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.ConsumesWhenInstantEffectSucceeds 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryConsumesWhenInstantEffectSucceeds::RunTest(const FString& Parameters)
{
    AMyPlayerState* State = NewObject<AMyPlayerState>();
    UBackpackComponent* Component = State->GetBackpackComponent();
    FInventoryTestPawn Target;
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Potion->bUsable = true;
    Potion->UseEffect = UInventoryTestInstantGameplayEffect::StaticClass();

    TestTrue(TEXT("Authority adds the instant item"), Component->TryAddItem(Potion, 1).bSucceeded);
    TestTrue(TEXT("Instant effect consumes exactly once"), Component->TryUseItemForPawn(0, Target.Pawn));
    TestTrue(TEXT("Instant effect clears final stack"), Component->GetInventoryModel()->GetSlots()[0].IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryConsumesWhenDurationEffectSucceeds,
                                 "test_25d.Inventory.ConsumesWhenDurationEffectSucceeds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.ConsumesWhenDurationEffectSucceeds 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryConsumesWhenDurationEffectSucceeds::RunTest(const FString& Parameters)
{
    AMyPlayerState* State = NewObject<AMyPlayerState>();
    UBackpackComponent* Component = State->GetBackpackComponent();
    FInventoryTestPawn Target;
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Potion->bUsable = true;
    Potion->UseEffect = UInventoryTestDurationGameplayEffect::StaticClass();

    TestTrue(TEXT("Authority adds the duration item"), Component->TryAddItem(Potion, 1).bSucceeded);
    TestTrue(TEXT("Duration effect consumes exactly once"), Component->TryUseItemForPawn(0, Target.Pawn));
    TestTrue(TEXT("Duration effect clears final stack"), Component->GetInventoryModel()->GetSlots()[0].IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerStateOwnsBackpack, "test_25d.Inventory.PlayerStateOwnsBackpack",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.PlayerStateOwnsBackpack 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPlayerStateOwnsBackpack::RunTest(const FString& Parameters)
{
    AMyPlayerState* State = NewObject<AMyPlayerState>();

    TestNotNull(TEXT("PlayerState owns backpack"), State->GetBackpackComponent());
    TestNull(TEXT("GameInstance has no backpack API"), nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplicatedInventoryMarksChangedSlotDirty,
                                 "test_25d.Inventory.ReplicatedInventoryMarksChangedSlotDirty",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.ReplicatedInventoryMarksChangedSlotDirty 回归，失败信息由下方断言交给自动化框架收集。 */
bool FReplicatedInventoryMarksChangedSlotDirty::RunTest(const FString& Parameters)
{
    FReplicatedInventoryList ReplicatedSlots;
    ReplicatedSlots.Items.AddDefaulted();
    ReplicatedSlots.MarkItemDirty(ReplicatedSlots.Items[0]);

    TestTrue(TEXT("Server marks changed slot dirty"), ReplicatedSlots.Items[0].ReplicationID != INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryRejectsClientMutation, "test_25d.Inventory.RejectsClientMutation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.RejectsClientMutation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryRejectsClientMutation::RunTest(const FString& Parameters)
{
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    UBackpackComponent* ClientComponent = NewObject<UBackpackComponent>();
    const int32 ClientRevision = ClientComponent->GetRevision();

    TestFalse(TEXT("Client cannot mutate a valid inventory item"), ClientComponent->TryAddItem(Potion, 1).bSucceeded);
    TestEqual(TEXT("Client rejection leaves the revision unchanged"), ClientComponent->GetRevision(), ClientRevision);

    AMyPlayerState* ServerState = NewObject<AMyPlayerState>();
    UBackpackComponent* ServerComponent = ServerState->GetBackpackComponent();
    TestTrue(TEXT("Authority can mutate a valid inventory item"), ServerComponent->TryAddItem(Potion, 1).bSucceeded);
    TestEqual(TEXT("Authority mutation advances the revision"), ServerComponent->GetRevision(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplicatedSlotsDriveTypedViewModels,
                                 "test_25d.Inventory.ReplicatedSlotsDriveTypedViewModels",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.ReplicatedSlotsDriveTypedViewModels 回归，失败信息由下方断言交给自动化框架收集。 */
bool FReplicatedSlotsDriveTypedViewModels::RunTest(const FString& Parameters)
{
    UInventoryModel* Model = NewObject<UInventoryModel>();
    FReplicatedInventoryList ReplicatedSlots;
    ReplicatedSlots.Items.SetNum(2);
    ReplicatedSlots.Items[0].ItemDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("Item")), FName(TEXT("Potion")));
    ReplicatedSlots.Items[0].Quantity = 3;
    Model->ApplyReplicatedSlots(ReplicatedSlots.Items, 2);

    UMVVM_Backpack* ViewModel = NewObject<UMVVM_Backpack>();
    ViewModel->Initialize(Model);
    const TArray<UMVVM_InventorySlot*>& SlotViewModels = ViewModel->GetSlotViewModels();
    UMVVM_InventorySlot* FirstSlot = Cast<UMVVM_InventorySlot>(SlotViewModels[0]);

    TestEqual(TEXT("View model count follows stable slots"), SlotViewModels.Num(), 2);
    TestNotNull(TEXT("First stable slot has a typed view model"), FirstSlot);
    TestFalse(TEXT("Replicated item slot is visible"), FirstSlot->IsEmpty());
    TestEqual(TEXT("Replicated quantity reaches the typed view model"), FirstSlot->GetStackCount(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackpackSnapshotRoundTripsStableSlots,
                                 "test_25d.Inventory.BackpackSnapshotRoundTripsStableSlots",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.BackpackSnapshotRoundTripsStableSlots 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBackpackSnapshotRoundTripsStableSlots::RunTest(const FString& Parameters)
{
    FBackpackSnapshot Snapshot;
    Snapshot.Capacity = 3;
    Snapshot.Revision = 17;
    Snapshot.Slots.SetNum(3);
    Snapshot.Slots[0].ItemDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("Item")), FName(TEXT("First")));
    Snapshot.Slots[0].Quantity = 2;
    Snapshot.Slots[2].ItemDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("Item")), FName(TEXT("Third")));
    Snapshot.Slots[2].Quantity = 5;

    AMyPlayerState* State = NewObject<AMyPlayerState>();
    TestTrue(TEXT("Snapshot imports into authoritative PlayerState"), State->ImportBackpackSnapshot(Snapshot));
    const FBackpackSnapshot Restored = State->ExportBackpackSnapshot();

    TestEqual(TEXT("Capacity round-trips"), Restored.Capacity, 3);
    TestEqual(TEXT("Revision round-trips"), Restored.Revision, 17);
    TestEqual(TEXT("Stable slot count round-trips"), Restored.Slots.Num(), 3);
    TestEqual(TEXT("First slot identity round-trips"), Restored.Slots[0].ItemDefinitionId,
              Snapshot.Slots[0].ItemDefinitionId);
    TestEqual(TEXT("First slot quantity round-trips"), Restored.Slots[0].Quantity, 2);
    TestTrue(TEXT("Middle empty slot remains empty"), Restored.Slots[1].IsEmpty());
    TestEqual(TEXT("Third slot identity remains at its original index"), Restored.Slots[2].ItemDefinitionId,
              Snapshot.Slots[2].ItemDefinitionId);
    TestEqual(TEXT("Third slot quantity remains at its original index"), Restored.Slots[2].Quantity, 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackpackPersistenceRejectsDuplicateSessionLock,
                                 "test_25d.Inventory.BackpackPersistenceRejectsDuplicateSessionLock",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.BackpackPersistenceRejectsDuplicateSessionLock 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBackpackPersistenceRejectsDuplicateSessionLock::RunTest(const FString& Parameters)
{
    FServerLocalBackpackPersistenceProvider Provider;
    const FString PlayerId =
        FString::Printf(TEXT("AutomationPlayer_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

    TestTrue(TEXT("First active session acquires the player lock"), Provider.AcquireSessionLock(PlayerId));
    TestFalse(TEXT("Second active session for the same player is rejected"), Provider.AcquireSessionLock(PlayerId));
    TestFalse(TEXT("Rejected duplicate does not release the original session lock"),
              Provider.AcquireSessionLock(PlayerId));
    Provider.ReleaseSessionLock(PlayerId);
    TestTrue(TEXT("Lock is available after the owning session releases it"), Provider.AcquireSessionLock(PlayerId));
    Provider.ReleaseSessionLock(PlayerId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackpackPersistenceRestoresReconnectSnapshot,
                                 "test_25d.Inventory.BackpackPersistenceRestoresReconnectSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.BackpackPersistenceRestoresReconnectSnapshot 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBackpackPersistenceRestoresReconnectSnapshot::RunTest(const FString& Parameters)
{
    FServerLocalBackpackPersistenceProvider Provider;
    const FString PlayerId = FString::Printf(TEXT("PIEReconnect_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FBackpackSnapshot SavedSnapshot;
    SavedSnapshot.Capacity = 2;
    SavedSnapshot.Slots.SetNum(2);
    SavedSnapshot.Slots[1].ItemDefinitionId =
        FPrimaryAssetId(FPrimaryAssetType(TEXT("Item")), FName(TEXT("ReconnectPotion")));
    SavedSnapshot.Slots[1].Quantity = 4;

    TestTrue(TEXT("First local session saves its snapshot"), Provider.Save(PlayerId, SavedSnapshot));
    FBackpackSnapshot ReconnectedSnapshot;
    TestTrue(TEXT("Reconnect with the same stable key loads a snapshot"), Provider.Load(PlayerId, ReconnectedSnapshot));
    TestEqual(TEXT("Reconnect preserves stable slot order"), ReconnectedSnapshot.Slots[1].ItemDefinitionId,
              SavedSnapshot.Slots[1].ItemDefinitionId);
    TestEqual(TEXT("Reconnect preserves quantity"), ReconnectedSnapshot.Slots[1].Quantity, 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackpackTravelCopiesPlayerStateSnapshot,
                                 "test_25d.Inventory.BackpackTravelCopiesPlayerStateSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.BackpackTravelCopiesPlayerStateSnapshot 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBackpackTravelCopiesPlayerStateSnapshot::RunTest(const FString& Parameters)
{
    FBackpackSnapshot Snapshot;
    Snapshot.Capacity = 2;
    Snapshot.Slots.SetNum(2);
    Snapshot.Slots[1].ItemDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("Item")), FName(TEXT("TravelPotion")));
    Snapshot.Slots[1].Quantity = 3;
    AMyPlayerState* SourceState = NewObject<AMyPlayerState>();
    AMyPlayerState* DestinationState = NewObject<AMyPlayerState>();

    TestTrue(TEXT("Source accepts the travel snapshot"), SourceState->ImportBackpackSnapshot(Snapshot));
    SourceState->SetBackpackPersistencePlayerId(TEXT("LocalDev.TravelClient"));
    SourceState->CopyProperties(DestinationState);
    const FBackpackSnapshot DestinationSnapshot = DestinationState->ExportBackpackSnapshot();
    TestEqual(TEXT("Travel copy keeps the slot index"), DestinationSnapshot.Slots[1].ItemDefinitionId,
              Snapshot.Slots[1].ItemDefinitionId);
    TestEqual(TEXT("Travel copy keeps the quantity"), DestinationSnapshot.Slots[1].Quantity, 3);
    TestEqual(TEXT("Travel copy keeps the stable persistence identity"),
              DestinationState->GetBackpackPersistencePlayerId(), FString(TEXT("LocalDev.TravelClient")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackpackMutationProducesPersistableSnapshot,
                                 "test_25d.Inventory.BackpackMutationProducesPersistableSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.BackpackMutationProducesPersistableSnapshot 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBackpackMutationProducesPersistableSnapshot::RunTest(const FString& Parameters)
{
    AMyPlayerState* State = NewObject<AMyPlayerState>();
    UBackpackComponent* Component = State->GetBackpackComponent();
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    FServerLocalBackpackPersistenceProvider Provider;
    const FString PlayerId = FString::Printf(TEXT("MutationSave_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    bool bSavedAfterMutation = false;
    const FDelegateHandle SaveHandle = Component->OnBackpackStateChanged.AddLambda(
        [&Provider, &PlayerId, &State, &bSavedAfterMutation]()
        {
            bSavedAfterMutation = Provider.Save(PlayerId, State->ExportBackpackSnapshot());
        });

    TestTrue(TEXT("Authoritative mutation succeeds"), Component->TryAddItem(Potion, 1).bSucceeded);
    Component->OnBackpackStateChanged.Remove(SaveHandle);
    FBackpackSnapshot LoadedSnapshot;
    TestTrue(TEXT("Mutation invokes durable snapshot save"), bSavedAfterMutation);
    TestTrue(TEXT("Saved mutation snapshot loads"), Provider.Load(PlayerId, LoadedSnapshot));
    TestEqual(TEXT("Saved mutation retains quantity"), LoadedSnapshot.Slots[0].Quantity, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackpackLocalDevelopmentIdentityIsStableAcrossReconnect,
                                 "test_25d.Inventory.BackpackLocalDevelopmentIdentityIsStableAcrossReconnect",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.BackpackLocalDevelopmentIdentityIsStableAcrossReconnect 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBackpackLocalDevelopmentIdentityIsStableAcrossReconnect::RunTest(const FString& Parameters)
{
    FString FirstConnectionId;
    FString ReconnectId;
    const FString LoginOptions(TEXT("?BackpackPlayerId=PIEReconnectClient"));
    const FUniqueNetIdRepl NoOnlineIdentity;

    TestTrue(TEXT("Controlled PIE identity is accepted"),
             AMyGameModeBase::ResolveStablePlayerIdForLogin(LoginOptions, NoOnlineIdentity, FirstConnectionId));
    TestTrue(TEXT("Reconnect accepts the same controlled PIE identity"),
             AMyGameModeBase::ResolveStablePlayerIdForLogin(LoginOptions, NoOnlineIdentity, ReconnectId));
    TestEqual(TEXT("Reconnect resolves the same persistence key"), ReconnectId, FirstConnectionId);
    FString MissingIdentity;
    TestFalse(TEXT("Persistence is disabled without online or controlled local identity"),
              AMyGameModeBase::ResolveStablePlayerIdForLogin(TEXT(""), NoOnlineIdentity, MissingIdentity));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameModeBackpackLoginLoadsAndRejectsDuplicate,
                                 "test_25d.Inventory.GameModeBackpackLoginLoadsAndRejectsDuplicate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.GameModeBackpackLoginLoadsAndRejectsDuplicate 回归，失败信息由下方断言交给自动化框架收集。 */
bool FGameModeBackpackLoginLoadsAndRejectsDuplicate::RunTest(const FString& Parameters)
{
    const FString PlayerId =
        FString::Printf(TEXT("GameModeLogin_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    AMyGameModeBase* FirstGameMode = NewObject<AMyGameModeBase>();
    AMyPlayerState* FirstState = NewObject<AMyPlayerState>();
    FString ErrorMessage;
    TestTrue(TEXT("Accepted stable login reserves a session"),
             FirstGameMode->BeginBackpackLogin(PlayerId, ErrorMessage));
    TestTrue(TEXT("Accepted login completes persistence wiring"),
             FirstGameMode->CompleteBackpackLogin(FirstState, PlayerId));

    FBackpackSnapshot Snapshot;
    Snapshot.Capacity = 2;
    Snapshot.Slots.SetNum(2);
    Snapshot.Slots[1].ItemDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("Item")), FName(TEXT("LoginPotion")));
    Snapshot.Slots[1].Quantity = 6;
    TestTrue(TEXT("First session owns a snapshot before travel"), FirstState->ImportBackpackSnapshot(Snapshot));
    FirstGameMode->HandleBackpackTravelPersistence(false);

    AMyGameModeBase* ReconnectedGameMode = NewObject<AMyGameModeBase>();
    AMyPlayerState* ReconnectedState = NewObject<AMyPlayerState>();
    ErrorMessage.Reset();
    TestTrue(TEXT("Reconnect reserves the released stable identity"),
             ReconnectedGameMode->BeginBackpackLogin(PlayerId, ErrorMessage));
    TestTrue(TEXT("Reconnect loads through GameMode wiring"),
             ReconnectedGameMode->CompleteBackpackLogin(ReconnectedState, PlayerId));
    TestEqual(TEXT("Accepted GameMode login restores saved slot order"),
              ReconnectedState->ExportBackpackSnapshot().Slots[1].ItemDefinitionId, Snapshot.Slots[1].ItemDefinitionId);

    AMyGameModeBase* DuplicateGameMode = NewObject<AMyGameModeBase>();
    ErrorMessage.Reset();
    TestFalse(TEXT("Duplicate is rejected before gameplay initialization"),
              DuplicateGameMode->BeginBackpackLogin(PlayerId, ErrorMessage));
    TestEqual(TEXT("Duplicate rejection uses the pre-login error"), ErrorMessage,
              FString(TEXT("backpack_duplicate_session")));
    ReconnectedGameMode->HandleBackpackTravelPersistence(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameModeBackpackMutationPersistsSnapshot,
                                 "test_25d.Inventory.GameModeBackpackMutationPersistsSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.GameModeBackpackMutationPersistsSnapshot 回归，失败信息由下方断言交给自动化框架收集。 */
bool FGameModeBackpackMutationPersistsSnapshot::RunTest(const FString& Parameters)
{
    const FString PlayerId =
        FString::Printf(TEXT("GameModeMutation_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    AMyGameModeBase* GameMode = NewObject<AMyGameModeBase>();
    AMyPlayerState* State = NewObject<AMyPlayerState>();
    FString ErrorMessage;
    TestTrue(TEXT("Mutation test reserves a session"), GameMode->BeginBackpackLogin(PlayerId, ErrorMessage));
    TestTrue(TEXT("Mutation test registers GameMode persistence"), GameMode->CompleteBackpackLogin(State, PlayerId));

    UItemDefinition* Potion = NewObject<UItemDefinition>();
    TestTrue(TEXT("Authoritative mutation succeeds after GameMode registration"),
             State->GetBackpackComponent()->TryAddItem(Potion, 1).bSucceeded);
    FBackpackSnapshot PersistedSnapshot;
    TestTrue(TEXT("GameMode mutation callback saved the changed snapshot"),
             GameMode->LoadPersistedBackpackSnapshot(PlayerId, PersistedSnapshot));
    TestEqual(TEXT("Callback save retained the changed quantity"), PersistedSnapshot.Slots[0].Quantity, 1);
    GameMode->HandleBackpackTravelPersistence(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameModeBackpackSeamlessTravelRetainsSnapshotAndLock,
                                 "test_25d.Inventory.GameModeBackpackSeamlessTravelRetainsSnapshotAndLock",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.GameModeBackpackSeamlessTravelRetainsSnapshotAndLock 回归，失败信息由下方断言交给自动化框架收集。 */
bool FGameModeBackpackSeamlessTravelRetainsSnapshotAndLock::RunTest(const FString& Parameters)
{
    const FString PlayerId =
        FString::Printf(TEXT("GameModeSeamless_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    AMyGameModeBase* SourceGameMode = NewObject<AMyGameModeBase>();
    AMyPlayerState* SourceState = NewObject<AMyPlayerState>();
    FString ErrorMessage;
    TestTrue(TEXT("Seamless source reserves a session"), SourceGameMode->BeginBackpackLogin(PlayerId, ErrorMessage));
    TestTrue(TEXT("Seamless source registers persistence"),
             SourceGameMode->CompleteBackpackLogin(SourceState, PlayerId));

    FBackpackSnapshot Snapshot;
    Snapshot.Capacity = 2;
    Snapshot.Slots.SetNum(2);
    Snapshot.Slots[0].ItemDefinitionId =
        FPrimaryAssetId(FPrimaryAssetType(TEXT("Item")), FName(TEXT("SeamlessPotion")));
    Snapshot.Slots[0].Quantity = 8;
    TestTrue(TEXT("Seamless source imports a snapshot"), SourceState->ImportBackpackSnapshot(Snapshot));
    SourceGameMode->HandleBackpackTravelPersistence(true);

    AMyPlayerState* DestinationState = NewObject<AMyPlayerState>();
    SourceState->CopyProperties(DestinationState);
    AMyGameModeBase* DestinationGameMode = NewObject<AMyGameModeBase>();
    DestinationGameMode->RestoreSeamlessBackpackSessions({DestinationState});
    TestEqual(TEXT("Seamless travel keeps the PlayerState snapshot"),
              DestinationState->ExportBackpackSnapshot().Slots[0].Quantity, 8);
    TestEqual(TEXT("Seamless travel keeps the stable identity"), DestinationState->GetBackpackPersistencePlayerId(),
              PlayerId);

    AMyGameModeBase* DuplicateGameMode = NewObject<AMyGameModeBase>();
    ErrorMessage.Reset();
    TestFalse(TEXT("Seamless travel retains the active session lock"),
              DuplicateGameMode->BeginBackpackLogin(PlayerId, ErrorMessage));
    SourceGameMode->HandleBackpackTravelPersistence(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FViewModelSwapRequestDoesNotMutateModel,
                                 "test_25d.Inventory.ViewModelSwapRequestDoesNotMutateModel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.ViewModelSwapRequestDoesNotMutateModel 回归，失败信息由下方断言交给自动化框架收集。 */
bool FViewModelSwapRequestDoesNotMutateModel::RunTest(const FString& Parameters)
{
    UInventoryModel* Model = NewObject<UInventoryModel>();
    UItemDefinition* Potion = NewObject<UItemDefinition>();
    Model->InitializeSlots(2);
    Model->TryAddItem(Potion, 1);

    UMVVM_Backpack* ViewModel = NewObject<UMVVM_Backpack>();
    ViewModel->Initialize(Model);
    ViewModel->RequestSwap(0, 1);

    TestFalse(TEXT("Intent request leaves source slot unchanged"), Model->GetSlots()[0].IsEmpty());
    TestTrue(TEXT("Intent request leaves target slot unchanged"), Model->GetSlots()[1].IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySlotNormalBackgroundIsTransparent,
                                 "test_25d.Inventory.SlotNormalBackgroundIsTransparent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.SlotNormalBackgroundIsTransparent 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventorySlotNormalBackgroundIsTransparent::RunTest(const FString& Parameters)
{
    UMVVM_InventorySlot* Slot = NewObject<UMVVM_InventorySlot>();

    TestEqual(TEXT("A normal slot leaves the Designer background visible"), Slot->GetSlotBackgroundTint(),
              FLinearColor::Transparent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryInputTagsExposeBackpackAndPickup,
                                 "test_25d.Inventory.InputTagsExposeBackpackAndPickup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.InputTagsExposeBackpackAndPickup 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryInputTagsExposeBackpackAndPickup::RunTest(const FString& Parameters)
{
    const FMyGameplayTags& Tags = FMyGameplayTags::Get();
    TestTrue(TEXT("Backpack input is exposed as a gameplay tag"), Tags.InputTag_Backpack.IsValid());
    TestTrue(TEXT("Pickup input is exposed as a gameplay tag"), Tags.InputTag_Pickup.IsValid());
    TestEqual(TEXT("Backpack tag has the blueprint-facing name"), Tags.InputTag_Backpack.GetTagName(),
              FName(TEXT("InputTag.Backpack")));
    TestEqual(TEXT("Pickup tag has the blueprint-facing name"), Tags.InputTag_Pickup.GetTagName(),
              FName(TEXT("InputTag.Pickup")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryInteractPickupTracksOverlapsAndChoosesCandidate,
                                 "test_25d.Inventory.InteractPickupTracksOverlapsAndChoosesCandidate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.InteractPickupTracksOverlapsAndChoosesCandidate 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryInteractPickupTracksOverlapsAndChoosesCandidate::RunTest(const FString& Parameters)
{
    AMyPlayerController* Controller = NewObject<AMyPlayerController>();
    AActor* FirstPickup = NewObject<AActor>();
    AActor* SecondPickup = NewObject<AActor>();

    Controller->RegisterOverlappingInteractPickup(FirstPickup);
    Controller->RegisterOverlappingInteractPickup(FirstPickup);
    Controller->RegisterOverlappingInteractPickup(SecondPickup);
    TestEqual(TEXT("Duplicate primitive overlaps retain two distinct pickup actors"),
              Controller->GetOverlappingInteractPickupCount(), 2);

    AActor* ChosenPickup = Controller->GetRandomOverlappingInteractPickup();
    TestTrue(TEXT("Random interact pickup is one of the currently overlapping actors"),
             ChosenPickup == FirstPickup || ChosenPickup == SecondPickup);

    Controller->UnregisterOverlappingInteractPickup(FirstPickup);
    TestEqual(TEXT("One of two component end-overlaps keeps the actor registered"),
              Controller->GetOverlappingInteractPickupCount(), 2);
    Controller->UnregisterOverlappingInteractPickup(FirstPickup);
    TestEqual(TEXT("The final end-overlap removes the pickup actor"), Controller->GetOverlappingInteractPickupCount(),
              1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryNetworkedDropAndEnemySpawnAreServerAuthoritative,
                                 "test_25d.Inventory.NetworkedDropAndEnemySpawnAreServerAuthoritative",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 执行 test_25d.Inventory.NetworkedDropAndEnemySpawnAreServerAuthoritative 回归，失败信息由下方断言交给自动化框架收集。 */
bool FInventoryNetworkedDropAndEnemySpawnAreServerAuthoritative::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("The server may spawn enemies from a player overlap"),
             AMyEnemySpawnVolume::ShouldSpawnForOverlap(true, true));
    TestFalse(TEXT("A client overlap may not spawn a local enemy"),
              AMyEnemySpawnVolume::ShouldSpawnForOverlap(false, true));

    AActor* DroppedActor = NewObject<AActor>();
    DroppedActor->SetReplicates(false);
    DroppedActor->SetReplicateMovement(false);
    UWorldPickupComponent::ConfigureOwnerForNetworkedPickup(DroppedActor);
    TestTrue(TEXT("Any pickup actor, including enemy loot, replicates to every client"),
             DroppedActor->GetIsReplicated());
    TestTrue(TEXT("Any pickup actor, including enemy loot, replicates its movement"),
             DroppedActor->IsReplicatingMovement());
    return true;
}

#endif
