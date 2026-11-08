#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DungeonRunEconomy.generated.h"

class UBackpackComponent;
class UAbilitySystemComponent;
class AMyPlayerState;

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonTransactionResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    bool bSucceeded = false;
    UPROPERTY(BlueprintReadOnly)
    int32 Delivered = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 Remaining = 0;
    UPROPERTY(BlueprintReadOnly)
    FString Error;
};

/** Server-only run ledger. The director validates room state and interaction distance. */
UCLASS()
class TEST_25D_API UDungeonRunEconomy : public UObject
{
    GENERATED_BODY()
public:
    virtual UWorld* GetWorld() const override;
    bool Initialize(FGuid RunId);
    bool CreditClear(int32 PlayerId, FName RoomId, int32 Amount);
    int32 GetBalance(int32 PlayerId) const;
    FDungeonTransactionResult ClaimChest(int32 PlayerId, FName RewardId, UBackpackComponent* Backpack,
                                         FPrimaryAssetId ItemId, int32 Quantity);
    FDungeonTransactionResult Purchase(int32 PlayerId, FName OfferId, UBackpackComponent* Backpack,
                                       FPrimaryAssetId ItemId, int32 Price);
    FDungeonTransactionResult Rest(int32 PlayerId, FName RoomId, UAbilitySystemComponent* AbilitySystem);
    bool IsPurchased(int32 PlayerId, FName OfferId) const;
    bool HasChestRecord(int32 PlayerId, FName RewardId) const;
    int32 GetChestRemaining(int32 PlayerId, FName RewardId) const;
    bool HasRested(int32 PlayerId, FName RoomId) const;

private:
    struct FChest
    {
        FPrimaryAssetId ItemId;
        int32 Quantity = 0;
        int32 Remaining = 0;
    };
    struct FAccount
    {
        int32 Balance = 0;
        TSet<FName> ClearedRooms;
        TSet<FName> PurchasedOffers;
        TSet<FName> RestedRooms;
        TMap<FName, FChest> Chests;
    };
    bool HasAuthority() const;
    bool CanMutate() const;
    bool IsPlayerOwner(int32 PlayerId, const AMyPlayerState* State) const;
    bool IsBackpackOwner(int32 PlayerId, const UBackpackComponent* Backpack) const;
    FGuid CurrentRunId;
    TMap<int32, FAccount> Accounts;
    // Inventory and GAS delegates are synchronous; block nested writes across all accounts.
    bool bBusy = false;
};
