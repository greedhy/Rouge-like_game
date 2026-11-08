/**
 * 调用链：控制器输入 -> AbilityInputTagPressed/Held/Released -> GAS激活/输入事件；技能菜单 -> ServerEquipAbility/ServerSpendSpellPoint -> 修改Spec -> 标记复制并向所属客户端发UI通知。
 * 同步：技能Spec由GAS管理复制，ClientEquipAbility/ClientUpdateAbilityStatus是UI事件通知，不能替代Spec复制；OnRep_ActivateAbilities等待能力类引用可用后广播就绪。ServerOnly能力不能用客户端Spec.IsActive来决定服务端是否执行切换。
 * 生命周期：组件属于PlayerState，InitAbilityActorInfo把当前Pawn绑定为Avatar。永久学习、装备临时授予、持续激活是不同生命周期；装备能力只按其授予句柄回收，旅行存档不应永久记录装备专属Spec。回调绑定与ActorInfo重建必须由角色初始化入口协调，避免重生重复订阅。
 */
// 主要调用链：玩家控制器输入/技能菜单RPC/角色初始化 → ASC授予、激活、换槽与恢复 → GAS能力执行及UI委托；装备专属能力由EquipmentComponent管理。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "MyAbilitySystemComponent.generated.h"

class ULoadScreenSaveGame;
class UMyAbilitySystemComponent;
DECLARE_MULTICAST_DELEGATE_OneParam(FEffectAssectTags, const FGameplayTagContainer);
DECLARE_MULTICAST_DELEGATE(FAbilitiesGiven);
DECLARE_DELEGATE_OneParam(FForEachAbility, const FGameplayAbilitySpec&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FAbilityStatusChanged, const FGameplayTag& /*AbilityTag*/,
                                       const FGameplayTag& /*StatusTag*/, int32 /*AbilityLevel*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FAbilityEquipped, const FGameplayTag& /*AbilityTag*/,
                                      const FGameplayTag& /*Status*/, const FGameplayTag& /*Slot*/,
                                      const FGameplayTag& /*PrevSlot*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDeactivatePassiveAbility, const FGameplayTag& /*AbilityTag*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FActivatePassiveEffect, const FGameplayTag& /*AbilityTag*/, bool /*bActivate*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOffensiveAttackTargeted, UGameplayAbility*, AActor*);

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyAbilitySystemComponent : public UAbilitySystemComponent
{
    GENERATED_BODY()
public:
    // 绑定自身效果应用委托，交给客户端回调提取资产标签。
    void AbilityActorInfoSet();

    FEffectAssectTags EffectAssectTags;
    FAbilitiesGiven AbilitiesGiven;
    FAbilityStatusChanged AbilityStatusChanged;
    FAbilityEquipped AbilityEquipped;
    FDeactivatePassiveAbility DeactivatePassiveAbility;
    FActivatePassiveEffect ActivatePassiveEffect;
    // Server-local accepted attack signal. Cosmetic/projectile damage must not emit it.
    FOffensiveAttackTargeted OffensiveAttackTargeted;
    // 校验服务器权限、攻击提交及敌对存活目标，广播有效攻击事件。
    void NotifyOffensiveAttack(UGameplayAbility* SourceAbility, AActor* Target);

    bool bStartupAbilityGiven = false;

    // 授予初始主动技能，写入输入与装备标签，最后广播就绪。
    void AddCharacterAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupAbilities);
    // 授予初始被动技能并立即尝试激活。
    void AddCharacterPassiveAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupPassiveAbilities);

    // 跳过开关技能，对匹配输入标签的普通技能尝试激活。
    void AbilityInputTagHeld(const FGameplayTag& InputTag);
    // 跳过开关技能，为普通活动技能转发松开事件。
    void AbilityInputTagReleased(const FGameplayTag& InputTag);
    // 锁定技能列表并执行外部遍历委托。
    void ForEachAbility(const FForEachAbility& Delegate);
    static FGameplayTag GetAbilityTagFromSpec(const FGameplayAbilitySpec& AbilitySpec);
    static FGameplayTag GetInputTagFromSpec(const FGameplayAbilitySpec& AbilitySpec);
    static FGameplayTag GetStatusFromSpec(const FGameplayAbilitySpec& AbilitySpec);

    void UpgradeAttribute(const FGameplayTag& AttributeTag);
    void UpdateAbilityStatuses(int32 Level);
    FGameplayAbilitySpec* GetSpecFromAbilityTag(const FGameplayTag& AbilityTag);

    UFUNCTION(Server, Reliable)
    void ServerUpgradeAttribute(const FGameplayTag& AttributeTag);

    // 恢复存档等级槽位，兼容旧开关技能并恢复已装备被动。
    void AddCharacterAbilitiesFromSaveData(ULoadScreenSaveGame* SaveData);

    // 开关技能提交服务器切换，普通活动技能转发输入事件。
    void AbilityInputTagPressed(const FGameplayTag& InputTag);
    UFUNCTION(Server, Reliable)
    // 服务器校验输入槽及技能类型，再取消或激活开关技能。
    void ServerToggleAbility(FGameplayAbilitySpecHandle AbilityHandle, FGameplayTag InputTag);

    FGameplayTag GetStatusFromAbilityTag(const FGameplayTag& AbilityTag);
    UFUNCTION(BlueprintPure, Category = "Abilities|Input")
    FGameplayTag GetSlotFromAbilityTag(const FGameplayTag& AbilityTag);
    bool SlotIsEmpty(const FGameplayTag& Slot);
    static bool AbilityHasAnySlot(const FGameplayAbilitySpec& Spec);
    FGameplayAbilitySpec* GetSpecWithSlot(const FGameplayTag& Slot);
    bool IsPassiveAbility(const FGameplayAbilitySpec& Spec) const;
    static void AssignSlotToAbility(FGameplayAbilitySpec& Spec, const FGameplayTag& Slot);

    UFUNCTION(NetMulticast, Unreliable)
    void MulticastActivatePassiveEffect(const FGameplayTag& AbilityTag, bool bActivate);

    UFUNCTION(Server, Reliable)
    // 排除装备专属技能，消耗点数解锁或升级并同步等级状态。
    void ServerSpendSpellPoint(const FGameplayTag& AbilityTag);

    UFUNCTION(Server, Reliable)
    // 校验槽位资格，处理原占用技能与被动激活后同步装备结果。
    void ServerEquipAbility(const FGameplayTag& AbilityTag, const FGameplayTag& Slot);

    UFUNCTION(Client, Reliable)
    void ClientEquipAbility(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot,
                            const FGameplayTag& PreviousSlot);

    bool GetDescriptionsByAbilityTag(const FGameplayTag& AbilityTag, FString& OutDescription,
                                     FString& OutNextLevelDescription);

    static void ClearSlot(FGameplayAbilitySpec* Spec);
    void ClearAbilitiesOfSlot(const FGameplayTag& Slot);
    static bool AbilityHasSlot(const FGameplayAbilitySpec& Spec, const FGameplayTag& Slot);
    static bool AbilityHasSlot(FGameplayAbilitySpec* Spec, const FGameplayTag& Slot);

protected:
    virtual void OnRep_ActivateAbilities() override;

    UFUNCTION(Client, Reliable)
    // 提取效果资产标签并广播给订阅者。
    void ClientEffectApplied(UAbilitySystemComponent* AbilitySystemComponent, const FGameplayEffectSpec& EffectSpec,
                             FActiveGameplayEffectHandle ActiveEffectHandle);

    UFUNCTION(Client, Reliable)
    void ClientUpdateAbilityStatus(const FGameplayTag& AbilityTag, const FGameplayTag& StatusTag, int32 AbilityLevel);
};
