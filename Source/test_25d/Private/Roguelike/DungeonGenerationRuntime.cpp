// 主要调用链：生成布局 → 本地装配与导航检查 → 控制器上报 → 屏障确认 → 放行移动；失败关闭门禁。
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonGenerationProfile.h"
#include "Roguelike/DungeonGenerationController.h"
#include "Roguelike/DungeonLayoutLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "Net/UnrealNetwork.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"

// 建立复制宿主及低频装配检查周期。
ADungeonGenerationRuntime::ADungeonGenerationRuntime()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .1f;
    bReplicates = true;
    bAlwaysRelevant = true;
}

// 登记需要同步给客户端的运行状态字段。
void ADungeonGenerationRuntime::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADungeonGenerationRuntime, Manifest);
    DOREPLIFETIME(ADungeonGenerationRuntime, Phase);
    DOREPLIFETIME(ADungeonGenerationRuntime, FailureReason);
    DOREPLIFETIME(ADungeonGenerationRuntime, ReadyPlayers);
}

// 验证原点和人数，由服务器生成布局摘要并重置就绪屏障后开始装配。
void ADungeonGenerationRuntime::BeginPlay()
{
    Super::BeginPlay();
    StartedAt = FPlatformTime::Seconds();
    if (!GetActorLocation().IsNearlyZero() || !GetActorRotation().IsNearlyZero() ||
        !GetActorScale3D().Equals(FVector::OneVector))
    {
        Fail(TEXT("GeneratorMustBeAtPersistentEntranceOrigin"));
        return;
    }
    if (!HasAuthority())
        return;
    if (ExpectedPlayers < 1 || ExpectedPlayers > 2)
    {
        Fail(TEXT("SupportsOneOrTwoPlayers"));
        return;
    }
    FString Error;
    const double GenerationStart = FPlatformTime::Seconds();
    const bool Generated = Profile && Profile->bGenerateFullFloor
                               ? UDungeonLayoutLibrary::BuildFloor(Profile, Seed, Manifest.Layout, Error)
                               : UDungeonLayoutLibrary::BuildAssemblyProbe(Profile, Seed, Manifest.Layout, Error);
    GenerationMilliseconds = (FPlatformTime::Seconds() - GenerationStart) * 1000;
    if (!Generated)
    {
        Fail(Error);
        return;
    }
    Manifest.GenerationId = FGuid::NewGuid();
    Manifest.RoomSpacing = Profile->RoomSpacing;
    Manifest.ExpectedPlayers = ExpectedPlayers;
    const FString Canonical = UDungeonLayoutLibrary::CanonicalLayout(Manifest.Layout) +
                              FString::Printf(TEXT("|spacing=%.9g"), Manifest.RoomSpacing);
    Manifest.Digest = FMD5::HashAnsiString(*Canonical);
    Barrier.Reset(Manifest.GenerationId, Manifest.Digest, Manifest.ExpectedPlayers);
    Phase = EDungeonGenerationPhase::Loading;
    StartLocalAssembly();
    ForceNetUpdate();
}

// 清单复制到客户端后尝试启动本地装配。
void ADungeonGenerationRuntime::OnRep_Manifest()
{
    StartLocalAssembly();
}

// 验证布局与摘要，创建门禁并请求房间流式加载；生成编号阻止重复装配。
void ADungeonGenerationRuntime::StartLocalAssembly()
{
    if (!HasActorBegunPlay() || !Manifest.GenerationId.IsValid() || Manifest.GenerationId == AssembledGeneration)
        return;
    if (AssembledGeneration.IsValid())
    {
        Fail(TEXT("RegenerationRequiresANewWorld"));
        return;
    }
    FString Error;
    const bool Valid = Profile && Profile->bGenerateFullFloor
                           ? UDungeonLayoutLibrary::ValidateFloor(Profile, Manifest.Layout, Error)
                           : UDungeonLayoutLibrary::ValidateLayout(Profile, Manifest.Layout, Error);
    if (!Valid)
    {
        Fail(TEXT("ManifestInvalid:") + Error);
        return;
    }
    if (!FMath::IsNearlyEqual(Manifest.RoomSpacing, Profile->RoomSpacing))
    {
        Fail(TEXT("ProfileSpacingMismatch"));
        return;
    }
    const FString Canonical = UDungeonLayoutLibrary::CanonicalLayout(Manifest.Layout) +
                              FString::Printf(TEXT("|spacing=%.9g"), Manifest.RoomSpacing);
    if (FMD5::HashAnsiString(*Canonical) != Manifest.Digest)
    {
        Fail(TEXT("ManifestDigestMismatch"));
        return;
    }
    AssembledGeneration = Manifest.GenerationId;
    for (const auto& Room : Manifest.Layout.Rooms)
        if (!Room.bPersistent && !FPackageName::DoesPackageExist(Room.Level.ToSoftObjectPath().GetLongPackageName()))
        {
            Fail(TEXT("TemplatePackageMissing:") + Room.TemplateId.ToString());
            return;
        }
    CreateDoorBarriers();
    if (!LocalFailure.IsEmpty())
        return;
    for (int32 I = 0; I < Manifest.Layout.Rooms.Num(); ++I)
    {
        const auto& Room = Manifest.Layout.Rooms[I];
        if (Room.bPersistent)
            continue;
        bool Success = false;
        const FString Name =
            TEXT("DG_") + Manifest.GenerationId.ToString(EGuidFormats::Digits) + TEXT("_") + Room.RoomId.ToString();
        auto* Level = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
            GetWorld(), Room.Level, RoomCenter(I), FRotator(0, Room.QuarterTurns * 90.f, 0), Success, Name);
        if (!Success || !Level)
        {
            Fail(TEXT("TemplateLoadRequestFailed:") + Room.RoomId.ToString());
            return;
        }
        StreamingRooms.Add(Level);
    }
}

// 按清单格坐标和房间间距计算世界中心。
FVector ADungeonGenerationRuntime::RoomCenter(int32 Index) const
{
    const auto Cell = Manifest.Layout.Rooms[Index].Cell;
    return FVector(Cell.X * Manifest.RoomSpacing, Cell.Y * Manifest.RoomSpacing, 0);
}

// 封闭未连接出口，并在入口连接处建立等待全员就绪的安全门。
void ADungeonGenerationRuntime::CreateDoorBarriers()
{
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Cube)
    {
        Fail(TEXT("DoorBarrierMeshMissing"));
        return;
    }
    auto Make = [&](int32 Room, int32 Direction, bool Safety)
    {
        auto* Mesh = NewObject<UStaticMeshComponent>(this);
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetStaticMesh(Cube);
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetCanEverAffectNavigation(!Safety);
        const FIntPoint D = UDungeonLayoutLibrary::Direction(Direction);
        Mesh->SetRelativeLocation(RoomCenter(Room) + FVector(D.X, D.Y, 0) * (Manifest.RoomSpacing * .5f) +
                                  FVector(0, 0, 140));
        Mesh->SetRelativeScale3D(Direction % 2 == 0 ? FVector(1, 8, 2.8f) : FVector(8, 1, 2.8f));
        Mesh->RegisterComponent();
        DoorBarriers.Add(Mesh);
        if (Safety)
            SafetyGate = Mesh;
    };
    for (int32 I = 0; I < Manifest.Layout.Rooms.Num(); ++I)
    {
        int32 Used = 0;
        for (const auto& Edge : Manifest.Layout.Connections)
        {
            const int32 Other = Edge.From == I ? Edge.To : Edge.To == I ? Edge.From : INDEX_NONE;
            if (Other != INDEX_NONE)
                Used |= 1 << UDungeonLayoutLibrary::DirectionIndex(Manifest.Layout.Rooms[Other].Cell -
                                                                   Manifest.Layout.Rooms[I].Cell);
        }
        for (int32 D = 0; D < 4; ++D)
        {
            if (!(Used & (1 << D)))
                Make(I, D, false);
            else if (I == 0)
                Make(I, D, true);
        }
    }
}

// 检查动态导航、房间地面、连接路径及实际门洞胶囊通行条件。
bool ADungeonGenerationRuntime::CheckNavigation(FString& Reason) const
{
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav)
    {
        Reason = TEXT("NavigationSystemMissing");
        return false;
    }
    const auto* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
    if (!Data)
    {
        Reason = TEXT("NavigationDataMissing");
        return false;
    }
    if (Data->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic)
    {
        Reason = TEXT("DynamicNavigationRequired");
        return false;
    }
    if (UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()))
    {
        Reason = TEXT("NavigationBuilding");
        return false;
    }
    TArray<FVector> Points;
    for (int32 I = 0; I < Manifest.Layout.Rooms.Num(); ++I)
    {
        FNavLocation Position;
        if (!Nav->ProjectPointToNavigation(RoomCenter(I) + FVector(0, 0, 80), Position, FVector(100, 100, 250), Data))
        {
            Reason = TEXT("RoomNotOnNavigation:") + Manifest.Layout.Rooms[I].RoomId.ToString();
            return false;
        }
        FHitResult Floor;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DungeonFloor), false, this);
        if (!GetWorld()->LineTraceSingleByChannel(Floor, RoomCenter(I) + FVector(200, 200, 500),
                                                  RoomCenter(I) + FVector(200, 200, -500), ECC_Visibility, Params) ||
            FMath::Abs(Floor.ImpactPoint.Z) > 30.f)
        {
            Reason = TEXT("RoomFloorCollisionMissing:") + Manifest.Layout.Rooms[I].RoomId.ToString();
            return false;
        }
        Points.Add(Position.Location);
    }
    for (const auto& Edge : Manifest.Layout.Connections)
    {
        const auto* Path =
            UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Points[Edge.From], Points[Edge.To]);
        if (!Path || !Path->IsValid() || Path->IsPartial())
        {
            Reason = TEXT("RoomConnectionNotNavigable:") + Manifest.Layout.Rooms[Edge.From].RoomId.ToString() +
                     TEXT("->") + Manifest.Layout.Rooms[Edge.To].RoomId.ToString();
            return false;
        }
    }
    // Navigation can omit Pawn-only/invisible blockers. Validate the physical
    // doorway separately, excluding the deliberate readiness gate and players.
    FCollisionQueryParams DoorParams(SCENE_QUERY_STAT(DungeonDoorway), false);
    if (SafetyGate)
        DoorParams.AddIgnoredComponent(SafetyGate.Get());
    float Radius = 42.f, HalfHeight = 96.f;
    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
    {
        DoorParams.AddIgnoredActor(*It);
        if (const auto* Capsule = It->FindComponentByClass<UCapsuleComponent>())
        {
            Radius = FMath::Max(Radius, Capsule->GetScaledCapsuleRadius());
            HalfHeight = FMath::Max(HalfHeight, Capsule->GetScaledCapsuleHalfHeight());
        }
    }
    for (const auto& Edge : Manifest.Layout.Connections)
    {
        const FVector A = RoomCenter(Edge.From), B = RoomCenter(Edge.To);
        const FVector Direction = (B - A).GetSafeNormal();
        const FVector Seam = (A + B) * .5f;
        const FVector Center = Seam + FVector(0, 0, HalfHeight + 4.f);
        const FString EdgeId = Manifest.Layout.Rooms[Edge.From].RoomId.ToString() + TEXT("->") +
                               Manifest.Layout.Rooms[Edge.To].RoomId.ToString();
        FHitResult Hit;
        if (GetWorld()->SweepSingleByChannel(Hit, Center - Direction * 200.f, Center + Direction * 200.f,
                                             FQuat::Identity, ECC_Pawn,
                                             FCollisionShape::MakeCapsule(Radius, HalfHeight), DoorParams))
        {
            Reason = TEXT("DoorwayCollisionBlocked:") + EdgeId;
            return false;
        }
        for (float Offset : {-150.f, 0.f, 150.f})
        {
            const FVector FloorPoint = Seam + Direction * Offset;
            if (!GetWorld()->LineTraceSingleByChannel(Hit, FloorPoint + FVector(0, 0, 100),
                                                      FloorPoint - FVector(0, 0, 100), ECC_Pawn, DoorParams) ||
                FMath::Abs(Hit.ImpactPoint.Z) > 30.f)
            {
                Reason = TEXT("DoorwayFloorMissing:") + EdgeId;
                return false;
            }
        }
    }
    Reason.Reset();
    return true;
}

// 轮询装配及导航验证，等待全员确认后开门，超时则转入失败。
void ADungeonGenerationRuntime::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasActorBegunPlay())
        return;
    if (Phase == EDungeonGenerationPhase::Failed || !LocalFailure.IsEmpty())
    {
        if (SafetyGate)
        {
            SafetyGate->SetVisibility(true);
            SafetyGate->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }
        return;
    }
    if (!AssembledGeneration.IsValid() && Manifest.GenerationId.IsValid())
        StartLocalAssembly();
    if (!bLocalReady && AssembledGeneration.IsValid() && LocalFailure.IsEmpty())
    {
        const bool Loaded = StreamingRooms.Num() == Manifest.Layout.Rooms.Num() - 1 &&
                            !StreamingRooms.ContainsByPredicate(
                                [](const auto& Level)
                                {
                                    return !Level || !Level->IsLevelLoaded() || !Level->IsLevelVisible();
                                });
        PendingReason = TEXT("WaitingForStreaming");
        if (Loaded && CheckNavigation(PendingReason))
        {
            bLocalReady = true;
            if (HasAuthority())
                Phase = EDungeonGenerationPhase::WaitingForPlayers;
        }
    }
    if (HasAuthority() && bLocalReady && Barrier.IsReady() && Phase != EDungeonGenerationPhase::Ready)
    {
        Phase = EDungeonGenerationPhase::Ready;
        ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("DungeonGeneration Ready seed=%d digest=%s players=%d"), Manifest.Layout.Seed,
               *Manifest.Digest, ReadyPlayers);
    }
    if (SafetyGate)
    {
        const bool Open = CanLocalPlayerMove();
        SafetyGate->SetCollisionEnabled(Open ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        SafetyGate->SetVisibility(!Open);
    }
    const double Timeout = Profile ? Profile->ReadyTimeout : 30.f;
    if ((!bLocalReady || Phase != EDungeonGenerationPhase::Ready) && FPlatformTime::Seconds() - StartedAt > Timeout)
        Fail(TEXT("ReadyTimeout:") + (bLocalReady ? TEXT("WaitingForPlayers") : PendingReason));
}

// 确认上报者仍属于当前服务器世界的玩家列表。
bool ADungeonGenerationRuntime::IsCurrentController(const ADungeonGenerationController* Controller) const
{
    if (!HasAuthority() || !IsValid(Controller) || Controller->GetWorld() != GetWorld() || !Controller->PlayerState)
        return false;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (It->Get() == Controller)
            return true;
    return false;
}

// 校验当前控制器后提交就绪屏障，成功时同步就绪人数。
void ADungeonGenerationRuntime::ReportReady(ADungeonGenerationController* Controller, FGuid Generation,
                                            const FString& Digest)
{
    if (Phase == EDungeonGenerationPhase::Failed || !IsCurrentController(Controller))
        return;
    if (Barrier.Acknowledge(Controller->PlayerState->GetPlayerId(), Generation, Digest))
    {
        ReadyPlayers = Barrier.NumReady();
        ForceNetUpdate();
    }
}

// 仅接收当前玩家对本次生成的失败报告。
void ADungeonGenerationRuntime::ReportFailure(ADungeonGenerationController* Controller, FGuid Generation,
                                              const FString& Reason)
{
    if (IsCurrentController(Controller) && Manifest.GenerationId == Generation &&
        Phase != EDungeonGenerationPhase::Failed)
        Fail(FString::Printf(TEXT("Client%d:%s"), Controller->PlayerState->GetPlayerId(), *Reason.Left(200)));
}

// 记录本地失败并关闭安全门，服务器同时复制失败阶段。
void ADungeonGenerationRuntime::Fail(const FString& Reason)
{
    LocalFailure = Reason;
    if (HasAuthority())
    {
        FailureReason = Reason;
        Phase = EDungeonGenerationPhase::Failed;
        ForceNetUpdate();
    }
    if (SafetyGate)
    {
        SafetyGate->SetVisibility(true);
        SafetyGate->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
    UE_LOG(LogTemp, Warning, TEXT("DungeonGeneration Failed: %s"), *Reason);
}

// 序列化布局、连接、加载进度和门禁状态用于诊断。
FString ADungeonGenerationRuntime::GetGenerationReport() const
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("world"), GetWorld() ? GetWorld()->GetMapName() : TEXT("none"));
    Report->SetBoolField(TEXT("authority"), HasAuthority());
    Report->SetStringField(TEXT("generation_id"), Manifest.GenerationId.ToString());
    Report->SetStringField(TEXT("digest"), Manifest.Digest);
    Report->SetStringField(TEXT("phase"), UEnum::GetValueAsString(Phase));
    Report->SetStringField(TEXT("error"), LocalFailure.IsEmpty() ? FailureReason : LocalFailure);
    Report->SetStringField(TEXT("pending"), PendingReason);
    Report->SetNumberField(TEXT("seed"), Manifest.Layout.Seed);
    Report->SetNumberField(TEXT("layout_version"), Manifest.Layout.Version);
    Report->SetNumberField(TEXT("main_path_length"), Manifest.Layout.MainPathLength);
    Report->SetBoolField(TEXT("used_fallback"), Manifest.Layout.bUsedFallback);
    Report->SetBoolField(TEXT("diversity_relaxed"), Manifest.Layout.bDiversityRelaxed);
    Report->SetNumberField(TEXT("placement_attempts"), Manifest.Layout.PlacementAttempts);
    Report->SetNumberField(TEXT("search_steps"), Manifest.Layout.SearchSteps);
    Report->SetNumberField(TEXT("generation_ms"), GenerationMilliseconds);
    Report->SetNumberField(TEXT("expected_players"), Manifest.ExpectedPlayers);
    Report->SetNumberField(TEXT("ready_players"), ReadyPlayers);
    Report->SetNumberField(TEXT("streamed_rooms"), StreamingRooms.Num());
    int32 Loaded = 0;
    for (const auto& Level : StreamingRooms)
        if (Level && Level->IsLevelLoaded() && Level->IsLevelVisible())
            ++Loaded;
    Report->SetNumberField(TEXT("loaded_rooms"), Loaded);
    Report->SetBoolField(TEXT("local_ready"), bLocalReady);
    Report->SetBoolField(TEXT("gate_closed"),
                         SafetyGate && SafetyGate->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
    Report->SetNumberField(TEXT("door_barriers"), DoorBarriers.Num());
    Report->SetNumberField(TEXT("elapsed_seconds"), StartedAt > 0 ? FPlatformTime::Seconds() - StartedAt : 0);
    TArray<TSharedPtr<FJsonValue>> Rooms;
    for (int32 I = 0; I < Manifest.Layout.Rooms.Num(); ++I)
    {
        const auto& Room = Manifest.Layout.Rooms[I];
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Room.RoomId.ToString());
        Item->SetStringField(TEXT("template"), Room.TemplateId.ToString());
        Item->SetStringField(TEXT("role"), UEnum::GetValueAsString(Room.Role));
        Item->SetNumberField(TEXT("main_index"), Room.MainPathIndex);
        Item->SetNumberField(TEXT("branch_id"), Room.BranchId);
        Item->SetNumberField(TEXT("cell_x"), Room.Cell.X);
        Item->SetNumberField(TEXT("cell_y"), Room.Cell.Y);
        Item->SetStringField(TEXT("level"), Room.Level.ToSoftObjectPath().ToString());
        Item->SetBoolField(TEXT("persistent"), Room.bPersistent);
        const FVector Center = RoomCenter(I);
        Item->SetNumberField(TEXT("x"), Center.X);
        Item->SetNumberField(TEXT("y"), Center.Y);
        Item->SetNumberField(TEXT("yaw"), Room.QuarterTurns * 90);
        Rooms.Add(MakeShared<FJsonValueObject>(Item));
    }
    Report->SetArrayField(TEXT("rooms"), Rooms);
    TArray<TSharedPtr<FJsonValue>> Connections;
    for (const auto& Edge : Manifest.Layout.Connections)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("from"), Edge.From);
        Item->SetNumberField(TEXT("to"), Edge.To);
        Connections.Add(MakeShared<FJsonValueObject>(Item));
    }
    Report->SetArrayField(TEXT("connections"), Connections);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

// 退出时释放本对象持有的资源与状态，并调用父类结束生命周期。
void ADungeonGenerationRuntime::EndPlay(EEndPlayReason::Type Reason)
{
    for (const auto& Level : StreamingRooms)
        if (IsValid(Level))
        {
            Level->SetShouldBeVisible(false);
            Level->SetShouldBeLoaded(false);
            Level->SetIsRequestingUnloadAndRemoval(true);
        }
    StreamingRooms.Reset();
    for (const auto& Mesh : DoorBarriers)
        if (IsValid(Mesh))
            Mesh->DestroyComponent();
    DoorBarriers.Reset();
    SafetyGate = nullptr;
    Super::EndPlay(Reason);
}
