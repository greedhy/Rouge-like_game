// 调用链：运行时配方 -> 注册节点 -> 共享查询/几何/节拍/状态服务 -> 类型化端口 -> 后续投递或效果。
// 这些节点不激活其他 GA，不施加隐式伤害；有限节拍状态属于每次调用，减速到期仍属于原状态服务。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityBehaviorNodes.generated.h"

class FAbilityNodeRegistry;

UENUM(BlueprintType)
enum class EAbilityRepeatCadenceProfile : uint8
{
    CatchUp,
    MixedEightPreserve,
    ExtensionSixteenClamp,
    SingleDiscard
};

UENUM(BlueprintType)
enum class EAbilitySpatialAnchor : uint8
{
    OriginalOrigin,
    Source,
    ConfirmedImpact
};

/** 查询完整半径候选后选择一个最近的原生命，不先按列表数量截断候选。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNearestNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 350.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeActors = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeMass = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRequireVisibility = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bExcludeInputTarget = false;
    /** 拒绝无目标类型或不在 (0,2000] 的半径，避免隐式空查询和无界搜索配置。 */
    virtual bool Validate(FString& Error) const override;
};

/** 有限节拍；Count 或 Duration 先到时结束。该配置不声明无限引导/被动权限。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityRepeatNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Count = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Period = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bEmitImmediately = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeFinalBoundary = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityRepeatCadenceProfile CadenceProfile = EAbilityRepeatCadenceProfile::CatchUp;
    /** 仅接受 Count 1..64、时长/周期 .01..60 和已登记追赶策略，不提供无限持续权限。 */
    virtual bool Validate(FString& Error) const override;
    /** 调参只修改真实有限时钟的次数、周期与时长，不自动授予持续策略权限。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

/** 改变位置载荷而不授予活目标权限；Offset 为世界空间偏移。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityAnchorNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilitySpatialAnchor Anchor = EAbilitySpatialAnchor::OriginalOrigin;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Offset = FVector::ZeroVector;
    /** 校验位置枚举及有限偏移；是否实际存在 Impact 由本次执行输入判断。 */
    virtual bool Validate(FString& Error) const override;
};

/** 独立状态 payload，复用原 Actor/Mass 减速所有权和到期通路。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilitySlowNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Multiplier = .65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 2.f;
    /** 保持原状态服务的正倍率合同，并限制本节点持续时间不超过 60 秒。 */
    virtual bool Validate(FString& Error) const override;
    /** 在配方副本上编辑实际减速时长；移动倍率由原配置独立决定。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Scale) override;
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityNearestNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 服务器完整收集并捕获候选，发布一个稳定最近目标；查询为空正常发布 NoTarget。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityRepeatNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 初始化本次独立时钟和次数，可选即时首发；不读取或修改后继效果配置。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 每帧按显式追赶/尾边界策略发布 Pulse；达到 Count 或 Duration 后只完成一次。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 发布入口关闭后释放配置和时钟，不派生取消后的 Pulse 或 Completed。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 每次实际发布才计数；发布/取消回调后重新校验本次调用，不安排未来 Pulse。 */
    bool EmitPulse();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityRepeatNodeConfig> Settings;
    float Age = 0.f;
    float Clock = 0.f;
    int32 Emitted = 0;
};

UCLASS(Transient)
class TEST_25D_API UAbilityAnchorNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 按入口位置、当前施法者或历史命中选择位置；不重新捕获目标，缺少历史事实时失败。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilitySlowNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 先复核原目标生命再调用共享减速服务，图完成后仍由目标服务独立到期清理。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityBehaviorNodes
{
    /** 登记真实执行类型和载荷端口；重复调用不覆盖已有注册。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityBehaviorNodes
