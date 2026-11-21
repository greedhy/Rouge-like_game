// 调用链：准备节点 BindExecution -> 图输入 BelongsTo；仅管理只读参数身份，不执行效果或复制运行对象。
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"

bool UAbilityExecutionParameters::BindExecution(FGuid InRoot, const FAbilitySourceToken& InSource)
{
    AActor* Avatar = nullptr;
    if (Root.IsValid() || !InRoot.IsValid() || !AbilityExecutionIdentity::ResolveSource(InSource, Avatar))
    {
        return false;
    }
    Root = InRoot;
    Source = InSource;
    return true;
}

bool UAbilityExecutionParameters::BelongsTo(FGuid InRoot, const FAbilitySourceToken& InSource) const
{
    return Root.IsValid() && Root == InRoot && Source.World == InSource.World && Source.Avatar == InSource.Avatar &&
           Source.ASC == InSource.ASC && Source.DungeonRunId == InSource.DungeonRunId &&
           Source.EnemyLease == InSource.EnemyLease;
}
