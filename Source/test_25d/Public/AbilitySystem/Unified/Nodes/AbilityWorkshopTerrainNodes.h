// 调用链：Origin -> 创建地表、转换或清除租约 -> 原 ATerrainSurface 接触服务。
// Authority 租约保存当前来源生命，地表自己拥有寿命，切换预览技能不会撤销地表。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "Components/ActorComponent.h"
#include "Terrain/TerrainSurfaceConfig.h"
#include "TimerManager.h"
#include "AbilityWorkshopTerrainNodes.generated.h"

class ATerrainSurface;
class FAbilityNodeRegistry;

/** 只挂在本节点生成的地表上。配方不能写入地图地表或其它玩家的租约。 */
UCLASS(Transient)
class TEST_25D_API UAbilityWorkshopTerrainLease : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 接管已生成地表的服务器寿命，登记死亡、退出及 Run/Avatar 复核。 */
    void Initialize(const FAbilitySourceToken& Source, double Deadline);
    /** 查询时重验原生命、到期和排他转换状态，同一个角色换生命后无法引燃旧地表。 */
    bool IsActiveFor(const AActor& Caster) const;
    /** 重验施法者拥有当前租约后停止接触并销毁地表，失败时撤销临时占用。 */
    bool TryDispelFor(const AActor& Caster);
    /** 转换开始前占用旧地表，重入调用不会重复创建替换物。 */
    void SetTransformReserved(bool bReserved);
    /** Actor 或 Mass 接触入口，复核权威地表和双方生命后结算一次共享元素反应。 */
    static void ApplyElementalContact(ATerrainSurface& Surface, const FPureMassCombatTarget& Target, bool bNewContact);
    double GetExpiry() const;

protected:
    virtual void OnUnregister() override;

private:
    /** 只核对来源；地表扫描和伤害仍由原 Terrain 服务处理。 */
    void RevalidateSource();
    UFUNCTION()
    void OnSourceDied(AActor* Actor);
    UFUNCTION()
    void OnSourceEnded(AActor* Actor, EEndPlayReason::Type Reason);
    /** 先关闭接触，然后销毁本节点生成的地表。 */
    void Retire();

    FAbilitySourceToken SourceToken;
    double ExpiresAt = 0.;
    FTimerHandle SourceCheck;
    bool bTransformReserved = false;
};

/** 调整地表完整尺寸的一半、寿命和资产对应的种类。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityElementSurfaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Terrain")
    float Radius = 210.f;
    UPROPERTY(EditAnywhere, Category = "Terrain")
    float Duration = 8.f;
    UPROPERTY(EditAnywhere, Category = "Terrain")
    ETerrainSurfaceType Kind = ETerrainSurfaceType::Toxic;

    virtual bool Validate(FString& Error) const override;
    /** 验证实际资产枚举和有限数值。非法编辑不会改变原配置。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

/** 唯一转换在本次位置附近的同来源毒床，范围为平面圆。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityIgniteSurfaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Terrain")
    float Radius = 360.f;

    virtual bool Validate(FString& Error) const override;
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityElementSurfaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 成功时发布 Spawned 和 Completed；不足以生成时只完成，不为下游制造虚假落点。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityIgniteSurfaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 引燃后发布旧地表的位置供后继使用；不在这个节点内附加一次伤害。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityWorkshopTerrainNodes
{
    /** 已复核的施法者与选点尽量贴合静态可走地面。 */
    TEST_25D_API FVector ResolveTerrainOrigin(AActor& Caster, const FVector& Requested);
    /** 统一地表搜索的有限半径。 */
    TEST_25D_API bool IsValidSearchRadius(float Radius);
    /** 选取同层和同来源活跃租约，默认只查地面。调用者操作前仍须重验。 */
    TEST_25D_API TArray<TWeakObjectPtr<ATerrainSurface>> FindOwnedSurfaces(AActor& Caster, const FVector& Origin,
                                                                           float Radius,
                                                                           TOptional<ETerrainSurfaceType> Kind,
                                                                           int32 Limit,
                                                                           bool bCloudLayer = false);
    /** 用原地块的位置、形状和到期时刻完成同来源转换，成功返回地块中心。 */
    TEST_25D_API TOptional<FVector> ConvertSurface(const FAbilitySourceToken& Source, const FVector& RequestedOrigin,
                                                   float Radius, ETerrainSurfaceType InputKind,
                                                   ETerrainSurfaceType ResultKind);
    /** 注册两个有限 Origin 动作，不开放持续图的无限地表生成。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityWorkshopTerrainNodes
