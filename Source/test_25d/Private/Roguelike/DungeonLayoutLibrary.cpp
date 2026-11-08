#include "Roguelike/DungeonLayoutLibrary.h"
#include "Roguelike/DungeonGenerationProfile.h"
#include "Misc/PackageName.h"
#include "DungeonGenerationValidation.h"

namespace
{
    bool Fail(FString& Error, const FString& Reason)
    {
        Error = Reason;
        return false;
    }
    bool ValidId(FName Id)
    {
        const FString Text = Id.ToString();
        if (Id.IsNone() || Text.Len() > 64)
            return false;
        for (TCHAR C : Text)
            if (!(C >= 'a' && C <= 'z') && !(C >= 'A' && C <= 'Z') && !(C >= '0' && C <= '9') && C != '_')
                return false;
        return true;
    }
} // namespace

namespace DungeonGeneration
{
    bool CheckProfile(const UDungeonGenerationProfile* Profile, FString& Error)
    {
        if (!Profile)
            return Fail(Error, TEXT("ProfileMissing"));
        if (!FMath::IsFinite(Profile->RoomSpacing) || Profile->RoomSpacing < 1200.f || Profile->RoomSpacing > 20000.f)
            return Fail(Error, TEXT("InvalidRoomSpacing"));
        if (!FMath::IsFinite(Profile->ReadyTimeout) || Profile->ReadyTimeout <= 0.f || Profile->ReadyTimeout > 300.f)
            return Fail(Error, TEXT("InvalidReadyTimeout"));
        if (Profile->Entrance.Role != EDungeonRoomRole::Entrance || !Profile->Entrance.Level.IsNull() ||
            !(Profile->Entrance.AllowedRotations & 1))
            return Fail(Error, TEXT("EntranceMustBePersistentAndUnrotated"));
        if (Profile->Templates.IsEmpty() || Profile->Templates.Num() > 128)
            return Fail(Error, TEXT("InvalidTemplateCount"));
        TSet<FName> Ids;
        auto Check = [&Ids, &Error](const FDungeonRoomTemplate& T, bool Persistent)
        {
            if (!ValidId(T.TemplateId) || Ids.Contains(T.TemplateId))
                return Fail(Error, TEXT("InvalidOrDuplicateTemplateId"));
            Ids.Add(T.TemplateId);
            if (T.DoorMask < 1 || T.DoorMask > 15 || T.AllowedRotations < 1 || T.AllowedRotations > 15)
                return Fail(Error, FString::Printf(TEXT("InvalidPortsOrRotations:%s"), *T.TemplateId.ToString()));
            if (!Persistent && (T.Level.IsNull() ||
                                !FPackageName::IsValidLongPackageName(T.Level.ToSoftObjectPath().GetLongPackageName())))
                return Fail(Error, FString::Printf(TEXT("TemplateLevelMissing:%s"), *T.TemplateId.ToString()));
            return true;
        };
        if (!Check(Profile->Entrance, true))
            return false;
        bool Combat = false, Boss = false;
        for (const auto& T : Profile->Templates)
        {
            if (!Check(T, false) || T.Role == EDungeonRoomRole::Entrance)
                return Fail(Error, Error.IsEmpty() ? TEXT("DuplicateEntranceRole") : Error);
            Combat |= T.Role == EDungeonRoomRole::Combat;
            Boss |= T.Role == EDungeonRoomRole::Boss;
        }
        return Combat && Boss ? true : Fail(Error, TEXT("ProbeRequiresCombatAndBossTemplates"));
    }
} // namespace DungeonGeneration

FIntPoint UDungeonLayoutLibrary::Direction(int32 Index)
{
    static const FIntPoint Directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    return Index >= 0 && Index < 4 ? Directions[Index] : FIntPoint::ZeroValue;
}
int32 UDungeonLayoutLibrary::DirectionIndex(FIntPoint Delta)
{
    for (int32 I = 0; I < 4; ++I)
        if (Direction(I) == Delta)
            return I;
    return INDEX_NONE;
}
int32 UDungeonLayoutLibrary::RotateDoorMask(int32 Mask, int32 Turns)
{
    return Turns >= 0 && Turns < 4 ? ((Mask << Turns) | (Mask >> (4 - Turns))) & 15 : 0;
}

bool UDungeonLayoutLibrary::BuildAssemblyProbe(const UDungeonGenerationProfile* Profile, int32 Seed,
                                               FDungeonLayout& OutLayout, FString& OutError)
{
    OutLayout = FDungeonLayout();
    OutError.Reset();
    if (!DungeonGeneration::CheckProfile(Profile, OutError))
        return false;
    TArray<FDungeonRoomTemplate> Templates = Profile->Templates;
    Templates.Sort(
        [](const auto& A, const auto& B)
        {
            return A.TemplateId.LexicalLess(B.TemplateId);
        });
    FRandomStream Geometry(Seed);
    FRandomStream Selection(static_cast<int32>(static_cast<uint32>(Seed) ^ 0x51ED270Bu));
    TArray<FIntPoint> Paths;
    for (int32 A = 0; A < 4; ++A)
        for (int32 B = 0; B < 4; ++B)
            if (B != (A + 2) % 4)
                Paths.Add({A, B});
    for (int32 I = Paths.Num() - 1; I > 0; --I)
        Paths.Swap(I, Geometry.RandRange(0, I));
    for (FIntPoint Path : Paths)
    {
        const int32 Required[] = {1 << Path.X, (1 << ((Path.X + 2) % 4)) | (1 << Path.Y), 1 << ((Path.Y + 2) % 4)};
        if ((Profile->Entrance.DoorMask & Required[0]) != Required[0])
            continue;
        FDungeonLayout Candidate;
        Candidate.Seed = Seed;
        Candidate.Rooms.SetNum(3);
        auto& Entry = Candidate.Rooms[0];
        Entry.RoomId = TEXT("R00");
        Entry.TemplateId = Profile->Entrance.TemplateId;
        Entry.Role = EDungeonRoomRole::Entrance;
        Entry.bPersistent = true;
        bool Fits = true;
        for (int32 I = 1; I < 3; ++I)
        {
            auto& Room = Candidate.Rooms[I];
            Room.RoomId = FName(*FString::Printf(TEXT("R%02d"), I));
            Room.Role = I == 1 ? EDungeonRoomRole::Combat : EDungeonRoomRole::Boss;
            Room.Cell = Candidate.Rooms[I - 1].Cell + Direction(I == 1 ? Path.X : Path.Y);
            TArray<FIntPoint> Options;
            for (int32 T = 0; T < Templates.Num(); ++T)
                for (int32 Rotation = 0; Rotation < 4; ++Rotation)
                    if (Templates[T].Role == Room.Role && (Templates[T].AllowedRotations & (1 << Rotation)) &&
                        (RotateDoorMask(Templates[T].DoorMask, Rotation) & Required[I]) == Required[I])
                        Options.Add({T, Rotation});
            if (Options.IsEmpty())
            {
                Fits = false;
                break;
            }
            const auto Choice = Options[Selection.RandRange(0, Options.Num() - 1)];
            Room.TemplateId = Templates[Choice.X].TemplateId;
            Room.Level = Templates[Choice.X].Level;
            Room.QuarterTurns = Choice.Y;
            auto& Edge = Candidate.Connections.AddDefaulted_GetRef();
            Edge.From = I - 1;
            Edge.To = I;
        }
        if (Fits && ValidateLayout(Profile, Candidate, OutError))
        {
            OutLayout = MoveTemp(Candidate);
            OutError.Reset();
            return true;
        }
    }
    return Fail(OutError, TEXT("NoCompatibleThreeRoomAssembly"));
}

bool UDungeonLayoutLibrary::ValidateLayout(const UDungeonGenerationProfile* Profile, const FDungeonLayout& Layout,
                                           FString& OutError)
{
    OutError.Reset();
    if (!DungeonGeneration::CheckProfile(Profile, OutError))
        return false;
    if ((Layout.Version != 1 && Layout.Version != 2) || Layout.Rooms.Num() < 2 || Layout.Rooms.Num() > 128 ||
        Layout.Connections.Num() != Layout.Rooms.Num() - 1)
        return Fail(OutError, TEXT("InvalidLayoutVersionOrTreeSize"));
    TSet<FIntPoint> Cells;
    TSet<FName> RoomIds;
    TArray<int32> Masks;
    for (int32 I = 0; I < Layout.Rooms.Num(); ++I)
    {
        const auto& Room = Layout.Rooms[I];
        if (!ValidId(Room.RoomId) || RoomIds.Contains(Room.RoomId) || Cells.Contains(Room.Cell) || Room.Cell.X < -128 ||
            Room.Cell.X > 128 || Room.Cell.Y < -128 || Room.Cell.Y > 128 || Room.QuarterTurns < 0 ||
            Room.QuarterTurns > 3)
            return Fail(OutError, TEXT("InvalidRoomIdentityCellOrRotation"));
        Cells.Add(Room.Cell);
        RoomIds.Add(Room.RoomId);
        const auto* Template = I == 0 ? &Profile->Entrance
                                      : Profile->Templates.FindByPredicate(
                                            [&](const auto& T)
                                            {
                                                return T.TemplateId == Room.TemplateId;
                                            });
        if (!Template || Template->TemplateId != Room.TemplateId || Template->Level != Room.Level ||
            Template->Role != Room.Role || !(Template->AllowedRotations & (1 << Room.QuarterTurns)) ||
            Room.bPersistent != (I == 0))
            return Fail(OutError, TEXT("RoomTemplateMismatch"));
        if (I == 0 && (Room.Cell != FIntPoint::ZeroValue || Room.QuarterTurns != 0))
            return Fail(OutError, TEXT("EntranceMoved"));
        Masks.Add(RotateDoorMask(Template->DoorMask, Room.QuarterTurns));
    }
    TSet<FIntPoint> UniqueEdges;
    TArray<TArray<int32>> Adjacent;
    Adjacent.SetNum(Layout.Rooms.Num());
    for (const auto& Edge : Layout.Connections)
    {
        if (!Layout.Rooms.IsValidIndex(Edge.From) || !Layout.Rooms.IsValidIndex(Edge.To) || Edge.From == Edge.To)
            return Fail(OutError, TEXT("InvalidEdgeIndex"));
        const FIntPoint Key(FMath::Min(Edge.From, Edge.To), FMath::Max(Edge.From, Edge.To));
        if (UniqueEdges.Contains(Key))
            return Fail(OutError, TEXT("DuplicateEdge"));
        UniqueEdges.Add(Key);
        const int32 D = DirectionIndex(Layout.Rooms[Edge.To].Cell - Layout.Rooms[Edge.From].Cell);
        if (D == INDEX_NONE || !(Masks[Edge.From] & (1 << D)) || !(Masks[Edge.To] & (1 << ((D + 2) % 4))))
            return Fail(OutError, TEXT("NonAdjacentOrIncompatibleDoors"));
        Adjacent[Edge.From].Add(Edge.To);
        Adjacent[Edge.To].Add(Edge.From);
    }
    TSet<int32> Reached;
    TArray<int32> Queue{0};
    Reached.Add(0);
    for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
        for (int32 Next : Adjacent[Queue[Cursor]])
            if (!Reached.Contains(Next))
            {
                Reached.Add(Next);
                Queue.Add(Next);
            }
    return Reached.Num() == Layout.Rooms.Num() ? true : Fail(OutError, TEXT("DisconnectedLayout"));
}

FString UDungeonLayoutLibrary::CanonicalLayout(const FDungeonLayout& Layout)
{
    FString Result = FString::Printf(TEXT("v%d|seed=%d"), Layout.Version, Layout.Seed);
    if (Layout.Version == 2)
        Result += FString::Printf(TEXT("|main=%d"), Layout.MainPathLength);
    for (const auto& R : Layout.Rooms)
    {
        Result += FString::Printf(TEXT("|%s:%d:%s:%s:%d,%d:%d:%d"), *R.RoomId.ToString(), static_cast<int32>(R.Role),
                                  *R.TemplateId.ToString(), *R.Level.ToSoftObjectPath().ToString(), R.Cell.X, R.Cell.Y,
                                  R.QuarterTurns, R.bPersistent ? 1 : 0);
        if (Layout.Version == 2)
            Result += FString::Printf(TEXT(":main=%d:branch=%d"), R.MainPathIndex, R.BranchId);
    }
    for (const auto& E : Layout.Connections)
        Result += FString::Printf(TEXT("|edge=%d,%d"), E.From, E.To);
    return Result;
}

void FDungeonReadyBarrier::Reset(FGuid InGeneration, const FString& InDigest, int32 InExpected)
{
    Generation = InGeneration;
    Digest = InDigest;
    Expected = InExpected;
    ReadyPlayers.Reset();
}
bool FDungeonReadyBarrier::Acknowledge(int32 PlayerId, FGuid InGeneration, const FString& InDigest)
{
    if (PlayerId < 0 || !Generation.IsValid() || InGeneration != Generation || Digest.IsEmpty() || InDigest != Digest ||
        Expected < 1 || Expected > 8)
        return false;
    ReadyPlayers.Add(PlayerId);
    return true;
}
bool FDungeonReadyBarrier::IsReady() const
{
    return Generation.IsValid() && !Digest.IsEmpty() && Expected > 0 && Expected <= 8 && ReadyPlayers.Num() >= Expected;
}
