// 调用链：ArcaneShards 自动化 -> 原 ApplyDamageEffect -> 本测试 GE -> 真实 Health 属性下降。
#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "AbilityArcaneShardTestTypes.generated.h"

/** 测试用真实即时 GE，固定扣除10点 Health，不依赖内容曲线或随机格挡。 */
UCLASS()
class UArcaneShardTestDamageEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UArcaneShardTestDamageEffect();
};
