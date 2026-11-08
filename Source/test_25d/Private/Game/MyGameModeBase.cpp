// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/MyGameModeBase.h"

#include "GameplayEffectTypes.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Kismet/GameplayStatics.h"

#include "EngineUtils.h"
#include "test_25d/MyLogChannels.h"
#include "Game/MyGameInstance.h"
#include "Game/LoadScreenSaveGame.h"
#include "GameFramework/PlayerStart.h"
#include "Interface/SaveInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UI/ViewModel/MVVM_LoadSlot.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Interface/SaveInterface.h"
#include "Backpack/BackpackComponent.h"
#include "Player/MyPlayerState.h"
#include "Character/PlayerCharacter.h"
#include "CheckPoint/TravelPlayerStart.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Engine/Level.h"
#include "Misc/PackageName.h"
#include "Online/RoomSubsystem.h"

AMyGameModeBase::AMyGameModeBase() : BackpackPersistenceProvider(MakeUnique<FServerLocalBackpackPersistenceProvider>())
{
}

void AMyGameModeBase::PostLogin(APlayerController* NewPlayer)
{
    FString PlayerId;
    if (!NewPlayer || !PendingLoginPlayerIds.RemoveAndCopyValue(NewPlayer, PlayerId))
    {
        Super::PostLogin(NewPlayer);
        return;
    }

    CompleteBackpackLogin(NewPlayer->GetPlayerState<AMyPlayerState>(), PlayerId);
    Super::PostLogin(NewPlayer);
}

void AMyGameModeBase::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                               FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (!ErrorMessage.IsEmpty())
        return;
    if (auto* Rooms = GetGameInstance()->GetSubsystem<URoomSubsystem>();
        Rooms && !Rooms->AuthorizeIncoming(Options, GetNumPlayers(), ErrorMessage))
        return;

    FString PlayerId;
    if (!ResolveStablePlayerIdForLogin(Options, UniqueId, PlayerId))
    {
        return;
    }

    BeginBackpackLogin(PlayerId, ErrorMessage);
}

APlayerController* AMyGameModeBase::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
                                          const FString& Options, const FUniqueNetIdRepl& UniqueId,
                                          FString& ErrorMessage)
{
    APlayerController* PlayerController =
        Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
    FString PlayerId;
    if (!ResolveStablePlayerIdForLogin(Options, UniqueId, PlayerId) || !PendingSessionLocks.Contains(PlayerId))
    {
        return PlayerController;
    }

    if (!IsValid(PlayerController) || !ErrorMessage.IsEmpty())
    {
        BackpackPersistenceProvider->ReleaseSessionLock(PlayerId);
        PendingSessionLocks.Remove(PlayerId);
        return PlayerController;
    }

    PendingLoginPlayerIds.Add(PlayerController, PlayerId);
    return PlayerController;
}

void AMyGameModeBase::Logout(AController* Exiting)
{
    AMyPlayerState* PlayerState = Exiting ? Exiting->GetPlayerState<AMyPlayerState>() : nullptr;
    FString PlayerId;
    if (TryGetStablePlayerId(PlayerState, PlayerId))
    {
        const FActiveBackpackSession* Session = ActiveBackpackSessions.Find(PlayerId);
        if (Session && Session->PlayerState.Get() == PlayerState)
        {
            PersistBackpack(PlayerId);
            ReleaseBackpackSession(PlayerId);
        }
    }
    Super::Logout(Exiting);
}

void AMyGameModeBase::ProcessServerTravel(const FString& URL, bool bAbsolute)
{
    HandleBackpackTravelPersistence(bUseSeamlessTravel);
    Super::ProcessServerTravel(URL, bAbsolute);
}

void AMyGameModeBase::PostSeamlessTravel()
{
    Super::PostSeamlessTravel();
    if (!HasAuthority() || !GameState)
        return;

    TArray<AMyPlayerState*> PlayerStates;
    for (APlayerState* CurrentPlayerState : GameState->PlayerArray)
    {
        if (AMyPlayerState* PlayerState = Cast<AMyPlayerState>(CurrentPlayerState))
            PlayerStates.Add(PlayerState);
    }
    RestoreSeamlessBackpackSessions(PlayerStates);
}

void AMyGameModeBase::HandleSeamlessTravelPlayer(AController*& C)
{
    Super::HandleSeamlessTravelPlayer(C);
    // Remote clients may finish loading after PostSeamlessTravel has returned.
    if (C)
    {
        AMyPlayerState* State = C->GetPlayerState<AMyPlayerState>();
        FString PlayerId;
        if (TryGetStablePlayerId(State, PlayerId))
            RegisterBackpackSession(State, PlayerId, false);
        else if (State && ResolveStablePlayerIdForLogin(FString(), State->GetUniqueId(), PlayerId))
        {
            // The UI-only lobby uses native PlayerStates. Their first seamless entry skips Login,
            // so initialize the new gameplay state's normal server-owned persistence session here.
            FString Error;
            if (BeginBackpackLogin(PlayerId, Error))
                CompleteBackpackLogin(State, PlayerId);
        }
    }
}

bool AMyGameModeBase::ResolveStablePlayerIdForLogin(const FString& Options, const FUniqueNetIdRepl& UniqueId,
                                                    FString& OutPlayerId)
{
    OutPlayerId.Reset();
    if (UniqueId.IsValid())
    {
        OutPlayerId = FString::Printf(TEXT("Online.%s"), *UniqueId.ToString());
    }
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
    else if (const FString LocalDevelopmentId = UGameplayStatics::ParseOption(Options, TEXT("BackpackPlayerId"));
             !LocalDevelopmentId.IsEmpty())
    {
        OutPlayerId = FString::Printf(TEXT("LocalDev.%s"), *LocalDevelopmentId);
    }
#endif
    return FServerLocalBackpackPersistenceProvider::IsValidPlayerId(OutPlayerId);
}

bool AMyGameModeBase::TryGetStablePlayerId(const AMyPlayerState* PlayerState, FString& OutPlayerId) const
{
    OutPlayerId.Reset();
    if (!IsValid(PlayerState))
        return false;
    OutPlayerId = PlayerState->GetBackpackPersistencePlayerId();
    return FServerLocalBackpackPersistenceProvider::IsValidPlayerId(OutPlayerId);
}

bool AMyGameModeBase::BeginBackpackLogin(const FString& PlayerId, FString& OutError)
{
    OutError.Reset();
    if (!BackpackPersistenceProvider || !FServerLocalBackpackPersistenceProvider::IsValidPlayerId(PlayerId))
    {
        OutError = TEXT("backpack_invalid_identity");
        return false;
    }
    if (!BackpackPersistenceProvider->AcquireSessionLock(PlayerId))
    {
        OutError = TEXT("backpack_duplicate_session");
        return false;
    }
    PendingSessionLocks.Add(PlayerId);
    return true;
}

bool AMyGameModeBase::CompleteBackpackLogin(AMyPlayerState* PlayerState, const FString& PlayerId)
{
    if (!PendingSessionLocks.Contains(PlayerId))
    {
        return false;
    }
    if (!IsValid(PlayerState))
    {
        BackpackPersistenceProvider->ReleaseSessionLock(PlayerId);
        PendingSessionLocks.Remove(PlayerId);
        return false;
    }
    PlayerState->SetBackpackPersistencePlayerId(PlayerId);
    RegisterBackpackSession(PlayerState, PlayerId, true);
    PendingSessionLocks.Remove(PlayerId);
    return true;
}

bool AMyGameModeBase::LoadPersistedBackpackSnapshot(const FString& PlayerId, FBackpackSnapshot& OutSnapshot) const
{
    return BackpackPersistenceProvider && BackpackPersistenceProvider->Load(PlayerId, OutSnapshot);
}

void AMyGameModeBase::HandleBackpackTravelPersistence(bool bSeamlessTravel)
{
    for (const TPair<FString, FActiveBackpackSession>& Session : ActiveBackpackSessions)
        PersistBackpack(Session.Key);
    if (!bSeamlessTravel)
    {
        TArray<FString> PlayerIds;
        ActiveBackpackSessions.GetKeys(PlayerIds);
        for (const FString& PlayerId : PlayerIds)
            ReleaseBackpackSession(PlayerId);
    }
}

void AMyGameModeBase::RestoreSeamlessBackpackSessions(const TArray<AMyPlayerState*>& PlayerStates)
{
    for (AMyPlayerState* PlayerState : PlayerStates)
    {
        FString PlayerId;
        if (TryGetStablePlayerId(PlayerState, PlayerId))
            RegisterBackpackSession(PlayerState, PlayerId, false);
    }
}

void AMyGameModeBase::RegisterBackpackSession(AMyPlayerState* PlayerState, const FString& PlayerId, bool bLoadSnapshot)
{
    if (!IsValid(PlayerState) || ActiveBackpackSessions.Contains(PlayerId))
        return;
    if (bLoadSnapshot)
    {
        FBackpackSnapshot Snapshot;
        if (LoadPersistedBackpackSnapshot(PlayerId, Snapshot))
            PlayerState->ImportBackpackSnapshot(Snapshot);
    }

    FActiveBackpackSession Session;
    Session.PlayerState = PlayerState;
    TWeakObjectPtr<AMyGameModeBase> WeakGameMode(this);
    Session.MutationHandle = PlayerState->GetBackpackComponent()->OnBackpackStateChanged.AddLambda(
        [WeakGameMode, PlayerId]()
        {
            if (AMyGameModeBase* GameMode = WeakGameMode.Get())
                GameMode->PersistBackpack(PlayerId);
        });
    ActiveBackpackSessions.Add(PlayerId, MoveTemp(Session));
}

void AMyGameModeBase::PersistBackpack(const FString& PlayerId)
{
    const FActiveBackpackSession* Session = ActiveBackpackSessions.Find(PlayerId);
    if (BackpackPersistenceProvider && Session && Session->PlayerState.IsValid())
    {
        BackpackPersistenceProvider->Save(PlayerId, Session->PlayerState->ExportBackpackSnapshot());
    }
}

void AMyGameModeBase::ReleaseBackpackSession(const FString& PlayerId)
{
    if (FActiveBackpackSession* Session = ActiveBackpackSessions.Find(PlayerId))
    {
        if (Session->PlayerState.IsValid())
            Session->PlayerState->GetBackpackComponent()->OnBackpackStateChanged.Remove(Session->MutationHandle);
        ActiveBackpackSessions.Remove(PlayerId);
    }
    if (BackpackPersistenceProvider)
        BackpackPersistenceProvider->ReleaseSessionLock(PlayerId);
}

void AMyGameModeBase::SaveSlotData(UMVVM_LoadSlot* LoadSlot, int32 SlotIndex)
{
    if (UGameplayStatics::DoesSaveGameExist(LoadSlot->GetLoadSlotName(), SlotIndex))
    {
        UGameplayStatics::DeleteGameInSlot(LoadSlot->GetLoadSlotName(), SlotIndex);
    }
    USaveGame* SaveGameObject = UGameplayStatics::CreateSaveGameObject(LoadScreenSaveGameClass);
    ULoadScreenSaveGame* LoadScreenSaveGame = Cast<ULoadScreenSaveGame>(SaveGameObject);
    LoadScreenSaveGame->PlayerName = LoadSlot->GetPlayerName();
    LoadScreenSaveGame->SaveSlotStatus = Taken;
    LoadScreenSaveGame->MapName = LoadSlot->GetMapName();
    LoadScreenSaveGame->MapAssetName = LoadSlot->MapAssetName;
    LoadScreenSaveGame->PlayerStartTag = LoadSlot->PlayerStartTag;

    UGameplayStatics::SaveGameToSlot(LoadScreenSaveGame, LoadSlot->GetLoadSlotName(), SlotIndex);
}

ULoadScreenSaveGame* AMyGameModeBase::GetSaveSlotData(const FString& SlotName, int32 SlotIndex) const
{
    USaveGame* SaveGameObject = nullptr;
    if (UGameplayStatics::DoesSaveGameExist(SlotName, SlotIndex))
    {
        SaveGameObject = UGameplayStatics::LoadGameFromSlot(SlotName, SlotIndex);
    }
    else
    {
        SaveGameObject = UGameplayStatics::CreateSaveGameObject(LoadScreenSaveGameClass);
    }
    ULoadScreenSaveGame* LoadScreenSaveGame = Cast<ULoadScreenSaveGame>(SaveGameObject);
    return LoadScreenSaveGame;
}

void AMyGameModeBase::DeleteSlot(const FString& SlotName, int32 SlotIndex)
{
    if (UGameplayStatics::DoesSaveGameExist(SlotName, SlotIndex))
    {
        UGameplayStatics::DeleteGameInSlot(SlotName, SlotIndex);
    }
}

ULoadScreenSaveGame* AMyGameModeBase::RetrieveInGameSaveData()
{
    UMyGameInstance* MyGameInstance = Cast<UMyGameInstance>(GetGameInstance());

    const FString InGameLoadSlotName = MyGameInstance->LoadSlotName;
    const int32 InGameLoadSlotIndex = MyGameInstance->LoadSlotIndex;

    return GetSaveSlotData(InGameLoadSlotName, InGameLoadSlotIndex);
}

void AMyGameModeBase::SaveInGameProgressData(ULoadScreenSaveGame* SaveObject)
{
    UMyGameInstance* MyGameInstance = Cast<UMyGameInstance>(GetGameInstance());

    const FString InGameLoadSlotName = MyGameInstance->LoadSlotName;
    const int32 InGameLoadSlotIndex = MyGameInstance->LoadSlotIndex;
    MyGameInstance->PlayerStartTag = SaveObject->PlayerStartTag;

    UGameplayStatics::SaveGameToSlot(SaveObject, InGameLoadSlotName, InGameLoadSlotIndex);
}

void AMyGameModeBase::SaveWorldState(UWorld* World, const FString& DestinationMapAssetName) const
{
    FString WorldName = World->GetMapName();
    WorldName.RemoveFromStart(World->StreamingLevelsPrefix);

    UMyGameInstance* MyGI = Cast<UMyGameInstance>(GetGameInstance());
    check(MyGI);

    if (ULoadScreenSaveGame* SaveGame = GetSaveSlotData(MyGI->LoadSlotName, MyGI->LoadSlotIndex))
    {
        if (DestinationMapAssetName != FString(""))
        {
            SaveGame->MapAssetName = DestinationMapAssetName;
            SaveGame->MapName = GetMapNameFromMapAssetName(DestinationMapAssetName);
        }

        if (!SaveGame->HasMap(WorldName))
        {
            FSavedMap NewSavedMap;
            NewSavedMap.MapAssetName = WorldName;
            SaveGame->SavedMaps.Add(NewSavedMap);
        }

        FSavedMap SavedMap = SaveGame->GetSavedMapWithMapName(WorldName);
        SavedMap.SavedActors.Empty();

        for (FActorIterator It(World); It; ++It)
        {
            AActor* Actor = *It;

            if (!IsValid(Actor) || !Actor->Implements<USaveInterface>())
                continue;

            FSavedActor SavedActor;
            SavedActor.ActorName = Actor->GetFName();
            SavedActor.Transform = Actor->GetTransform();

            FMemoryWriter MemoryWriter(SavedActor.Bytes);

            FObjectAndNameAsStringProxyArchive Archive(MemoryWriter, true);
            Archive.ArIsSaveGame = true;

            Actor->Serialize(Archive);

            SavedMap.SavedActors.AddUnique(SavedActor);
        }

        for (FSavedMap& MapToReplace : SaveGame->SavedMaps)
        {
            if (MapToReplace.MapAssetName == WorldName)
            {
                MapToReplace = SavedMap;
            }
        }
        UGameplayStatics::SaveGameToSlot(SaveGame, MyGI->LoadSlotName, MyGI->LoadSlotIndex);
    }
}

void AMyGameModeBase::LoadWorldState(UWorld* World) const
{
    FString WorldName = World->GetMapName();
    WorldName.RemoveFromStart(World->StreamingLevelsPrefix);

    UMyGameInstance* MyGI = Cast<UMyGameInstance>(GetGameInstance());
    check(MyGI);

    if (UGameplayStatics::DoesSaveGameExist(MyGI->LoadSlotName, MyGI->LoadSlotIndex))
    {

        ULoadScreenSaveGame* SaveGame =
            Cast<ULoadScreenSaveGame>(UGameplayStatics::LoadGameFromSlot(MyGI->LoadSlotName, MyGI->LoadSlotIndex));
        if (SaveGame == nullptr)
        {
            UE_LOG(LogMyChannel, Error, TEXT("Failed to load slot"));
            return;
        }

        for (FActorIterator It(World); It; ++It)
        {
            AActor* Actor = *It;

            if (!Actor->Implements<USaveInterface>())
                continue;

            for (FSavedActor SavedActor : SaveGame->GetSavedMapWithMapName(WorldName).SavedActors)
            {
                if (SavedActor.ActorName == Actor->GetFName())
                {
                    if (ISaveInterface::Execute_ShouldLoadTransform(Actor))
                    {
                        Actor->SetActorTransform(SavedActor.Transform);
                    }

                    FMemoryReader MemoryReader(SavedActor.Bytes);

                    FObjectAndNameAsStringProxyArchive Archive(MemoryReader, true);
                    Archive.ArIsSaveGame = true;
                    Actor->Serialize(Archive);

                    ISaveInterface::Execute_LoadActor(Actor);
                }
            }
        }
    }
}

void AMyGameModeBase::TravelToMap(UMVVM_LoadSlot* Slot)
{
    const FString SlotName = Slot->GetLoadSlotName();
    const int32 SlotIndex = Slot->SlotIndex;

    UGameplayStatics::OpenLevelBySoftObjectPtr(Slot, Maps.FindChecked(Slot->GetMapName()));
}

FString AMyGameModeBase::GetMapNameFromMapAssetName(const FString& MapAssetName) const
{
    for (auto& Map : Maps)
    {
        if (Map.Value.ToSoftObjectPath().GetAssetName() == MapAssetName)
        {
            return Map.Key;
        }
    }
    return FString();
}

AActor* AMyGameModeBase::ChoosePlayerStart_Implementation(AController* Player)
{
    // Menu/spectator worlds can use the native PlayerState and have no party slots.
    if (!Player || !Player->GetPlayerState<AMyPlayerState>())
        return Super::ChoosePlayerStart_Implementation(Player);
    return FindArrivalStart(Player);
}

AActor* AMyGameModeBase::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
    if (!Player || !Player->GetPlayerState<AMyPlayerState>())
        return Super::FindPlayerStart_Implementation(Player, IncomingName);
    // Never reuse a StartSpot from the old world or silently fall back to world origin.
    return FindArrivalStart(Player, FName(*IncomingName));
}

int32 AMyGameModeBase::AssignTravelSlot(AController* Player)
{
    AMyPlayerState* State = Player ? Player->GetPlayerState<AMyPlayerState>() : nullptr;
    if (!State)
        return INDEX_NONE;
    if (State->GetTravelPlayerSlot() != INDEX_NONE)
        return State->GetTravelPlayerSlot();
    TSet<int32> Used;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const AMyPlayerState* Other = It->Get()->GetPlayerState<AMyPlayerState>())
            if (Other != State && Other->GetTravelPlayerSlot() >= 0)
                Used.Add(Other->GetTravelPlayerSlot());
    }
    int32 Slot = 0;
    while (Used.Contains(Slot))
        ++Slot;
    State->SetTravelPlayerSlot(Slot);
    return Slot;
}

AActor* AMyGameModeBase::FindArrivalStart(AController* Player, FName OverrideGroup)
{
    const int32 Slot = AssignTravelSlot(Player);
    FName Group = OverrideGroup;
    if (Group.IsNone())
        Group = FName(*UGameplayStatics::ParseOption(OptionsString, TEXT("ArrivalGroup")));
    TArray<APlayerStart*> Starts;
    FName MapDefault;
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
    {
        Starts.Add(*It);
        if (const auto* Start = Cast<ATravelPlayerStart>(*It); Start && Start->bDefaultEntryGroup)
        {
            if (!MapDefault.IsNone() && MapDefault != Start->PlayerStartTag)
            {
                UE_LOG(LogTemp, Error, TEXT("[PortalTravel] More than one default arrival group in %s"),
                       *GetWorld()->GetName());
                return nullptr;
            }
            MapDefault = Start->PlayerStartTag;
        }
    }
    if (Group.IsNone() && GetNetMode() == NM_Standalone)
    {
        if (const auto* GI = Cast<UMyGameInstance>(GetGameInstance()))
            Group = GI->PlayerStartTag;
    }
    if (Group.IsNone())
        Group = MapDefault.IsNone() ? DefaultPlayerStartTag : MapDefault;
    FString Error;
    APlayerStart* Start = ATravelPlayerStart::FindForSlot(Starts, Group, Slot, Error);
    if (!Start)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PortalTravel] %s: %s"), *GetWorld()->GetName(), *Error);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[PortalTravel] Player slot %d -> %s at %s"), Slot, *Start->GetName(),
               *Start->GetActorLocation().ToString());
    }
    return Start;
}

bool AMyGameModeBase::TravelPartyThroughPortal(const TSoftObjectPtr<UWorld>& Destination, FName ArrivalGroup,
                                               FString& OutError)
{
    OutError.Reset();
    if (!HasAuthority() || bPartyTravelRequested)
    {
        OutError = TEXT("A party transfer is already in progress.");
        return false;
    }
    const FString DestinationAssetPath = UWorld::RemovePIEPrefix(Destination.ToSoftObjectPath().ToString());
    const FString MapPackage = FSoftObjectPath(DestinationAssetPath).GetLongPackageName();
    if (ArrivalGroup.IsNone() || ArrivalGroup.ToString().Contains(TEXT("?")) ||
        ArrivalGroup.ToString().Contains(TEXT("#")) || !FPackageName::IsValidLongPackageName(MapPackage) ||
        !FPackageName::DoesPackageExist(MapPackage))
    {
        OutError = TEXT("Configure a saved destination map and a valid arrival group.");
        return false;
    }
    // Validate authored starts, not a live (or already destroyed) PIE duplicate.
    // SoftObjectPtr::LoadSynchronous remaps visited maps to the current PIE instance.
    UWorld* TargetWorld = LoadObject<UWorld>(nullptr, *DestinationAssetPath);
    if (!TargetWorld || !TargetWorld->PersistentLevel)
    {
        OutError = TEXT("Could not load destination map for spawn validation.");
        return false;
    }
    TArray<APlayerStart*> Starts;
    for (AActor* Actor : TargetWorld->PersistentLevel->Actors)
        if (auto* Start = Cast<APlayerStart>(Actor))
            Starts.Add(Start);
    TArray<APlayerCharacter*> Party;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC) || !IsValid(PC->PlayerState) || PC->PlayerState->IsOnlyASpectator())
            continue;
        APlayerCharacter* Character = Cast<APlayerCharacter>(PC->GetPawn());
        if (!Character || !PC->GetPlayerState<AMyPlayerState>())
        {
            OutError = TEXT("A player is not ready to travel.");
            return false;
        }
        if (!ATravelPlayerStart::FindForSlot(Starts, ArrivalGroup, AssignTravelSlot(PC), OutError))
            return false;
        Party.Add(Character);
    }
    if (Party.IsEmpty())
    {
        OutError = TEXT("No players to transfer.");
        return false;
    }
    // Each player's snapshot travels with their own PlayerState. Never load everyone
    // from the triggering player's single-player save slot.
    for (APlayerCharacter* Character : Party)
    {
        auto* Progress = NewObject<ULoadScreenSaveGame>();
        Character->WriteProgressToSaveData(Progress, ArrivalGroup);
        const UMyAttributeSet* Attributes = CastChecked<UMyAttributeSet>(Character->GetAttributeSet());
        Character->GetPlayerState<AMyPlayerState>()->SetTravelProgress(Progress, Attributes->GetHealth(),
                                                                       Attributes->GetMana());
    }
    if (const auto* GI = Cast<UMyGameInstance>(GetGameInstance()); GI && !GI->LoadSlotName.IsEmpty())
    {
        SaveWorldState(GetWorld(), Destination.ToSoftObjectPath().GetAssetName());
        if (GetNetMode() == NM_Standalone)
            Party[0]->SaveProgress_Implementation(ArrivalGroup);
    }
    bUseSeamlessTravel = true;
    bPartyTravelRequested = true;
    const FString URL = MapPackage + TEXT("?ArrivalGroup=") + ArrivalGroup.ToString();
    UE_LOG(LogTemp, Log, TEXT("[PortalTravel] Sending %d players to %s"), Party.Num(), *URL);
    if (!GetWorld()->ServerTravel(URL, true))
    {
        bPartyTravelRequested = false;
        for (APlayerCharacter* Character : Party)
            Character->GetPlayerState<AMyPlayerState>()->ClearTravelProgress();
        OutError = TEXT("Server rejected map travel.");
        return false;
    }
    return true;
}

void AMyGameModeBase::PlayerDied(ACharacter* DeadCharacter)
{
    if (IsValid(DeadCharacter))
    {
        if (AMyPlayerState* PlayerState = DeadCharacter->GetPlayerState<AMyPlayerState>())
        {
            FString PlayerId;
            if (TryGetStablePlayerId(PlayerState, PlayerId))
                PersistBackpack(PlayerId);
        }
    }

    ULoadScreenSaveGame* SaveGame = RetrieveInGameSaveData();
    if (!IsValid(SaveGame))
        return;

    UGameplayStatics::OpenLevel(DeadCharacter, FName(SaveGame->MapAssetName));
}

void AMyGameModeBase::BeginPlay()
{
    Super::BeginPlay();
    Maps.Add(DefaultMapName, DefaultMap);
}
