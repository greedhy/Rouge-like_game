// 调用链：Actor 伤害结算 -> 独立受伤硬直和破防 Spec -> 世界计时、标签和属性恢复。
#pragma once

#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "NativeGameplayTags.h"
#include "EnemyCombatReactionAbilities.generated.h"

namespace EnemyCombatReactionTags
{
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hitstun);
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(PoiseBroken);
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(ReactionAbility);
}

USTRUCT(BlueprintType)
struct FEnemyCombatReactionSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Hitstun")
    bool bAllowHitstun = true;
    UPROPERTY(EditAnywhere, Category = "Hitstun", meta = (ClampMin = "0.05"))
    float HitstunSeconds = .27f;
    UPROPERTY(EditAnywhere, Category = "Hitstun", meta = (ClampMin = "0.05"))
    float HitstunLimitSeconds = 1.2f;
    UPROPERTY(EditAnywhere, Category = "Hitstun", meta = (ClampMin = "1"))
    int32 HitLimit = 5;
    UPROPERTY(EditAnywhere, Category = "Hitstun", meta = (ClampMin = "0.0"))
    float EscapeGraceSeconds = .55f;
    UPROPERTY(EditAnywhere, Category = "Hitstun")
    bool bAllowLaunch = true;

    UPROPERTY(EditAnywhere, Category = "Poise", meta = (ClampMin = "1.0"))
    float MaxPoise = 60.f;
    UPROPERTY(EditAnywhere, Category = "Poise", meta = (ClampMin = "1.0"))
    float PoiseRecoveryPerSecond = 45.f;
    UPROPERTY(EditAnywhere, Category = "Poise", meta = (ClampMin = "0.0"))
    float PoiseDamageMultiplier = 1.f;
};

/** 仅承接实际伤害。续击延长短硬直，达到独立次数或时间上限后开始脱离保护。 */
UCLASS(NotBlueprintable)
class TEST_25D_API UEnemyHitstunAbility : public UMyGameplayAbility
{
    GENERATED_BODY()

public:
    UEnemyHitstunAbility();
    bool ReceiveHit(const FEnemyCombatReactionSettings& Settings);
    /** 每轮普通硬直最多施加一次活体位移，续击无法反复刷新滞空。 */
    bool ConsumeLaunch();

protected:
    void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                         FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                    FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;

private:
    void UpdateExpiry(const FEnemyCombatReactionSettings& Settings, double Now);
    void Expire();

    FTimerHandle ExpiryTimer;
    double Started = 0.;
    double GraceEnds = 0.;
    int32 Hits = 0;
    bool bUsedLaunch = false;
    bool bOwnsTag = false;
};

/** 从零韧性启动，独立恢复属性至满值。普通硬直的次数和免控不参与恢复。 */
UCLASS(NotBlueprintable)
class TEST_25D_API UEnemyPoiseBreakAbility : public UMyGameplayAbility
{
    GENERATED_BODY()

public:
    UEnemyPoiseBreakAbility();

protected:
    void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                         FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                    FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;

private:
    void Recover();
    FTimerHandle RecoveryTimer;
    double LastRecovery = 0.;
    float RecoveryPerSecond = 0.f;
    bool bOwnsTag = false;
};
