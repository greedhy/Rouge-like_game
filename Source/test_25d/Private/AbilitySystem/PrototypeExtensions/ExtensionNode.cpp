/**
 * 扩展数据图：Behavior 决定执行分支，HitChild/PeriodChild/CompleteChild 描述连接；禁止混入父类旧 OnHit/OnPeriod/OnExpire。GA 激活及 Runtime::Spawn -> ValidateGraph -> 文件内 Validate 递归。DA 是共享配置；Context 则记录单次施法预算、地表数及已引爆效果。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "Terrain/TerrainSurface.h"
namespace
{
    /** 递归校验当前节点的数值、行为连接、深度及路径环；Path 是当前递归路径，允许不同分支复用同一合法节点。 */
    bool Validate(const UExtensionNode* N, int32 Depth, TSet<const UExtensionNode*>& Path)
    {
        if (!N || Depth > 3 || Path.Contains(N))
            return false;
        for (float V : {N->Damage, N->DamagePerLevel, N->Range, N->Radius, N->Duration, N->Period, N->Speed,
                        N->CollisionRadius, N->ManaCost, N->Cooldown, N->ArmDelay, N->TransferRadius, N->StackLifetime,
                        N->DetonationDelay, N->MoveDistance, N->SafeDistance, N->DamageFalloff})
            if (!FMath::IsFinite(V) || V < 0)
                return false;
        if (N->Duration < .1f || N->Duration > 60 || N->Period < .1f || N->Range < 1 || N->Radius < 1 || N->Speed < 1 ||
            N->CollisionRadius < 1 || N->Count < 1 || N->Count > 16 || N->MaxTransfers < 0 || N->MaxTransfers > 16 ||
            N->StackThreshold < 1 || N->StackThreshold > 16 || N->StackLifetime < .1f || N->StackLifetime > 60 ||
            N->DetonationDelay > 10 || N->MoveDistance > 1200 || N->DamageFalloff > 1 || N->OnHit || N->OnPeriod ||
            N->OnExpire)
            return false;
        using K = EExtensionKind;
        if (N->Behavior == K::Stack && (N->EffectId.IsNone() || !N->CompleteChild || N->HitChild || N->PeriodChild))
            return false;
        if (N->Behavior == K::Attach && (!N->PeriodChild || N->HitChild))
            return false;
        if (N->Behavior != K::Attach && N->PeriodChild)
            return false;
        if ((N->Behavior == K::Delay || N->Behavior == K::Displace || N->Behavior == K::Surface) && N->HitChild)
            return false;
        if (N->Behavior == K::Delay && N->ArmDelay > N->Duration)
            return false;
        if (N->Behavior == K::Surface && !N->SurfaceClass)
            return false;
        for (auto& Pair : N->ActorTagMovementScale)
            if (!FMath::IsFinite(Pair.Value) || Pair.Value < 0 || Pair.Value > 1)
                return false;
        Path.Add(N);
        for (auto* Child : {N->HitChild.Get(), N->PeriodChild.Get(), N->CompleteChild.Get()})
            if (Child && !Validate(Child, Depth + 1, Path))
                return false;
        Path.Remove(N);
        return true;
    }
} // namespace
/** 新建路径集合，从深度零执行 Validate；失败阻止激活/生成，不修改配置资产。 */
bool UExtensionNode::ValidateGraph() const
{
    TSet<const UExtensionNode*> Path;
    /** 递归校验当前节点的数值、行为连接、深度及路径环；Path 是当前递归路径，允许不同分支复用同一合法节点。 */
    return Validate(this, 0, Path);
}
