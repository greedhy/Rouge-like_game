/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Stander.WeaponEquipmentTransaction
 * - test_25d.Stander.WeaponVisualsBeforeActorInfo
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/WeaponSetConfig.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Player/MyPlayerState.h"
#include "Character/StanderPlayer.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Ability/StanderAbility.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "Game/LoadScreenSaveGame.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "Tests/EquipmentTestEffects.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(WeaponEquipmentTestSlot, "Equipment.Test.Weapon");
UE_DEFINE_GAMEPLAY_TAG_STATIC(WeaponEquipmentRingSlot, "Equipment.Test.WeaponRing");
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanderWeaponVisualsBeforeActorInfoTest,
                                 "test_25d.Stander.WeaponVisualsBeforeActorInfo",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 重现初次恢复快照早于 ASC 绑定：表现刷新应跳过，物品数据仍可恢复且不能隐式初始化 ASC。 */
bool FStanderWeaponVisualsBeforeActorInfoTest::RunTest(const FString&)
{
    auto* State = NewObject<AMyPlayerState>();
    auto* Equipment = State->GetEquipmentComponent();
    auto* ASC = State->GetAbilitySystemComponent();
    TestFalse(TEXT("Fixture ASC has not allocated actor info"), ASC->AbilityActorInfo.IsValid());
    TestNull(TEXT("Fixture has no current pawn"), State->GetPawn());

    // 直接刷新和 RestoreCombined -> OnRep_State 两条入口都可能早于 Pawn/ASC 初始化。
    Equipment->RefreshWeaponVisuals();
    FBackpackSnapshot Snapshot;
    Snapshot.Capacity = 2;
    Snapshot.Revision = 7;
    Snapshot.Slots.SetNum(2);
    TestTrue(TEXT("Snapshot restoration is allowed before actor info"), State->ImportBackpackSnapshot(Snapshot));
    Equipment->RefreshWeaponVisuals();

    const auto Restored = State->ExportBackpackSnapshot();
    TestEqual(TEXT("Deferred visuals preserve restored capacity"), Restored.Capacity, 2);
    TestEqual(TEXT("Deferred visuals preserve restored revision"), Restored.Revision, 7);
    TestFalse(TEXT("Visual refresh does not initialize ASC as a side effect"), ASC->AbilityActorInfo.IsValid());
    TestEqual(TEXT("No abilities granted before avatar initialization"), ASC->GetActivatableAbilities().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanderWeaponEquipmentTest, "test_25d.Stander.WeaponEquipmentTransaction",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Stander.WeaponEquipmentTransaction 回归，失败信息由下方断言交给自动化框架收集。 */
bool FStanderWeaponEquipmentTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* State = World->SpawnActor<AMyPlayerState>();
    auto* Pawn = World->SpawnActor<AStanderPlayer>();
    Pawn->SetPlayerState(State);
    auto* E = State->GetEquipmentComponent();
    auto* B = State->GetBackpackComponent();
    auto* A = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    A->InitAbilityActorInfo(State, Pawn);
    A->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
    A->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100);
    A->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100);
    E->AvailableSlots.AddTag(WeaponEquipmentTestSlot);
    E->AvailableSlots.AddTag(WeaponEquipmentRingSlot);
    auto* Catalog = NewObject<UBackpackItemDefinitions>(State);
    auto* Set = NewObject<UWeaponSetConfig>(State);
    Set->Family = TEXT("TestWeapon");
    Set->AnimClass = UAnimInstance::StaticClass();
    Set->MainMesh = NewObject<USkeletalMesh>(State);
    const auto Input = FGameplayTag::RequestGameplayTag(TEXT("InputTag.LMB"));
    FWeaponAbilityEntry Entry;
    Entry.Ability = UStanderLiftAbility::StaticClass();
    Entry.InputTag = Input;
    Set->Abilities.Add(Entry);
    Entry.Ability = UStanderShieldBashAbility::StaticClass();
    Entry.InputTag = FGameplayTag();
    Set->Abilities.Add(Entry);
    auto& Def = Catalog->Items.AddDefaulted_GetRef();
    Def.ItemDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("WeaponTest")));
    const auto Id = Def.ItemDefinitionId;
    Def.bEquippable = true;
    Def.bStackable = false;
    Def.MaxStack = 1;
    Def.AllowedEquipmentSlots.AddTag(WeaponEquipmentTestSlot);
    Def.EquipEffect = UEquipmentTestArmor::StaticClass();
    Def.WeaponSet = Set;
    B->SetItemDefinitions(Catalog);
    B->Initialize();
    B->SetCapacity(2);
    E->ReconcileEffects();
    FGameplayAbilitySpec Existing(UStanderComboAbility::StaticClass(), 1);
    Existing.DynamicAbilityTags.AddTag(Input);
    const auto Original = A->GiveAbility(Existing);
    auto Cooldown = A->MakeOutgoingSpec(USwordTimedEffect::StaticClass(), 1, A->MakeEffectContext());
    Cooldown.Data->SetDuration(15.f, true);
    const auto CooldownHandle = A->ApplyGameplayEffectSpecToSelf(*Cooldown.Data);
    B->TryAddItem(Id, 1);
    auto Equip = [&]()
    {
        return E->TryEquip(0, WeaponEquipmentTestSlot, B->GetInventoryModel()->GetSlots()[0].ItemInstanceId,
                           B->GetRevision(), E->Revision);
    };
    TestTrue(TEXT("Weapon set validates"), Set->Validate());
    TestTrue(TEXT("Equip transaction succeeds"), Equip());
    TestEqual(TEXT("All actions including unslotted action granted"), A->GetActivatableAbilities().Num(), 3);
    auto* OriginalSpec = A->FindAbilitySpecFromHandle(Original);
    TestTrue(TEXT("Nonweapon ability survives and original slot is saved"),
             OriginalSpec && E->GetSavedInputSlot(*OriginalSpec) == Input && !A->AbilityHasAnySlot(*OriginalSpec));
    TestFalse(TEXT("Nonweapon RPC cannot replace weapon-owned input"), E->CanAssignWeaponSlot(Original, Input));
    const uint32 WeaponEpoch = E->GetWeaponGeneration();
    auto Ring = Catalog->Items[0];
    Ring.ItemDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("WeaponTestRing")));
    Ring.WeaponSet = nullptr;
    Ring.AllowedEquipmentSlots.Reset();
    Ring.AllowedEquipmentSlots.AddTag(WeaponEquipmentRingSlot);
    Catalog->Items.Add(Ring);
    B->TryAddItem(Ring.ItemDefinitionId, 1);
    TestTrue(TEXT("Unrelated ring equips normally"),
             E->TryEquip(0, WeaponEquipmentRingSlot, B->GetInventoryModel()->GetSlots()[0].ItemInstanceId,
                         B->GetRevision(), E->Revision));
    TestEqual(TEXT("Ring equip cannot invalidate an airborne weapon projectile"), E->GetWeaponGeneration(),
              WeaponEpoch);
    TestTrue(TEXT("Repeated initialization succeeds"), E->ReconcileEffects());
    TestEqual(TEXT("Idempotent reconcile preserves weapon generation"), E->GetWeaponGeneration(), WeaponEpoch);
    TestEqual(TEXT("Repeated initialization does not duplicate grants"), A->GetActivatableAbilities().Num(), 3);
    const auto Snapshot = State->ExportBackpackSnapshot();
    TestTrue(TEXT("Combined snapshot restore rebuilds equipment without duplicate abilities"),
             State->ImportBackpackSnapshot(Snapshot));
    TestEqual(TEXT("Restore grants exactly once"), A->GetActivatableAbilities().Num(), 3);
    TestTrue(TEXT("Cooldown survives restore and weapon reconciliation"),
             A->GetActiveGameplayEffect(CooldownHandle) != nullptr);
    // A valid data asset can still conflict with independently owned live skills.
    auto* ConflictSet = DuplicateObject<UWeaponSetConfig>(Set, State);
    ConflictSet->Family = TEXT("ConflictWeapon");
    ConflictSet->Abilities[0].Ability = UStanderComboAbility::StaticClass();
    TestTrue(TEXT("Conflict set passes asset validation"), ConflictSet->Validate());
    auto ConflictDef = Catalog->Items[0];
    ConflictDef.ItemDefinitionId =
        FPrimaryAssetId(FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("ConflictWeapon")));
    ConflictDef.WeaponSet = ConflictSet;
    Catalog->Items.Add(ConflictDef);
    const auto BeforeRejectedRestore = State->ExportBackpackSnapshot();
    auto RejectedRestore = BeforeRejectedRestore;
    for (auto& Record : RejectedRestore.Equipment)
        if (Record.SlotTag == WeaponEquipmentTestSlot)
            Record.ItemDefinitionId = ConflictDef.ItemDefinitionId;
    RejectedRestore.EquipmentRevision += 10;
    RejectedRestore.Revision += 10;
    // Also change inventory in the proposed snapshot to catch a half-applied restore.
    RejectedRestore.Slots[0].ItemDefinitionId = Ring.ItemDefinitionId;
    RejectedRestore.Slots[0].Quantity = 1;
    RejectedRestore.Slots[0].ItemInstanceId = FGuid::NewGuid();
    TArray<FGameplayAbilitySpecHandle> BeforeHandles;
    for (const auto& Spec : A->GetActivatableAbilities())
        BeforeHandles.Add(Spec.Handle);
    int32 RestoreNotifications = 0;
    const auto NotificationHandle = B->InventoryModel->OnInventoryUpdated.AddLambda(
        [&](const TArray<FInventorySlot>&)
        {
            ++RestoreNotifications;
        });
    TestFalse(TEXT("Live restore rejects conflicting ability before publishing state"),
              State->ImportBackpackSnapshot(RejectedRestore));
    B->InventoryModel->OnInventoryUpdated.Remove(NotificationHandle);
    const auto AfterRejectedRestore = State->ExportBackpackSnapshot();
    TestEqual(TEXT("Rejected restore does not publish inventory"), RestoreNotifications, 0);
    TestEqual(TEXT("Rejected restore preserves equipment revision"), AfterRejectedRestore.EquipmentRevision,
              BeforeRejectedRestore.EquipmentRevision);
    TestEqual(TEXT("Rejected restore preserves bag revision"), AfterRejectedRestore.Revision,
              BeforeRejectedRestore.Revision);
    TestEqual(TEXT("Rejected restore preserves original equipped definition"),
              AfterRejectedRestore.Equipment[0].ItemDefinitionId, BeforeRejectedRestore.Equipment[0].ItemDefinitionId);
    TestEqual(TEXT("Rejected restore preserves equipped instance"), AfterRejectedRestore.Equipment[0].ItemInstanceId,
              BeforeRejectedRestore.Equipment[0].ItemInstanceId);
    TestEqual(TEXT("Rejected restore leaves inventory untouched"), AfterRejectedRestore.Slots[0].ItemDefinitionId,
              BeforeRejectedRestore.Slots[0].ItemDefinitionId);
    TestEqual(TEXT("Rejected restore preserves ability count"), A->GetActivatableAbilities().Num(),
              BeforeHandles.Num());
    for (auto Handle : BeforeHandles)
        TestNotNull(TEXT("Rejected restore retains every original skill handle"), A->FindAbilitySpecFromHandle(Handle));
    TestEqual(TEXT("Rejected restore preserves projectile generation"), E->GetWeaponGeneration(), WeaponEpoch);
    // Invalid configuration cannot remove the installed weapon or consume the candidate item.
    auto* Bad = NewObject<UWeaponSetConfig>(State);
    auto BadDef = Catalog->Items[0];
    BadDef.ItemDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("BadWeapon")));
    BadDef.WeaponSet = Bad;
    Catalog->Items.Add(BadDef);
    B->TryAddItem(BadDef.ItemDefinitionId, 1);
    TestFalse(TEXT("Invalid replacement rejected before old abilities removed"), Equip());
    TestEqual(TEXT("Old group retained"), A->GetActivatableAbilities().Num(), 3);
    const auto Instance = E->Slots
                              .FindByPredicate(
                                  [](const FEquipmentSlot& S)
                                  {
                                      return S.SlotTag == WeaponEquipmentTestSlot;
                                  })
                              ->ItemInstanceId;
    TestTrue(TEXT("Unequip succeeds"), E->TryUnequip(WeaponEquipmentTestSlot, Instance, B->GetRevision(), E->Revision));
    TestTrue(TEXT("Weapon removal invalidates old projectiles"), E->GetWeaponGeneration() != WeaponEpoch);
    TestTrue(TEXT("Unequip preserves cooldown effect"), A->GetActiveGameplayEffect(CooldownHandle) != nullptr);
    TestEqual(TEXT("Only weapon-owned grants removed"), A->GetActivatableAbilities().Num(), 1);
    OriginalSpec = A->FindAbilitySpecFromHandle(Original);
    TestTrue(TEXT("Nonweapon input restored"), OriginalSpec && A->AbilityHasSlot(*OriginalSpec, Input));
    auto* LegacySave = NewObject<ULoadScreenSaveGame>();
    FSavedAbility LegacyWeapon;
    LegacyWeapon.GameplayAbility = UStanderWeaponActionAbility::StaticClass();
    LegacyWeapon.AbilityType = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Type.Offensive"));
    LegacySave->SavedAbilities.Add(LegacyWeapon);
    const int32 BeforeLegacyLoad = A->GetActivatableAbilities().Num();
    A->AddCharacterAbilitiesFromSaveData(LegacySave);
    TestEqual(TEXT("Weapon skills cannot leak through ordinary saved skill grants"), A->GetActivatableAbilities().Num(),
              BeforeLegacyLoad);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
