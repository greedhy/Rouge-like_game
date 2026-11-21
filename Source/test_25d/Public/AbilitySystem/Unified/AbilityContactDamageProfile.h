// 调用链：真实投递捕获接触合同 -> 历史Impact -> 实际伤害owner准备局部参数；不携带目标权限。
#pragma once
#include "CoreMinimal.h"
#include "AbilityContactDamageProfile.generated.h"

UENUM(BlueprintType)
enum class EAbilityContactDamageProfile : uint8
{
    Unified,
    NativeActorProjectile,
    NativeMassProjectile,
    ConfiguredForces
};
