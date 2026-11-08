/**
 * 激活链：装备组件授予Spec -> 输入触发GAS -> 服务器CanActivateAbility/Commit -> 可选Timeline任务或旧定时器 -> AdvanceAt按动画时钟执行事件 -> EndAbility。
 * 生命周期：InstancedPerActor的GA对象可重复激活，一次激活状态必须在结束时清理。EndAbility清定时器、格挡GE、蒙太奇及移动占用；取消时销毁记录的弹体，正常结束则由弹体自己的命中/寿命/代次负责收尾。
 * 网络：NetExecutionPolicy为ServerOnly，伤害与位移在权威端执行；不要把客户端本地GA实例活跃状态当作权威激活状态。人物/蒙太奇与弹体表现依赖各自已有复制通路，本类的事件数组与本次Projectiles记录不是独立复制快照。
 */
// 主要调用链：装备授予动作 GA -> CanActivateAbility -> ActivateAbility -> Advance -> Execute / Move；发射物交由 AStanderWeaponProjectile 推进，EndAbility 清理占用。
#pragma once
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Player/StanderWeaponActionConfig.h"
#include "StanderWeaponActionAbility.generated.h"
class AStanderPlayer;
class AStanderWeaponProjectile;
class UAbilityTask_RunTimeline;
UCLASS()
class TEST_25D_API UStanderWeaponActionAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    // 设置该技能的默认执行策略、模式和激活约束。
    UStanderWeaponActionAbility();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TObjectPtr<UStanderWeaponActionConfig> ActionConfig;
    /** 当前施法的时间轴任务，未配置/已结束时为空；仅供阶段策略查询和运行诊断。 */
    UFUNCTION(BlueprintPure, Category = "Ability|Timeline")
    UAbilityTask_RunTimeline* GetActiveTimelineTask() const
    {
        return TimelineTask;
    }
    // 在蓝图默认对象上同步动作配置的技能身份标签。
    UFUNCTION(BlueprintCallable, Category = "Weapon Authoring")
    void InitializeActionIdentity();
    // 编辑器中重算蒙太奇长度、标脏并检查长度一致性；非编辑器环境返回失败。
    UFUNCTION(BlueprintCallable, Category = "Weapon Authoring")
    static bool RebuildActionMontage(UAnimMontage* Montage);
    // 加载完成后同步动作身份。
    virtual void PostLoad() override;
    // 激活前检查角色状态、配置及伤害依赖，再交给 GAS 检查通用条件。
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                                    const FGameplayTagContainer* = nullptr, const FGameplayTagContainer* = nullptr,
                                    FGameplayTagContainer* = nullptr) const override;
    // 根据冷却标签判断当前技能是否仍处于冷却。
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                               FGameplayTagContainer*) const override;
    // 服务器施加带冷却标签的限时 GE。
    virtual void ApplyCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                               FGameplayAbilityActivationInfo) const override;
    // 读取 ASC 的当前法力，判断是否足够支付本次消耗。
    virtual bool CheckCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                           FGameplayTagContainer*) const override;
    // 服务器通过 SetByCaller 法力 GE 扣除消耗。
    virtual void ApplyCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                           FGameplayAbilityActivationInfo) const override;
    // 提交技能消耗和冷却，锁定朝向与移动，播放蒙太奇并启动推进定时器。
    virtual void ActivateAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                                 FGameplayAbilityActivationInfo, const FGameplayEventData*) override;
    // 清除定时器、技能效果和动画占用，按角色状态恢复移动并结束 GAS 激活。
    virtual void EndAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                            FGameplayAbilityActivationInfo, bool, bool) override;
    // 按技能模式或动作事件生成界面描述。
    virtual FString GetDescription(int32 Level) override;
    // 装备动作没有升级描述，返回空文本。
    virtual FString GetNextLevelDescription(int32 Level) override
    {
        return FString();
    }
    // 换装时取消此技能，避免旧武器动作继续生效。
    virtual bool ShouldCancelOnEquipChange() const override
    {
        return true;
    }
    // 声明此能力只能通过装备授予。
    virtual bool IsEquipmentGrantedOnly() const override
    {
        return true;
    }

private:
    /** 新路径由任务逐帧推进；保留旧Advance内的伤害/死亡/Mass判定。 */
    UFUNCTION()
    void TimelinePosition(float SourcePosition);
    /** 动画自然结束或中断都走现有EndAbility收尾。 */
    UFUNCTION()
    void TimelineFinished(bool bInterrupted);
    UPROPERTY(Transient)
    TObjectPtr<UAbilityTask_RunTimeline> TimelineTask;
    // 定时读取蒙太奇位置，处理命中或事件窗口，并在失效、打断或结束时收尾。
    void Advance();
    /** 两种播放路径共用事件逻辑；时间轴直接传入仍在自然混出的实例位置。 */
    void AdvanceAt(float SourcePosition);
    // 按事件类型处理格挡、发射物或近战命中，表现事件只生成特效。
    void Execute(const FStanderWeaponActionEvent& Event);
    // 根据两次动画位置的差值分段移动，叠加跳跃弧线并阻止跨房间或穿过障碍。
    void Move(const FStanderWeaponActionEvent& Event, float Position);
    TWeakObjectPtr<AStanderPlayer> Player;
    TArray<TWeakObjectPtr<AStanderWeaponProjectile>> Projectiles;
    FTimerHandle Timer;
    FActiveGameplayEffectHandle GuardHandle;
    FVector Forward;
    int32 NextEvent = 0;
    uint8 PreviousMovement = 0;
    bool OwnMovement = false;
    float LastPosition = 0;
    float JumpOffset = 0;
    int32 OriginRoom = INDEX_NONE;
    TSet<int32> BlockedMoves;
    float GuardEnds = -1;
};
