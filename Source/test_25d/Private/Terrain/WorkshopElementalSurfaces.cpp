// 调用链：服务器生成或客户端复制地表 → 原生默认配置 → ATerrainSurface::OnConstruction/BeginPlay。
// 配置与外观由同一原生类模板持有，网络只需要传播 Actor 类和原地表尺寸。
#include "Terrain/WorkshopElementalSurfaces.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

AWorkshopWaterTerrainSurface::AWorkshopWaterTerrainSurface()
{
    Config = CreateDefaultSubobject<UTerrainSurfaceConfig>(TEXT("WorkshopWaterConfig"));
    Config->SurfaceType = ETerrainSurfaceType::Water;
    Config->MovementMultiplier = 1.f;
    Config->ExitDuration = 0.f;
    Config->DamagePerSecond = 0.f;
    Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    static const ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterMaterial(
        TEXT("/Game/resources/N3D/Materials/Examples/Instances/MI_N3D_Water_Stylized.MI_N3D_Water_Stylized"));
    Config->GroundMesh = Plane.Object;
    Ground->SetMaterial(0, WaterMaterial.Object);
}

void AWorkshopWaterTerrainSurface::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Ground->AddLocalOffset(FVector(0.f, 0.f, 2.f));
}

AWorkshopConductiveTerrainSurface::AWorkshopConductiveTerrainSurface()
{
    Config->SurfaceType = ETerrainSurfaceType::Conductive;
    static const ConstructorHelpers::FObjectFinder<UNiagaraSystem> Charge(
        TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Lightning.NS_Free_Spells_Aura_Lightning"));
    Config->SurfaceVFX = Charge.Object;
}

AWorkshopMistCloudSurface::AWorkshopMistCloudSurface()
{
    Config = CreateDefaultSubobject<UTerrainSurfaceConfig>(TEXT("WorkshopCloudConfig"));
    Config->SurfaceType = ETerrainSurfaceType::MistCloud;
    Config->MovementMultiplier = 1.f;
    Config->ExitDuration = 0.f;
    Config->DamagePerSecond = 0.f;
    ContactHeight = 100.f;
    bRequireGrounded = false;
    Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static const ConstructorHelpers::FObjectFinder<UNiagaraSystem> Mist(
        TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Air.NS_Free_Spells_Aura_Air"));
    Config->SurfaceVFX = Mist.Object;
}

void AWorkshopMistCloudSurface::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    SurfaceParticles->SetRelativeLocation(FVector(0.f, 0.f, CloudCenterHeight));
}

AWorkshopConductiveCloudSurface::AWorkshopConductiveCloudSurface()
{
    Config->SurfaceType = ETerrainSurfaceType::ConductiveCloud;
    static const ConstructorHelpers::FObjectFinder<UNiagaraSystem> Charge(
        TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Lightning.NS_Free_Spells_Aura_Lightning"));
    Config->SurfaceVFX = Charge.Object;
}
