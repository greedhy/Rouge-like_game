/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Equipment.Random.NoviceAssets
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Equipment/EquipmentStats.h"
#include "Equipment/EquipmentGameplayEffect.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoviceEquipmentAssetsTest, "test_25d.Equipment.Random.NoviceAssets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Equipment.Random.NoviceAssets 回归，失败信息由下方断言交给自动化框架收集。 */
bool FNoviceEquipmentAssetsTest::RunTest(const FString& Parameters)
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

    const auto Initial = State->ExportBackpackSnapshot();
    const TCHAR* Names[] = {TEXT("NoviceIronSword"), TEXT("NoviceBattleAxe"),   TEXT("NoviceArcaneStaff"),
                            TEXT("NoviceIronMail"),  TEXT("NoviceScholarRobe"), TEXT("NoviceIronBoots"),
                            TEXT("NoviceGuardHelm"), TEXT("NoviceMageHat"),     TEXT("NovicePrismStone"),
                            TEXT("NoviceJadeRing")};
    for (const auto* Name : Names)
    {
        const FPrimaryAssetId ItemId(TEXT("BackpackItem"), FName(Name));
        const auto* Item = Catalog->FindDefinition(ItemId);
        if (!TestNotNull(Name, Item))
            return false;
        TestNotNull(TEXT("Icon loaded"), Item->Icon.Get());
        TArray<FGameplayTag> Allowed;
        Item->AllowedEquipmentSlots.GetGameplayTagArray(Allowed);
        if (!TestTrue(TEXT("Allowed slot"), !Allowed.IsEmpty()))
            return false;
        for (int32 Seed = 0; Seed < 16; ++Seed)
        {
            FEquipmentRollData Roll;
            FString Error;
            if (!TestTrue(TEXT("Generate authored profile"),
                          FEquipmentGenerator::Generate(*Catalog, *Item, 1, Seed, Roll, Error)))
                return false;
            const auto Totals = FEquipmentGenerator::GetTotals(Roll);
            TArray<float> Before;
            for (const auto& Stat : EquipmentStats::All())
                Before.Add(ASC->GetNumericAttribute(Stat.Attribute));
            const FGuid Guid = FGuid::NewGuid();
            if (!TestTrue(TEXT("Add generated item"), Bag->TryAddExistingItem(ItemId, Guid, Roll)))
                return false;
            if (!TestTrue(TEXT("Equip authored slot"),
                          Equipment->TryEquip(0, Allowed[0], Guid, Bag->GetRevision(), Equipment->Revision)))
                return false;
            TArray<float> Equipped;
            for (int32 I = 0; I < Before.Num(); ++I)
            {
                const auto& Stat = EquipmentStats::All()[I];
                const float Value = ASC->GetNumericAttribute(Stat.Attribute);
                Equipped.Add(Value);
                TestTrue(TEXT("Direct bonus present; primary stats may add derived bonuses"),
                         Value + 0.01f >= Before[I] + Totals.FindRef(Stat.Tag));
            }
            const auto Saved = State->ExportBackpackSnapshot();
            TestTrue(TEXT("Restore"), State->ImportBackpackSnapshot(Saved));
            for (int32 I = 0; I < Equipped.Num(); ++I)
                TestTrue(TEXT("Restore not stacked"),
                         FMath::IsNearlyEqual(Equipped[I], ASC->GetNumericAttribute(EquipmentStats::All()[I].Attribute),
                                              0.01f));
            TestTrue(TEXT("Unequip"), Equipment->TryUnequip(Allowed[0], Guid, Bag->GetRevision(), Equipment->Revision));
            for (int32 I = 0; I < Before.Num(); ++I)
                TestTrue(TEXT("All stats restored"),
                         FMath::IsNearlyEqual(Before[I], ASC->GetNumericAttribute(EquipmentStats::All()[I].Attribute),
                                              0.01f));
            TestTrue(TEXT("Reset fixture"), State->ImportBackpackSnapshot(Initial));
        }
    }
    return true;
}
#endif
