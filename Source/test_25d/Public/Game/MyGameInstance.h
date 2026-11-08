// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MyGameInstance.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    // Referenced by the configured GameInstance Blueprint so the lobby map is included when cooking.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rooms")
    TSoftObjectPtr<UWorld> RoomLobbyMap;

    UPROPERTY()
    FName PlayerStartTag = FName();

    UPROPERTY()
    FString LoadSlotName = FString();

    UPROPERTY()
    int32 LoadSlotIndex = 0;
};
