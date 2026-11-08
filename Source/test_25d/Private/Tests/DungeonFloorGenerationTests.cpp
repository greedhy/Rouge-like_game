/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.DungeonGeneration.Floor.ThousandSeeds
 * - test_25d.DungeonGeneration.Floor.Repeatability
 * - test_25d.DungeonGeneration.Floor.BoundedFallback
 * - test_25d.DungeonGeneration.Floor.RejectTampering
 * - test_25d.DungeonGeneration.Floor.PortsAndDiversity
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Roguelike/DungeonGenerationProfile.h"
#include "Roguelike/DungeonLayoutLibrary.h"
#include "Algo/Reverse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // 构造包含主路和分支角色的测试模板目录，软引用只用于生成规则验证。
    UDungeonGenerationProfile* FloorProfile()
    {
        auto* Profile = NewObject<UDungeonGenerationProfile>();
        Profile->bGenerateFullFloor = true;
        Profile->Entrance.TemplateId = TEXT("Entrance");
        Profile->Entrance.Role = EDungeonRoomRole::Entrance;
        Profile->Entrance.AllowedRotations = 1;
        auto Add = [&](FString Id, EDungeonRoomRole Role)
        {
            auto& Template = Profile->Templates.AddDefaulted_GetRef();
            Template.TemplateId = FName(*Id);
            Template.Role = Role;
            Template.Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/QA/") + Id + TEXT(".") + Id));
        };
        for (int32 I = 0; I < 5; ++I)
            Add(FString::Printf(TEXT("Combat%d"), I), EDungeonRoomRole::Combat);
        Add(TEXT("Treasure"), EDungeonRoomRole::Treasure);
        Add(TEXT("Elite"), EDungeonRoomRole::Elite);
        Add(TEXT("Merchant"), EDungeonRoomRole::Merchant);
        Add(TEXT("Rest"), EDungeonRoomRole::Rest);
        Add(TEXT("Boss"), EDungeonRoomRole::Boss);
        return Profile;
    }
    // 无向边查询，用于独立核对主路连续性。
    bool HasEdge(const FDungeonLayout& Layout, int32 A, int32 B)
    {
        return Layout.Connections.ContainsByPredicate(
            [&](const auto& Edge)
            {
                return (Edge.From == A && Edge.To == B) || (Edge.From == B && Edge.To == A);
            });
    }
    // 独立重算主路顺序、分支数量、格点唯一性及边结构，不调用生产Validate作为自己的预期。
    bool IndependentRules(const FDungeonLayout& L)
    {
        const int32 Main = L.MainPathLength;
        if (L.Version != 2 || Main < 10 || Main > 12 || L.Rooms.Num() - Main < 4 || L.Rooms.Num() - Main > 6 ||
            L.Connections.Num() != L.Rooms.Num() - 1)
            return false;
        int32 Combat = 0, Streak = 0, TreasureAt = -1, EliteAt = -1, MerchantAt = -1, AllTreasure = 0, AllElite = 0;
        TMap<int32, TArray<int32>> Branches;
        TSet<FIntPoint> Cells;
        for (int32 I = 0; I < L.Rooms.Num(); ++I)
        {
            const auto& R = L.Rooms[I];
            if (Cells.Contains(R.Cell) || FMath::Abs(R.Cell.X) > 12 || FMath::Abs(R.Cell.Y) > 12)
                return false;
            Cells.Add(R.Cell);
            AllTreasure += R.Role == EDungeonRoomRole::Treasure;
            AllElite += R.Role == EDungeonRoomRole::Elite;
            if (I < Main)
            {
                if (R.MainPathIndex != I || R.BranchId != -1 || (I > 0 && !HasEdge(L, I - 1, I)))
                    return false;
                const bool Fighting = R.Role == EDungeonRoomRole::Combat || R.Role == EDungeonRoomRole::Elite;
                Streak = Fighting ? Streak + 1 : 0;
                if (Streak > 3)
                    return false;
                Combat += R.Role == EDungeonRoomRole::Combat;
                if (R.Role == EDungeonRoomRole::Treasure)
                {
                    if (TreasureAt >= 0)
                        return false;
                    TreasureAt = I;
                }
                if (R.Role == EDungeonRoomRole::Elite)
                {
                    if (EliteAt >= 0 || Combat < 3)
                        return false;
                    EliteAt = I;
                }
                if (R.Role == EDungeonRoomRole::Merchant)
                {
                    if (MerchantAt >= 0)
                        return false;
                    MerchantAt = I;
                }
            }
            else
            {
                if (R.MainPathIndex != -1 || R.BranchId < 0)
                    return false;
                Branches.FindOrAdd(R.BranchId).Add(I);
            }
        }
        if (L.Rooms[0].Role != EDungeonRoomRole::Entrance || !L.Rooms[0].bPersistent ||
            L.Rooms[0].Cell != FIntPoint::ZeroValue || L.Rooms[1].Role != EDungeonRoomRole::Combat ||
            L.Rooms[2].Role != EDungeonRoomRole::Combat || L.Rooms[Main - 2].Role != EDungeonRoomRole::Rest ||
            L.Rooms[Main - 1].Role != EDungeonRoomRole::Boss || Combat < 4 || Combat > 6 || TreasureAt < 3 ||
            TreasureAt >= EliteAt || EliteAt >= MerchantAt || MerchantAt >= Main - 2 || AllTreasure < 2 ||
            AllTreasure > 3 || AllElite < 1 || AllElite > 2 || Branches.Num() < 2 || Branches.Num() > 3)
            return false;
        for (const auto& Branch : Branches)
        {
            if (Branch.Value.Num() < 1 || Branch.Value.Num() > 3)
                return false;
            int32 Attachments = 0, Leaves = 0;
            for (int32 I : Branch.Value)
            {
                int32 Degree = 0;
                for (const auto& E : L.Connections)
                {
                    const int32 Other = E.From == I ? E.To : E.To == I ? E.From : -1;
                    if (Other < 0)
                        continue;
                    ++Degree;
                    if (Other < Main)
                        ++Attachments;
                    else if (L.Rooms[Other].BranchId != Branch.Key)
                        return false;
                }
                if (Degree == 1)
                {
                    ++Leaves;
                    if (L.Rooms[I].Role != EDungeonRoomRole::Treasure && L.Rooms[I].Role != EDungeonRoomRole::Elite)
                        return false;
                }
                else if (Degree != 2 || L.Rooms[I].Role != EDungeonRoomRole::Combat)
                    return false;
            }
            if (Attachments != 1 || Leaves != 1)
                return false;
        }
        return true;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorSeedRules, "test_25d.DungeonGeneration.Floor.ThousandSeeds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.Floor.ThousandSeeds 回归，失败信息由下方断言交给自动化框架收集。 */
bool FFloorSeedRules::RunTest(const FString&)
{
    auto* Profile = FloorProfile();
    TSet<FString> Layouts, Graphs, Geometry, Roles;
    TArray<double> Times;
    int32 Fallbacks = 0;
    for (int32 Seed = 0; Seed < 1000; ++Seed)
    {
        FDungeonLayout L;
        FString Error;
        const double Start = FPlatformTime::Seconds();
        if (!TestTrue(FString::Printf(TEXT("Seed %d generates"), Seed),
                      UDungeonLayoutLibrary::BuildFloor(Profile, Seed, L, Error)))
        {
            AddError(Error);
            return false;
        }
        Times.Add((FPlatformTime::Seconds() - Start) * 1000);
        if (!TestTrue(TEXT("Independent mission, branch and occupancy rules"), IndependentRules(L)))
            return false;
        if (!TestTrue(TEXT("Production validator accepts generated floor"),
                      UDungeonLayoutLibrary::ValidateFloor(Profile, L, Error)))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("Finite search attempts"), L.PlacementAttempts <= Profile->FloorSettings.MaxPlacementAttempts);
        TestTrue(TEXT("Finite search steps"), L.SearchSteps <= Profile->FloorSettings.MaxPlacementAttempts *
                                                                   Profile->FloorSettings.MaxSearchStepsPerAttempt);
        Fallbacks += L.bUsedFallback;
        if (Seed < 100)
        {
            L.Seed = 0;
            Layouts.Add(UDungeonLayoutLibrary::CanonicalLayout(L));
            FString G, S, R;
            for (const auto& Room : L.Rooms)
            {
                G += FString::Printf(TEXT("%d:%s;"), int32(Room.Role), *Room.TemplateId.ToString());
                S += FString::Printf(TEXT("%d,%d;"), Room.Cell.X, Room.Cell.Y);
                R += FString::Printf(TEXT("%d;"), int32(Room.Role));
            }
            for (const auto& E : L.Connections)
                G += FString::Printf(TEXT("%d-%d;"), E.From, E.To);
            Graphs.Add(G);
            Geometry.Add(S);
            Roles.Add(R);
        }
    }
    Times.Sort();
    const double P95 = Times[949];
    TestTrue(TEXT("Normal library fallback rate <=1%"), Fallbacks <= 10);
    TestTrue(TEXT("100-seed normalized layout uniqueness >=90%"), Layouts.Num() >= 90);
    TestTrue(TEXT("Mission orders vary"), Roles.Num() >= 5);
    TestTrue(TEXT("Template graph and geometry both vary"), Graphs.Num() >= 90 && Geometry.Num() >= 90);
    const FString Report = FString::Printf(
        TEXT(
            "{\"seeds\":1000,\"fallbacks\":%d,\"unique_layouts_100\":%d,\"unique_graphs_100\":%d,\"unique_geometry_100\":%d,\"unique_role_orders_100\":%d,\"generation_p95_ms\":%.6f}"),
        Fallbacks, Layouts.Num(), Graphs.Num(), Geometry.Num(), Roles.Num(), P95);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("RoguelikeGeneration");
    IFileManager::Get().MakeDirectory(*Directory, true);
    FFileHelper::SaveStringToFile(Report, *(Directory / TEXT("floor-seed-metrics.json")));
    AddInfo(Report);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorDeterminism, "test_25d.DungeonGeneration.Floor.Repeatability",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.Floor.Repeatability 回归，失败信息由下方断言交给自动化框架收集。 */
bool FFloorDeterminism::RunTest(const FString&)
{
    auto* Profile = FloorProfile();
    for (int32 Seed : {0, 1, 2, 42, 137, -1, MIN_int32, MAX_int32})
    {
        FDungeonLayout A;
        FString Error;
        if (!TestTrue(TEXT("Fixture generated"), UDungeonLayoutLibrary::BuildFloor(Profile, Seed, A, Error)))
            return false;
        const FString Expected = UDungeonLayoutLibrary::CanonicalLayout(A);
        for (int32 Repeat = 0; Repeat < 10; ++Repeat)
        {
            Algo::Reverse(Profile->Templates);
            FDungeonLayout B;
            if (!TestTrue(TEXT("Reversed pool generates"), UDungeonLayoutLibrary::BuildFloor(Profile, Seed, B, Error)))
                return false;
            TestEqual(TEXT("Stable across calls and pool ordering"), UDungeonLayoutLibrary::CanonicalLayout(B),
                      Expected);
        }
        A.SearchSteps += 17;
        A.PlacementAttempts += 3;
        A.bUsedFallback = !A.bUsedFallback;
        TestEqual(TEXT("Diagnostics excluded from normalized layout"), UDungeonLayoutLibrary::CanonicalLayout(A),
                  Expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorFailureBudget, "test_25d.DungeonGeneration.Floor.BoundedFallback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.Floor.BoundedFallback 回归，失败信息由下方断言交给自动化框架收集。 */
bool FFloorFailureBudget::RunTest(const FString&)
{
    auto* Profile = FloorProfile();
    FDungeonLayout L;
    FString Error;
    Profile->FloorSettings.MaxSearchStepsPerAttempt = 1;
    Profile->FloorSettings.MaxPlacementAttempts = 1;
    if (!TestTrue(TEXT("Exhausted random search uses legal fallback"),
                  UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error)))
        return false;
    TestTrue(TEXT("Fallback disclosed"), L.bUsedFallback);
    TestTrue(TEXT("Fallback respects mission rules"), IndependentRules(L));
    TestTrue(TEXT("Fallback search remains bounded"), L.SearchSteps <= 1 && L.PlacementAttempts <= 1);
    const FString Expected = UDungeonLayoutLibrary::CanonicalLayout(L);
    UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error);
    TestEqual(TEXT("Fallback deterministic"), UDungeonLayoutLibrary::CanonicalLayout(L), Expected);
    Profile->FloorSettings.bAllowFallback = false;
    TestFalse(TEXT("Budget exhaustion fails without fallback"),
              UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error));
    TestTrue(TEXT("No partial successful layout on failure"), L.Rooms.IsEmpty());
    TestFalse(TEXT("Specific failure reason"), Error.IsEmpty());
    Profile = FloorProfile();
    for (auto& T : Profile->Templates)
    {
        T.DoorMask = 1;
        T.AllowedRotations = 1;
    }
    TestFalse(TEXT("Impossible ports cannot be rescued by invalid fallback"),
              UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error));
    Profile = FloorProfile();
    Profile->Templates.RemoveAll(
        [](const auto& T)
        {
            return T.Role == EDungeonRoomRole::Merchant;
        });
    TestFalse(TEXT("Required role missing"), UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error));
    Profile = FloorProfile();
    Profile->FloorSettings.MainRoomsMin = 13;
    TestFalse(TEXT("Invalid range rejected"), UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error));
    Profile = FloorProfile();
    Profile->FloorSettings.MaxPlacementAttempts = 0;
    TestFalse(TEXT("Zero budget rejected"), UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorTampering, "test_25d.DungeonGeneration.Floor.RejectTampering",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.Floor.RejectTampering 回归，失败信息由下方断言交给自动化框架收集。 */
bool FFloorTampering::RunTest(const FString&)
{
    auto* Profile = FloorProfile();
    FDungeonLayout L;
    FString Error;
    if (!TestTrue(TEXT("Valid fixture"), UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error)))
        return false;
    auto Bad = L;
    Bad.Rooms[1].MainPathIndex = -1;
    TestFalse(TEXT("Cannot remove main membership"), UDungeonLayoutLibrary::ValidateFloor(Profile, Bad, Error));
    Bad = L;
    Bad.Rooms.Last().BranchId = 99;
    TestFalse(TEXT("Cannot detach a branch node"), UDungeonLayoutLibrary::ValidateFloor(Profile, Bad, Error));
    Bad = L;
    Bad.Connections[1].To = L.MainPathLength;
    TestFalse(TEXT("Cannot bypass main chain"), UDungeonLayoutLibrary::ValidateFloor(Profile, Bad, Error));
    Bad = L;
    Bad.Rooms[1].Cell = Bad.Rooms[0].Cell;
    TestFalse(TEXT("Cannot overlap"), UDungeonLayoutLibrary::ValidateFloor(Profile, Bad, Error));
    Bad = L;
    Bad.MainPathLength -= 1;
    TestFalse(TEXT("Cannot redefine main extent"), UDungeonLayoutLibrary::ValidateFloor(Profile, Bad, Error));
    Bad = L;
    Bad.Rooms[1].Role = EDungeonRoomRole::Elite;
    TestFalse(TEXT("Cannot promote early combat to elite"), UDungeonLayoutLibrary::ValidateFloor(Profile, Bad, Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorPortsAndDiversity, "test_25d.DungeonGeneration.Floor.PortsAndDiversity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.DungeonGeneration.Floor.PortsAndDiversity 回归，失败信息由下方断言交给自动化框架收集。 */
bool FFloorPortsAndDiversity::RunTest(const FString&)
{
    auto* Profile = FloorProfile();
    FDungeonLayout L;
    FString Error;
    for (auto& Template : Profile->Templates)
        Template.DoorMask = 7;
    for (int32 Seed = 0; Seed < 20; ++Seed)
    {
        if (!TestTrue(TEXT("Rotatable asymmetric ports fit"),
                      UDungeonLayoutLibrary::BuildFloor(Profile, Seed, L, Error)))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("Ports validated"), UDungeonLayoutLibrary::ValidateFloor(Profile, L, Error));
        for (int32 I = 1; I < L.MainPathLength; ++I)
            if (L.Rooms[I].Role == EDungeonRoomRole::Combat && L.Rooms[I - 1].Role == EDungeonRoomRole::Combat)
                TestNotEqual(TEXT("Adjacent fights use distinct eligible templates"), L.Rooms[I].TemplateId,
                             L.Rooms[I - 1].TemplateId);
    }
    Profile = FloorProfile();
    Profile->Templates.RemoveAll(
        [](const auto& T)
        {
            return T.Role == EDungeonRoomRole::Combat && T.TemplateId != FName(TEXT("Combat0"));
        });
    if (!TestTrue(TEXT("Small library relaxes only diversity preference"),
                  UDungeonLayoutLibrary::BuildFloor(Profile, 42, L, Error)))
        return false;
    TestTrue(TEXT("Diversity relaxation is disclosed"), L.bDiversityRelaxed);
    TestTrue(TEXT("Hard mission rules survive relaxation"), IndependentRules(L));
    return true;
}
#endif
