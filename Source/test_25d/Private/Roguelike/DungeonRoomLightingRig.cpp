// 调用链：编辑器/子关卡加载 → 固定组件配方；本地 Tick → 视距开关和火光闪烁。
#include "Roguelike/DungeonRoomLightingRig.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
#include "Roguelike/DungeonRunDirector.h"
#endif

ADungeonRoomLightingRig::ADungeonRoomLightingRig()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .1f;
    SetReplicates(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Assets/Dungeon/Beacon/SM_Pillar"));
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Fire(TEXT("/Game/Assets/Effects/Fire/NS_Fire"));
    for (int32 Index = 0; Index < 6; ++Index)
    {
        auto* Light = CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("Pool_%d"), Index));
        Light->SetupAttachment(RootComponent);
        Light->SetMobility(EComponentMobility::Movable);
        Light->IntensityUnits = ELightUnits::Lumens;
        Light->bUseInverseSquaredFalloff = true;
        Light->SourceRadius = 10.f;
        Light->IndirectLightingIntensity = .25f;
        Light->VolumetricScatteringIntensity = .4f;
        Points.Add(Light);
        if (Index < 4)
        {
            auto* Base = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Brazier_%d"), Index));
            Base->SetupAttachment(RootComponent);
            Base->SetStaticMesh(Mesh.Object);
            Base->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Base->SetCanEverAffectNavigation(false);
            Base->SetRelativeScale3D(FVector(.35f, .35f, .32f));
            Bases.Add(Base);
            auto* Flame = CreateDefaultSubobject<UNiagaraComponent>(*FString::Printf(TEXT("Flame_%d"), Index));
            Flame->SetupAttachment(RootComponent);
            Flame->SetAsset(Fire.Object);
            Flame->SetAutoActivate(false);
            Flame->SetCanEverAffectNavigation(false);
            Flame->SetRelativeScale3D(FVector(1.f));
            Flames.Add(Flame);
        }
    }
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Light = CreateDefaultSubobject<USpotLightComponent>(*FString::Printf(TEXT("Ritual_%d"), Index));
        Light->SetupAttachment(RootComponent);
        Light->SetMobility(EComponentMobility::Movable);
        Light->IntensityUnits = ELightUnits::Lumens;
        Light->bUseInverseSquaredFalloff = true;
        Light->IndirectLightingIntensity = .15f;
        Light->VolumetricScatteringIntensity = 2.5f;
        Light->SourceRadius = 16.f;
        Spots.Add(Light);
    }
}

void ADungeonRoomLightingRig::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Configure();
}

void ADungeonRoomLightingRig::Configure()
{
    PointBase.Init(0.f, Points.Num());
    SpotBase.Init(0.f, Spots.Num());
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        const bool bTorch = Index < 4;
        const FVector Position = bTorch ? FVector((Index & 1) ? 850.f : -850.f, (Index & 2) ? 850.f : -850.f, 190.f)
                                        : FVector(Index == 4 ? -850.f : 850.f, 0.f, 150.f);
        Points[Index]->SetRelativeLocation(Position);
        Points[Index]->SetLightColor(bTorch ? FLinearColor(1.f, .46f, .12f) : FLinearColor(.14f, .32f, .55f));
        Points[Index]->SetAttenuationRadius(bTorch ? 760.f : 520.f);
        PointBase[Index] = bTorch ? TorchLumens : AccentLumens;
        if (bTorch)
        {
            Bases[Index]->SetRelativeLocation(FVector(Position.X, Position.Y, 0));
            Bases[Index]->SetVisibility(true);
            Flames[Index]->SetRelativeLocation(FVector(Position.X, Position.Y, 155.f));
        }
    }
    const bool bBoss = RoomRole == EDungeonRoomRole::Boss;
    const bool bElite = RoomRole == EDungeonRoomRole::Elite;
    for (int32 Index = 0; Index < Spots.Num(); ++Index)
    {
        const FVector Position = Index == 0 ? FVector(0, bBoss || bElite ? 0.f : -280.f, bBoss ? 900.f : 650.f)
                                            : FVector(Index == 1 ? -850.f : 850.f, 150.f, 260.f);
        Spots[Index]->SetRelativeLocation(Position);
        Spots[Index]->SetRelativeRotation((FVector(0, 0, 50) - Position).Rotation());
        Spots[Index]->SetAttenuationRadius(1200.f);
        Spots[Index]->SetInnerConeAngle(Index == 0 ? 24.f : 25.f);
        Spots[Index]->SetOuterConeAngle(Index == 0 ? 48.f : 48.f);
        Spots[Index]->SetLightColor(Index == 0 && bBoss ? FLinearColor(.55f, .7f, 1.f) : ThemeColor);
    }
    // 中央低强度冷色补光保留角色与预警可读性，角部火盆仍承担主要明暗节奏。
    SpotBase[0] = 350.f;
    Spots[0]->SetLightColor(FLinearColor(.62f, .7f, .85f));
    Spots[0]->SetVolumetricScatteringIntensity(.25f);
    if (bBoss || bElite)
    {
        Spots[0]->SetVolumetricScatteringIntensity(bBoss ? 3.f : 1.5f);
        Spots[0]->SetLightColor(bBoss ? FLinearColor(.55f, .7f, 1.f) : ThemeColor);
        SpotBase[0] = bBoss ? BossLumens : RitualLumens;
        SpotBase[1] = SpotBase[2] = bBoss ? 850.f : 350.f;
        if (bBoss)
        {
            for (int32 Index = 0; Index < 4; ++Index)
            {
                PointBase[Index] *= .65f;
            }
            PointBase[4] = PointBase[5] = 0.f;
        }
    }
    else if (RoomRole == EDungeonRoomRole::Treasure)
    {
        SpotBase[0] = 1400.f;
        Spots[0]->SetLightColor(FLinearColor(1.f, .68f, .22f));
        PointBase[4] = PointBase[5] = 0.f;
    }
    else if (RoomRole == EDungeonRoomRole::Rest)
    {
        Points[4]->SetRelativeLocation(FVector(0, 0, 240.f));
        Points[4]->SetAttenuationRadius(950.f);
        Points[4]->SetLightColor(FLinearColor(1.f, .54f, .23f));
        Spots[0]->SetLightColor(FLinearColor(1.f, .65f, .35f));
        PointBase[4] = 650.f;
        PointBase[5] = 0.f;
    }
    else if (RoomRole == EDungeonRoomRole::Merchant)
    {
        for (int32 Index = 0; Index < 3; ++Index)
        {
            Points[Index]->SetRelativeLocation(FVector((Index - 1) * 480.f, 450.f, 230.f));
            Points[Index]->SetLightColor(FLinearColor(1.f, .68f, .34f));
            PointBase[Index] = TorchLumens;
            Bases[Index]->SetRelativeLocation(FVector((Index - 1) * 480.f, 450.f, 0.f));
            Flames[Index]->SetRelativeLocation(FVector((Index - 1) * 480.f, 450.f, 155.f));
        }
        PointBase[3] = PointBase[4] = PointBase[5] = 0.f;
        Bases[3]->SetVisibility(false);
        Spots[0]->SetLightColor(FLinearColor(1.f, .7f, .4f));
    }
    // 游戏世界保留之前的裁剪状态；整层加载与重新配置不会临时点亮远房或启动火焰。
    const bool bEditorPreview = GetWorld() && !GetWorld()->IsGameWorld();
    SetViewState(bEditorPreview || ViewWeight > 0.f, bEditorPreview || bCurrentRoom);
}

void ADungeonRoomLightingRig::BeginPlay()
{
    Super::BeginPlay();
    Configure();
    if (GetNetMode() == NM_DedicatedServer)
    {
        SetViewState(false, false);
        SetActorTickEnabled(false);
        return;
    }
    // 初始化时保持关闭，避免整层同时加载造成一帧的灯光/粒子峰值。
    SetViewState(false, false);
    Tick(0.f);
}

void ADungeonRoomLightingRig::SetViewState(bool bNearby, bool bCurrent)
{
    bCurrentRoom = bNearby && bCurrent;
    ViewWeight = bCurrentRoom ? 1.f : (bNearby ? .18f : 0.f);
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        const float Base = PointBase.IsValidIndex(Index) ? PointBase[Index] : 0.f;
        Points[Index]->SetVisibility(ViewWeight > 0.f && Base > 0.f);
        Points[Index]->SetIntensity(Base * ViewWeight);
        Points[Index]->SetCastShadows(bCurrentRoom && Index < 4 && Index % 2 == 0 &&
                                      RoomRole != EDungeonRoomRole::Boss);
    }
    for (int32 Index = 0; Index < Spots.Num(); ++Index)
    {
        const float Base = SpotBase.IsValidIndex(Index) ? SpotBase[Index] : 0.f;
        Spots[Index]->SetVisibility(ViewWeight > 0.f && Base > 0.f);
        Spots[Index]->SetIntensity(Base * ViewWeight);
        Spots[Index]->SetCastShadows(bCurrentRoom && Base > 0.f &&
                                     (Index == 0 || (Index == 1 && RoomRole == EDungeonRoomRole::Boss)));
    }
    for (int32 Index = 0; Index < Flames.Num(); ++Index)
    {
        const bool bVisible = bCurrentRoom && (RoomRole != EDungeonRoomRole::Merchant || Index < 3) &&
                              FApp::CanEverRender() && GetWorld() && GetWorld()->IsGameWorld() &&
                              GetNetMode() != NM_DedicatedServer;
        Flames[Index]->SetVisibility(bVisible);
        if (bVisible && !Flames[Index]->IsActive())
        {
            Flames[Index]->Activate(true);
        }
        else if (!bVisible && Flames[Index]->IsActive())
        {
            Flames[Index]->DeactivateImmediate();
        }
    }
}

void ADungeonRoomLightingRig::SetThemeColor(FLinearColor Color)
{
    ThemeColor = FLinearColor::LerpUsingHSV(FLinearColor(.38f, .06f, .55f), Color, .5f);
    if (RoomRole == EDungeonRoomRole::Elite || RoomRole == EDungeonRoomRole::Boss)
    {
        for (int32 Index = 0; Index < Spots.Num(); ++Index)
        {
            if (Index != 0 || RoomRole != EDungeonRoomRole::Boss)
            {
                Spots[Index]->SetLightColor(ThemeColor);
            }
        }
    }
}

void ADungeonRoomLightingRig::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!Pawn)
    {
        SetViewState(false, false);
        return;
    }
    const FVector Position = GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
    const bool bCurrent = ContainsPlayerLocation(Pawn->GetActorLocation());
    const bool bNearby = Position.SizeSquared2D() < FMath::Square(2700.f);
    SetViewState(bNearby, bCurrent);
    if (bCurrentRoom)
    {
        const float Time = GetWorld()->GetTimeSeconds();
        for (int32 Index = 0; Index < 4; ++Index)
        {
            const float Wave =
                FMath::Sin(Time * 7.1f + Index * 1.7f) * .6f + FMath::Sin(Time * 11.3f + Index * 2.3f) * .4f;
            Points[Index]->SetIntensity(PointBase[Index] * (1.f + FMath::Clamp(FlickerAmount, 0.f, .15f) * Wave));
        }
    }
}

bool ADungeonRoomLightingRig::ContainsPlayerLocation(FVector Location) const
{
    const auto Cell = [](FVector Position)
    {
        return FIntPoint(FMath::FloorToInt(Position.X / 2400.f + .5f), FMath::FloorToInt(Position.Y / 2400.f + .5f));
    };
    return Cell(Location) == Cell(GetActorLocation());
}

FString ADungeonRoomLightingRig::GetLightingReport() const
{
    int32 Active = 0;
    int32 Shadows = 0;
    int32 Fire = 0;
    for (const auto* Light : TInlineComponentArray<ULightComponent*>(this))
    {
        const bool bActive = Light->IsVisible() && Light->Intensity > 0.f;
        Active += bActive;
        Shadows += bActive && Light->CastShadows;
    }
    for (const UNiagaraComponent* Flame : Flames)
    {
        Fire += Flame->IsActive();
    }
    return FString::Printf(TEXT("role=%d current=%d weight=%.2f lights=%d shadows=%d flames=%d"),
                           static_cast<int32>(RoomRole), bCurrentRoom, ViewWeight, Active, Shadows, Fire);
}

#if WITH_EDITOR
namespace
{
    void ReportLighting(UWorld* World)
    {
        if (!World)
        {
            return;
        }
        int32 Count = 0;
        for (TActorIterator<ADungeonRoomLightingRig> It(World); It; ++It)
        {
            UE_LOG(LogTemp, Display, TEXT("DungeonLighting %s %s at=%s"), *It->GetName(), *It->GetLightingReport(),
                   *It->GetActorLocation().ToCompactString());
            ++Count;
        }
        UE_LOG(LogTemp, Display, TEXT("DungeonLighting total_rigs=%d"), Count);
    }
    FAutoConsoleCommandWithWorld LightingProbe(TEXT("Dungeon.Lighting.Probe"), TEXT("Report room lighting budgets."),
                                               FConsoleCommandWithWorldDelegate::CreateStatic(&ReportLighting));
    // 临时美术巡检暂停战斗导演，防止战斗门禁将验收传送拉回；关闭 PIE 即恢复。
    void PreviewLighting(UWorld* World)
    {
        if (World && World->IsGameWorld())
        {
            for (TActorIterator<ADungeonRunDirector> It(World); It; ++It)
            {
                It->SetActorTickEnabled(false);
            }
            UE_LOG(LogTemp, Display, TEXT("DungeonLighting preview=1 combat_director_tick=0 (PIE only)"));
        }
    }
    FAutoConsoleCommandWithWorld LightingPreview(TEXT("Dungeon.Lighting.Preview"),
                                                 TEXT("PIE art inspection: pause combat director until PIE ends."),
                                                 FConsoleCommandWithWorldDelegate::CreateStatic(&PreviewLighting));
} // namespace
#endif
