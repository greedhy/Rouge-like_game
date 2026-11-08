/** 主要调用链：PIE验证脚本 -> ScheduleDamagePair -> 世界定时器 -> ApplyDamageEffect两次 -> 伤害数字反馈。只接受编辑器权威PIE世界。 */
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyAbilityTypes.h"
#include "DamageNumberTestLibrary.generated.h"
UCLASS()
class UDamageNumberTestLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
    static bool ScheduleDamagePair(const FDamageEffectParams& Params);
};
