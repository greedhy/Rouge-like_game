#include "Roguelike/DungeonRunEconomy.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayEffect.h"
#include "Player/MyPlayerState.h"

namespace
{
    FDungeonTransactionResult Rejected(const TCHAR* Error, int32 Remaining = 0)
    {
        FDungeonTransactionResult Result;
        Result.Error = Error;
        Result.Remaining = Remaining;
        return Result;
    }
} // namespace

UWorld* UDungeonRunEconomy::GetWorld() const
{
    if (HasAnyFlags(RF_ClassDefaultObject) || !GetOuter())
        return nullptr;
    if (UWorld* World = Cast<UWorld>(GetOuter()))
        return World;
    return GetOuter()->GetWorld();
}

bool UDungeonRunEconomy::HasAuthority() const
{
    return IsValid(GetWorld()) && GetWorld()->GetNetMode() != NM_Client;
}

bool UDungeonRunEconomy::CanMutate() const
{
    return HasAuthority() && CurrentRunId.IsValid() && !bBusy;
}

bool UDungeonRunEconomy::IsPlayerOwner(int32 PlayerId, const AMyPlayerState* State) const
{
    return PlayerId >= 0 && IsValid(State) && State->HasAuthority() && State->GetWorld() == GetWorld() &&
           State->GetPlayerId() == PlayerId;
}

bool UDungeonRunEconomy::IsBackpackOwner(int32 PlayerId, const UBackpackComponent* Backpack) const
{
    const auto* State = IsValid(Backpack) ? Cast<AMyPlayerState>(Backpack->GetOwner()) : nullptr;
    return IsPlayerOwner(PlayerId, State) && State->GetBackpackComponent() == Backpack;
}

bool UDungeonRunEconomy::Initialize(FGuid RunId)
{
    if (!HasAuthority() || bBusy || !RunId.IsValid())
        return false;
    if (CurrentRunId != RunId)
    {
        Accounts.Reset();
        CurrentRunId = RunId;
    }
    return true;
}

bool UDungeonRunEconomy::CreditClear(int32 PlayerId, FName RoomId, int32 Amount)
{
    if (!CanMutate() || PlayerId < 0 || RoomId.IsNone() || Amount <= 0)
        return false;
    FAccount& Account = Accounts.FindOrAdd(PlayerId);
    if (Account.ClearedRooms.Contains(RoomId) || Account.Balance > MAX_int32 - Amount)
        return false;
    Account.ClearedRooms.Add(RoomId);
    Account.Balance += Amount;
    return true;
}

int32 UDungeonRunEconomy::GetBalance(int32 PlayerId) const
{
    const FAccount* Account = Accounts.Find(PlayerId);
    return Account ? Account->Balance : 0;
}

FDungeonTransactionResult UDungeonRunEconomy::ClaimChest(int32 PlayerId, FName RewardId, UBackpackComponent* Backpack,
                                                         FPrimaryAssetId ItemId, int32 Quantity)
{
    if (!CanMutate())
        return Rejected(TEXT("RunUnavailable"));
    if (!IsBackpackOwner(PlayerId, Backpack))
        return Rejected(TEXT("WrongOwner"));
    if (RewardId.IsNone() || !ItemId.IsValid() || Quantity <= 0)
        return Rejected(TEXT("InvalidReward"));
    if (!IsValid(Backpack->ItemDefinitions) || !Backpack->ItemDefinitions->FindDefinition(ItemId))
        return Rejected(TEXT("UnknownItem"));
    TGuardValue<bool> Transaction(bBusy, true);
    FAccount& Account = Accounts.FindOrAdd(PlayerId);
    FChest* Chest = Account.Chests.Find(RewardId);
    if (Chest && (Chest->ItemId != ItemId || Chest->Quantity != Quantity))
        return Rejected(TEXT("RewardChanged"), Chest->Remaining);
    if (!Chest)
    {
        FChest NewChest;
        NewChest.ItemId = ItemId;
        NewChest.Quantity = Quantity;
        NewChest.Remaining = Quantity;
        Chest = &Account.Chests.Add(RewardId, NewChest);
    }
    if (Chest->Remaining == 0)
        return Rejected(TEXT("AlreadyClaimed"));
    const FInventoryAddResult Added = Backpack->TryAddItem(ItemId, Chest->Remaining);
    Chest->Remaining -= Added.AcceptedQuantity;
    if (Added.AcceptedQuantity <= 0)
        return Rejected(TEXT("InventoryFull"), Chest->Remaining);
    FDungeonTransactionResult Result;
    Result.bSucceeded = true;
    Result.Delivered = Added.AcceptedQuantity;
    Result.Remaining = Chest->Remaining;
    return Result;
}

FDungeonTransactionResult UDungeonRunEconomy::Purchase(int32 PlayerId, FName OfferId, UBackpackComponent* Backpack,
                                                       FPrimaryAssetId ItemId, int32 Price)
{
    if (!CanMutate())
        return Rejected(TEXT("RunUnavailable"));
    if (!IsBackpackOwner(PlayerId, Backpack))
        return Rejected(TEXT("WrongOwner"));
    if (OfferId.IsNone() || !ItemId.IsValid() || Price <= 0)
        return Rejected(TEXT("InvalidOffer"));
    if (!IsValid(Backpack->ItemDefinitions) || !Backpack->ItemDefinitions->FindDefinition(ItemId))
        return Rejected(TEXT("UnknownItem"));
    TGuardValue<bool> Transaction(bBusy, true);
    FAccount& Account = Accounts.FindOrAdd(PlayerId);
    if (Account.PurchasedOffers.Contains(OfferId))
        return Rejected(TEXT("AlreadyPurchased"));
    if (Account.Balance < Price)
        return Rejected(TEXT("InsufficientFunds"));
    // One unit cannot partially fit. Reserve the receipt and funds before inventory delegates fire.
    Account.Balance -= Price;
    Account.PurchasedOffers.Add(OfferId);
    const FInventoryAddResult Added = Backpack->TryAddItem(ItemId, 1);
    if (Added.AcceptedQuantity != 1)
    {
        Account.Balance += Price;
        Account.PurchasedOffers.Remove(OfferId);
        return Rejected(TEXT("InventoryFull"));
    }
    FDungeonTransactionResult Result;
    Result.bSucceeded = true;
    Result.Delivered = 1;
    return Result;
}

FDungeonTransactionResult UDungeonRunEconomy::Rest(int32 PlayerId, FName RoomId, UAbilitySystemComponent* AbilitySystem)
{
    if (!CanMutate())
        return Rejected(TEXT("RunUnavailable"));
    const auto* State = IsValid(AbilitySystem) ? Cast<AMyPlayerState>(AbilitySystem->GetOwner()) : nullptr;
    if (!IsPlayerOwner(PlayerId, State) || State->GetAbilitySystemComponent() != AbilitySystem ||
        AbilitySystem->GetOwnerActor() != State)
        return Rejected(TEXT("WrongOwner"));
    if (RoomId.IsNone())
        return Rejected(TEXT("InvalidRest"));
    // MyAttributeSet's GameplayEffect callback requires an initialized character avatar.
    if (!IsValid(Cast<ACharacter>(AbilitySystem->GetAvatarActor())) ||
        !AbilitySystem->HasAttributeSetForAttribute(UMyAttributeSet::GetHealthAttribute()) ||
        !AbilitySystem->HasAttributeSetForAttribute(UMyAttributeSet::GetManaAttribute()))
        return Rejected(TEXT("AttributesUnavailable"));
    const float Health = AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    const float MaxHealth = AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute());
    const float Mana = AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
    const float MaxMana = AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute());
    if (!FMath::IsFinite(Health) || !FMath::IsFinite(MaxHealth) || !FMath::IsFinite(Mana) ||
        !FMath::IsFinite(MaxMana) || Health <= 0.f || MaxHealth <= 0.f || MaxMana < 0.f)
        return Rejected(TEXT("CannotRest"));
    TGuardValue<bool> Transaction(bBusy, true);
    FAccount& Account = Accounts.FindOrAdd(PlayerId);
    if (Account.RestedRooms.Contains(RoomId))
        return Rejected(TEXT("AlreadyRested"));
    const float HealthGain = FMath::Clamp(MaxHealth * .3f, 0.f, FMath::Max(0.f, MaxHealth - Health));
    const float ManaGain = FMath::Clamp(MaxMana * .3f, 0.f, FMath::Max(0.f, MaxMana - Mana));
    if (HealthGain <= 0.f && ManaGain <= 0.f)
        return Rejected(TEXT("AlreadyFull"));
    // Instant server GE follows the existing inventory/ability attribute replication pipeline.
    auto* Effect = NewObject<UGameplayEffect>(GetTransientPackage());
    Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
    const auto AddRestore = [Effect](FGameplayAttribute Attribute, float Amount)
    {
        if (Amount <= 0.f)
            return;
        FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
        Modifier.Attribute = Attribute;
        Modifier.ModifierOp = EGameplayModOp::Additive;
        Modifier.ModifierMagnitude = FScalableFloat(Amount);
    };
    AddRestore(UMyAttributeSet::GetHealthAttribute(), HealthGain);
    AddRestore(UMyAttributeSet::GetManaAttribute(), ManaGain);
    FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
    Context.AddSourceObject(this);
    if (!AbilitySystem->ApplyGameplayEffectToSelf(Effect, 1.f, Context).WasSuccessfullyApplied())
        return Rejected(TEXT("RestRejected"));
    Account.RestedRooms.Add(RoomId);
    FDungeonTransactionResult Result;
    Result.bSucceeded = true;
    return Result;
}

bool UDungeonRunEconomy::IsPurchased(int32 PlayerId, FName OfferId) const
{
    const FAccount* Account = Accounts.Find(PlayerId);
    return Account && Account->PurchasedOffers.Contains(OfferId);
}

bool UDungeonRunEconomy::HasChestRecord(int32 PlayerId, FName RewardId) const
{
    const FAccount* Account = Accounts.Find(PlayerId);
    return Account && Account->Chests.Contains(RewardId);
}

int32 UDungeonRunEconomy::GetChestRemaining(int32 PlayerId, FName RewardId) const
{
    const FAccount* Account = Accounts.Find(PlayerId);
    const FChest* Chest = Account ? Account->Chests.Find(RewardId) : nullptr;
    return Chest ? Chest->Remaining : 0;
}

bool UDungeonRunEconomy::HasRested(int32 PlayerId, FName RoomId) const
{
    const FAccount* Account = Accounts.Find(PlayerId);
    return Account && Account->RestedRooms.Contains(RoomId);
}
