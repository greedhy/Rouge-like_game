// 调用链：GA 提交后 Start/Adopt -> 世界 Tick -> Graph::Advance；换图 Deinitialize -> Cancel -> 策略 Stop。
// 世界拥有执行对象的强引用；有限技能正常结束 GA 后仍可运行，原来源生命失效则由图验证关闭。
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"
#include "AbilityGraphWorldSubsystem.generated.h"

class UAbilityGraphInstance;

UCLASS()
class TEST_25D_API UAbilityGraphWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    /** 只接受本世界权威执行；幂等接管，关闭中拒绝新执行，不改变其来源令牌。 */
    bool Adopt(UAbilityGraphInstance* Instance, FString& Error);
    /** 外部取消指定施法；不存在或已经结束时无副作用。 */
    void CancelExecution(FGuid ExecutionId, EAbilityGraphEndReason Reason = EAbilityGraphEndReason::Cancelled);
    /** 引擎逐帧驱动当前快照；回调中新接管的图下一帧开始推进，避免数组重入失效。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 世界退出先关闭新接管，再停止全部图并释放引用；不影响其它世界。 */
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual TStatId GetStatId() const override;
    /** 自动化/诊断只读查询，不对客户端复制运行对象。 */
    int32 GetActiveExecutionCount() const;

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAbilityGraphInstance>> Executions;
    bool bClosing = false;
};
