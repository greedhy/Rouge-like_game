/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Equipment.Random.Lifecycle
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/BackpackPersistence.h"
#include "Kismet/GameplayStatics.h"
#include "Backpack/WorldPickupComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRandomEquipmentLifecycleTest, "test_25d.Equipment.Random.Lifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Equipment.Random.Lifecycle 回归，失败信息由下方断言交给自动化框架收集。 */
bool FRandomEquipmentLifecycleTest::RunTest(const FString& Parameters)
{
    struct FWorldScope
    {
        UWorld* W;
        FWorldScope()
        {
            W = UWorld::CreateWorld(EWorldType::Game, false,
                                    MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        }
        ~FWorldScope()
        {
            GEngine->DestroyWorldContext(W);
            W->DestroyWorld(false);
        }
    } Scope;
    auto* State = Scope.W->SpawnActor<AMyPlayerState>();
    auto* Pawn = Scope.W->SpawnActor<APawn>();
    if (!TestNotNull(TEXT("State"), State) || !TestNotNull(TEXT("Pawn"), Pawn))
        return false;
    Pawn->SetPlayerState(State);
    auto* Root = NewObject<USceneComponent>(Pawn);
    Pawn->AddInstanceComponent(Root);
    Pawn->SetRootComponent(Root);
    Root->RegisterComponent();
    auto* ASC = State->GetAbilitySystemComponent();
    auto* Bag = State->GetBackpackComponent();
    auto* Equipment = State->GetEquipmentComponent();
    ASC->InitAbilityActorInfo(State, Pawn);
    ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
    UClass* Secondary = LoadClass<UGameplayEffect>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_SecondaryAttributes.GE_SecondaryAttributes_C"));
    if (!TestNotNull(TEXT("Real derived-attribute GE"), Secondary))
        return false;
    auto Context = ASC->MakeEffectContext();
    Context.AddSourceObject(Pawn);
    auto Spec = ASC->MakeOutgoingSpec(Secondary, 1.f, Context);
    if (!TestTrue(TEXT("Initialization spec"), Spec.IsValid()))
        return false;
    if (!TestTrue(TEXT("Base override active"), ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data).IsValid()))
        return false;
    auto* Catalog = LoadObject<UBackpackItemDefinitions>(
        nullptr, TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
    if (!TestNotNull(TEXT("Authored catalog"), Catalog))
        return false;
    const FPrimaryAssetId Id(TEXT("BackpackItem"), TEXT("RolledSapphireRing"));
    const auto* Def = Catalog->FindDefinition(Id);
    if (!TestNotNull(TEXT("Authored random ring"), Def))
        return false;
    Bag->SetItemDefinitions(Catalog);
    Bag->Initialize();
    Bag->SetCapacity(3);
    const auto Left = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Left"));
    const auto Right = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Right"));
    Equipment->AvailableSlots.AddTag(Left);
    Equipment->AvailableSlots.AddTag(Right);
    Equipment->ReconcileEffects();
    FEquipmentRollData A, B;
    FString Error;
    if (!TestTrue(TEXT("Generate A"), FEquipmentGenerator::Generate(*Catalog, *Def, 5, 101, A, Error)) ||
        !TestTrue(TEXT("Generate B"), FEquipmentGenerator::Generate(*Catalog, *Def, 5, 202, B, Error)))
        return false;
    // Keep this lifecycle fixture independent of designer affix-pool changes.
    for (auto* Roll : {&A, &B})
    {
        Roll->Affixes.Empty();
        FEquipmentRolledAffix Affix;
        Affix.AffixId = TEXT("LifecycleMana");
        Affix.DisplayName = FText::FromString(TEXT("Mana"));
        Affix.Tier = 1;
        FEquipmentRolledStat Stat;
        Stat.StatTag = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Stat.MaxMana"));
        Stat.Value = 5;
        Affix.Stats.Add(Stat);
        Roll->Affixes.Add(Affix);
    }
    const FGuid GuidA = FGuid::NewGuid(), GuidB = FGuid::NewGuid();
    if (!TestTrue(TEXT("Add A"), Bag->TryAddExistingItem(Id, GuidA, A)) ||
        !TestTrue(TEXT("Add B"), Bag->TryAddExistingItem(Id, GuidB, B)))
        return false;
    auto* ClientModel = NewObject<UInventoryModel>();
    ClientModel->ApplyReplicatedSlots(Bag->GetReplicatedSlots().Items, 3);
    if (!TestEqual(TEXT("Replicated model capacity"), ClientModel->GetSlots().Num(), 3))
        return false;
    TestTrue(TEXT("Replication preserves full saved roll"), ClientModel->GetSlots()[0].RollData == A);
    const TArray<FGameplayAttribute> Attributes = {UMyAttributeSet::GetArmorAttribute(),
                                                   UMyAttributeSet::GetMaxHealthAttribute(),
                                                   UMyAttributeSet::GetMaxManaAttribute()};
    const TArray<FGameplayTag> Tags = {FGameplayTag::RequestGameplayTag(TEXT("Equipment.Stat.Armor")),
                                       FGameplayTag::RequestGameplayTag(TEXT("Equipment.Stat.MaxHealth")),
                                       FGameplayTag::RequestGameplayTag(TEXT("Equipment.Stat.MaxMana"))};
    TArray<float> Base;
    for (auto Attribute : Attributes)
        Base.Add(ASC->GetNumericAttribute(Attribute));
    const auto TA = FEquipmentGenerator::GetTotals(A), TB = FEquipmentGenerator::GetTotals(B);
    auto Check = [&](const TCHAR* Label, bool HasA, bool HasB)
    {
        for (int32 I = 0; I < Attributes.Num(); ++I)
            TestTrue(Label, FMath::IsNearlyEqual(
                                ASC->GetNumericAttribute(Attributes[I]),
                                Base[I] + (HasA ? TA.FindRef(Tags[I]) : 0) + (HasB ? TB.FindRef(Tags[I]) : 0), 0.01f));
    };
    Check(TEXT("Bag grants no effect"), false, false);
    auto* Profile = Catalog->GenerationProfiles ? Catalog->GenerationProfiles->FindRow<FEquipmentGenerationProfileRow>(
                                                      Def->GenerationProfile, TEXT("Lifecycle"))
                                                : nullptr;
    if (!TestNotNull(TEXT("Generation profile"), Profile))
        return false;
    // Temporarily remove generation ranges: equipping must consume the saved payload, never regenerate it.
    const auto OriginalRanges = Profile->BaseStats;
    Profile->BaseStats.Reset();
    const bool EquippedA = Equipment->TryEquip(0, Left, GuidA, Bag->GetRevision(), Equipment->Revision);
    Profile->BaseStats = OriginalRanges;
    if (!TestTrue(TEXT("Saved equipment survives designer range edit"), EquippedA))
        return false;
    if (!TestTrue(TEXT("Equip B"), Equipment->TryEquip(1, Right, GuidB, Bag->GetRevision(), Equipment->Revision)))
        return false;
    Check(TEXT("Both saved bonuses survive base override"), true, true);
    auto Saved = State->ExportBackpackSnapshot();
    if (!TestEqual(TEXT("Two equipment records"), Saved.Equipment.Num(), 2))
        return false;
    TestTrue(TEXT("Saved equipped payload"), Saved.Equipment[0].RollData == A);
    auto* SaveGame = NewObject<UBackpackPersistenceSaveGame>();
    SaveGame->Snapshot = Saved;
    TArray<uint8> SaveBytes;
    if (!TestTrue(TEXT("Serialize actual SaveGame"), UGameplayStatics::SaveGameToMemory(SaveGame, SaveBytes)))
        return false;
    auto* Loaded = Cast<UBackpackPersistenceSaveGame>(UGameplayStatics::LoadGameFromMemory(SaveBytes));
    if (!TestNotNull(TEXT("Deserialize actual SaveGame"), Loaded))
        return false;
    if (!TestEqual(TEXT("Serialized equipped record count"), Loaded->Snapshot.Equipment.Num(), 2))
        return false;
    TestTrue(TEXT("Serialized A preserves nested stats and affixes"), Loaded->Snapshot.Equipment[0].RollData == A);
    TestTrue(TEXT("Serialized B preserves nested stats and affixes"), Loaded->Snapshot.Equipment[1].RollData == B);
    TestEqual(TEXT("Serialized A preserves identity"), Loaded->Snapshot.Equipment[0].ItemInstanceId, GuidA);
    TestTrue(TEXT("Restore deserialized equipment"), State->ImportBackpackSnapshot(Loaded->Snapshot));
    Check(TEXT("Serialized restore applies exactly once"), true, true);
    TestTrue(TEXT("Restore saved equipment"), State->ImportBackpackSnapshot(Saved));
    Check(TEXT("Restore applies exactly once"), true, true);
    TestTrue(TEXT("Independent removal A"),
             Equipment->TryUnequip(Left, GuidA, Bag->GetRevision(), Equipment->Revision));
    Check(TEXT("B survives A removal"), false, true);
    TestTrue(TEXT("Independent removal B"),
             Equipment->TryUnequip(Right, GuidB, Bag->GetRevision(), Equipment->Revision));
    Check(TEXT("Base restored"), false, false);
    const int32 Index = Bag->GetInventoryModel()->GetSlots().IndexOfByPredicate(
        [&](const FInventorySlot& S)
        {
            return S.ItemInstanceId == GuidA;
        });
    if (!TestTrue(TEXT("A returned to bag"), Index != INDEX_NONE))
        return false;
    if (!TestTrue(TEXT("Drop saved item"), Bag->DropWholeStack(Index, FVector::ZeroVector)))
        return false;
    UWorldPickupComponent* Pickup = nullptr;
    for (TActorIterator<AActor> It(Scope.W); It; ++It)
    {
        auto* Candidate = It->FindComponentByClass<UWorldPickupComponent>();
        if (Candidate && Candidate->ItemInstanceId == GuidA)
        {
            Pickup = Candidate;
            break;
        }
    }
    if (!TestNotNull(TEXT("Dropped pickup"), Pickup))
        return false;
    TestTrue(TEXT("Drop preserves roll"), Pickup->RollData == A);
    Check(TEXT("Dropped item grants nothing"), false, false);
    if (!TestTrue(TEXT("Repickup"), Pickup->TryPickup(State)))
        return false;
    const auto* Returned = Bag->GetInventoryModel()->GetSlots().FindByPredicate(
        [&](const FInventorySlot& S)
        {
            return S.ItemInstanceId == GuidA;
        });
    if (!TestNotNull(TEXT("Original identity repicked"), Returned))
        return false;
    TestTrue(TEXT("Repickup preserves exact payload"), Returned->RollData == A);
    Check(TEXT("Repicked item grants nothing"), false, false);
    auto Invalid = A;
    Invalid.GenerationVersion = 999;
    TestFalse(TEXT("Malformed instance rejected"), Bag->TryAddExistingItem(Id, FGuid::NewGuid(), Invalid));
    auto BadSave = State->ExportBackpackSnapshot();
    for (auto& S : BadSave.Slots)
        if (S.ItemInstanceId == GuidA)
            S.RollData = Invalid;
    TestFalse(TEXT("Malformed snapshot rejected"), State->ImportBackpackSnapshot(BadSave));
    return true;
}
#endif
