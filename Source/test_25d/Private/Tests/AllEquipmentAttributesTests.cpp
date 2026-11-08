/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Equipment.Random.AllAttributes
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllEquipmentAttributesTest, "test_25d.Equipment.Random.AllAttributes",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Equipment.Random.AllAttributes 回归，失败信息由下方断言交给自动化框架收集。 */
bool FAllEquipmentAttributesTest::RunTest(const FString& Parameters)
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

    const auto* Template = Cast<UEquipmentGameplayEffect>(Def->EquipEffect.GetDefaultObject());
    if (!TestNotNull(TEXT("Template"), Template))
        return false;
    TestTrue(TEXT("Authored full template safe"), Template->IsSafeEquipmentEffect(1, true));
    TestEqual(TEXT("All supported modifiers migrated"), Template->Modifiers.Num(), EquipmentStats::All().Num());
    auto* Subset = NewObject<URandomEquipmentGameplayEffect>();
    Subset->Modifiers.SetNum(1);
    TestTrue(TEXT("One modifier is valid"), Subset->IsSafeEquipmentEffect(1, true));
    const auto Copy = Subset->Modifiers[0];
    Subset->Modifiers.Add(Copy);
    TestFalse(TEXT("Duplicate modifier rejected"), Subset->IsSafeEquipmentEffect(1, true));
    const auto Initial = State->ExportBackpackSnapshot();
    FEquipmentRollData Roll;
    Roll.GenerationVersion = 1;
    Roll.ProfileId = Def->GenerationProfile;
    Roll.ItemLevel = 1;
    Roll.RarityId = TEXT("Rare");
    for (const auto& Stat : EquipmentStats::All())
    {
        const FString Name = Stat.Tag.ToString();
        const auto* Suffix = Catalog->Affixes->FindRow<FEquipmentAffixRow>(
            FName(*(TEXT("Of") + Stat.Attribute.GetName())), TEXT("AllAttributes"));
        TestNotNull(*Name, Suffix);
        if (Suffix)
        {
            TestFalse(TEXT("Suffix flag"), Suffix->bPrefix);
            TestTrue(TEXT("Suffix has tiers"), !Suffix->Tiers.IsEmpty());
        }
        TArray<float> Before;
        for (const auto& Entry : EquipmentStats::All())
            Before.Add(ASC->GetNumericAttribute(Entry.Attribute));
        Roll.BaseStats.Empty();
        FEquipmentRolledStat Value;
        Value.StatTag = Stat.Tag;
        Value.Value = 2;
        Roll.BaseStats.Add(Value);
        TestTrue(*Name, FEquipmentGenerator::ValidateRoll(Roll));
        TestTrue(TEXT("Localized description"),
                 FEquipmentGenerator::MakeDescription(Roll).ToString().Contains(Stat.Label));
        const FGuid IdInstance = FGuid::NewGuid();
        if (!TestTrue(TEXT("Add"), Bag->TryAddExistingItem(Id, IdInstance, Roll)))
            return false;
        if (!TestTrue(TEXT("Equip"), Equipment->TryEquip(0, Left, IdInstance, Bag->GetRevision(), Equipment->Revision)))
            return false;
        const int32 StatIndex = EquipmentStats::All().IndexOfByPredicate(
            [&](const auto& Entry)
            {
                return Entry.Tag == Stat.Tag;
            });
        TestTrue(*Name, FMath::IsNearlyEqual(ASC->GetNumericAttribute(Stat.Attribute), Before[StatIndex] + 2, 0.01f));
        const auto Saved = State->ExportBackpackSnapshot();
        TestTrue(TEXT("Restore equipped"), State->ImportBackpackSnapshot(Saved));
        TestTrue(TEXT("No duplicate application"),
                 FMath::IsNearlyEqual(ASC->GetNumericAttribute(Stat.Attribute), Before[StatIndex] + 2, 0.01f));
        TestTrue(TEXT("Unequip"), Equipment->TryUnequip(Left, IdInstance, Bag->GetRevision(), Equipment->Revision));
        for (int32 I = 0; I < Before.Num(); ++I)
            TestTrue(
                TEXT("All attributes restored including derived stats"),
                FMath::IsNearlyEqual(ASC->GetNumericAttribute(EquipmentStats::All()[I].Attribute), Before[I], 0.01f));
        TestTrue(TEXT("Reset bag"), State->ImportBackpackSnapshot(Initial));
    }
    Roll.BaseStats.Empty();
    for (const auto& Stat : EquipmentStats::All())
    {
        FEquipmentRolledStat V;
        V.StatTag = Stat.Tag;
        V.Value = 1;
        Roll.BaseStats.Add(V);
    }
    TestTrue(TEXT("20 stat snapshot supported"), FEquipmentGenerator::ValidateRoll(Roll));
    return true;
}
#endif
