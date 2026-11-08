/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Roguelike.Economy.PersonalCredits
 * - test_25d.Roguelike.Economy.ChestPendingAndIdentity
 * - test_25d.Roguelike.Economy.AtomicPurchase
 * - test_25d.Roguelike.Economy.ReentrantDelegates
 * - test_25d.Roguelike.Economy.OwnerValidation
 * - test_25d.Roguelike.Economy.ClampedPersonalRest
 * - test_25d.Roguelike.Economy.NewRunReset
 * - test_25d.Roguelike.Economy.ClientAuthority
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Roguelike/DungeonRunEconomy.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace DungeonEconomyTests
{
    struct FFixture
    {
        explicit FFixture(EWorldType::Type Type = EWorldType::Game)
        {
            World = UWorld::CreateWorld(
                Type, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DungeonEconomyTest")));
            GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
            Economy = NewObject<UDungeonRunEconomy>(World);
            Catalog = NewObject<UBackpackItemDefinitions>(World);
            for (const FPrimaryAssetId& Id : {Health, Mana})
            {
                FBackpackItemDefEntry& Entry = Catalog->Items.AddDefaulted_GetRef();
                Entry.ItemDefinitionId = Id;
                Entry.bStackable = true;
                Entry.MaxStack = 10;
            }
            First = Player(101);
            Second = Player(202);
        }
        ~FFixture()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        AMyPlayerState* Player(int32 Id)
        {
            auto* State = World->SpawnActor<AMyPlayerState>();
            State->SetPlayerId(Id);
            State->GetBackpackComponent()->SetItemDefinitions(Catalog);
            State->GetBackpackComponent()->Initialize();
            auto* ASC = State->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(State, World->SpawnActor<ACharacter>());
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 200.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 50.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10.f);
            return State;
        }
        int32 Count(AMyPlayerState* State, const FPrimaryAssetId& Id) const
        {
            int32 Result = 0;
            for (const auto& Slot : State->GetBackpackComponent()->GetInventoryModel()->GetSlots())
                if (Slot.ItemDefinitionId == Id)
                    Result += Slot.Quantity;
            return Result;
        }
        UWorld* World = nullptr;
        UDungeonRunEconomy* Economy = nullptr;
        UBackpackItemDefinitions* Catalog = nullptr;
        AMyPlayerState* First = nullptr;
        AMyPlayerState* Second = nullptr;
        const FPrimaryAssetId Health{FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("HealthPotion"))};
        const FPrimaryAssetId Mana{FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("ManaPotion"))};
    };
} // namespace DungeonEconomyTests
using namespace DungeonEconomyTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEconomyCredits, "test_25d.Roguelike.Economy.PersonalCredits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Economy.PersonalCredits 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonEconomyCredits::RunTest(const FString&)
{
    FFixture F;
    TestFalse(TEXT("Uninitialized ledger rejects credits"), F.Economy->CreditClear(101, TEXT("R01"), 15));
    TestTrue(TEXT("Valid run starts"), F.Economy->Initialize(FGuid::NewGuid()));
    TestTrue(TEXT("First clear grants funds"), F.Economy->CreditClear(101, TEXT("R01"), 15));
    TestFalse(TEXT("Repeated clear never grants twice"), F.Economy->CreditClear(101, TEXT("R01"), 15));
    TestTrue(TEXT("Same room independently credits partner"), F.Economy->CreditClear(202, TEXT("R01"), 15));
    TestTrue(TEXT("Another room adds funds"), F.Economy->CreditClear(101, TEXT("R02"), 30));
    TestEqual(TEXT("First personal balance"), F.Economy->GetBalance(101), 45);
    TestEqual(TEXT("Partner personal balance"), F.Economy->GetBalance(202), 15);
    TestFalse(TEXT("Negative credit rejected"), F.Economy->CreditClear(101, TEXT("R03"), -1));
    TestFalse(TEXT("Empty room rejected"), F.Economy->CreditClear(101, NAME_None, 15));
    TestFalse(TEXT("Overflow rejected"), F.Economy->CreditClear(101, TEXT("R04"), MAX_int32));
    TestEqual(TEXT("Invalid credits preserve funds"), F.Economy->GetBalance(101), 45);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEconomyChest, "test_25d.Roguelike.Economy.ChestPendingAndIdentity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Economy.ChestPendingAndIdentity 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonEconomyChest::RunTest(const FString&)
{
    FFixture F;
    F.Economy->Initialize(FGuid::NewGuid());
    auto* Bag = F.First->GetBackpackComponent();
    Bag->SetCapacity(1);
    Bag->TryAddItem(F.Health, 9);
    TestFalse(TEXT("Unseen chest has no receipt"), F.Economy->HasChestRecord(101, TEXT("ChestA")));
    auto Result = F.Economy->ClaimChest(101, TEXT("ChestA"), Bag, F.Health, 2);
    TestTrue(TEXT("Partial chest has a receipt"), F.Economy->HasChestRecord(101, TEXT("ChestA")));
    TestTrue(TEXT("Partial claim succeeds"), Result.bSucceeded);
    TestEqual(TEXT("Only free stack space delivered"), Result.Delivered, 1);
    TestEqual(TEXT("Undelivered reward remains pending"), Result.Remaining, 1);
    Result = F.Economy->ClaimChest(101, TEXT("ChestA"), Bag, F.Health, 2);
    TestFalse(TEXT("Full bag cannot claim again"), Result.bSucceeded);
    TestEqual(TEXT("Pending reward is not lost"), F.Economy->GetChestRemaining(101, TEXT("ChestA")), 1);
    TestFalse(TEXT("Reward cannot change item"), F.Economy->ClaimChest(101, TEXT("ChestA"), Bag, F.Mana, 2).bSucceeded);
    TestFalse(TEXT("Reward cannot change total quantity"),
              F.Economy->ClaimChest(101, TEXT("ChestA"), Bag, F.Health, 3).bSucceeded);
    Bag->SetCapacity(2);
    Result = F.Economy->ClaimChest(101, TEXT("ChestA"), Bag, F.Health, 2);
    TestEqual(TEXT("Only remainder delivered after freeing room"), Result.Delivered, 1);
    TestEqual(TEXT("No pending remainder"), Result.Remaining, 0);
    TestFalse(TEXT("Completed reward is not repeated"),
              F.Economy->ClaimChest(101, TEXT("ChestA"), Bag, F.Health, 2).bSucceeded);
    TestEqual(TEXT("Chest granted exactly two total"), F.Count(F.First, F.Health), 11);
    TestTrue(TEXT("Partner claims their own chest"),
             F.Economy->ClaimChest(202, TEXT("ChestA"), F.Second->GetBackpackComponent(), F.Health, 2).bSucceeded);
    TestEqual(TEXT("Partner received full reward"), F.Count(F.Second, F.Health), 2);
    Bag->SetCapacity(0);
    Result = F.Economy->ClaimChest(101, TEXT("ChestB"), Bag, F.Mana, 2);
    TestFalse(TEXT("Initially full bag preserves unclaimed chest"), Result.bSucceeded);
    TestEqual(TEXT("Full reward pending from first failed attempt"), F.Economy->GetChestRemaining(101, TEXT("ChestB")),
              2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEconomyPurchase, "test_25d.Roguelike.Economy.AtomicPurchase",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Economy.AtomicPurchase 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonEconomyPurchase::RunTest(const FString&)
{
    FFixture F;
    F.Economy->Initialize(FGuid::NewGuid());
    auto* Bag = F.First->GetBackpackComponent();
    TestFalse(TEXT("Insufficient funds cannot buy"),
              F.Economy->Purchase(101, TEXT("OfferA"), Bag, F.Health, 30).bSucceeded);
    TestEqual(TEXT("Failed purchase grants nothing"), F.Count(F.First, F.Health), 0);
    F.Economy->CreditClear(101, TEXT("Elite"), 60);
    Bag->SetCapacity(0);
    TestFalse(TEXT("Full bag cannot buy"), F.Economy->Purchase(101, TEXT("OfferA"), Bag, F.Health, 30).bSucceeded);
    TestEqual(TEXT("Full bag does not charge"), F.Economy->GetBalance(101), 60);
    TestFalse(TEXT("Full bag does not consume offer"), F.Economy->IsPurchased(101, TEXT("OfferA")));
    Bag->SetCapacity(1);
    const auto Result = F.Economy->Purchase(101, TEXT("OfferA"), Bag, F.Health, 30);
    TestTrue(TEXT("Affordable purchase succeeds"), Result.bSucceeded);
    TestEqual(TEXT("Purchase delivers one"), Result.Delivered, 1);
    TestEqual(TEXT("Purchase charges exactly price"), F.Economy->GetBalance(101), 30);
    TestTrue(TEXT("Offer marked sold"), F.Economy->IsPurchased(101, TEXT("OfferA")));
    TestFalse(TEXT("Sold offer cannot be purchased twice"),
              F.Economy->Purchase(101, TEXT("OfferA"), Bag, F.Health, 30).bSucceeded);
    TestFalse(TEXT("Free purchase rejected"), F.Economy->Purchase(101, TEXT("OfferB"), Bag, F.Health, 0).bSucceeded);
    TestFalse(TEXT("Negative price rejected"), F.Economy->Purchase(101, TEXT("OfferB"), Bag, F.Health, -30).bSucceeded);
    TestEqual(TEXT("Rejected purchases do not change balance"), F.Economy->GetBalance(101), 30);
    TestEqual(TEXT("Rejected purchases do not deliver"), F.Count(F.First, F.Health), 1);
    F.Economy->CreditClear(202, TEXT("Elite"), 30);
    TestTrue(TEXT("Partner has separate offer stock"),
             F.Economy->Purchase(202, TEXT("OfferA"), F.Second->GetBackpackComponent(), F.Mana, 30).bSucceeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEconomyReentry, "test_25d.Roguelike.Economy.ReentrantDelegates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Economy.ReentrantDelegates 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonEconomyReentry::RunTest(const FString&)
{
    FFixture F;
    const FGuid Run = FGuid::NewGuid();
    F.Economy->Initialize(Run);
    F.Economy->CreditClear(101, TEXT("Clear"), 60);
    auto* Bag = F.First->GetBackpackComponent();
    bool bObserved = false;
    bool bNestedPurchase = true, bNestedChest = true, bNestedCredit = true, bNestedReset = true;
    const auto Handle = Bag->InventoryModel->OnInventoryUpdated.AddLambda(
        [&](const TArray<FInventorySlot>&)
        {
            if (bObserved)
                return;
            bObserved = true;
            bNestedPurchase = F.Economy->Purchase(101, TEXT("OfferB"), Bag, F.Health, 30).bSucceeded;
            bNestedChest = F.Economy->ClaimChest(101, TEXT("ChestA"), Bag, F.Health, 2).bSucceeded;
            bNestedCredit = F.Economy->CreditClear(101, TEXT("OtherClear"), 15);
            bNestedReset = F.Economy->Initialize(FGuid::NewGuid());
        });
    TestTrue(TEXT("Outer purchase succeeds"), F.Economy->Purchase(101, TEXT("OfferA"), Bag, F.Health, 30).bSucceeded);
    Bag->InventoryModel->OnInventoryUpdated.Remove(Handle);
    TestTrue(TEXT("Real inventory delegate observed"), bObserved);
    TestFalse(TEXT("Reentrant purchase rejected"), bNestedPurchase);
    TestFalse(TEXT("Reentrant chest rejected"), bNestedChest);
    TestFalse(TEXT("Reentrant credit rejected"), bNestedCredit);
    TestFalse(TEXT("Reentrant run reset rejected"), bNestedReset);
    TestEqual(TEXT("Only outer item delivered"), F.Count(F.First, F.Health), 1);
    TestEqual(TEXT("Only outer charge committed"), F.Economy->GetBalance(101), 30);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEconomyOwner, "test_25d.Roguelike.Economy.OwnerValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Economy.OwnerValidation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonEconomyOwner::RunTest(const FString&)
{
    FFixture F;
    F.Economy->Initialize(FGuid::NewGuid());
    F.Economy->CreditClear(101, TEXT("Clear"), 60);
    auto* OtherBag = F.Second->GetBackpackComponent();
    TestFalse(TEXT("Cannot claim to another player's bag"),
              F.Economy->ClaimChest(101, TEXT("Chest"), OtherBag, F.Health, 2).bSucceeded);
    TestFalse(TEXT("Cannot buy into another player's bag"),
              F.Economy->Purchase(101, TEXT("Offer"), OtherBag, F.Health, 30).bSucceeded);
    TestFalse(TEXT("Cannot rest another player's ASC"),
              F.Economy->Rest(101, TEXT("Rest"), F.Second->GetAbilitySystemComponent()).bSucceeded);
    TestEqual(TEXT("Wrong owner preserves balance"), F.Economy->GetBalance(101), 60);
    TestEqual(TEXT("Wrong owner grants nothing"), F.Count(F.Second, F.Health), 0);
    TestFalse(TEXT("Wrong owner does not consume rest"), F.Economy->HasRested(101, TEXT("Rest")));
    TestFalse(TEXT("Null bag rejected"), F.Economy->ClaimChest(101, TEXT("Chest"), nullptr, F.Health, 1).bSucceeded);
    TestFalse(TEXT("Null ASC rejected"), F.Economy->Rest(101, TEXT("Rest"), nullptr).bSucceeded);
    TestFalse(TEXT("No world cannot initialize"), NewObject<UDungeonRunEconomy>()->Initialize(FGuid::NewGuid()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEconomyRest, "test_25d.Roguelike.Economy.ClampedPersonalRest",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Economy.ClampedPersonalRest 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonEconomyRest::RunTest(const FString&)
{
    FFixture F;
    F.Economy->Initialize(FGuid::NewGuid());
    auto* ASC = F.First->GetAbilitySystemComponent();
    TestTrue(TEXT("First rest succeeds"), F.Economy->Rest(101, TEXT("Rest"), ASC).bSucceeded);
    TestEqual(TEXT("Health adds 30 percent of max"), ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
              110.f);
    TestEqual(TEXT("Mana adds 30 percent of max"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 40.f);
    TestTrue(TEXT("Rest receipt persisted"), F.Economy->HasRested(101, TEXT("Rest")));
    TestFalse(TEXT("Second rest rejected"), F.Economy->Rest(101, TEXT("Rest"), ASC).bSucceeded);
    TestEqual(TEXT("Repeat cannot heal twice"), ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 110.f);
    auto* Other = F.Second->GetAbilitySystemComponent();
    Other->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 190.f);
    Other->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 95.f);
    TestTrue(TEXT("Partner has independent rest"), F.Economy->Rest(202, TEXT("Rest"), Other).bSucceeded);
    TestEqual(TEXT("Health capped to max"), Other->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 200.f);
    TestEqual(TEXT("Mana capped to max"), Other->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEconomyReset, "test_25d.Roguelike.Economy.NewRunReset",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Economy.NewRunReset 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonEconomyReset::RunTest(const FString&)
{
    FFixture F;
    const FGuid Run = FGuid::NewGuid();
    F.Economy->Initialize(Run);
    auto* Bag = F.First->GetBackpackComponent();
    F.Economy->CreditClear(101, TEXT("Clear"), 60);
    F.Economy->Purchase(101, TEXT("Offer"), Bag, F.Health, 30);
    F.Economy->ClaimChest(101, TEXT("Chest"), Bag, F.Health, 2);
    F.Economy->Rest(101, TEXT("Rest"), F.First->GetAbilitySystemComponent());
    TestTrue(TEXT("Same run initialization is idempotent"), F.Economy->Initialize(Run));
    TestEqual(TEXT("Same run keeps balance"), F.Economy->GetBalance(101), 30);
    TestTrue(TEXT("Same run keeps stock receipt"), F.Economy->IsPurchased(101, TEXT("Offer")));
    TestFalse(TEXT("Invalid run rejected"), F.Economy->Initialize(FGuid()));
    TestEqual(TEXT("Invalid run does not erase state"), F.Economy->GetBalance(101), 30);
    TestTrue(TEXT("New run starts"), F.Economy->Initialize(FGuid::NewGuid()));
    TestEqual(TEXT("New run resets balance"), F.Economy->GetBalance(101), 0);
    TestFalse(TEXT("New run resets purchases"), F.Economy->IsPurchased(101, TEXT("Offer")));
    TestFalse(TEXT("New run resets rest"), F.Economy->HasRested(101, TEXT("Rest")));
    TestEqual(TEXT("Normal inventory persists across run reset"), F.Count(F.First, F.Health), 3);
    TestTrue(TEXT("New run can credit same room"), F.Economy->CreditClear(101, TEXT("Clear"), 15));
    TestTrue(TEXT("New run can claim same reward"),
             F.Economy->ClaimChest(101, TEXT("Chest"), Bag, F.Health, 2).bSucceeded);
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEconomyClient, "test_25d.Roguelike.Economy.ClientAuthority",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Economy.ClientAuthority 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonEconomyClient::RunTest(const FString&)
{
    FFixture F(EWorldType::PIE);
    F.Economy->Initialize(FGuid::NewGuid());
    F.Economy->CreditClear(101, TEXT("Clear"), 60);
    F.World->SetPlayInEditorInitialNetMode(NM_Client);
    TestEqual(TEXT("Fixture is a real client world"), F.World->GetNetMode(), NM_Client);
    TestFalse(TEXT("Client cannot initialize run"), F.Economy->Initialize(FGuid::NewGuid()));
    TestFalse(TEXT("Client cannot credit"), F.Economy->CreditClear(101, TEXT("Other"), 15));
    TestFalse(TEXT("Client cannot claim"),
              F.Economy->ClaimChest(101, TEXT("Chest"), F.First->GetBackpackComponent(), F.Health, 2).bSucceeded);
    TestFalse(TEXT("Client cannot buy"),
              F.Economy->Purchase(101, TEXT("Offer"), F.First->GetBackpackComponent(), F.Health, 30).bSucceeded);
    TestFalse(TEXT("Client cannot rest"),
              F.Economy->Rest(101, TEXT("Rest"), F.First->GetAbilitySystemComponent()).bSucceeded);
    TestEqual(TEXT("Denied client requests preserve balance"), F.Economy->GetBalance(101), 60);
    TestEqual(TEXT("Denied client requests preserve inventory"), F.Count(F.First, F.Health), 0);
    return true;
}
#endif
#endif
