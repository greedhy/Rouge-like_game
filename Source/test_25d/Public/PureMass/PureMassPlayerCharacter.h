// 沙盒玩家生命周期：接管时绑定玩家状态能力系统，死亡后在当前地图重生并重新接管，不写入正式存档。
#pragma once

#include "CoreMinimal.h"
#include "Character/PlayerCharacter.h"
#include "PureMassPlayerCharacter.generated.h"

/** Isolated test-map pawn: fresh Mass ability loadout, without importing/writing Actor ability saves. */
UCLASS()
class TEST_25D_API APureMassPlayerCharacter : public APlayerCharacter
{
    GENERATED_BODY()
public:
    // 控制器接管后初始化属性、沙盒技能配置和装备效果。
    virtual void PossessedBy(AController* NewController) override;
    // 服务端只处理一次死亡，延迟清理旧能力效果并在当前地图重新生成玩家。
    virtual void Die(const FVector& DeathImpulse) override;
    // 优先读取玩家状态等级，失去玩家状态时沿用最后缓存。
    virtual int32 GetPlayerLevel_Implementation() override;
    // 测试角色不写入正式玩家进度。
    virtual void SaveProgress_Implementation(const FName& CheckpointTag) override
    {
    }

protected:
    // 将持久玩家状态能力系统绑定到当前角色，并恢复眩晕和界面订阅。
    virtual void InitAbilityActorInfo() override;

private:
    int32 LastKnownLevel = 1;
};
