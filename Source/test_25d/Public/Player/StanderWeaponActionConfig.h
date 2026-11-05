#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StanderWeaponActionConfig.generated.h"
class UAnimMontage;
class UNiagaraSystem;
UENUM(BlueprintType)
enum class EStanderWeaponActionKind : uint8 { Melee, Projectile, Guard, Move, Presentation };
USTRUCT(BlueprintType)
struct TEST_25D_API FStanderWeaponActionEvent
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float Time = 0;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) EStanderWeaponActionKind Kind = EStanderWeaponActionKind::Melee;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float Damage = 25;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float Radius = 180;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float Angle = 120;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector DeathImpulse = FVector(800,0,300);
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjectileSpeed = 1200;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjectileLifetime = 3;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float LaunchYaw = 0;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float MoveDistance = 0;
 /** Capsule jump arc, in cm; zero keeps the movement on the walkable floor. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float MoveHeight = 0;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float Duration = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SocketName = NAME_None;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UNiagaraSystem> Effect = nullptr;
};
UCLASS(BlueprintType)
class TEST_25D_API UStanderWeaponActionConfig : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FName WeaponFamily = NAME_None;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FGameplayTag ActionTag;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FGameplayTag CooldownTag;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> Montage = nullptr;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float PlayRate = 1;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float ManaCost = 0;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float Cooldown = 0;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FStanderWeaponActionEvent> Events;
 bool Validate() const;
 static float JumpHeight(float Fraction, float Height);
 static bool IsInAimCone(FVector Forward, FVector TargetOffset);
};
