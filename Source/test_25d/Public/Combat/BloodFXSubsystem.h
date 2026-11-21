// 调用链：角色/Crowd 本地表现 → 血液世界管理器 → Niagara；Tick/世界退出释放独立血迹。
#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Subsystems/WorldSubsystem.h"
#include "BloodFXSubsystem.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UDecalComponent;
class UMaterialInterface;

/** 仅保存美术引用与尺寸，软引用在实际需要表现时加载。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FBloodFXStyle
{
    GENERATED_BODY()

    FBloodFXStyle();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    bool bGreenBlood = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood", meta = (ClampMin = "0.1", ClampMax = "3"))
    float SizeMultiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> AirborneHit;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> AirborneHitGreen;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> HitStain;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> HitStainGreen;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> HeavyStain;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> DeathStain;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> DeathStainGreen;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> SmallPool;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> LargePool;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    TSoftObjectPtr<UNiagaraSystem> Drip;
};

USTRUCT()
struct FBloodFXInstance
{
    GENERATED_BODY()
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> Component;
    UPROPERTY(Transient)
    TObjectPtr<UDecalComponent> Decal;
    double ExpiresAt = 0;
    bool bPool = false;
};

/** 每个本地世界最多 20 个血泊、64 个短特效；组件独立于怪物的池生命周期。 */
UCLASS()
class TEST_25D_API UBloodFXSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    // 校验位置后显示一次空中血花和贴地血斑。
    void ShowHit(const FBloodFXStyle& Style, FVector Location, FVector Direction, float Radius, bool bHeavy);
    // 一次死亡扩散与血泊，位置投射失败时保持失败关闭。
    void ShowDeath(const FBloodFXStyle& Style, FVector Location, FVector Direction, float Radius, bool bLarge);
    // 每次采样生成独立的短暂地面滴血，旧滴血不随角色移动。
    void ShowDrip(const FBloodFXStyle& Style, FVector Location, float Radius);
    // 将血斑的局部 Y 法线对齐地面，Yaw 沿地面旋转。
    static FTransform MakeGroundTransform(FVector Point, FVector Normal, float Yaw, float Scale);
    // 仅在真实 Game/PIE 世界创建本地表现管理器。
    bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    bool IsTickable() const override;
    void Deinitialize() override;
    int32 GetPoolCount() const;
    int32 GetTransientCount() const;
    int32 GetTotalSpawned() const
    {
        return TotalSpawned;
    }

private:
    // 使用 WorldStatic 对象查询，忽略角色与武器。
    bool FindGround(FVector Location, FHitResult& Hit) const;
    // Spawn 时先设置用户参数再激活，管理器保活并统一到期清理。
    void Spawn(const TSoftObjectPtr<UNiagaraSystem>& Asset, const FTransform& Transform, float Duration, bool bPool,
               bool bTintGreen, bool bGround, bool bDrip = false);
    void RemoveAt(int32 Index);
    UPROPERTY(Transient)
    TArray<FBloodFXInstance> Instances;
    UPROPERTY(Transient)
    TMap<FSoftObjectPath, TObjectPtr<UNiagaraSystem>> LoadedSystems;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> GroundMaterial;
    int32 TotalSpawned = 0;
};
