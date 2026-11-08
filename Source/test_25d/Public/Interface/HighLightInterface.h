// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HighLightInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI, BlueprintType)
class UHighLightInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * 
 */
class TEST_25D_API IHighLightInterface
{
    GENERATED_BODY()

    // Add interface functions to this class. This is the class that will be inherited to implement this interface.

public:
    UFUNCTION(BlueprintNativeEvent)
    void HighLightActor();

    UFUNCTION(BlueprintNativeEvent)
    void UnHighLightActor();

    UFUNCTION(BlueprintNativeEvent)
    void SetMoveToLocation(FVector& OutDestination);
};
