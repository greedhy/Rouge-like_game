/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 管理链：DungeonRunDirector维护Actor池与PendingPoolReturns -> 死亡进入等待队列 -> DrainPoolReturns准备休眠/取消能力 -> CanFinishPoolReturn -> Retire并SleepForDungeonPool -> 后续借出复用。
 * 本文件中的池记录是Director持有的普通C++状态，不是独立UObject子系统或全局单例；Actor寿命仍由UWorld管理。非池管理Actor在ReturnEnemyToPool直接Destroy。
 * 死亡与回收分开：先清一次生命的逻辑状态，再等待尸体阶段结束后转为可复用；移动队列后再处理，是为了防止取消技能或广播回调重入当前队列。
 * 池账本不复制；网络可见的怪物表现由EnemyCharacter自身复制/表现通路负责。清理世界时应走Director/Actor生命周期，不能仅清空池记录而假定Actor已销毁。
 */
// 主要调用链：借出或接纳 → 死亡等待 → 归还休眠 → 后续房间复用 → 世界退出销毁。
#pragma once
#include "CoreMinimal.h"
#include "Character/EnemyCharacter.h"

/** Floor-local ownership ledger. Actors remain owned by the World, never the GameInstance. */
struct FDungeonEnemyPool
{
    struct FEntry
    {
        TWeakObjectPtr<AEnemyCharacter> Actor;
        FName LastRoom;
        bool Idle = false;
    };
    TArray<FEntry> Entries;
    // 统计池内有效且尚未销毁的指定精确类对象。
    int32 CountClass(UClass* Class) const
    {
        return Entries
            .FilterByPredicate(
                [Class](const FEntry& E)
                {
                    return E.Actor.IsValid() && !E.Actor->IsActorBeingDestroyed() && E.Actor->GetClass() == Class;
                })
            .Num();
    }
    // 剔除失效和正在销毁的弱引用条目。
    void Prune()
    {
        Entries.RemoveAll(
            [](const FEntry& E)
            {
                return !E.Actor.IsValid() || E.Actor->IsActorBeingDestroyed();
            });
    }
    // 在容量限制内接纳未登记敌人，初始标记为使用中。
    bool Adopt(AEnemyCharacter* Actor, FName Room, int32 Capacity)
    {
        // 剔除失效和正在销毁的弱引用条目。
        Prune();
        if (!IsValid(Actor) || Entries.Num() >= FMath::Clamp(Capacity, 0, 64) ||
            Entries.ContainsByPredicate(
                [&](const FEntry& E)
                {
                    return E.Actor == Actor;
                }))
            return false;
        // 累计操作次数、总耗时和最大耗时。
        Entries.Add({Actor, Room, false});
        return true;
    }
    // 只借出其他房间归还的同类闲置敌人，并更新租用房间。
    AEnemyCharacter* Acquire(UClass* Class, FName Room)
    {
        // 剔除失效和正在销毁的弱引用条目。
        Prune();
        for (auto& E : Entries)
            if (E.Idle && E.LastRoom != Room && E.Actor->GetClass() == Class)
            {
                E.Idle = false;
                E.LastRoom = Room;
                return E.Actor.Get();
            }
        return nullptr;
    }
    // 将正在使用的池对象标记闲置，重复归还返回失败。
    bool Retire(AEnemyCharacter* Actor)
    {
        for (auto& E : Entries)
            if (E.Actor == Actor && !E.Idle)
            {
                E.Idle = true;
                return true;
            }
        return false;
    }
    // 统计有效闲置对象供性能报告使用。
    int32 IdleCount() const
    {
        return Entries
            .FilterByPredicate(
                [](const FEntry& E)
                {
                    return E.Idle && E.Actor.IsValid();
                })
            .Num();
    }
};
