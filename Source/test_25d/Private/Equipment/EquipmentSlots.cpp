// 调用链：装备与界面读取 All → 获得固定槽位 Tag、名称及元素槽标记。
#include "Equipment/EquipmentSlots.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot0, "Equipment.Slot.Weapon");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot1, "Equipment.Slot.Ring.Left");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot2, "Equipment.Slot.Ring.Right");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot3, "Equipment.Slot.Chest");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot4, "Equipment.Slot.Boots");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot5, "Equipment.Slot.Helmet");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot6, "Equipment.Slot.MagicStone");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot7, "Equipment.Slot.Element.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot8, "Equipment.Slot.Element.2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentSlot9, "Equipment.Slot.Element.3");
// 返回进程内共享槽位定义，保留装备界面的配置顺序。
const TArray<FEquipmentSlotDefinition>& EquipmentSlots::All()
{
    static const TArray<FEquipmentSlotDefinition> Slots = {
        {TAG_EquipmentSlot0, TEXT("武器"), false},   {TAG_EquipmentSlot1, TEXT("左戒指"), false},
        {TAG_EquipmentSlot2, TEXT("右戒指"), false}, {TAG_EquipmentSlot3, TEXT("护甲"), false},
        {TAG_EquipmentSlot4, TEXT("鞋子"), false},   {TAG_EquipmentSlot5, TEXT("头盔"), false},
        {TAG_EquipmentSlot6, TEXT("魔法石"), false}, {TAG_EquipmentSlot7, TEXT("元素1"), true},
        {TAG_EquipmentSlot8, TEXT("元素2"), true},   {TAG_EquipmentSlot9, TEXT("元素3"), true},
    };
    return Slots;
}
