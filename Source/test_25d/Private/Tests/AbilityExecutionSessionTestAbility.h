// 测试调用链：真实ASC授予 -> 被动GA会话 -> 完整Commit计数/停用解绑；游戏内容不引用此类。
#pragma once
#include "AbilitySystem/Ability/MyPassiveAbility.h"
#include "AbilityExecutionSessionTestAbility.generated.h"

UCLASS()
class UAbilityExecutionSessionTestAbility : public UMyPassiveAbility
{
    GENERATED_BODY()
public:
    /** 使用可复用实例验证第二次激活，避免把新UObject误当状态清理。 */
    UAbilityExecutionSessionTestAbility();
    /** 记录实际GAS提交次数；仍通过父类的费用/冷却执行入口。 */
    virtual void CommitExecute(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayAbilityActivationInfo Activation) override;
    int32 CommitCount = 0;
};
