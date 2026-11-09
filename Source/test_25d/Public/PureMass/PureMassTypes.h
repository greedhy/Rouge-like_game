#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MassEntityTypes.h"
#include "PureMassTypes.generated.h"

class USkeletalMesh;
class UAnimSequenceTransformProviderData;
class ATerrainSurface;

UCLASS(BlueprintType)
class TEST_25D_API UPureMassMonsterType : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    TObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    TObjectPtr<UAnimSequenceTransformProviderData> AnimationProvider;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    FTransform MeshOffset = FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    int32 IdleAnimation = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    int32 RunAnimation = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    int32 AttackAnimation = 2;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    TObjectPtr<USkeletalMesh> WeaponMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    FName WeaponSocket = TEXT("WeaponHandSocket");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering")
    FTransform WeaponOffset = FTransform::Identity;
    /** Clip contact time, used to align the strike to the server windup. Zero keeps legacy playback. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering", meta = (ClampMin = "0"))
    float AttackContactTime = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rendering", meta = (ClampMin = "0.01"))
    float AttackClipDuration = .35f;
    float AttackPlayRate() const
    {
        return AttackContactTime > 0.f && AttackWindupSeconds > 0.f
                   ? FMath::Clamp(AttackContactTime / AttackWindupSeconds, .05f, 10.f)
                   : 1.f;
    }
    float AttackRecoverySeconds() const
    {
        return FMath::Max(.01f, (AttackClipDuration -
                                 (AttackContactTime > 0.f && AttackWindupSeconds > 0.f ? AttackContactTime : 0.f)) /
                                    AttackPlayRate());
    }
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
    float Radius = 32.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
    float HalfHeight = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
    float Speed = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float Health = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float AttackDamage = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float AttackRange = 120.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float AttackPeriod = 1.5f;
    /** Opt-in warning before melee impact. Zero preserves legacy test types. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0", ClampMax = "3"))
    float AttackWindupSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float Armor = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float BlockChance = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float CriticalResistance = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    TMap<FGameplayTag, float> Resistances;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    int32 Level = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    int32 XPReward = 10;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FPureMassView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int32 Id = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly)
    FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    FVector Velocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    float Health = 0.f;
    UPROPERTY(BlueprintReadOnly)
    float MaxHealth = 0.f;
    UPROPERTY(BlueprintReadOnly)
    uint8 State = 0;
    UPROPERTY(BlueprintReadOnly)
    bool bPetrified = false;
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UPureMassMonsterType> Type;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FPureMassDamage
{
    GENERATED_BODY()
    UPROPERTY()
    TWeakObjectPtr<AActor> Source;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Amount = 0.f;
    /** Cosmetic velocity-change impulse, consumed only by the lethal hit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector DeathImpulse = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag DamageType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StatusDuration = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamagePerSecond = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MovementMultiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRoot = false;
};

/** All enemy state lives in this entity fragment. No Actor/ASC/component is created. */
USTRUCT()
struct TEST_25D_API FPureMassFragment : public FMassFragment
{
    GENERATED_BODY()
    int32 Id = INDEX_NONE;
    // 每个实体的等级，房间覆盖不会修改共享 MonsterType 资产。
    int32 Level = 1;
    TWeakObjectPtr<UPureMassMonsterType> Type;
    FVector Location = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    FName Room;
    float Health = 0.f;
    float MaxHealth = 0.f;
    uint8 State = 0;
    TArray<FVector> Path;
    int32 PathPoint = 0;
    FVector PathGoal = FVector::ZeroVector;
    float NextPathTime = 0.f;
    float AttackRemaining = 0.f;
    float AttackVisualRemaining = 0.f;
    float AttackWindupRemaining = 0.f;
    TWeakObjectPtr<AActor> PendingAttackTarget;
    float PetrifyRemaining = 0.f;
    float RootRemaining = 0.f;
    float RootRecovery = 0.f;
    float StatusRemaining = 0.f;
    float StatusDPS = 0.f;
    float StatusSlow = 1.f;
    TArray<FVector2D> SpellSlows; // X multiplier, Y remaining duration; entity lifetime owned.
    TWeakObjectPtr<AActor> StatusSource;
    struct FTerrainContact
    {
        TWeakObjectPtr<ATerrainSurface> Surface;
        float Remaining = 0.f;
    };
    TArray<FTerrainContact> TerrainContacts;
};

template <> struct TMassFragmentTraits<FPureMassFragment>
{
    enum
    {
        AuthorAcceptsItsNotTriviallyCopyable = true
    };
};

namespace PureMassGeometry
{
    /** Exact moving sphere against an upright data capsule; returns first time in [0,1]. */
    TEST_25D_API bool SweepCapsule(const FVector& Start, const FVector& End, float SweepRadius, const FVector& Center,
                                   float Radius, float HalfHeight, float& OutTime);
} // namespace PureMassGeometry
