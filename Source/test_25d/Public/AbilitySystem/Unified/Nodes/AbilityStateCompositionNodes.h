// 调用链：Schema2 LiveTarget/历史命中 -> 附着周期或共享 World 叠层/位移 -> 类型端口 -> 任意后继 payload。
// 本层不激活完整旧 GA，不保存资产；Stop 先封口，再只取消本执行拥有的准确租约。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "AbilityStateCompositionNodes.generated.h"

class FAbilityNodeRegistry;

/** 仅管理锚点、周期和转移；Period 端口的 payload 由图决定。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityAttachNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Period = .25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTransfers = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TransferRadius = 350.f;
    virtual bool Validate(FString& Error) const override;
    /** 仅配置真实通用Attach参数；原型帧配置由父Scope管理，不能静默写入未消费字段。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityAttachNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityAttachNodeConfig> Settings;
    FAbilityTargetToken Anchor;
    TSet<FPureMassCombatTarget> Visited;
    FVector LastPosition = FVector::ZeroVector;
    float Age = 0.f;
    float Clock = 0.f;
    int32 Transfers = 0;
    bool bUsePrototypeFrame = false;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PrototypeVisual;
    uint32 VisualRevision = 1;
};

/** 同配置服务 LiveTarget/ConfirmedImpact/DamageResult 三个显式策略，不把历史事实变回活目标。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityStackNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FExtensionStackSettings Stack;
    virtual bool Validate(FString& Error) const override;
    /** 阈值/寿命/延迟均写入同一个世界状态请求配置；失败不得改变已接受字段。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityStackNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UPrototypeCastContext> Context;
    TWeakObjectPtr<UExtensionWorldSubsystem> WorldState;
    FGuid StackLease;
    bool bUsePrototypeFrame = false;
    FAbilityTargetToken TargetLife;
    FVector LastPosition = FVector::ZeroVector;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PrototypeVisual;
    /** 叠层接管只关闭自己的renderer；不生成结束命中，也不取消接管后的新lease。 */
    void ClosePrototypeVisual(bool bCancelled);
    void Threshold(const FExtensionStackTrigger& Trigger);
    void Released(EExtensionStackReleaseReason Reason);
};

/** 复用真实 Displace 免疫/几何/RootMotion/Mass 协议；配置必须为无子图的单一位移行为。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityTargetDisplaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UExtensionNode> Settings;
    /** 推开时沿权威施法方向；原配置默认继续使用传入位置作为径向中心。 */
    UPROPERTY(EditAnywhere, Category = "Displacement")
    bool bUseCastDirection = false;
    /** 多目标控场可单独跳过移动服务拒绝的目标。 */
    UPROPERTY(EditAnywhere, Category = "Displacement")
    bool bContinueOnRejected = false;
    virtual bool Validate(FString& Error) const override;
    /** 位移配置修改必须私有化Settings，不能写回资产或另一玩家共享的原设置。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 缩放实际移动时长，数值范围与直接设值使用同一校验。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityTargetDisplaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UPrototypeCastContext> Context;
    UPROPERTY(Transient)
    TObjectPtr<UExtensionNode> Settings;
    TWeakObjectPtr<UExtensionWorldSubsystem> WorldState;
    FGuid MovementLease;
    bool bUsePrototypeFrame = false;
    FAbilityTargetToken TargetLife;
    FVector LastPosition = FVector::ZeroVector;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PrototypeVisual;
    uint32 VisualRevision = 1;
};

/** 一个原区域片段拥有一组准确位移租约；世界/处理器仍是唯一移动执行器，节点只管理原区域寿命。 */
UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeAreaDisplaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UPrototypeCastContext> Context;
    UPROPERTY(Transient)
    TObjectPtr<UExtensionNode> Settings;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PrototypeVisual;
    TWeakObjectPtr<UExtensionWorldSubsystem> WorldState;
    TArray<FGuid> MoveLeases;
    float Age = 0.f;
};

namespace AbilityStateCompositionNodes
{
    /** 注册 State.Attach/Stack/StackImpact/StackDamage 与 Movement.TargetDisplace，重复调用幂等。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityStateCompositionNodes
