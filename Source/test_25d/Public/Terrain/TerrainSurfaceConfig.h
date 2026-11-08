// 主要调用链：地块 Config → 几何/粒子构造与接触过滤 → 状态组件按 SurfaceType 汇总伤害、减速或缠绕参数；本文件仅定义配置数据。
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TerrainSurfaceConfig.generated.h"

class UNiagaraSystem;
class UStaticMesh;

UENUM(BlueprintType)
enum class ETerrainSurfaceType : uint8
{
    Lava,
    Frost,
    Toxic,
    Vines
};

/** Shared tuning for a terrain tile. Values are read by the authority. */
UCLASS(BlueprintType)
class TEST_25D_API UTerrainSurfaceConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
    ETerrainSurfaceType SurfaceType = ETerrainSurfaceType::Lava;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "0"))
    float DamagePerSecond = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "0"))
    float ExitDuration = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "0.05", ClampMax = "1"))
    float MovementMultiplier = .6f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "0.1"))
    float EntangleDuration = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "0.5"))
    float EntangleRecovery = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    TObjectPtr<UStaticMesh> GroundMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    TObjectPtr<UNiagaraSystem> SurfaceVFX;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    TObjectPtr<UNiagaraSystem> StatusVFX;
};
