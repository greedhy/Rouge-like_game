// 调用链：原角色动画宿主/统一图 -> 角色动作服务 -> 原几何、伤害、弹体和格挡GE；不激活完整GA。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "Player/SwordShieldConfig.h"
#include "Player/StanderWeaponActionConfig.h"
#include "StoneGolem/StoneGolemConfig.h"
#include "GameplayEffectTypes.h"
#include "AbilityCharacterNodeExecutions.generated.h"

class AStanderWeaponProjectile;
class FAbilityNodeRegistry;

/** 兼容差异必须显式选择，不能用通用查询暗改高度、房间或目标通路。 */
UENUM(BlueprintType)
enum class EAbilityCharacterStrikeProfile : uint8
{
    SwordShieldActor,
    StanderMixed,
    WeaponMixed
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityCharacterStrikeNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityCharacterStrikeProfile Profile = EAbilityCharacterStrikeProfile::StanderMixed;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FSwordStrike Strike;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector LocalDeathImpulse = FVector(800, 0, 300);
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRadialImpulse = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UNiagaraSystem> SwingEffect;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SwingOffset = 80.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UNiagaraSystem> HitEffect;
    virtual bool Validate(FString& Error) const override;
};

/** 宿主只提供失效/报告回调；目标查询、伤害参数与实际结算仍由同一动作实现。 */
struct FAbilityCharacterActionCallbacks
{
    TFunction<bool()> IsCurrent;
    TFunction<void(AActor*)> BeforeActorDamage;
    TFunction<bool(const FAbilityTargetToken&, const FVector&, const FAbilityDamageResult&)> AfterDamage;
};

namespace AbilityCharacterActions
{
    TEST_25D_API void ExecuteStrike(const FAbilitySourceToken& Source, const FDamageEffectParams& Damage,
                                    const UAbilityCharacterStrikeNodeConfig& Settings, const FVector& Origin,
                                    const FVector& Forward, const FAbilityCharacterActionCallbacks& Callbacks = {});
    TEST_25D_API AStanderWeaponProjectile* SpawnWeaponProjectile(const FAbilitySourceToken& Source,
                                                                 const FDamageEffectParams& Damage,
                                                                 const FStanderWeaponActionEvent& Event,
                                                                 const FVector& Origin, const FVector& Forward,
                                                                 int32 OriginRoom);
    /** Duration<0表示由动画窗口关闭；句柄仅属于本次动作，不移除别的技能的格挡。 */
    TEST_25D_API FActiveGameplayEffectHandle OpenGuardWindow(const FAbilitySourceToken& Source, float Duration = -1.f);
    TEST_25D_API void CloseGuardWindow(UAbilitySystemComponent* ASC, FActiveGameplayEffectHandle& Handle);
    TEST_25D_API bool IsBlockedDamage(const FGameplayEffectSpec& Spec);
    TEST_25D_API void GatherTelegraphedTargets(const FAbilitySourceToken& Source, const FStoneGolemAttackTuning& Shape,
                                               const FVector& Center, float Yaw, const TArray<FVector>& Surface,
                                               const TArray<int32>& Triangles, AActor* SelectedTarget,
                                               TArray<AActor*>& Out);
    TEST_25D_API void ApplyTelegraphedTargets(const FAbilitySourceToken& Source, const FDamageEffectParams& Damage,
                                              const FStoneGolemAttackTuning& Shape, const TArray<AActor*>& Targets,
                                              const FAbilityCharacterActionCallbacks& Callbacks = {});
} // namespace AbilityCharacterActions

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityWeaponProjectileNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FStanderWeaponActionEvent Event;
    virtual bool Validate(FString& Error) const override;
};
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityGuardWindowNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bCloseOnBlock = false;
    virtual bool Validate(FString& Error) const override;
};
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityTelegraphedContactNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FStoneGolemAttackTuning Shape;
    /** 已锁定的真实预警地面；为空则按同一几何规则在入口构建。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FVector> LockedSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<int32> LockedTriangles;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityCharacterStrikeNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};
UCLASS(Transient)
class TEST_25D_API UAbilityWeaponProjectileNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UFUNCTION()
    void ProjectileDestroyed(AActor* Actor);
    TWeakObjectPtr<AStanderWeaponProjectile> Projectile;
};
UCLASS(Transient)
class TEST_25D_API UAbilityGuardWindowNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float Delta) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    void GuardHit(UAbilitySystemComponent* ASC, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Effect);
    FActiveGameplayEffectHandle Guard;
    FDelegateHandle Listener;
    float Remaining = 0.f;
    bool bCloseOnBlock = false;
    bool bBlockPending = false;
    FAbilityNodeInput BlockInput;
};
UCLASS(Transient)
class TEST_25D_API UAbilityTelegraphedContactNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityCharacterNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
