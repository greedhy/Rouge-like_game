#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StoneGolemConfig.generated.h"

class UAnimMontage;
class UAnimInstance;
class UGameplayAbility;
class UGameplayEffect;
class UBehaviorTree;
class UNiagaraSystem;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EStoneGolemShape : uint8
{
    Circle,
    Sector,
    Rectangle
};

USTRUCT(BlueprintType)
struct TEST_25D_API FStoneGolemAttackTuning
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
    EStoneGolemShape Shape = EStoneGolemShape::Circle;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (Units = "cm"))
    float Radius = 180;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (Units = "deg"))
    float Angle = 80;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (Units = "cm"))
    float Length = 600;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (Units = "cm"))
    float Width = 140;
    // Actor-local ground offset. X=forward, Y=right. Z is projected onto the floor.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
    FVector CenterOffset = FVector(27, -6, 0);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (Units = "cm"))
    float HeightTolerance = 60;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (Units = "s"))
    float AnimationDelay = .2f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (Units = "s"))
    float LockTime = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing")
    float PlayRate = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (Units = "s"))
    float ContactTime = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (Units = "s"))
    float ContactTolerance = .034f;
    // Actual time after activation, including animation delay.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (Units = "s"))
    float RecoveryEnd = 1.6f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
    float DamageMultiplier = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (Units = "s"))
    float Cooldown = 4;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (Units = "cm/s"))
    float KnockbackSpeed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (Units = "cm/s"))
    float KnockbackUpSpeed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (Units = "cm"))
    float ActivationRange = 230;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    float SelectionWeight = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
    TObjectPtr<UAnimMontage> Montage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warning")
    bool bExclamation = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warning")
    bool bDrawFootprint = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bSingleTarget = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bTargetGround = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bRequireLineOfSight = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
    bool bImpactAtTarget = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warning")
    FVector HeadOffset = FVector(0, 0, 160);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warning")
    TObjectPtr<UMaterialInterface> WarningMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
    TObjectPtr<UNiagaraSystem> ImpactEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
    FName ImpactSocket = TEXT("weapon_rSocket");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
    float ImpactScale = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact", meta = (Units = "s"))
    float ImpactLifetime = 2;
    bool Validate(FString& Error) const;
    bool Contains(const FVector& Center, float Yaw, const FVector& GroundPoint) const;
    float HitTime() const
    {
        return AnimationDelay + ContactTime / PlayRate;
    }
    float QueryRadius() const
    {
        return Shape == EStoneGolemShape::Rectangle ? FMath::Sqrt(Length * Length + Width * Width * .25f) : Radius;
    }
};

UCLASS(BlueprintType)
class TEST_25D_API UStoneGolemAttackConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
    FStoneGolemAttackTuning Tuning;
    // Empty preserves the existing physical damage behavior.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
    FGameplayTag DamageType;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType)
class TEST_25D_API UMonsterCombatConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
    TObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
    TSubclassOf<UAnimInstance> AnimClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float BaseDamage = 40;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    TSubclassOf<UGameplayEffect> DamageEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    float WalkSpeed = 150;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    TObjectPtr<UBehaviorTree> Behavior;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    TArray<TSubclassOf<UGameplayAbility>> Attacks;
};

UCLASS(BlueprintType)
class TEST_25D_API UStoneGolemConfig : public UMonsterCombatConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
    TObjectPtr<UStaticMesh> Hammer;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
    TObjectPtr<UStaticMesh> Shield;
};

// One point policy for the warning projection and the damage query.
namespace StoneGolemGeometry
{
    TEST_25D_API bool Project(UWorld* World, const AActor* Source, const FVector& Candidate, float Height,
                              FVector& Ground);
    TEST_25D_API bool Visible(UWorld* World, const AActor* Source, const FVector& Center, const FVector& Ground);
    TEST_25D_API void BuildSurface(UWorld* World, const AActor* Source, const FStoneGolemAttackTuning& Tuning,
                                   const FVector& Center, float Yaw, TArray<FVector>& Vertices,
                                   TArray<int32>& Triangles);
    TEST_25D_API bool SurfaceContains(const TArray<FVector>& Vertices, const TArray<int32>& Triangles,
                                      const FVector& Point);
} // namespace StoneGolemGeometry
