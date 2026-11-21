// 自动化调用链：Automation RunTests → 构造带分支布局 → 主题组件计算进深 → 断言。
#include "Misc/AutomationTest.h"
#include "Roguelike/DungeonThemeComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonThemeRoomDepth, "test_25d.DungeonTheme.RoomDepth",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDungeonThemeRoomDepth::RunTest(const FString&)
{
    FDungeonLayout Layout;
    Layout.Rooms.SetNum(6);
    Layout.Rooms[0].bPersistent = true;
    Layout.Connections = {{2, 1}, {0, 2}, {1, 3}, {2, 4}, {3, 4}, {4, 99}};
    const TArray<int32> Depths = UDungeonThemeComponent::CalculateRoomDepths(Layout);
    TestTrue(TEXT("Room order and branch edges preserve the shortest graph depth"),
             Depths == TArray<int32>{0, 2, 1, 3, 2, INDEX_NONE});
    return true;
}
#endif
