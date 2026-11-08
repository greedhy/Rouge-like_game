#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MonsterManagementConfig.generated.h"

UCLASS(BlueprintType)
class TEST_25D_API UMonsterManagementConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spatial", meta = (ClampMin = "100"))
    float CellSize = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spatial", meta = (ClampMin = "0.02", ClampMax = "5"))
    float UpdateInterval = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity", meta = (ClampMin = "0"))
    float WakeRadius = 2500.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity", meta = (ClampMin = "0"))
    float SleepRadius = 3500.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity", meta = (ClampMin = "0"))
    float DamageWakeSeconds = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity")
    bool bPauseDistantAI = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
    bool bDrawDebug = false;
};
