// 调用链：碎片原前端导出 -> 地面点时钟 -> 独立表现/查询/伤害节点；原整体策略保留兼容入口。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilityArcaneShardFragments.generated.h"

class UAbilityGraphDefinition;
class UAbilityArcaneShardNodeConfig;
class FAbilityNodeRegistry;

/** 同根不可变点参数/查询结果；仅包含配置值与弱生命，不拥有点Actor或计时器。 */
UCLASS(Transient)
class TEST_25D_API UAbilityShardPointFrame : public UAbilityExecutionParameters
{
    GENERATED_BODY()
public:
    /** Clock只冻结参数；Query创建新的只读结果帧，绝不修改已经发布的帧。 */
    static UAbilityShardPointFrame* Create(UObject* Outer, FGuid Root, const FAbilitySourceToken& Source,
                                           const UAbilityArcaneShardNodeConfig& Settings, uint64 Sequence,
                                           const TArray<FAbilityArcaneShardHit>* Hits = nullptr);
    const UAbilityArcaneShardNodeConfig& GetSettings() const;
    const TArray<FAbilityArcaneShardHit>& GetHits() const;
    bool HasCapturedHits() const;
    uint64 GetSequence() const;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityArcaneShardNodeConfig> Settings;
    TArray<FAbilityArcaneShardHit> Hits;
    uint64 Sequence = 0;
    bool bHasCapturedHits = false;
};

/** 时钟完全继承原timer与点Actor清理；每个Point的效果只由显式后继节点提供。 */
UCLASS(Transient)
class TEST_25D_API UAbilityShardPointsExecution : public UAbilityArcaneShardNodeExecution
{
    GENERATED_BODY()
protected:
    virtual void EmitPoint(FVector Location, uint64 Sequence) override;
};

/** 专用消费者没有第二套数值配置；全部读取本根Clock冻结的参数。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityShardFragmentConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

/** 独立payload参数；只允许编译器的类型化伤害轴，不改变原Clock/查询/表现参数。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityShardDamageConfig : public UAbilityShardFragmentConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplier = 1.f;
    virtual bool Validate(FString& Error) const override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityShardTargetsExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 一次查询先捕获整点集合，任何同步GE发生前固定旧生命。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityShardDamageExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 每个旧生命重新验证；LegacyFriendly只走碎片专用GE入口，不能扩大通用目标授权。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityShardCueExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 原Cue与伤害分离，生命周期仍由当前图/来源生命控制。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityArcaneShardFragments
{
    /** 只构造瞬态定义，不施法；各点使用同一时钟，但后继效果可以被配方单独替换。 */
    TEST_25D_API UAbilityGraphDefinition* Build(UObject* Outer, const UAbilityArcaneShardNodeConfig& Settings);
    /** 前端确认目标时读取冻结版本的地面点参数，不回读共享 CDO。 */
    TEST_25D_API const UAbilityArcaneShardNodeConfig* FindSettings(const UAbilityGraphDefinition& Graph);
    /** 注册细粒度节点，不注册另一个执行器。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityArcaneShardFragments
