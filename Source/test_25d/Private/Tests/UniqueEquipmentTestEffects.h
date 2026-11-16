// 调用链：暗金自动化夹具 → 测试 GE → 生产装备事务与 GAS 聚合器；不供游戏资产引用。
#pragma once

#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "UniqueEquipmentTestEffects.generated.h"

UCLASS()
class UUniqueEquipmentTestOffense : public UUniqueEquipmentGameplayEffect
{
    GENERATED_BODY()

public:
    UUniqueEquipmentTestOffense();
};

UCLASS()
class UUniqueEquipmentTestDefense : public UUniqueEquipmentGameplayEffect
{
    GENERATED_BODY()

public:
    UUniqueEquipmentTestDefense();
};
