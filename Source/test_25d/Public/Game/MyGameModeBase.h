// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Backpack/BackpackPersistence.h"
#include "Game/PlayerProfession.h"
#include "MyGameModeBase.generated.h"

class UAbilitySystemComponent;
enum class ECharacterClass : uint8;
class UCharacterClassInfo;
class UAbilityInfo;
class ULootTiers;
class ULoadScreenSaveGame;
class USaveGame;
class UMVVM_LoadSlot;
class AMyPlayerState;
class APlayerController;
class UPlayer;

/**
 * 
 */
UCLASS()
class TEST_25D_API AMyGameModeBase : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMyGameModeBase();
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                          FString& ErrorMessage) override;
    virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
                                     const FString& Options, const FUniqueNetIdRepl& UniqueId,
                                     FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void ProcessServerTravel(const FString& URL, bool bAbsolute = false) override;
    virtual void PostSeamlessTravel() override;
    virtual void HandleSeamlessTravelPlayer(AController*& C) override;
    virtual AActor* FindPlayerStart_Implementation(AController* Player,
                                                   const FString& IncomingName = TEXT("")) override;
    bool TravelPartyThroughPortal(const TSoftObjectPtr<UWorld>& Destination, FName ArrivalGroup, FString& OutError);

    static bool ResolveStablePlayerIdForLogin(const FString& Options, const FUniqueNetIdRepl& UniqueId,
                                              FString& OutPlayerId);
    bool BeginBackpackLogin(const FString& PlayerId, FString& OutError);
    bool CompleteBackpackLogin(AMyPlayerState* PlayerState, const FString& PlayerId);
    bool LoadPersistedBackpackSnapshot(const FString& PlayerId, FBackpackSnapshot& OutSnapshot) const;
    void HandleBackpackTravelPersistence(bool bSeamlessTravel);
    void RestoreSeamlessBackpackSessions(const TArray<AMyPlayerState*>& PlayerStates);

    UPROPERTY(EditDefaultsOnly, Category = "Character Class Defaults")
    TObjectPtr<UCharacterClassInfo> CharacterClassInfo;
    //初始化角色的属性

    UPROPERTY(EditDefaultsOnly, Category = "Ability Info")
    TObjectPtr<UAbilityInfo> AbilityInfo;

    UPROPERTY(EditDefaultsOnly, Category = "Loot Tiers")
    TObjectPtr<ULootTiers> LootTiers;

    bool SaveSlotData(UMVVM_LoadSlot* LoadSlot, int32 SlotIndex,
                      EPlayerProfession Profession = EPlayerProfession::Mage);
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* Controller) override;
    virtual void InitializeHUDForPlayer_Implementation(APlayerController* Controller) override;
    UAbilityInfo* GetAbilityInfoForActor(const AActor* Actor) const;
    ULoadScreenSaveGame* GetSaveSlotData(const FString& SlotName, int32 SlotIndex) const;
    static void DeleteSlot(const FString& SlotName, int32 SlotIndex);
    ULoadScreenSaveGame* RetrieveInGameSaveData();
    void SaveInGameProgressData(ULoadScreenSaveGame* SaveObject);

    void SaveWorldState(UWorld* World, const FString& DestinationMapAssetName = FString("")) const;
    void LoadWorldState(UWorld* World) const;

    void TravelToMap(UMVVM_LoadSlot* Slot);

    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<USaveGame> LoadScreenSaveGameClass;

    UPROPERTY(EditDefaultsOnly)
    FString DefaultMapName;

    UPROPERTY(EditDefaultsOnly)
    TSoftObjectPtr<UWorld> DefaultMap;

    UPROPERTY(EditDefaultsOnly)
    FName DefaultPlayerStartTag;

    UPROPERTY(EditDefaultsOnly)
    TMap<FString, TSoftObjectPtr<UWorld>> Maps;

    FString GetMapNameFromMapAssetName(const FString& MapAssetName) const;

    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

    void PlayerDied(ACharacter* DeadCharacter);

protected:
    virtual void BeginPlay() override;

private:
    int32 AssignTravelSlot(AController* Player);
    AActor* FindArrivalStart(AController* Player, FName OverrideGroup = NAME_None);
    bool bPartyTravelRequested = false;
    struct FActiveBackpackSession
    {
        TWeakObjectPtr<AMyPlayerState> PlayerState;
        FDelegateHandle MutationHandle;
    };

    bool TryGetStablePlayerId(const AMyPlayerState* PlayerState, FString& OutPlayerId) const;
    void RegisterBackpackSession(AMyPlayerState* PlayerState, const FString& PlayerId, bool bLoadSnapshot);
    void PersistBackpack(const FString& PlayerId);
    void ReleaseBackpackSession(const FString& PlayerId);

    TUniquePtr<IBackpackPersistenceProvider> BackpackPersistenceProvider;
    TMap<FString, FActiveBackpackSession> ActiveBackpackSessions;
    TSet<FString> PendingSessionLocks;
    TMap<APlayerController*, FString> PendingLoginPlayerIds;
};
