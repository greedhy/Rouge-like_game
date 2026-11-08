// 主要调用链：公共预警/接触窗口 → ResolveHit 启动孢子区域 → Advance 按脉冲时钟结算 → EndAbility 清理委托。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Ability/TelegraphedEnemyAbility.h"
#include "ShroomEnemyAbility.generated.h"

/** Server field cadence: consume at most one due pulse, never replay missed slots. */
struct FShroomPulseClock
{
    double Started = 0, Ends = 0;
    float Period = 1;
    int32 NextSlot = 0;
    // 以当前时刻重置区域起止时间和下一个脉冲槽位。
    void Reset(double Now, float InPeriod, float Duration)
    {
        Started = Now;
        Ends = Now + Duration;
        Period = InPeriod;
        NextSlot = 0;
    }
    // 只消费当前到期槽位一次，跳过卡顿期间错过的脉冲，避免补发伤害。
    bool Consume(double Now)
    {
        if (!FMath::IsFinite(Now) || !FMath::IsFinite(Period) || Period <= 0 || Now < Started || Now >= Ends)
            return false;
        const int32 Slot = FMath::FloorToInt((Now - Started) / Period + 1.e-6);
        if (Slot < NextSlot)
            return false;
        NextSlot = Slot + 1;
        return true;
    }
};

UCLASS(BlueprintType)
class TEST_25D_API UShroomSporeConfig : public UStoneGolemAttackConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spore")
    float Duration = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spore")
    float Period = 1;
    // 校验可见区域、持续时长、脉冲周期及次数上限。
    bool ValidateSpore(FString& Error) const;
};
UCLASS()
class TEST_25D_API UShroomSlamAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UShroomSlamAbility();
};

UCLASS()
class TEST_25D_API UShroomSporeAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UShroomSporeAbility();
    // 要求孢子配置有效后复用公共敌人激活检查。
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* Source = nullptr,
                                    const FGameplayTagContainer* Target = nullptr,
                                    FGameplayTagContainer* Relevant = nullptr) const override;
    // 重置区域状态并缓存持续时间、周期，再开始公共预警。
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    // 解除持续区域绑定的动画委托，再清理公共攻击状态。
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled) override;

protected:
    // 把预警切为持续危险区域，绑定中断回调并尝试消费首个脉冲。
    virtual void ResolveHit() override;
    // 区域生效前复用父类时序，生效后检查租约并按时钟结算或到期结束。
    virtual void Advance() override;

private:
    FShroomPulseClock Clock;
    float FieldDuration = 3, FieldPeriod = 1;
    bool bFieldActive = false;
};
