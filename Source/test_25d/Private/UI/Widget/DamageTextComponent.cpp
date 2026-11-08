/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 生命周期链：蓝图飘字动画结束调用DestroyComponent -> 若NumberPool仍有效则Release -> 池化且世界未退出时保留组件；否则调用Super真正销毁。
 * NumberPool为弱引用，不拥有世界子系统。正常入池不等于销毁；池退出会先清回指与bNumberPooled，确保最终DestroyComponent不会被本覆盖函数拦截。组件是本地表现，不同步权威伤害。
 */
// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Widget/DamageTextComponent.h"
#include "UI/Widget/DamageNumberPool.h"
#include "Engine/World.h"

// 动画/蓝图原本的销毁入口被转换成可复用释放。若子系统已无效、未启用池或世界正在退出，则必须落到Super真正销毁。
void UDamageTextComponent::DestroyComponent(bool bPromoteChildren)
{
    if (NumberPool.IsValid())
    {
        NumberPool->Release(this);
        if (bNumberPooled && GetWorld() && !GetWorld()->bIsTearingDown)
            return;
    }
    Super::DestroyComponent(bPromoteChildren);
}
