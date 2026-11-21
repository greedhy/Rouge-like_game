// 调用链：加载界面读取槽位 → 同步 ViewModel 字段 → WidgetSwitcher 显示对应页面。

#include "UI/ViewModel/MVVM_LoadSlot.h"

void UMVVM_LoadSlot::ApplySaveData(const ULoadScreenSaveGame* Save)
{
    bUnreadable = Save == nullptr;
    SlotStatus = Save ? Save->SaveSlotStatus.GetValue() : Taken;
    SetPlayerName(Save ? Save->PlayerName : NSLOCTEXT("LoadScreen", "UnreadableSave", "存档无法读取").ToString());
    SetMapName(Save ? Save->MapName : FString());
    PlayerStartTag = Save ? Save->PlayerStartTag : NAME_None;
    MapAssetName = Save ? Save->MapAssetName : FString();
    SetPlayerLevel(Save ? Save->PlayerLevel : 0);
    InitializeSlot();
}

void UMVVM_LoadSlot::InitializeSlot()
{
    const int32 WidgetSwitcherIndex = SlotStatus.GetValue();
    SetWidgetSwitcherIndex.Broadcast(WidgetSwitcherIndex);
}

void UMVVM_LoadSlot::SetPlayerName(FString InPlayerName)
{
    UE_MVVM_SET_PROPERTY_VALUE(PlayerName, InPlayerName);
}

void UMVVM_LoadSlot::SetMapName(FString InMapName)
{
    UE_MVVM_SET_PROPERTY_VALUE(MapName, InMapName);
}

void UMVVM_LoadSlot::SetPlayerLevel(int32 InLevel)
{
    UE_MVVM_SET_PROPERTY_VALUE(PlayerLevel, InLevel);
}

void UMVVM_LoadSlot::SetLoadSlotName(FString InLoadSlotName)
{
    UE_MVVM_SET_PROPERTY_VALUE(LoadSlotName, InLoadSlotName);
}
