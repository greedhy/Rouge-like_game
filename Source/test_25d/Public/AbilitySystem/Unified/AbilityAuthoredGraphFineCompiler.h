// 调用链：实际 authored Graph 快照 -> 每个旧策略的孤立细片段 -> 原边逐条重连 -> 同一 GraphInstance。
// 展开只读 Config.Parameters，稳定表现引用不拥有玩法拓扑；不能从原DA重生成已断开的孩子。
#pragma once
#include "CoreMinimal.h"
class UAbilityGraphDefinition;

namespace AbilityAuthoredGraphFineCompiler
{
    /** 返回独立定义；保留实际额外边、参数覆盖与逻辑深度。失败不修改输入，不回退完整旧宏。 */
    TEST_25D_API UAbilityGraphDefinition* Compile(UObject* Outer, const UAbilityGraphDefinition* Authored,
                                                  FString& Error);
} // namespace AbilityAuthoredGraphFineCompiler
