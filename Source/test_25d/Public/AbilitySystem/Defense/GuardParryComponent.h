// 调用链：共享GuardWindow打开GE窗口 -> 接触前裁决 -> 反射/眩晕及独立时间奖励 -> 窗口与奖励分别清理。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "GameplayEffect.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "GuardParryComponent.generated.h"

/** 显式开启的格挡扩展：窗口按暂停感知动作时间，眩晕按世界时间计时。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FGuardParrySettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Window = .15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MeleeStunDuration = 1.f;
    /** 已赢得的独立时间奖励不随格挡结束或取消撤销。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRewardWorldSlow = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RewardLock = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAbilityWorldSlowSettings Reward;
    bool Validate(FString& Error) const;
    /** 共用技能提示，读取当前配置；关闭弹反时不追加文字。 */
    FString Describe() const;
};

/** 仅授予真实眩晕与技能封锁，无伤害、石化材质或周期执行。 */
UCLASS()
class TEST_25D_API UGuardParryStunEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGuardParryStunEffect();
};

/** 精确格挡GE与来源生命共同拥有窗口；攻击ID只在该窗口内去重。 */
struct FGuardParryWindow
{
    FAbilitySourceToken Source;
    FActiveGameplayEffectHandle Guard;
    FGuardParrySettings Settings;
    double Started = 0.;
    TSet<FGuid> Attacks;
    bool bRewardAttempted = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnGuardParried, FActiveGameplayEffectHandle);

/** 窗口属于具体GE；已赢得的奖励属于来源生命，独立于格挡取消/完成并自然到期。 */
UCLASS(ClassGroup = (Ability), meta = (BlueprintSpawnableComponent))
class TEST_25D_API UGuardParryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuardParryComponent();
    static UGuardParryComponent* FindOrCreate(AActor* Target);
    void Open(const FAbilitySourceToken& Source, FActiveGameplayEffectHandle Guard,
              const FGuardParrySettings& Settings);
    void Invalidate();
    /** 只读诊断：自然到期后旧标识可保留，活跃性由时间服务判断。 */
    FGuid GetRewardLease() const
    {
        return RewardLease;
    }
    static bool CanParryProjectile(AActor* Target, const FAbilitySourceToken& AttackSource);
    static void NotifyProjectileParried(AActor* Target, const FAbilitySourceToken& AttackSource, FGuid AttackId);
    /** 仅真实直接近战调用。成功返回true整次拦截伤害/Debuff，并控制原攻击者生命。 */
    static bool TryParryMelee(AActor* Target, const FAbilitySourceToken& AttackSource, FGuid AttackId);
    static bool TryParryMass(AActor* Target, int32 EntityId, FMassEntityHandle Entity, const FVector& Origin,
                             FGuid AttackId);
    FOnGuardParried OnParried;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;

private:
    double Now() const;
    bool Current(const FGuardParryWindow& Window) const;
    FGuardParryWindow* FindWindow();
    static UGuardParryComponent* Eligible(AActor* Target, const FAbilitySourceToken& AttackSource);
    bool Confirm(FGuid AttackId, FActiveGameplayEffectHandle& Guard, FAbilitySourceToken& Defender,
                 float& StunDuration);
    void RewardSuccess(FGuardParryWindow& Window);
    UFUNCTION()
    void OwnerDied(AActor* Actor);
    TArray<FGuardParryWindow> Windows;
    FGuid RewardLease;
    FAbilitySourceToken RewardSource;
    double NextRewardTime = 0.;
};
