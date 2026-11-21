// 调用链：Scope.Prototype 的只读参数 -> 独立发射调度 -> Launch 活目标 -> 任意子片段/投递。
// 当前默认合同保留现有 LegacyGraph 的 helper 成功语义；真实逻辑准入是独立待实现合同。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilityPrototypeEmitterNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;

UENUM()
enum class EAbilityPrototypeEmitterAdmission : uint8
{
    LegacyGraphHelper,
    LogicalScope
};

/** 数值只来自冻结帧；准入语义显式登记，不能把 helper 请求谎报为实际子片段成功。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeEmitterNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere)
    EAbilityPrototypeEmitterAdmission Admission = EAbilityPrototypeEmitterAdmission::LegacyGraphHelper;
    /** LogicalScope 尚无独立 receipt 合同，当前明确拒绝，不悄悄退回 helper 语义。 */
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeEmitterNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 初始化本次调度，不施加伤害或自动嵌套激活 GA。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 每次调度保留被选原生命；后继决定实际投递与 payload。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 取消只清理本调用，不补发 Launch 或正常 Expired。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 一次已消费周期的选择/请求；只按显式helper成功计数，不谎报子Scope实际准入。 */
    bool EmitScheduled();
    /** 正常结束排队独立到期分支，并清理准确表现宿主；不等待孩子的独立寿命。 */
    void CompleteEmitter();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    FAbilityPrototypePresentationFrame Visual;
    TOptional<FAbilityTargetToken> Current;
    FVector Direction = FVector::ForwardVector;
    float Age = 0.f;
    float Clock = 0.f;
    int32 SuccessfulRequests = 0;
    bool bCompleting = false;
};

namespace AbilityPrototypeEmitterNodes
{
    /** Origin 输入；Launch LiveTarget，Expired/Completed Origin。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeEmitterNodes
