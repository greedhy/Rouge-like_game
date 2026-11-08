/** 主要调用链：装备自动化夹具 -> 构造测试GE -> Channel1加法Modifier -> EquipmentComponent应用/卸除 -> 属性断言。 */
#pragma once
#include "Equipment/EquipmentGameplayEffect.h"
#include "EquipmentTestEffects.generated.h"
UCLASS()
class UEquipmentTestArmor : public UEquipmentGameplayEffect
{
    GENERATED_BODY()
public:
    UEquipmentTestArmor();
};
UCLASS()
class UEquipmentTestArmorSeven : public UEquipmentGameplayEffect
{
    GENERATED_BODY()
public:
    UEquipmentTestArmorSeven();
};
UCLASS()
class UEquipmentTestHealth : public UEquipmentGameplayEffect
{
    GENERATED_BODY()
public:
    UEquipmentTestHealth();
};
