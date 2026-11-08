// 主要调用链：武器动作技能激活调用 Validate -> Move 调用 JumpHeight -> 发射物选敌调用 IsInAimCone。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StanderWeaponActionConfig.generated.h"
class UAnimMontage;
class UNiagaraSystem;
class UAbilityTimelineDefinition;
UENUM(BlueprintType)
enum class EStanderWeaponActionKind : uint8
{
    Melee,
    Projectile,
    Guard,
    Move,
    Presentation
};
USTRUCT(BlueprintType)
struct TEST_25D_API FStanderWeaponActionEvent
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Time = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EStanderWeaponActionKind Kind = EStanderWeaponActionKind::Melee;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Damage = 25;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Radius = 180;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Angle = 120;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FVector DeathImpulse = FVector(800, 0, 300);
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float ProjectileSpeed = 1200;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float ProjectileLifetime = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float LaunchYaw = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float MoveDistance = 0;
    /** Capsule jump arc, in cm; zero keeps the movement on the walkable floor. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float MoveHeight = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Duration = .3f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName SocketName = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UNiagaraSystem> Effect = nullptr;
};
UCLASS(BlueprintType)
class TEST_25D_API UStanderWeaponActionConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName WeaponFamily = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag ActionTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag CooldownTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UAnimMontage> Montage = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float PlayRate = 1;
    /** 可选统一时间轴。配置后由其阶段时长决定倍率，PlayRate仅用于旧动作兼容。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timeline")
    TObjectPtr<UAbilityTimelineDefinition> Timeline = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float ManaCost = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Cooldown = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FStanderWeaponActionEvent> Events;
    // 检查配置引用、数值范围与时间约束是否可供技能使用。
    bool Validate() const;
    // 使用首尾为零、中点达峰值的抛物线计算跳跃高度。
    static float JumpHeight(float Fraction, float Height);
    // 检查目标是否处于水平朝向左右各十五度的辅助瞄准锥内。
    static bool IsInAimCone(FVector Forward, FVector TargetOffset);
};
