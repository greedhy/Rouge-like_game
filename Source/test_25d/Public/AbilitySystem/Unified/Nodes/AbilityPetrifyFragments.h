// 调用链：石化真实前端/动态默认图 -> 原范围查询 -> TargetSet -> 原状态服务；不另建GA或伤害执行器。
// 查询结果只持有原生命令牌；状态组件/世界子系统继续拥有到期、复制及对象池清理。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityPetrifyFragments.generated.h"

class FAbilityNodeRegistry;
class UAbilityGraphDefinition;

/** 只负责原范围选择，不携带没有被查询消费的石化时长或材质。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPetrifyAreaQueryConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 650.f;
    virtual bool Validate(FString& Error) const override;
    /** 重复端口只准入同调度器内的有界同步查询；库存责任不能降级为位置。 */
    virtual bool SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                            FString& Error) const override;
    /** 半径是本配置的唯一可缩放量；持续时间由后继状态配置独占。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

/** 原状态效果可作为任何完整 TargetSet 查询的后继，不重新查询或截取命中前缀。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPetrifyBatchConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UMaterialInterface> StoneMaterial;
    virtual bool Validate(FString& Error) const override;
    /** 仅消费完整原生命集合；状态到期由既有目标状态所有者清理，不创建图内时钟。 */
    virtual bool SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                            FString& Error) const override;
    /** 只调整本状态持续时间，不能连带修改选取半径或输入伤害。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPetrifyAreaQueryExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 按原 Gather/Enemy/LOS 顺序冻结整批生命；没有目标也正常输出空集合。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPetrifyBatchExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 每个状态施加前重验原生命和来源；同步取消立即停止后续目标，不回滚已落地状态。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityPetrifyFragments
{
    /** 旧完整动作与细查询共用原 broadphase/类型/可见性规则，不替换成中心距离筛选。 */
    TEST_25D_API bool Gather(const FAbilitySourceToken& Source, FVector Origin, float Radius,
                             TArray<FAbilityTargetToken>& OutTargets, FString& Error);
    /** 同步共享状态动作；Continue 在每个目标前调用，回调不被保存或跨帧执行。 */
    TEST_25D_API bool Apply(const FAbilitySourceToken& Source, const TArray<FAbilityTargetToken>& Targets,
                            float Duration, UMaterialInterface* Material, TFunctionRef<bool()> Continue);
    /** 由真实 GA 默认值建立独立查询/效果定义；所有配置属于本图副本。 */
    TEST_25D_API UAbilityGraphDefinition* Build(UObject* Outer, float Radius, float Duration,
                                                UMaterialInterface* Material);
    /** 只登记已经实现的同步批动作；持续/原型根仍由各自显式根合同准入。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPetrifyFragments
