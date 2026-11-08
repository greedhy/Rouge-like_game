/**
 * Mass 管线：引擎 PostPhysics 调度 -> Execute -> 逐块可写 FPureMassFragment -> 世界子系统 MoveMass。仅 Server/Standalone 游戏线程执行，排序在 PureMassProcessor 前，保证位移先于普通导航。处理器不持有单次施法状态。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/PrototypeExtensions/ExtensionDisplacementProcessor.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "PureMass/PureMassProcessor.h"
#include "MassExecutionContext.h"
#include "Engine/World.h"
/** 构造 Query 并设置阶段、权威执行标志、游戏线程要求及相对普通移动处理器的顺序。 */
UExtensionDisplacementProcessor::UExtensionDisplacementProcessor() : Query(*this)
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    ExecutionFlags = int32(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
    bRequiresGameThreadExecution = true;
    ExecutionOrder.ExecuteBefore.Add(UPureMassProcessor::StaticClass()->GetFName());
}
/** 向 Mass 查询登记 FPureMassFragment 的读写依赖。 */
void UExtensionDisplacementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
    Query.AddRequirement<FPureMassFragment>(EMassFragmentAccess::ReadWrite);
}
/** 取得本世界扩展子系统，遍历匹配实体块，按本帧 Delta 调用 MoveMass；子系统缺失时跳过。 */
void UExtensionDisplacementProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
    auto* S = Context.GetWorld()->GetSubsystem<UExtensionWorldSubsystem>();
    if (!S)
        return;
    Query.ForEachEntityChunk(Context,
                             [S](FMassExecutionContext& Chunk)
                             {
                                 for (auto& F : Chunk.GetMutableFragmentView<FPureMassFragment>())
                                     S->MoveMass(F, Chunk.GetDeltaTimeSeconds());
                             });
}
