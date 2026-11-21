// 测试链：Continuation 源节点 -> 真实延迟/GE 后继 -> 同执行器回执；仅供自动化注册表使用。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityGraphContinuationTestTypes.generated.h"

/** 三次接触之间必须完整等待下游，不使用测试定时器模拟回执。 */
UCLASS()
class UGraphContinuationTestConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    int32 Count = 3;
};

UCLASS()
class UGraphContinuationTestExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 发布首个接触；后继完成后由同一个 drainer 恢复，不在 Start 内递归调用策略。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 原子地发下一次接触，全部完成后才关闭源调用。 */
    void Next();

private:
    int32 Remaining = 0;
};

/** 一次真实帧边界发布逻辑片段；测试普通 Flush 不能把被推迟的工作误报为已排空。 */
UCLASS()
class UGraphDeferredTestExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual void Advance(float DeltaSeconds) override;

private:
    bool bPublished = false;
};
