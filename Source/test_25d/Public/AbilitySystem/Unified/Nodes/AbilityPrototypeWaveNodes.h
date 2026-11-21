// 调用链：Scope.Prototype 的只读帧 -> 空间波形选择 -> 独立批效果或活目标状态；时钟不拥有伤害。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilityPrototypeWaveNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;
class FAbilitySelectionAdmissionLedger;

/** 几何、速度、期限及兼容族只来自本片段的冻结参数，没有第二份波形数值。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeWaveNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeWaveNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 初始化本次空间调用；不施加伤害、状态或完整旧 Driver。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 按冻结 profile 推进波形；Contact 先完成，再向状态端口重新验证原生命。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 取消只释放本节点资源，不发布正常到期。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 完整集合先送批载荷；独立状态端口随后逐项复核原生命并有界串行分派。 */
    bool SelectContacts();
    void ContinueStateTargets();
    void FinishWave();
    void FinishStep();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    FAbilityPrototypePresentationFrame Visual;
    TArray<FAbilityTargetToken> StateTargets;
    TSharedPtr<const FAbilityTargetSetSnapshot> StateSelection;
    TSharedPtr<FAbilitySelectionAdmissionLedger> Admissions;
    float Age = 0.f;
    float Radius = 0.f;
    float PreviousRadius = 0.f;
    float PeriodClock = 0.f;
    int32 NextStateTarget = 0;
    bool bStepPending = false;
    bool bFinishAfterStep = false;
    bool bCompleting = false;
};

namespace AbilityPrototypeWaveNodes
{
    /** Contact 整批 TargetSet；TargetFound 活目标；Period/Expired/Completed 仅携带空间事实。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeWaveNodes
