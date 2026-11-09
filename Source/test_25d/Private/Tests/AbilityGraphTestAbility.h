// 调用链：统一图集成测试 -> ASC 授予测试 GA -> 生产目标任务/图任务；这些类型不授予正式玩家。
#pragma once
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "AbilityGraphTestAbility.generated.h"

/** 模拟服务器所见的远程控制端，使生产 TargetData 任务走 ASC 复制数据委托。 */
UCLASS()
class AAbilityGraphRemoteTestController : public APlayerController
{
    GENERATED_BODY()
public:
    /** 不模拟真实网络传输；仅选择生产服务器收取目标的分支。 */
    virtual bool IsLocalController() const override
    {
        return false;
    }
};

UCLASS()
class UAbilityGraphTestCost : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 标准瞬时 Mana -10 GE，用真实属性断言次数。 */
    UAbilityGraphTestCost();
};

UCLASS()
class UAbilityGraphTestCooldown : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 标准 2 秒冷却 GE，授予独立样例冷却 Tag。 */
    UAbilityGraphTestCooldown();
};

UCLASS()
class UAbilityGraphTestAbility : public UUnifiedGraphAbility
{
    GENERATED_BODY()
public:
    /** 配置标准 GE；其余激活/目标/Commit/结束沿用生产实现。 */
    UAbilityGraphTestAbility();
    /** 测试世界无需运行引擎任务 Tick 管理器，显式驱动真实任务。 */
    void StepGraph(float Delta);
};

/** 使用真实ASC开关入口，CDO即声明开关语义，避免测试只修改实例绕过输入路由。 */
UCLASS()
class UAbilityGraphToggleTestAbility : public UAbilityGraphTestAbility
{
    GENERATED_BODY()
public:
    UAbilityGraphToggleTestAbility()
    {
        bToggleInput = true;
        TargetMode = EUnifiedAbilityTargetMode::Self;
    }
};
