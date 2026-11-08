/**
 * 瞬发原型朝向适配：原型 GA 成功生成 Runtime 后 -> Apply -> 设置服务器 Avatar 水平朝向 -> ForceNetUpdate。自身施法保持朝向；该路径没有蒙太奇 Motion Warping 消费窗口，不应等待动画通知。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "Character/PlayerCharacter.h"

namespace PrototypeFacing
{
    // Instant prototype casts have no montage window to consume a Motion Warping target.
    // Called only after a successful authoritative cast; self casts retain their facing.
    inline void Apply(AActor* Avatar, const FVector& Target, bool bTargetSelf)
    {
        if (bTargetSelf || !IsValid(Avatar) || !Avatar->HasAuthority() || !Avatar->IsA<APlayerCharacter>() ||
            Target.ContainsNaN())
            return;
        FVector Direction = Target - Avatar->GetActorLocation();
        Direction.Z = 0;
        if (Direction.IsNearlyZero() || Direction.ContainsNaN())
            return;
        Avatar->SetActorRotation(FRotator(0, Direction.Rotation().Yaw, 0));
        Avatar->ForceNetUpdate();
    }
} // namespace PrototypeFacing
