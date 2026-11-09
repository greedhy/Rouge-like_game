// 调用链：导演初始化布局 → 建立房间/等级 → 遭遇账本推进 → FinishEncounter 唯一清房结果。
#include "Roguelike/DungeonRunModel.h"
#include "Equipment/SharedLootSubsystem.h"

bool FDungeonEncounterLedger::Start(int32 Waves)
{
    if (Waves < 1 || Waves > 16 || (bStarted && !IsClear() && Error.IsEmpty()))
        return false;
    *this = FDungeonEncounterLedger();
    bStarted = true;
    PendingWaves = Waves;
    return true;
}

int32 FDungeonEncounterLedger::NumRequired() const
{
    int32 Count = 0;
    for (const auto& Pair : Living)
        Count += Pair.Value ? 1 : 0;
    return Count;
}

bool FDungeonEncounterLedger::CanSpawnNextWave() const
{
    return bStarted && Error.IsEmpty() && !bSpawning && PendingWaves > 0 && NumRequired() == 0;
}

bool FDungeonEncounterLedger::BeginWave()
{
    if (!CanSpawnNextWave())
        return false;
    bSpawning = true;
    WaveRegistrations = 0;
    --PendingWaves;
    ++Wave;
    return true;
}

bool FDungeonEncounterLedger::RegisterTarget(FName Id, bool bRequired)
{
    if (!bStarted || !Error.IsEmpty() || Id.IsNone() || Registered.Contains(Id) ||
        (!bSpawning && (Wave == 0 || IsClear())))
        return false;
    Registered.Add(Id);
    Living.Add(Id, bRequired);
    if (bSpawning)
        ++WaveRegistrations;
    return true;
}

bool FDungeonEncounterLedger::EndWave(bool bSpawnSucceeded)
{
    if (!bSpawning)
        return false;
    bSpawning = false;
    if (!bSpawnSucceeded || WaveRegistrations == 0)
    {
        Error = bSpawnSucceeded ? TEXT("EmptyWave") : TEXT("WaveSpawnFailed");
        return false;
    }
    return Error.IsEmpty();
}

bool FDungeonEncounterLedger::ResolveTarget(FName Id, EDungeonTargetResolution Reason)
{
    const bool* Required = Living.Find(Id);
    if (!Required)
        return false;
    const bool bRequired = *Required;
    Living.Remove(Id);
    if (Reason == EDungeonTargetResolution::Unexpected || (bRequired && Reason != EDungeonTargetResolution::Killed))
        Error = TEXT("TargetRemovedWithoutKill:") + Id.ToString();
    return true;
}

bool FDungeonEncounterLedger::IsClear() const
{
    return bStarted && Wave > 0 && !bSpawning && PendingWaves == 0 && NumRequired() == 0 && Error.IsEmpty();
}

bool FDungeonRunModel::IsCombat(EDungeonRoomRole Role)
{
    return Role == EDungeonRoomRole::Combat || Role == EDungeonRoomRole::Elite || Role == EDungeonRoomRole::Boss;
}

bool FDungeonRunModel::Initialize(const FDungeonLayout& InLayout)
{
    *this = FDungeonRunModel();
    const int32 Count = InLayout.Rooms.Num();
    auto Invalid = [this]()
    {
        Error = TEXT("InvalidRunGraph");
        return false;
    };
    if (Count < 1 || Count > 64 || InLayout.Connections.Num() != Count - 1 ||
        InLayout.Rooms[0].Role != EDungeonRoomRole::Entrance)
        return Invalid();
    TSet<FName> Ids;
    TSet<FIntPoint> Cells, Edges;
    TArray<TArray<int32>> Adjacent;
    Adjacent.SetNum(Count);
    for (const auto& R : InLayout.Rooms)
    {
        if (R.RoomId.IsNone() || Ids.Contains(R.RoomId) || Cells.Contains(R.Cell) ||
            FMath::Abs(int64(R.Cell.X)) > 32768 || FMath::Abs(int64(R.Cell.Y)) > 32768)
            return Invalid();
        Ids.Add(R.RoomId);
        Cells.Add(R.Cell);
    }
    for (const auto& E : InLayout.Connections)
    {
        if (!InLayout.Rooms.IsValidIndex(E.From) || !InLayout.Rooms.IsValidIndex(E.To) || E.From == E.To)
            return Invalid();
        const FIntPoint Key(FMath::Min(E.From, E.To), FMath::Max(E.From, E.To));
        const FIntPoint D = InLayout.Rooms[E.From].Cell - InLayout.Rooms[E.To].Cell;
        if (Edges.Contains(Key) || FMath::Abs(D.X) + FMath::Abs(D.Y) != 1)
            return Invalid();
        Edges.Add(Key);
        Adjacent[E.From].Add(E.To);
        Adjacent[E.To].Add(E.From);
    }
    Parents.Init(INDEX_NONE, Count);
    Parents[0] = 0;
    TArray<int32> Queue{0};
    for (int32 I = 0; I < Queue.Num(); ++I)
        for (int32 Next : Adjacent[Queue[I]])
            if (Parents[Next] == INDEX_NONE)
            {
                Parents[Next] = Queue[I];
                Queue.Add(Next);
            }
    if (Queue.Num() != Count)
        return Invalid();
    Layout = InLayout;
    for (const auto& R : Layout.Rooms)
    {
        auto& S = Rooms.AddDefaulted_GetRef();
        S.RoomId = R.RoomId;
        S.Role = R.Role;
    }
    // BFS 顺序保证父房间先处理；分支房间按深度而非数组序号定级。
    TArray<int32> Depths;
    Depths.Init(0, Count);
    for (int32 Index : Queue)
    {
        if (Index != 0)
        {
            Depths[Index] = Depths[Parents[Index]] + 1;
        }
        Rooms[Index].RoomLevel = GetDefault<USharedLootSettings>()->RoomLevel(Depths[Index]);
    }
    Rooms[0].Phase = EDungeonRoomPhase::Cleared;
    Rooms[0].bVisited = true;
    return true;
}

int32 FDungeonRunModel::Observe(const TArray<int32>& PlayerRooms, const TArray<bool>& Safe)
{
    ReadyRoom = INDEX_NONE;
    if (!Error.IsEmpty() || Rooms.IsEmpty() || ActiveRoom != INDEX_NONE || PlayerRooms.Num() != Safe.Num())
        return INDEX_NONE;
    for (int32 Room : PlayerRooms)
    {
        if (!Rooms.IsValidIndex(Room))
            continue;
        auto& S = Rooms[Room];
        S.bVisited = true;
        if (S.Phase == EDungeonRoomPhase::Hidden)
            S.Phase = EDungeonRoomPhase::Discovered;
        if (!IsCombat(S.Role))
            S.Phase = EDungeonRoomPhase::Cleared;
        for (const auto& Edge : Layout.Connections)
        {
            const int32 Other = Edge.From == Room ? Edge.To : Edge.To == Room ? Edge.From : INDEX_NONE;
            if (Rooms.IsValidIndex(Other) && Rooms[Other].Phase == EDungeonRoomPhase::Hidden)
                Rooms[Other].Phase = EDungeonRoomPhase::Discovered;
        }
    }
    if (GatheringRoom != INDEX_NONE && !PlayerRooms.Contains(GatheringRoom))
    {
        Rooms[GatheringRoom].Phase = EDungeonRoomPhase::Discovered;
        GatheringRoom = INDEX_NONE;
    }
    if (GatheringRoom == INDEX_NONE)
        for (int32 Room : PlayerRooms)
            if (Rooms.IsValidIndex(Room) && IsCombat(Rooms[Room].Role) &&
                Rooms[Room].Phase == EDungeonRoomPhase::Discovered &&
                (GatheringRoom == INDEX_NONE || Room < GatheringRoom))
                GatheringRoom = Room;
    if (GatheringRoom == INDEX_NONE)
        return INDEX_NONE;
    Rooms[GatheringRoom].Phase = EDungeonRoomPhase::Gathering;
    bool AllReady = !PlayerRooms.IsEmpty();
    for (int32 I = 0; I < PlayerRooms.Num(); ++I)
        AllReady &= PlayerRooms[I] == GatheringRoom && Safe[I];
    if (AllReady)
        ReadyRoom = GatheringRoom;
    return ReadyRoom;
}

bool FDungeonRunModel::BeginEncounter(int32 Room, int32 Waves)
{
    if (!Error.IsEmpty() || Room != ReadyRoom || Room != GatheringRoom || !Rooms.IsValidIndex(Room) ||
        ActiveRoom != INDEX_NONE || !IsCombat(Rooms[Room].Role) || !Encounter.Start(Waves))
        return false;
    ActiveRoom = Room;
    GatheringRoom = ReadyRoom = INDEX_NONE;
    Rooms[Room].Phase = EDungeonRoomPhase::Active;
    Rooms[Room].PendingWaves = Waves;
    return true;
}

int32 FDungeonRunModel::FinishEncounter()
{
    if (!Error.IsEmpty() || ActiveRoom == INDEX_NONE || !Encounter.IsClear())
        return INDEX_NONE;
    const int32 Finished = ActiveRoom;
    auto& S = Rooms[Finished];
    S.Phase = EDungeonRoomPhase::Cleared;
    S.RemainingRequired = S.PendingWaves = 0;
    S.Wave = Encounter.Wave;
    ActiveRoom = INDEX_NONE;
    return Finished;
}

void FDungeonRunModel::Fail(const FString& Reason)
{
    if (!Error.IsEmpty())
        return;
    Error = Reason.IsEmpty() ? TEXT("RunFailed") : Reason;
    if (Rooms.IsValidIndex(ActiveRoom))
        Rooms[ActiveRoom].Phase = EDungeonRoomPhase::Failed;
    else if (Rooms.IsValidIndex(GatheringRoom))
        Rooms[GatheringRoom].Phase = EDungeonRoomPhase::Failed;
    ReadyRoom = INDEX_NONE;
}

bool FDungeonRunModel::CanCross(int32 A, int32 B) const
{
    if (!Error.IsEmpty() || !Rooms.IsValidIndex(A) || !Rooms.IsValidIndex(B) || A == B || ActiveRoom == A ||
        ActiveRoom == B)
        return false;
    int32 Parent = INDEX_NONE;
    if (Parents[A] == B)
        Parent = B;
    else if (Parents[B] == A)
        Parent = A;
    if (Parent == INDEX_NONE)
        return false;
    return !IsCombat(Rooms[Parent].Role) || Rooms[Parent].Phase == EDungeonRoomPhase::Cleared;
}

int32 FDungeonRunModel::FindRoom(const FVector& Point, float Spacing) const
{
    if (Point.ContainsNaN() || !FMath::IsFinite(Spacing) || Spacing < 1.f || FMath::Abs(Point.Z) > 1000 ||
        FMath::Abs(Point.X / Spacing) > 32769 || FMath::Abs(Point.Y / Spacing) > 32769)
        return INDEX_NONE;
    const FIntPoint Cell(FMath::FloorToInt(Point.X / Spacing + .5), FMath::FloorToInt(Point.Y / Spacing + .5));
    return Layout.Rooms.IndexOfByPredicate(
        [&Cell](const FDungeonRoomPlacement& R)
        {
            return R.Cell == Cell;
        });
}

bool FDungeonRunModel::CanMove(const FVector& From, const FVector& To, float Spacing) const
{
    int32 Current = FindRoom(From, Spacing);
    const int32 Destination = FindRoom(To, Spacing);
    if (!Error.IsEmpty() || Current == INDEX_NONE || Destination == INDEX_NONE)
        return false;
    if (Current == Destination)
        return true;
    FIntPoint Cell = Layout.Rooms[Current].Cell;
    const FVector D = To - From;
    const int32 SX = D.X > 0 ? 1 : -1, SY = D.Y > 0 ? 1 : -1;
    const double Inf = TNumericLimits<double>::Max();
    double TX = FMath::Abs(D.X) < UE_SMALL_NUMBER ? Inf : ((Cell.X + SX * .5) * Spacing - From.X) / D.X;
    double TY = FMath::Abs(D.Y) < UE_SMALL_NUMBER ? Inf : ((Cell.Y + SY * .5) * Spacing - From.Y) / D.Y;
    const double DX = FMath::Abs(D.X) < UE_SMALL_NUMBER ? Inf : Spacing / FMath::Abs(D.X);
    const double DY = FMath::Abs(D.Y) < UE_SMALL_NUMBER ? Inf : Spacing / FMath::Abs(D.Y);
    for (int32 Step = 0; Step < 128 && Current != Destination; ++Step)
    {
        // Four-way doors never authorize clipping diagonally through a wall corner.
        if (FMath::IsNearlyEqual(TX, TY, 1.e-9))
            return false;
        if (TX < TY)
        {
            Cell.X += SX;
            TX += DX;
        }
        else
        {
            Cell.Y += SY;
            TY += DY;
        }
        const int32 Next = Layout.Rooms.IndexOfByPredicate(
            [&Cell](const FDungeonRoomPlacement& R)
            {
                return R.Cell == Cell;
            });
        if (!CanCross(Current, Next))
            return false;
        Current = Next;
    }
    return Current == Destination;
}
