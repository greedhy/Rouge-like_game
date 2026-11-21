// 调用链：角色/Crowd 的表现快照 → 本地独立 Niagara → Tick 到期与世界退出清理。
#include "Combat/BloodFXSubsystem.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

FBloodFXStyle::FBloodFXStyle()
{
    const FString Root(TEXT("/Game/Art/BloodFX/Systems/"));
    const auto BloodAsset = [&Root](const TCHAR* Name)
    {
        return TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(Root + Name + TEXT(".") + Name));
    };
    AirborneHit = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Assets/Effects/Combat/NS_BloodImpact.NS_BloodImpact")));
    AirborneHitGreen = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Assets/Effects/Combat/NS_BloodImpact_green.NS_BloodImpact_green")));
    HitStain = BloodAsset(TEXT("NS_Blood_Hit_Splash"));
    HitStainGreen = BloodAsset(TEXT("NS_Blood_Hit_Splash_Green"));
    HeavyStain = BloodAsset(TEXT("NS_Blood_Hit_Heavy"));
    DeathStain = BloodAsset(TEXT("NS_Blood_Death_Burst"));
    DeathStainGreen = BloodAsset(TEXT("NS_Blood_Death_Burst_Green"));
    SmallPool = BloodAsset(TEXT("NS_Blood_Pool_Small"));
    LargePool = BloodAsset(TEXT("NS_Blood_Pool_Large"));
    Drip = BloodAsset(TEXT("NS_Blood_Drip_Loop"));
}

FTransform UBloodFXSubsystem::MakeGroundTransform(FVector Point, FVector Normal, float Yaw, float Scale)
{
    Normal = Normal.GetSafeNormal(FVector::FReal(UE_SMALL_NUMBER), FVector::UpVector);
    const FQuat Facing = FRotationMatrix::MakeFromY(Normal).ToQuat();
    return FTransform(FQuat(Normal, FMath::DegreesToRadians(Yaw)) * Facing, Point + Normal * 1.5, FVector(Scale));
}

bool UBloodFXSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UBloodFXSubsystem::FindGround(FVector Location, FHitResult& Hit) const
{
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(BloodGround), false);
    return GetWorld() &&
           GetWorld()->LineTraceSingleByObjectType(Hit, Location + FVector(0, 0, 40), Location - FVector(0, 0, 600),
                                                   FCollisionObjectQueryParams(ECC_WorldStatic), Query) &&
           Hit.ImpactNormal.Z > .35;
}

void UBloodFXSubsystem::Spawn(const TSoftObjectPtr<UNiagaraSystem>& Asset, const FTransform& Transform, float Duration,
                              bool bPool, bool bTintGreen, bool bGround, bool bDrip)
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender() || Asset.IsNull() ||
        !Transform.IsValid() || !FMath::IsFinite(Duration) || Duration <= 0.f)
    {
        return;
    }

    TObjectPtr<UNiagaraSystem>& System = LoadedSystems.FindOrAdd(Asset.ToSoftObjectPath());
    if (!System)
    {
        System = Asset.LoadSynchronous();
    }
    if (!System)
    {
        return;
    }

    // 分别限制长血泊与短特效，达到上限先清除同类最旧的组件。
    const int32 Limit = bPool ? 20 : 64;
    const int32 Count = bPool ? GetPoolCount() : GetTransientCount();
    if (Count >= Limit)
    {
        const int32 Oldest = Instances.IndexOfByPredicate(
            [bPool](const FBloodFXInstance& Instance)
            {
                return Instance.bPool == bPool;
            });
        if (Oldest != INDEX_NONE)
        {
            RemoveAt(Oldest);
        }
    }
    UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        World, System, Transform.GetLocation(), Transform.Rotator(), Transform.GetScale3D(), false, false,
        ENCPoolMethod::None, false);
    if (!Component)
    {
        return;
    }

    Component->SetCanEverAffectNavigation(false);
    if (bGround)
    {
        // 同步粒子寿命与宿主清理时间，血泊不会因原始 1 秒粒子寿命提前消失。
        Component->SetVariableFloat(TEXT("User.Lifetime Min"), 1.f);
        Component->SetVariableFloat(TEXT("User.Lifetime Max"), 1.f);
        Component->SetVariableFloat(TEXT("User.Lifetime Multiply"), Duration);
        if (bTintGreen)
        {
            const FLinearColor Green(0.f, 1.f, .46f, 1.f);
            Component->SetVariableLinearColor(TEXT("User.Color A"), Green);
            Component->SetVariableLinearColor(TEXT("User.Color B"), Green);
        }
        if (bDrip)
        {
            // 使用短宿主期限截断循环源。每个采样独立，脚下移动不会搬走旧血迹。
            Component->SetVariableFloat(TEXT("User.Scale"), .6f);
            Component->SetVariableInt(TEXT("User.Spawn Burst Min"), 1);
            Component->SetVariableInt(TEXT("User.Spawn Burst Max"), 1);
        }
    }
    Component->Activate(true);
    FBloodFXInstance& Instance = Instances.AddDefaulted_GetRef();
    Instance.Component = Component;
    Instance.ExpiresAt = World->GetTimeSeconds() + Duration;
    Instance.bPool = bPool;
    if (bGround)
    {
        // 原血包的半透明面片在部分地面上过淡，独立贴花提供可读的持久底色。
        // 贴花与 Niagara 共用一个预算和期限，不依赖尸体，也不延长池中角色的生命。
        if (!GroundMaterial)
        {
            GroundMaterial = LoadObject<UMaterialInterface>(
                nullptr, TEXT("/Game/Art/BloodFX/Materials/M_BloodGroundDecal.M_BloodGroundDecal"));
        }
        if (GroundMaterial)
        {
            auto* Material = UMaterialInstanceDynamic::Create(GroundMaterial, this);
            Material->SetVectorParameterValue(TEXT("BloodTint"), bTintGreen ? FLinearColor(.025f, .22f, .045f)
                                                                            : FLinearColor(.25f, .008f, .004f));
            const FVector Normal = Transform.GetUnitAxis(EAxis::Y);
            const float Extent =
                (bPool ? (Duration > 20.f ? 120.f : 75.f) : (bDrip ? 18.f : 45.f)) * Transform.GetScale3D().X;
            Instance.Decal = UGameplayStatics::SpawnDecalAtLocation(World, Material, FVector(12.f, Extent, Extent),
                                                                    Transform.GetLocation(),
                                                                    FRotationMatrix::MakeFromX(-Normal).Rotator(), 0.f);
            if (Instance.Decal)
            {
                Instance.Decal->SetFadeScreenSize(.001f);
                Instance.Decal->SetFadeOut(FMath::Max(0.f, Duration - .3f), .3f, false);
            }
        }
    }
    ++TotalSpawned;
}

void UBloodFXSubsystem::ShowHit(const FBloodFXStyle& Style, FVector Location, FVector Direction, float Radius,
                                bool bHeavy)
{
    if (!Style.bEnabled || Location.ContainsNaN() || Direction.ContainsNaN())
    {
        return;
    }
    const float Scale = FMath::Clamp(Radius / 42.f * Style.SizeMultiplier, .4f, 3.f);
    const TSoftObjectPtr<UNiagaraSystem>& Airborne = Style.bGreenBlood ? Style.AirborneHitGreen : Style.AirborneHit;
    Spawn(Airborne, FTransform(Direction.Rotation(), Location, FVector(Scale)), .65f, false, false, false);
    FHitResult Ground;
    if (FindGround(Location, Ground))
    {
        const TSoftObjectPtr<UNiagaraSystem>& Stain =
            bHeavy ? Style.HeavyStain : (Style.bGreenBlood ? Style.HitStainGreen : Style.HitStain);
        const FTransform Placement =
            MakeGroundTransform(Ground.ImpactPoint, Ground.ImpactNormal, Direction.Rotation().Yaw, Scale);
        Spawn(Stain, Placement, bHeavy ? 1.f : .7f, false, Style.bGreenBlood, true);
    }
}

void UBloodFXSubsystem::ShowDeath(const FBloodFXStyle& Style, FVector Location, FVector Direction, float Radius,
                                  bool bLarge)
{
    if (!Style.bEnabled || Location.ContainsNaN() || Direction.ContainsNaN())
    {
        return;
    }
    FHitResult Ground;
    if (!FindGround(Location, Ground))
    {
        return;
    }
    const float Scale = FMath::Clamp(Radius / 42.f * Style.SizeMultiplier, .4f, 3.f);
    const FTransform Placement =
        MakeGroundTransform(Ground.ImpactPoint, Ground.ImpactNormal, Direction.Rotation().Yaw, Scale);
    Spawn(Style.bGreenBlood ? Style.DeathStainGreen : Style.DeathStain, Placement, 1.2f, false, Style.bGreenBlood,
          true);
    Spawn(bLarge ? Style.LargePool : Style.SmallPool, Placement, bLarge ? 40.f : 20.f, true, Style.bGreenBlood, true);
}

void UBloodFXSubsystem::ShowDrip(const FBloodFXStyle& Style, FVector Location, float Radius)
{
    if (!Style.bEnabled || Location.ContainsNaN())
    {
        return;
    }
    FHitResult Ground;
    if (FindGround(Location, Ground))
    {
        const float Scale = FMath::Clamp(Radius / 42.f * Style.SizeMultiplier, .4f, 3.f);
        Spawn(Style.Drip, MakeGroundTransform(Ground.ImpactPoint, Ground.ImpactNormal, 0.f, Scale), .85f, false,
              Style.bGreenBlood, true, true);
    }
}

void UBloodFXSubsystem::Tick(float DeltaTime)
{
    const double Now = GetWorld()->GetTimeSeconds();
    for (int32 Index = Instances.Num() - 1; Index >= 0; --Index)
    {
        const FBloodFXInstance& Instance = Instances[Index];
        if (!IsValid(Instance.Component) || Instance.ExpiresAt <= Now)
        {
            RemoveAt(Index);
        }
    }
}

TStatId UBloodFXSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UBloodFXSubsystem, STATGROUP_Tickables);
}

bool UBloodFXSubsystem::IsTickable() const
{
    return !IsTemplate() && !Instances.IsEmpty();
}

void UBloodFXSubsystem::RemoveAt(int32 Index)
{
    if (IsValid(Instances[Index].Component))
    {
        Instances[Index].Component->DestroyComponent();
    }
    if (IsValid(Instances[Index].Decal))
    {
        Instances[Index].Decal->DestroyComponent();
    }
    Instances.RemoveAt(Index);
}

int32 UBloodFXSubsystem::GetPoolCount() const
{
    int32 Count = 0;
    for (const FBloodFXInstance& Instance : Instances)
    {
        Count += Instance.bPool ? 1 : 0;
    }
    return Count;
}

int32 UBloodFXSubsystem::GetTransientCount() const
{
    return Instances.Num() - GetPoolCount();
}

void UBloodFXSubsystem::Deinitialize()
{
    while (!Instances.IsEmpty())
    {
        RemoveAt(Instances.Num() - 1);
    }
    LoadedSystems.Reset();
    GroundMaterial = nullptr;
    Super::Deinitialize();
}
