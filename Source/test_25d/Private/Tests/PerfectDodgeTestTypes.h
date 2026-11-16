// 测试夹具：原攻击适配器施加真实GE，观察Health和Debuff标签是否被整次拦截。
#pragma once
#include "GameplayEffect.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "PerfectDodgeTestTypes.generated.h"
UCLASS()
class UPerfectDodgeHitTestEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UPerfectDodgeHitTestEffect();
};

/** 临时测试世界直接绑定真实PlayerState ASC，避免PossessedBy启动存档和HUD。 */
UCLASS(Transient, NotBlueprintable)
class APerfectDodgeTestDungeonPlayer : public ADungeonPlayerCharacter
{
    GENERATED_BODY()
public:
    void AttachState(AMyPlayerState* State)
    {
        SetPlayerState(State);
        AbilitySystemComponent = State->GetAbilitySystemComponent();
        AttributeSet = State->GetAttributeSet();
        AbilitySystemComponent->InitAbilityActorInfo(State, this);
        AbilitySystemComponent->AddAttributeSetSubobject(State->GetAttributeSet());
    }
};
