// 调用链：运行时基础配方提取 -> 原型参数/旧 profile -> 细粒度 Schema2 定义 -> 原前端与同一执行器。
// 与零修正的 LegacyGraphCompiler 兼容入口分离，不激活完整旧 GA、不复制第二套调度器、不修改源 DA。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"

class UAbilityPrototype;
class UAbilityGraphDefinition;

struct FAbilityFineFragmentPort
{
    FGuid Node;
    FName Port;
    EAbilityPortPayload Payload = EAbilityPortPayload::Origin;
};

/** 原节点的外部接口表；玩法后继只由实际 authored edges 连接。 */
struct FAbilityFineFragmentBindings
{
    FGuid Entry;
    TMap<FName, TArray<FAbilityFineFragmentPort>> Outputs;
};

struct FAbilityFineFragmentSource
{
    const UAbilityPrototype* Parameters = nullptr;
    UAbilityPrototype* Presentation = nullptr;
    FName Profile;
    int32 LogicalDepth = 0;
    bool bSingleProjectile = false;
    FGuid StableNamespace;
};

namespace AbilityPrototypeFineCompiler
{
    /** 冻结真实参数，展开可替换行为/效果与原深度连接；保留旧逻辑预算、时点与表现寿命。 */
    TEST_25D_API UAbilityGraphDefinition* Compile(UObject* Outer, const UAbilityPrototype* Root, FName Profile,
                                                  FString& Error);
    /** 只展开一个原节点；禁止跟随私有/表现DA中的旧孩子，返回实际外部端口供原边逐条重连。 */
    TEST_25D_API bool AppendIsolated(UAbilityGraphDefinition& Destination, const FAbilityFineFragmentSource& Source,
                                     FAbilityFineFragmentBindings& Out, FString& Error);
} // namespace AbilityPrototypeFineCompiler
