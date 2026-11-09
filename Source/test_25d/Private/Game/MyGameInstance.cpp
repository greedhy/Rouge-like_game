// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/MyGameInstance.h"
#include "Game/LoadScreenSaveGame.h"
#include "Kismet/GameplayStatics.h"

// The old save schema defaults to Mage when the profession field is absent.
EPlayerProfession UMyGameInstance::GetSelectedProfession() const
{
    if (LoadSlotName.IsEmpty())
    {
        return EPlayerProfession::Unselected;
    }
    const auto* Save = Cast<ULoadScreenSaveGame>(UGameplayStatics::LoadGameFromSlot(LoadSlotName, LoadSlotIndex));
    if (!Save || Save->SaveSlotStatus != Taken)
    {
        return EPlayerProfession::Unselected;
    }
    return Save->Profession == EPlayerProfession::Stander ? EPlayerProfession::Stander : EPlayerProfession::Mage;
}
