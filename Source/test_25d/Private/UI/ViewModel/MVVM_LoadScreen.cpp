// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ViewModel/MVVM_LoadScreen.h"
#include "UI/ViewModel/MVVM_LoadSlot.h"

#include "Game/MyGameInstance.h"
#include "Game/LoadScreenSaveGame.h"
#include "Game/MyGameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Online/RoomSubsystem.h"
#include "UI/HUD/LoadScreenHUD.h"
#include "GameFramework/PlayerController.h"
void UMVVM_LoadScreen::InitializeLoadSlots()
{
    LoadSlot_0 = NewObject<UMVVM_LoadSlot>(this, LoadSlotViewModelClass);
    LoadSlot_0->SetLoadSlotName(FString("LoadSlot_0"));
    LoadSlot_0->SlotIndex = 0;
    LoadSlots.Add(0, LoadSlot_0);

    LoadSlot_1 = NewObject<UMVVM_LoadSlot>(this, LoadSlotViewModelClass);
    LoadSlots.Add(1, LoadSlot_1);
    LoadSlot_1->SlotIndex = 1;
    LoadSlot_1->SetLoadSlotName(FString("LoadSlot_1"));

    LoadSlot_2 = NewObject<UMVVM_LoadSlot>(this, LoadSlotViewModelClass);
    LoadSlots.Add(2, LoadSlot_2);
    LoadSlot_2->SetLoadSlotName(FString("LoadSlot_2"));
    LoadSlot_2->SlotIndex = 2;

    SetNumLoadSlots(LoadSlots.Num());
}

void UMVVM_LoadScreen::SetNumLoadSlots(int32 InNumLoadSlots)
{
    UE_MVVM_SET_PROPERTY_VALUE(NumLoadSlots, InNumLoadSlots);
}
UMVVM_LoadSlot* UMVVM_LoadScreen::GetLoadSlotViewModelByIndex(int32 Index) const
{
    return LoadSlots.FindChecked(Index);
}

void UMVVM_LoadScreen::NewSlotButtonPressed(int32 Slot, const FString& EnteredName)
{
    auto* PC = UGameplayStatics::GetPlayerController(this, 0);
    auto* HUD = PC ? Cast<ALoadScreenHUD>(PC->GetHUD()) : nullptr;
    if (!HUD || !LoadSlots.Contains(Slot) || LoadSlots[Slot]->SlotStatus == Taken ||
        EnteredName.TrimStartAndEnd().IsEmpty())
    {
        return;
    }
    PendingNewSlot = Slot;
    PendingPlayerName = EnteredName.TrimStartAndEnd();
    HUD->ShowProfessionSelection();
}

// Commit the name and profession together so backing out leaves the slot vacant.
bool UMVVM_LoadScreen::ConfirmNewSlotProfession(EPlayerProfession Profession)
{
    const int32 Slot = PendingNewSlot;
    auto* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
    if (!MyGameMode || !LoadSlots.Contains(Slot) ||
        (Profession != EPlayerProfession::Mage && Profession != EPlayerProfession::Stander))
    {
        return false;
    }
    LoadSlots[Slot]->SetMapName(MyGameMode->DefaultMapName);
    LoadSlots[Slot]->SetPlayerName(PendingPlayerName);
    LoadSlots[Slot]->SetPlayerLevel(1);
    LoadSlots[Slot]->PlayerStartTag = MyGameMode->DefaultPlayerStartTag;
    LoadSlots[Slot]->MapAssetName = MyGameMode->DefaultMap.ToSoftObjectPath().GetAssetName();

    if (!MyGameMode->SaveSlotData(LoadSlots[Slot], Slot, Profession))
    {
        return false;
    }
    PendingNewSlot = INDEX_NONE;
    PendingPlayerName.Reset();
    LoadSlots[Slot]->SlotStatus = Taken;
    LoadSlots[Slot]->InitializeSlot();
    SelectSlotButtonPressed(Slot);
    PlayButtonPressed();
    return true;
}

void UMVVM_LoadScreen::NewGameButtonPressed(int32 Slot)
{
    LoadSlots[Slot]->SetWidgetSwitcherIndex.Broadcast(1);
}

void UMVVM_LoadScreen::SelectSlotButtonPressed(int32 Slot)
{
    SlotSelected.Broadcast();
    GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::White, FString::Printf(TEXT("%d"), Slot));

    for (const TTuple<int32, UMVVM_LoadSlot*> LoadSlot : LoadSlots)
    {
        if (LoadSlot.Key == Slot)
        {
            LoadSlot.Value->EnableSelectSlotButton.Broadcast(false);
        }
        else
        {
            LoadSlot.Value->EnableSelectSlotButton.Broadcast(true);
        }
    }
    SelectedSlot = LoadSlots[Slot];
}

void UMVVM_LoadScreen::DeleteButtonPressed()
{
    if (IsValid(SelectedSlot))
    {
        AMyGameModeBase::DeleteSlot(SelectedSlot->GetLoadSlotName(), SelectedSlot->SlotIndex);
        SelectedSlot->SlotStatus = Vacant;
        SelectedSlot->InitializeSlot();
        SelectedSlot->EnableSelectSlotButton.Broadcast(true);
    }
}

void UMVVM_LoadScreen::PlayButtonPressed()
{
    if (!IsValid(SelectedSlot) || SelectedSlot->SlotStatus != Taken ||
        !UGameplayStatics::DoesSaveGameExist(SelectedSlot->GetLoadSlotName(), SelectedSlot->SlotIndex))
        return;
    AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
    if (!MyGameMode)
        return;
    UMyGameInstance* MyGameInstance = Cast<UMyGameInstance>(MyGameMode->GetGameInstance());
    if (!MyGameInstance)
        return;
    // Mode selection always starts in the configured default map, using that map's entry.
    MyGameInstance->PlayerStartTag = MyGameMode->DefaultPlayerStartTag;
    MyGameInstance->LoadSlotName = SelectedSlot->GetLoadSlotName();
    MyGameInstance->LoadSlotIndex = SelectedSlot->SlotIndex;

    if (auto* Rooms = MyGameInstance->GetSubsystem<URoomSubsystem>())
    {
        Rooms->ConfigureSelection(SelectedSlot->GetPlayerName(), MyGameMode->DefaultMap);
        if (auto* PC = MyGameInstance->GetFirstLocalPlayerController())
            if (auto* HUD = Cast<ALoadScreenHUD>(PC->GetHUD()))
                HUD->ShowModeSelection();
    }
}

void UMVVM_LoadScreen::LoadData()
{
    AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
    if (!IsValid(MyGameMode))
        return;
    for (const TTuple<int32, UMVVM_LoadSlot*> LoadSlot : LoadSlots)
    {
        ULoadScreenSaveGame* SaveObject = MyGameMode->GetSaveSlotData(LoadSlot.Value->GetLoadSlotName(), LoadSlot.Key);

        const FString PlayerName = SaveObject->PlayerName;
        TEnumAsByte<ESaveSlotStatus> SaveSlotStatus = SaveObject->SaveSlotStatus;

        LoadSlot.Value->SlotStatus = SaveSlotStatus;
        LoadSlot.Value->SetPlayerName(PlayerName);
        LoadSlot.Value->InitializeSlot();

        LoadSlot.Value->SetMapName(SaveObject->MapName);
        LoadSlot.Value->PlayerStartTag = SaveObject->PlayerStartTag;
        LoadSlot.Value->SetPlayerLevel(SaveObject->PlayerLevel);
    }
}
