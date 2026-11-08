/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.DungeonGeneration.ProbeTopology
 * - test_25d.DungeonGeneration.RepeatabilityAndDiversity
 * - test_25d.DungeonGeneration.RejectMalformedInput
 * - test_25d.DungeonGeneration.ReadinessBarrier
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Roguelike/DungeonGenerationProfile.h"
#include "Roguelike/DungeonLayoutLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    UDungeonGenerationProfile* MakeDungeonProfile()
    {
        auto* Profile = NewObject<UDungeonGenerationProfile>();
        Profile->Entrance.TemplateId = TEXT("Entrance");
        Profile->Entrance.Role = EDungeonRoomRole::Entrance;
        for (const TCHAR* Id : {TEXT("CombatA"), TEXT("CombatB")})
        {
            auto& Room = Profile->Templates.AddDefaulted_GetRef();
            Room.TemplateId = Id;
            Room.Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(FString(TEXT("/Game/QA/")) + Id + TEXT(".") + Id));
        }
        auto& Boss = Profile->Templates.AddDefaulted_GetRef();
        Boss.TemplateId = TEXT("Boss");
        Boss.Role = EDungeonRoomRole::Boss;
        Boss.Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/QA/Boss.Boss")));
        return Profile;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonProbeTopology, "test_25d.DungeonGeneration.ProbeTopology",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.ProbeTopology 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonProbeTopology::RunTest(const FString&)
{
    auto* Profile = MakeDungeonProfile();
    FDungeonLayout Layout;
    FString Error;
    if (!TestTrue(TEXT("A configured probe produces a connected layout"),
                  UDungeonLayoutLibrary::BuildAssemblyProbe(Profile, 42, Layout, Error)))
        return false;
    TestEqual(TEXT("Three rooms"), Layout.Rooms.Num(), 3);
    TestEqual(TEXT("Two connections"), Layout.Connections.Num(), 2);
    if (Layout.Rooms.Num() != 3 || Layout.Connections.Num() != 2)
        return false;
    TestEqual(TEXT("Fixed entrance cell"), Layout.Rooms[0].Cell, FIntPoint(0, 0));
    TestTrue(TEXT("Entrance stays in persistent world"), Layout.Rooms[0].bPersistent);
    TestFalse(TEXT("Combat is streamed"), Layout.Rooms[1].bPersistent);
    TestTrue(TEXT("Final probe slot represents the future boss room"), Layout.Rooms[2].Role == EDungeonRoomRole::Boss);
    for (int32 I = 0; I < 2; ++I)
    {
        TestEqual(TEXT("Chain source"), Layout.Connections[I].From, I);
        TestEqual(TEXT("Chain destination"), Layout.Connections[I].To, I + 1);
        const FIntPoint Delta = Layout.Rooms[I + 1].Cell - Layout.Rooms[I].Cell;
        TestEqual(TEXT("Exactly one adjacent room step"), FMath::Abs(Delta.X) + FMath::Abs(Delta.Y), 1);
    }
    TestNotEqual(TEXT("Third room does not overlap entrance"), Layout.Rooms[2].Cell, Layout.Rooms[0].Cell);
    TestTrue(TEXT("Generated result validates"), UDungeonLayoutLibrary::ValidateLayout(Profile, Layout, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonProbeRepeatability, "test_25d.DungeonGeneration.RepeatabilityAndDiversity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.RepeatabilityAndDiversity 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonProbeRepeatability::RunTest(const FString&)
{
    auto* Profile = MakeDungeonProfile();
    TSet<FString> Layouts;
    for (int32 Seed = 0; Seed < 100; ++Seed)
    {
        FDungeonLayout A, B;
        FString Error;
        if (!TestTrue(TEXT("Every seed produces a layout"),
                      UDungeonLayoutLibrary::BuildAssemblyProbe(Profile, Seed, A, Error)))
            return false;
        Swap(Profile->Templates[0], Profile->Templates[1]);
        if (!TestTrue(TEXT("Template input order is irrelevant"),
                      UDungeonLayoutLibrary::BuildAssemblyProbe(Profile, Seed, B, Error)))
            return false;
        TestEqual(TEXT("Same seed and assets have identical canonical output"),
                  UDungeonLayoutLibrary::CanonicalLayout(A), UDungeonLayoutLibrary::CanonicalLayout(B));
        A.Seed = 0;
        Layouts.Add(UDungeonLayoutLibrary::CanonicalLayout(A));
    }
    TestTrue(TEXT("Different seeds change geometry/templates, not just the seed field"), Layouts.Num() >= 12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonRejectMalformed, "test_25d.DungeonGeneration.RejectMalformedInput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.RejectMalformedInput 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonRejectMalformed::RunTest(const FString&)
{
    auto* Profile = MakeDungeonProfile();
    FDungeonLayout Layout;
    FString Error;
    TestFalse(TEXT("Null profile rejected"), UDungeonLayoutLibrary::BuildAssemblyProbe(nullptr, 1, Layout, Error));
    TestFalse(TEXT("Failure explains itself"), Error.IsEmpty());
    Profile->Templates.Last().Level.Reset();
    TestFalse(TEXT("Missing level rejected"), UDungeonLayoutLibrary::BuildAssemblyProbe(Profile, 1, Layout, Error));
    Profile = MakeDungeonProfile();
    Profile->Templates.Last().DoorMask = 0;
    TestFalse(TEXT("No compatible boss entrance rejected"),
              UDungeonLayoutLibrary::BuildAssemblyProbe(Profile, 1, Layout, Error));
    Profile = MakeDungeonProfile();
    Profile->RoomSpacing = 0;
    TestFalse(TEXT("Invalid footprint spacing rejected"),
              UDungeonLayoutLibrary::BuildAssemblyProbe(Profile, 1, Layout, Error));
    Profile = MakeDungeonProfile();
    if (!UDungeonLayoutLibrary::BuildAssemblyProbe(Profile, 1, Layout, Error))
    {
        AddError(TEXT("Valid fixture could not be generated"));
        return false;
    }
    auto Bad = Layout;
    Bad.Rooms[1].Cell = Bad.Rooms[0].Cell;
    TestFalse(TEXT("Overlapping rooms rejected"), UDungeonLayoutLibrary::ValidateLayout(Profile, Bad, Error));
    Bad = Layout;
    Bad.Connections[0].To = 99;
    TestFalse(TEXT("Out of bounds edge rejected"), UDungeonLayoutLibrary::ValidateLayout(Profile, Bad, Error));
    Bad = Layout;
    Bad.Rooms[1].QuarterTurns = 4;
    TestFalse(TEXT("Unsupported rotation rejected"), UDungeonLayoutLibrary::ValidateLayout(Profile, Bad, Error));
    Bad = Layout;
    Bad.Rooms[1].Level = Layout.Rooms[2].Level;
    TestFalse(TEXT("Template asset substitution rejected"), UDungeonLayoutLibrary::ValidateLayout(Profile, Bad, Error));
    Bad = Layout;
    Bad.Connections.RemoveAt(1);
    TestFalse(TEXT("Disconnected goal rejected"), UDungeonLayoutLibrary::ValidateLayout(Profile, Bad, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonBarrierRules, "test_25d.DungeonGeneration.ReadinessBarrier",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.ReadinessBarrier 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonBarrierRules::RunTest(const FString&)
{
    const FGuid Generation(1, 2, 3, 4);
    FDungeonReadyBarrier Barrier;
    Barrier.Reset(Generation, TEXT("layout-a"), 2);
    TestFalse(TEXT("No clients cannot release gate"), Barrier.IsReady());
    TestFalse(TEXT("Unknown identity rejected"), Barrier.Acknowledge(-1, Generation, TEXT("layout-a")));
    TestFalse(TEXT("Old generation rejected"), Barrier.Acknowledge(1, FGuid(4, 3, 2, 1), TEXT("layout-a")));
    TestFalse(TEXT("Wrong layout rejected"), Barrier.Acknowledge(1, Generation, TEXT("layout-b")));
    TestTrue(TEXT("First player accepted"), Barrier.Acknowledge(1, Generation, TEXT("layout-a")));
    Barrier.Acknowledge(1, Generation, TEXT("layout-a"));
    TestEqual(TEXT("Duplicate is counted once"), Barrier.NumReady(), 1);
    TestFalse(TEXT("One player cannot release two-player gate"), Barrier.IsReady());
    TestTrue(TEXT("Second player accepted"), Barrier.Acknowledge(2, Generation, TEXT("layout-a")));
    TestTrue(TEXT("Both players release gate"), Barrier.IsReady());
    Barrier.Reset(FGuid(5, 6, 7, 8), TEXT("layout-c"), 2);
    TestFalse(TEXT("Next generation discards old acknowledgements"), Barrier.IsReady());
    TestEqual(TEXT("New generation has no players ready"), Barrier.NumReady(), 0);
    return true;
}
#endif
