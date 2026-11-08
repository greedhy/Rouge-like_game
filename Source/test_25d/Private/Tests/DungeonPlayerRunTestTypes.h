/** 主要调用链：DungeonPlayerRunTests等自动化用例 -> AttachState建立测试ASC/Avatar -> 生产倒地/复活与GA通路 -> 断言。此处提供最小测试角色和能力类型。 */
#pragma once
#include "CoreMinimal.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystem/Ability/MyPassiveAbility.h"
#include "DungeonPlayerRunTestTypes.generated.h"

UCLASS(Transient, NotBlueprintable)
class ADungeonPlayerRunTestPawn : public ADungeonPlayerCharacter
{
    GENERATED_BODY()
public:
    // 把测试PlayerState作为ASC Owner、夹具角色作为Avatar，绕过正常存档加载。
    void AttachState(AMyPlayerState* State)
    {
        SetPlayerState(State);
        AbilitySystemComponent = State->GetAbilitySystemComponent();
        AttributeSet = State->GetAttributeSet();
        AbilitySystemComponent->InitAbilityActorInfo(State, this);
    }
    // 统计死亡广播次数，检查重复死亡和复用后的委托行为。
    UFUNCTION()
    void CountDeath(AActor* DeadActor)
    {
        ++Deaths;
    }
    // 模拟客户端/服务器角色，测试权威限制。
    void SetTestRole(ENetRole InRole)
    {
        SetRole(InRole);
    }
    int32 Deaths = 0;
};

UCLASS(Transient, NotBlueprintable)
class UDungeonPlayerRunTestPassive : public UMyPassiveAbility
{
    GENERATED_BODY()
public:
    // 每个Actor一个实例，只在服务器执行，保持测试激活可预测。
    UDungeonPlayerRunTestPassive()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    }
};

UCLASS(Transient, NotBlueprintable)
class UDungeonPlayerRunTestToggle : public UDungeonPlayerRunTestPassive
{
    GENERATED_BODY()
public:
    // 标识为按下切换型能力，验证恢复和输入不会按持续技能处理。
    virtual bool IsInputToggleAbility() const override
    {
        return true;
    }
};

UCLASS(Transient, NotBlueprintable)
class UDungeonPlayerRunTestTaggedAbility : public UMyGameplayAbility
{
    GENERATED_BODY()
public:
    // 设置稳定攻击身份，供技能栏与能力恢复查询使用。
    UDungeonPlayerRunTestTaggedAbility()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
        SetAssetTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Attack"))));
    }
};
