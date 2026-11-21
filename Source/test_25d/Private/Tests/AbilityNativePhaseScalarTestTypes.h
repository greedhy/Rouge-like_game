// 测试链：原target/source ASC创建真实Thunder Spec -> IncomingDamage -> 项目属性集实际扣血。
#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Player/MyPlayerController.h"
#include "AbilityNativePhaseScalarTestTypes.generated.h"

/** 不使用固定-10 GE；倍率必须真正改变SetByCaller和Health才能通过。 */
UCLASS()
class UAbilityNativeThunderScalarEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UAbilityNativeThunderScalarEffect();
    static FGameplayTag GetDamageTag();
};

/** 只选择服务器接收TargetData的分支；按键路由仍是正式MyPlayerController::InputKey。 */
UCLASS()
class ANativeRecipeRemoteInputController : public AMyPlayerController
{
    GENERATED_BODY()
public:
    virtual bool IsLocalController() const override
    {
        return false;
    }
};
