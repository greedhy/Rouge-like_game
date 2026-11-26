// 调用链：权威伤害结算和房间定位 → PlayerState 调用 Lua → 仅向所属客户端复制 HUD 快照。
#pragma once

#include "CoreMinimal.h"
#include "PlayerKillScore.generated.h"

/** 只供显示的当前视图，完整的每房间统计由 Lua 保存。 */
USTRUCT()
struct FPlayerKillScore
{
    GENERATED_BODY()

    UPROPERTY()
    FName Room;

    UPROPERTY()
    int32 WorldNormal = 0;

    UPROPERTY()
    int32 WorldElite = 0;

    UPROPERTY()
    int32 RoomNormal = 0;

    UPROPERTY()
    int32 RoomElite = 0;

    bool operator==(const FPlayerKillScore&) const = default;
};

namespace PlayerKillScore
{
    /** 将已确认的单次死亡记给同世界的玩家来源。世界伤害和敌方 AI 没有玩家身份。 */
    void Credit(AActor* Source, const UWorld* World, FName VictimRoom, bool bElite);
}
