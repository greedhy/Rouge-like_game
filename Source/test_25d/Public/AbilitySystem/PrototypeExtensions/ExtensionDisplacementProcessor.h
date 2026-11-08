/**
 * Mass 管线：引擎 PostPhysics 调度 -> Execute -> 逐块可写 FPureMassFragment -> 世界子系统 MoveMass。仅 Server/Standalone 游戏线程执行，排序在 PureMassProcessor 前，保证位移先于普通导航。处理器不持有单次施法状态。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "ExtensionDisplacementProcessor.generated.h"
UCLASS()
class TEST_25D_API UExtensionDisplacementProcessor : public UMassProcessor
{
    GENERATED_BODY()
public:
    /** 构造 Query 并设置阶段、权威执行标志、游戏线程要求及相对普通移动处理器的顺序。 */
    UExtensionDisplacementProcessor();

protected:
    /** 向 Mass 查询登记 FPureMassFragment 的读写依赖。 */
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& Manager) override;
    /** 取得本世界扩展子系统，遍历匹配实体块，按本帧 Delta 调用 MoveMass；子系统缺失时跳过。 */
    virtual void Execute(FMassEntityManager& Manager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery Query;
};
