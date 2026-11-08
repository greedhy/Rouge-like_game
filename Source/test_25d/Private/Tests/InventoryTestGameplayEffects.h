/** 主要调用链：背包自动化夹具 -> 构造1秒持续GE -> 物品使用通路 -> 效果与库存消耗断言。 */
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "InventoryTestGameplayEffects.generated.h"

UCLASS()
class UInventoryTestInstantGameplayEffect : public UGameplayEffect
{
    GENERATED_BODY()
};

UCLASS()
class UInventoryTestDurationGameplayEffect : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UInventoryTestDurationGameplayEffect();
};
