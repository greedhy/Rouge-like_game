// 调用链：Mixed Batch6根帧 -> 随动/持续费用/周期 -> 独立Query与Payload；取消不生成正常到期。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilityPrototypeFollowFieldNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;

/** 不复制参数；只读本次Mixed+Field+bFollowCasterToggle+depth0帧。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeFollowFieldNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeFollowFieldNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 安装本次随动周期调用；不直接造成伤害或激活完整GA。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 随动在费用前，费用在本帧周期前；Duration不截断此根开关合同。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 来源失效、用户取消和费用失败统一释放该调用，不伪造Expired。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 本体载荷先同步结算；OnHit/Period逻辑孩子仍由同图延迟到本帧末。 */
    bool Pulse();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    FAbilityPrototypePresentationFrame Visual;
    FVector Anchor = FVector::ZeroVector;
    float Clock = 0.f;
};

namespace AbilityPrototypeFollowFieldNodes
{
    /** Action/Period仅携带当前位置，独立效果/子片段仍由同图调度。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeFollowFieldNodes
