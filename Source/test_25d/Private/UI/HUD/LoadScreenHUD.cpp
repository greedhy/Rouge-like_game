// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/HUD/LoadScreenHUD.h"

#include "Blueprint/UserWidget.h"
#include "UI/ViewModel/MVVM_LoadScreen.h"
#include "UI/Widget/LoadScreenWidget.h"
#include "UI/Widget/RoomMenuWidget.h"
#include "Online/RoomSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"

void ALoadScreenHUD::BeginPlay()
{
    Super::BeginPlay();

    LoadScreenViewModel = NewObject<UMVVM_LoadScreen>(this, LoadScreenViewModelClass);
    LoadScreenViewModel->InitializeLoadSlots();

    LoadScreenWidget = CreateWidget<ULoadScreenWidget>(GetWorld(), LoadScreenWidgetClass);
    LoadScreenWidget->AddToViewport();
    LoadScreenWidget->BlueprintInitializeWidget();

    LoadScreenViewModel->LoadData();
    if (auto* Rooms = GetGameInstance()->GetSubsystem<URoomSubsystem>();
        Rooms && Rooms->bReturnToMode && Rooms->HasSelection())
        ShowModeSelection();
}

// Both creation and mode selection use the same menu frame and input focus.
bool ALoadScreenHUD::OpenRoomMenu()
{
    if (!RoomMenuWidget)
        RoomMenuWidget = CreateWidget<URoomMenuWidget>(
            GetOwningPlayerController(),
            RoomMenuWidgetClass ? RoomMenuWidgetClass : TSubclassOf<URoomMenuWidget>(URoomMenuWidget::StaticClass()));
    if (!RoomMenuWidget)
    {
        return false;
    }
    LoadScreenWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (!RoomMenuWidget->IsInViewport())
        RoomMenuWidget->AddToViewport(50);
    RoomMenuWidget->SetVisibility(ESlateVisibility::Visible);
    GetOwningPlayerController()->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(RoomMenuWidget->TakeWidget()));
    GetOwningPlayerController()->bShowMouseCursor = true;
    return true;
}

void ALoadScreenHUD::ShowModeSelection()
{
    if (OpenRoomMenu())
    {
        RoomMenuWidget->ShowModeSelection();
    }
}

void ALoadScreenHUD::ShowProfessionSelection()
{
    if (OpenRoomMenu())
    {
        RoomMenuWidget->ShowProfessionSelection();
    }
}
void ALoadScreenHUD::ShowSaveSelection()
{
    if (RoomMenuWidget)
        RoomMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
    LoadScreenWidget->SetVisibility(ESlateVisibility::Visible);
    GetOwningPlayerController()->SetInputMode(FInputModeUIOnly());
    if (auto* Rooms = GetGameInstance()->GetSubsystem<URoomSubsystem>())
        Rooms->bReturnToMode = false;
}
