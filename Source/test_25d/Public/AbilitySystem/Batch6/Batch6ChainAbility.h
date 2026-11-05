#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "Batch6ChainAbility.generated.h"

class UNiagaraSystem;
class UStaticMesh;
class UActorComponent;
USTRUCT(BlueprintType)
struct FBatch6ChainSegment
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FVector Start = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector End = FVector::ZeroVector;
};

UCLASS()
class TEST_25D_API ABatch6ChainPresentation : public AActor
{
    GENERATED_BODY()
public:
    ABatch6ChainPresentation();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(ReplicatedUsing=Present, BlueprintReadOnly) TArray<FBatch6ChainSegment> Segments;
    UPROPERTY(ReplicatedUsing=Present) TObjectPtr<UNiagaraSystem> BeamSystem;
    UFUNCTION() void Present();
private:
    UPROPERTY() TObjectPtr<UStaticMesh> FallbackMesh;
    UPROPERTY() TArray<TObjectPtr<UActorComponent>> PresentationComponents;
};

/** Shared breadth-first cast budget. Depth zero is the initial target. */
struct FBatch6ChainBudget
{
    static bool Allows(int32 Used, int32 Depth, int32 Siblings, int32 Total, int32 MaxDepth, int32 Branches)
    { return Used < FMath::Clamp(Total, 1, 128) && Depth <= FMath::Clamp(MaxDepth, 0, 16)
        && Siblings < FMath::Clamp(Branches, 1, 8); }
};

UCLASS()
class TEST_25D_API UBatch6ChainAbility : public UPrototypeAbility
{
    GENERATED_BODY()
public:
    UBatch6ChainAbility();
    virtual FString GetDescription(int32 Level) override;
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation) const override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chain") int32 MaxTargets = 12;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chain") int32 MaxDepth = 3;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chain") int32 BranchesPerTarget = 2;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chain") float JumpRadius = 400;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chain") float CastRange = 1200;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chain") TObjectPtr<UNiagaraSystem> BeamSystem;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled) override;
private:
    bool bResolved = false;
    FTimerHandle Timeout;
    UFUNCTION() void ReceiveTarget(const FGameplayAbilityTargetDataHandle& Data);
    void Abort();
};

