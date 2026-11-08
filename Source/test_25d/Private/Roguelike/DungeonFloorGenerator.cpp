#include "Roguelike/DungeonLayoutLibrary.h"
#include "Roguelike/DungeonGenerationProfile.h"
#include "DungeonGenerationValidation.h"

namespace
{
    using Role = EDungeonRoomRole;
    bool FloorError(FString& Error, const FString& Reason)
    {
        Error = Reason;
        return false;
    }
    bool Fighting(Role Value)
    {
        return Value == Role::Combat || Value == Role::Elite;
    }
    int32 DoorCount(int32 Mask)
    {
        int32 Count = 0;
        for (int32 I = 0; I < 4; ++I)
            Count += (Mask >> I) & 1;
        return Count;
    }
    int32 StreamSeed(int32 Seed, uint32 Domain, int32 Attempt = 0)
    {
        // Unsigned arithmetic makes negative and boundary seeds well-defined.
        uint32 Value = static_cast<uint32>(Seed) ^ Domain;
        Value += static_cast<uint32>(Attempt) * 0x9e3779b9u;
        Value ^= Value >> 16;
        Value *= 0x7feb352du;
        Value ^= Value >> 15;
        return static_cast<int32>(Value);
    }
    template <typename T> void Shuffle(TArray<T>& Values, FRandomStream& Random)
    {
        for (int32 I = Values.Num() - 1; I > 0; --I)
            Values.Swap(I, Random.RandRange(0, I));
    }
    bool CheckFloorProfile(const UDungeonGenerationProfile* Profile, FString& Error)
    {
        if (!DungeonGeneration::CheckProfile(Profile, Error))
            return false;
        if (!FMath::IsNearlyEqual(Profile->RoomSpacing, 2400.f))
            return FloorError(Error, TEXT("FloorRequires2400cmRoomSpacing"));
        const auto& S = Profile->FloorSettings;
        if (S.MainRoomsMin < 10 || S.MainRoomsMax > 12 || S.MainRoomsMin > S.MainRoomsMax || S.SideRoomsMin < 4 ||
            S.SideRoomsMax > 6 || S.SideRoomsMin > S.SideRoomsMax || S.BranchesMin < 2 || S.BranchesMax > 3 ||
            S.BranchesMin > S.BranchesMax)
            return FloorError(Error, TEXT("InvalidFloorRoomRanges"));
        if (S.GridRadius < 1 || S.GridRadius > 32 || S.MaxPlacementAttempts < 1 || S.MaxPlacementAttempts > 64 ||
            S.MaxSearchStepsPerAttempt < 1 || S.MaxSearchStepsPerAttempt > 100000)
            return FloorError(Error, TEXT("InvalidFloorSearchBudgetOrBounds"));
        bool Present[7] = {};
        for (const auto& T : Profile->Templates)
        {
            const int32 Index = static_cast<int32>(T.Role);
            if (Index <= 0 || Index >= 7)
                return FloorError(Error, TEXT("InvalidFloorTemplateRole"));
            Present[Index] = true;
        }
        for (int32 I = 1; I < 7; ++I)
            if (!Present[I])
                return FloorError(Error,
                                  TEXT("MissingFloorTemplateRole:") + UEnum::GetValueAsString(static_cast<Role>(I)));
        return true;
    }
    bool MainRolesValid(const TArray<Role>& Roles)
    {
        int32 Combat = 0, Streak = 0;
        for (Role R : Roles)
        {
            if (R == Role::Elite && Combat < 3)
                return false;
            Combat += R == Role::Combat;
            Streak = Fighting(R) ? Streak + 1 : 0;
            if (Streak > 3)
                return false;
        }
        return true;
    }
    bool HasPorts(const UDungeonGenerationProfile* Profile, Role R, int32 Required, int32 Degree)
    {
        if (R == Role::Entrance)
            return (Profile->Entrance.DoorMask & Required) == Required &&
                   DoorCount(Profile->Entrance.DoorMask) >= Degree;
        for (const auto& T : Profile->Templates)
            if (T.Role == R && DoorCount(T.DoorMask) >= Degree)
                for (int32 Rotation = 0; Rotation < 4; ++Rotation)
                    if ((T.AllowedRotations & (1 << Rotation)) &&
                        (UDungeonLayoutLibrary::RotateDoorMask(T.DoorMask, Rotation) & Required) == Required)
                        return true;
        return false;
    }

    bool MakeMission(const UDungeonGenerationProfile* Profile, int32 Seed, FDungeonLayout& Layout,
                     TArray<int32>& Parents, FString& Error)
    {
        const auto& S = Profile->FloorSettings;
        FRandomStream Random(StreamSeed(Seed, 0x6d697373u));
        const int32 Main = Random.RandRange(S.MainRoomsMin, S.MainRoomsMax);
        TArray<TArray<Role>> Schedules;
        for (int32 Treasure = 3; Treasure < Main - 3; ++Treasure)
            for (int32 Elite = Treasure + 1; Elite < Main - 3; ++Elite)
                for (int32 Merchant = Elite + 1; Merchant < Main - 2; ++Merchant)
                {
                    TArray<Role> Roles;
                    Roles.Init(Role::Combat, Main);
                    Roles[0] = Role::Entrance;
                    Roles[Treasure] = Role::Treasure;
                    Roles[Elite] = Role::Elite;
                    Roles[Merchant] = Role::Merchant;
                    Roles[Main - 2] = Role::Rest;
                    Roles[Main - 1] = Role::Boss;
                    if (MainRolesValid(Roles))
                        Schedules.Add(MoveTemp(Roles));
                }
        if (Schedules.IsEmpty())
            return FloorError(Error, TEXT("NoValidMainMissionSchedule"));
        const auto Roles = Schedules[Random.RandRange(0, Schedules.Num() - 1)];
        const int32 BranchCount = Random.RandRange(S.BranchesMin, S.BranchesMax);
        const int32 SideCount = Random.RandRange(S.SideRoomsMin, S.SideRoomsMax);
        TArray<FIntVector> LengthOptions;
        for (int32 A = 1; A <= 3; ++A)
            for (int32 B = 1; B <= 3; ++B)
                for (int32 C = BranchCount == 3 ? 1 : 0; C <= (BranchCount == 3 ? 3 : 0); ++C)
                    if (A + B + C == SideCount)
                        LengthOptions.Add(FIntVector(A, B, C));
        if (LengthOptions.IsEmpty())
            return FloorError(Error, TEXT("NoValidBranchLengths"));
        const FIntVector Lengths = LengthOptions[Random.RandRange(0, LengthOptions.Num() - 1)];
        TArray<Role> Rewards{Role::Treasure, Role::Treasure};
        if (BranchCount == 3)
            Rewards.Add(Role::Elite);
        else if (Random.RandRange(0, 1) == 0)
            Rewards[1] = Role::Elite;
        Shuffle(Rewards, Random);
        TArray<int32> Attachments;
        for (int32 I = 2; I < Main - 2; ++I)
            if (HasPorts(Profile, Roles[I], 0, 3))
                Attachments.Add(I);
        if (Attachments.Num() < BranchCount)
            return FloorError(Error, TEXT("InsufficientBranchAttachmentPorts"));
        Shuffle(Attachments, Random);
        Layout = FDungeonLayout();
        Layout.Version = 2;
        Layout.Seed = Seed;
        Layout.MainPathLength = Main;
        Layout.Rooms.SetNum(Main + SideCount);
        Parents.Init(INDEX_NONE, Layout.Rooms.Num());
        for (int32 I = 0; I < Main; ++I)
        {
            auto& Room = Layout.Rooms[I];
            Room.Role = Roles[I];
            Room.MainPathIndex = I;
            if (I > 0)
                Parents[I] = I - 1;
        }
        int32 Cursor = Main;
        for (int32 Branch = 0; Branch < BranchCount; ++Branch)
        {
            const int32 Length = Branch == 0 ? Lengths.X : Branch == 1 ? Lengths.Y : Lengths.Z;
            for (int32 I = 0; I < Length; ++I, ++Cursor)
            {
                auto& Room = Layout.Rooms[Cursor];
                Room.BranchId = Branch;
                Room.Role = I == Length - 1 ? Rewards[Branch] : Role::Combat;
                Parents[Cursor] = I == 0 ? Attachments[Branch] : Cursor - 1;
            }
        }
        for (int32 I = 0; I < Layout.Rooms.Num(); ++I)
        {
            Layout.Rooms[I].RoomId = FName(*FString::Printf(TEXT("R%02d"), I));
            if (I > 0)
            {
                auto& Edge = Layout.Connections.AddDefaulted_GetRef();
                Edge.From = Parents[I];
                Edge.To = I;
            }
        }
        auto& Entrance = Layout.Rooms[0];
        Entrance.bPersistent = true;
        Entrance.TemplateId = Profile->Entrance.TemplateId;
        return true;
    }

    TArray<TArray<int32>> Neighbors(const FDungeonLayout& Layout)
    {
        TArray<TArray<int32>> Result;
        Result.SetNum(Layout.Rooms.Num());
        for (const auto& E : Layout.Connections)
        {
            Result[E.From].Add(E.To);
            Result[E.To].Add(E.From);
        }
        return Result;
    }
    int32 RequiredDoors(const FDungeonLayout& Layout, const TArray<TArray<int32>>& Adjacent, int32 Room,
                        int32 LastPlaced)
    {
        int32 Mask = 0;
        for (int32 Other : Adjacent[Room])
            if (Other <= LastPlaced)
            {
                const int32 D =
                    UDungeonLayoutLibrary::DirectionIndex(Layout.Rooms[Other].Cell - Layout.Rooms[Room].Cell);
                if (D == INDEX_NONE)
                    return -1;
                Mask |= 1 << D;
            }
        return Mask;
    }
    bool SelectTemplates(const UDungeonGenerationProfile* Profile, FDungeonLayout& Layout, int32 Seed, FString& Error)
    {
        TArray<FDungeonRoomTemplate> Templates = Profile->Templates;
        Templates.Sort(
            [](const auto& A, const auto& B)
            {
                return A.TemplateId.LexicalLess(B.TemplateId);
            });
        const auto Adjacent = Neighbors(Layout);
        FRandomStream Random(StreamSeed(Seed, 0x74656d70u));
        Layout.bDiversityRelaxed = false;
        if (!HasPorts(Profile, Role::Entrance, RequiredDoors(Layout, Adjacent, 0, Layout.Rooms.Num() - 1),
                      Adjacent[0].Num()))
            return FloorError(Error, TEXT("FallbackEntrancePortsIncompatible"));
        for (int32 I = 1; I < Layout.Rooms.Num(); ++I)
        {
            auto& Room = Layout.Rooms[I];
            TArray<FIntPoint> Options, Preferred;
            const int32 Required = RequiredDoors(Layout, Adjacent, I, Layout.Rooms.Num() - 1);
            for (int32 T = 0; T < Templates.Num(); ++T)
                for (int32 Rotation = 0; Rotation < 4; ++Rotation)
                    if (Templates[T].Role == Room.Role && (Templates[T].AllowedRotations & (1 << Rotation)) &&
                        Required >= 0 &&
                        (UDungeonLayoutLibrary::RotateDoorMask(Templates[T].DoorMask, Rotation) & Required) == Required)
                        Options.Add(FIntPoint(T, Rotation));
            if (Options.IsEmpty())
                return FloorError(Error, TEXT("NoCompatibleTemplate:") + Room.RoomId.ToString());
            if (I < Layout.MainPathLength && Room.Role == Role::Combat && Layout.Rooms[I - 1].Role == Role::Combat)
            {
                for (const auto& Option : Options)
                    if (Templates[Option.X].Level != Layout.Rooms[I - 1].Level)
                        Preferred.Add(Option);
                if (Preferred.IsEmpty())
                    Layout.bDiversityRelaxed = true;
            }
            const auto& Choices = Preferred.IsEmpty() ? Options : Preferred;
            const auto Choice = Choices[Random.RandRange(0, Choices.Num() - 1)];
            Room.TemplateId = Templates[Choice.X].TemplateId;
            Room.Level = Templates[Choice.X].Level;
            Room.QuarterTurns = Choice.Y;
        }
        return true;
    }

    class FBoundedEmbedding
    {
    public:
        FBoundedEmbedding(const UDungeonGenerationProfile* InProfile, FDungeonLayout& InLayout,
                          const TArray<int32>& InParents, int32 Attempt)
            : Profile(InProfile), Layout(InLayout), Parents(InParents), Adjacent(Neighbors(InLayout)),
              Random(StreamSeed(InLayout.Seed, 0x73706163u, Attempt))
        {
            Occupied.Add(FIntPoint::ZeroValue);
        }
        bool Run()
        {
            return Place(1);
        }
        int32 Steps = 0;

    private:
        bool FitsPorts(int32 Room, int32 LastPlaced) const
        {
            const int32 Mask = RequiredDoors(Layout, Adjacent, Room, LastPlaced);
            return Mask >= 0 && HasPorts(Profile, Layout.Rooms[Room].Role, Mask, Adjacent[Room].Num());
        }
        bool Place(int32 Index)
        {
            if (Index == Layout.Rooms.Num())
                return true;
            TArray<int32> Directions{0, 1, 2, 3};
            Shuffle(Directions, Random);
            const int32 Parent = Parents[Index];
            for (int32 D : Directions)
            {
                if (Steps >= Profile->FloorSettings.MaxSearchStepsPerAttempt)
                    return false;
                ++Steps;
                const FIntPoint Cell = Layout.Rooms[Parent].Cell + UDungeonLayoutLibrary::Direction(D);
                if (Occupied.Contains(Cell) || FMath::Abs(Cell.X) > Profile->FloorSettings.GridRadius ||
                    FMath::Abs(Cell.Y) > Profile->FloorSettings.GridRadius)
                    continue;
                Layout.Rooms[Index].Cell = Cell;
                if (!FitsPorts(Index, Index) || !FitsPorts(Parent, Index))
                    continue;
                Occupied.Add(Cell);
                if (Place(Index + 1))
                    return true;
                Occupied.Remove(Cell);
            }
            return false;
        }
        const UDungeonGenerationProfile* Profile;
        FDungeonLayout& Layout;
        const TArray<int32>& Parents;
        TArray<TArray<int32>> Adjacent;
        FRandomStream Random;
        TSet<FIntPoint> Occupied;
    };
} // namespace

bool UDungeonLayoutLibrary::BuildFloor(const UDungeonGenerationProfile* Profile, int32 Seed, FDungeonLayout& OutLayout,
                                       FString& OutError)
{
    OutLayout = FDungeonLayout();
    OutError.Reset();
    if (!CheckFloorProfile(Profile, OutError))
        return false;
    FDungeonLayout Mission;
    TArray<int32> Parents;
    if (!MakeMission(Profile, Seed, Mission, Parents, OutError))
        return false;
    const auto& Settings = Profile->FloorSettings;
    int32 TotalSteps = 0;
    for (int32 Attempt = 0; Attempt < Settings.MaxPlacementAttempts; ++Attempt)
    {
        FDungeonLayout Candidate = Mission;
        FBoundedEmbedding Search(Profile, Candidate, Parents, Attempt);
        const bool Placed = Search.Run();
        TotalSteps += Search.Steps;
        Candidate.PlacementAttempts = Attempt + 1;
        Candidate.SearchSteps = TotalSteps;
        if (Placed && SelectTemplates(Profile, Candidate, Seed, OutError) &&
            ValidateFloor(Profile, Candidate, OutError))
        {
            OutLayout = MoveTemp(Candidate);
            OutError.Reset();
            return true;
        }
    }
    if (!Settings.bAllowFallback)
        return FloorError(OutError, FString::Printf(TEXT("FloorPlacementBudgetExhausted:attempts=%d,steps=%d"),
                                                    Settings.MaxPlacementAttempts, TotalSteps));
    FDungeonLayout Fallback = Mission;
    Fallback.bUsedFallback = true;
    Fallback.PlacementAttempts = Settings.MaxPlacementAttempts;
    Fallback.SearchSteps = TotalSteps;
    for (int32 I = 0; I < Fallback.MainPathLength; ++I)
        Fallback.Rooms[I].Cell = FIntPoint(0, -I);
    for (int32 I = Fallback.MainPathLength; I < Fallback.Rooms.Num(); ++I)
        Fallback.Rooms[I].Cell = Fallback.Rooms[Parents[I]].Cell + FIntPoint(1, 0);
    if (SelectTemplates(Profile, Fallback, Seed, OutError) && ValidateFloor(Profile, Fallback, OutError))
    {
        OutLayout = MoveTemp(Fallback);
        OutError.Reset();
        return true;
    }
    OutError = TEXT("FallbackLayoutUnavailable:") + OutError;
    return false;
}

bool UDungeonLayoutLibrary::ValidateFloor(const UDungeonGenerationProfile* Profile, const FDungeonLayout& Layout,
                                          FString& OutError)
{
    OutError.Reset();
    if (!CheckFloorProfile(Profile, OutError))
        return false;
    const auto& S = Profile->FloorSettings;
    const int32 Main = Layout.MainPathLength;
    const int32 Side = Layout.Rooms.Num() - Main;
    if (Layout.Version != 2 || Main < S.MainRoomsMin || Main > S.MainRoomsMax || Side < S.SideRoomsMin ||
        Side > S.SideRoomsMax || Layout.Rooms.Num() < 14 || Layout.Rooms.Num() > 18)
        return FloorError(OutError, TEXT("InvalidFloorVersionOrRoomCount"));
    if (!ValidateLayout(Profile, Layout, OutError))
        return false;
    int32 MainCounts[7] = {}, TreasureAt = -1, EliteAt = -1, MerchantAt = -1;
    int32 AllTreasure = 0, AllElite = 0;
    TArray<Role> MainRoles;
    TArray<TArray<int32>> Branches;
    Branches.SetNum(3);
    for (int32 I = 0; I < Layout.Rooms.Num(); ++I)
    {
        const auto& R = Layout.Rooms[I];
        if (FMath::Abs(R.Cell.X) > S.GridRadius || FMath::Abs(R.Cell.Y) > S.GridRadius)
            return FloorError(OutError, TEXT("FloorCellOutsideBounds:") + R.RoomId.ToString());
        AllTreasure += R.Role == Role::Treasure;
        AllElite += R.Role == Role::Elite;
        if (I < Main)
        {
            if (R.MainPathIndex != I || R.BranchId != -1)
                return FloorError(OutError, TEXT("InvalidMainPathMembership:") + R.RoomId.ToString());
            ++MainCounts[static_cast<int32>(R.Role)];
            MainRoles.Add(R.Role);
            if (R.Role == Role::Treasure)
                TreasureAt = I;
            if (R.Role == Role::Elite)
                EliteAt = I;
            if (R.Role == Role::Merchant)
                MerchantAt = I;
        }
        else
        {
            if (R.MainPathIndex != -1 || R.BranchId < 0 || R.BranchId >= Branches.Num())
                return FloorError(OutError, TEXT("InvalidBranchMembership:") + R.RoomId.ToString());
            if (R.Role != Role::Combat && R.Role != Role::Treasure && R.Role != Role::Elite)
                return FloorError(OutError, TEXT("InvalidBranchRole:") + R.RoomId.ToString());
            Branches[R.BranchId].Add(I);
        }
    }
    for (int32 R = 0; R < 7; ++R)
        if (MainCounts[R] != (R == static_cast<int32>(Role::Combat) ? Main - 6 : 1))
            return FloorError(OutError, TEXT("MainRoleQuotaMismatch"));
    if (MainRoles[0] != Role::Entrance || MainRoles[1] != Role::Combat || MainRoles[2] != Role::Combat ||
        MainRoles[Main - 2] != Role::Rest || MainRoles[Main - 1] != Role::Boss || TreasureAt < 3 ||
        TreasureAt >= EliteAt || EliteAt >= MerchantAt || MerchantAt >= Main - 2 || !MainRolesValid(MainRoles))
        return FloorError(OutError, TEXT("MainMissionOrderOrPacingMismatch"));
    if (AllTreasure < 2 || AllTreasure > 3 || AllElite < 1 || AllElite > 2)
        return FloorError(OutError, TEXT("FloorRewardQuotaMismatch"));
    const auto Adjacent = Neighbors(Layout);
    for (int32 I = 1; I < Main; ++I)
        if (!Adjacent[I - 1].Contains(I))
            return FloorError(OutError, TEXT("MainChainEdgeMissing"));
    int32 BranchCount = 0;
    for (const auto& Branch : Branches)
        BranchCount += !Branch.IsEmpty();
    if (BranchCount < S.BranchesMin || BranchCount > S.BranchesMax)
        return FloorError(OutError, TEXT("BranchCountMismatch"));
    for (int32 B = 0; B < Branches.Num(); ++B)
        if (Branches[B].IsEmpty() != (B >= BranchCount))
            return FloorError(OutError, TEXT("NonCanonicalBranchIds"));
    TArray<int32> AttachmentRooms, BranchRoots;
    AttachmentRooms.Init(-1, BranchCount);
    BranchRoots.Init(-1, BranchCount);
    TSet<int32> UsedAttachments;
    for (const auto& E : Layout.Connections)
    {
        const bool FromMain = E.From < Main, ToMain = E.To < Main;
        if (FromMain && ToMain)
        {
            if (FMath::Abs(E.From - E.To) != 1)
                return FloorError(OutError, TEXT("MainChainBypass"));
        }
        else if (!FromMain && !ToMain)
        {
            if (Layout.Rooms[E.From].BranchId != Layout.Rooms[E.To].BranchId)
                return FloorError(OutError, TEXT("CrossBranchConnection"));
        }
        else
        {
            const int32 Attachment = FromMain ? E.From : E.To;
            const int32 Root = FromMain ? E.To : E.From;
            const int32 B = Layout.Rooms[Root].BranchId;
            if (Attachment < 2 || Attachment >= Main - 2 || AttachmentRooms[B] != -1 ||
                UsedAttachments.Contains(Attachment))
                return FloorError(OutError, TEXT("InvalidOrDuplicateBranchAttachment"));
            AttachmentRooms[B] = Attachment;
            BranchRoots[B] = Root;
            UsedAttachments.Add(Attachment);
        }
    }
    for (int32 B = 0; B < BranchCount; ++B)
    {
        if (Branches[B].Num() < 1 || Branches[B].Num() > 3 || AttachmentRooms[B] == -1)
            return FloorError(OutError, TEXT("InvalidBranchLengthOrAttachment"));
        int32 Previous = AttachmentRooms[B], Current = BranchRoots[B], Visited = 0;
        while (Current != -1)
        {
            if (++Visited > Branches[B].Num() || Current < Main || Layout.Rooms[Current].BranchId != B ||
                Adjacent[Current].Num() > 2)
                return FloorError(OutError, TEXT("BranchMustBeLinear"));
            int32 Next = -1;
            for (int32 Neighbor : Adjacent[Current])
                if (Neighbor != Previous)
                    Next = Neighbor;
            const auto& Room = Layout.Rooms[Current];
            if (Next == -1)
            {
                if (Room.Role != Role::Treasure && Room.Role != Role::Elite)
                    return FloorError(OutError, TEXT("BranchTerminalHasNoReward:") + Room.RoomId.ToString());
            }
            else if (Room.Role != Role::Combat)
                return FloorError(OutError, TEXT("BranchInteriorMustBeCombat:") + Room.RoomId.ToString());
            Previous = Current;
            Current = Next;
        }
        if (Visited != Branches[B].Num())
            return FloorError(OutError, TEXT("BranchDisconnected"));
    }
    return true;
}
