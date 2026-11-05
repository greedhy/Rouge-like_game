#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "PureMass/PureMassTypes.h"
#include "PureMassCrowd.generated.h"

class APureMassCrowd;
class UAnimBank;
class USkeletalMeshComponent;
class UInstancedStaticMeshComponent;
class UAnimSequenceTransformProviderDataInstance;

/** Transient cosmetic events; never replayed as replicated persistent state. */
USTRUCT()
struct FPureMassDeathPresentation
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UPureMassMonsterType> Type;
    UPROPERTY() FVector_NetQuantize10 Location = FVector::ZeroVector;
    UPROPERTY() float Yaw = 0.f;
    UPROPERTY() FVector_NetQuantize10 DeathImpulse = FVector::ZeroVector;
};

/** Wire state only. There is no replicated Actor or component for an individual enemy. */
USTRUCT()
struct FPureMassNetEntity : public FFastArraySerializerItem
{
    GENERATED_BODY()
    UPROPERTY() int32 Id = INDEX_NONE;
    UPROPERTY() FVector_NetQuantize10 Location = FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantize10 Velocity = FVector::ZeroVector;
    UPROPERTY() float Health = 0.f;
    UPROPERTY() float MaxHealth = 0.f;
    UPROPERTY() uint8 State = 0;
    UPROPERTY() bool bPetrified = false;
    UPROPERTY() TObjectPtr<UPureMassMonsterType> Type;

    FPureMassView ToView() const;
    bool SetFromView(const FPureMassView& View);
};

USTRUCT()
struct FPureMassNetEntities : public FFastArraySerializer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FPureMassNetEntity> Items;
    TWeakObjectPtr<APureMassCrowd> Owner;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FastArrayDeltaSerialize<FPureMassNetEntity, FPureMassNetEntities>(Items, DeltaParams, *this);
    }
    void PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);
};

template<> struct TStructOpsTypeTraits<FPureMassNetEntities> : TStructOpsTypeTraitsBase2<FPureMassNetEntities>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT()
struct FPureMassRenderBatch
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UPureMassMonsterType> Type;
    UPROPERTY(Transient) bool bPetrified = false;
    UPROPERTY(Transient) TObjectPtr<UInstancedSkinnedMeshComponent> Component;
    UPROPERTY(Transient) TObjectPtr<UInstancedSkinnedMeshComponent> Weapons;
    UPROPERTY(Transient) TObjectPtr<UAnimSequenceTransformProviderDataInstance> Playback;
};

struct FPureMassRenderInstance
{
    FPrimitiveInstanceId Instance;
    FPrimitiveInstanceId WeaponInstance;
    int32 BatchIndex = INDEX_NONE;
    int32 Track = INDEX_NONE;
    uint8 State = 255;
    FVector DisplayLocation = FVector::ZeroVector;
    FVector StartLocation = FVector::ZeroVector;
    FVector TargetLocation = FVector::ZeroVector;
    FQuat Facing = FQuat::Identity;
    FQuat TargetFacing = FQuat::Identity;
    float InterpolationAge = 0.f;
};

/** The only crowd Actor: shared replication plus one GPU skinned component per type. */
UCLASS()
class TEST_25D_API APureMassCrowd : public AActor
{
    GENERATED_BODY()
public:
    APureMassCrowd();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PureMass|Petrify") TObjectPtr<UMaterialInterface> PetrifyMaterial;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintPure, Category="PureMass|Diagnostics") int32 GetRenderedInstanceCount() const;
    UFUNCTION(BlueprintPure, Category="PureMass|Diagnostics") int32 GetReplicatedEntityCount() const;
    UFUNCTION(BlueprintPure, Category="PureMass|Diagnostics") int32 GetRenderComponentCount() const;
    UFUNCTION(BlueprintPure, Category="PureMass|Diagnostics") int32 GetAnimationTrackCount() const;
    UFUNCTION(BlueprintPure, Category="PureMass|Diagnostics") int32 GetWeaponInstanceCount() const;

    /** Editor-only operation; refuses banks outside the new /Game/PureMass asset tree. */
    UFUNCTION(BlueprintCallable, Category="PureMass|Authoring", meta=(DevelopmentOnly))
    static UAnimSequenceTransformProviderData* CreateAnimationProvider(UAnimBank* Bank);

    UFUNCTION(BlueprintPure, Category="PureMass|Diagnostics") int32 GetActiveDeathCount() const;
    UFUNCTION(BlueprintPure, Category="PureMass|Diagnostics") int32 GetAllocatedDeathCount() const { return DeathMeshes.Num(); }
    UFUNCTION(BlueprintPure, Category="PureMass|Diagnostics") int32 GetWarningCount() const;
    void ApplyReplicatedSnapshot();
    friend class FPureMassDeathImpulseTest;
    void NotifyKilled(const FPureMassView& View, FVector DeathImpulse = FVector::ZeroVector);
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastDeaths(const TArray<FPureMassDeathPresentation>& Deaths);


protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void PresentDeath(UPureMassMonsterType* Type, FVector_NetQuantize10 Location, float Yaw, FVector DeathImpulse);
    void PublishSnapshots(const TArray<FPureMassView>& Views);
    void ReconcileInstances(const TArray<FPureMassView>& Views);
    void UpdateTransforms(float DeltaSeconds);
    int32 FindOrCreateBatch(UPureMassMonsterType* Type, bool bPetrified);
    void RemoveRenderInstance(FPureMassRenderInstance& Instance);
    void SetAnimation(FPureMassRenderInstance& Instance, uint8 State);

    UPROPERTY(Replicated) FPureMassNetEntities NetEntities;
    UPROPERTY(Transient) TArray<FPureMassRenderBatch> RenderBatches;
    TMap<int32, FPureMassRenderInstance> RenderInstances;
    TSet<TWeakObjectPtr<UPureMassMonsterType>> RejectedTypes;
    float TimeUntilPublish = 0.f;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> DeathMeshes;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> DeathWeapons;
    TArray<float> DeathExpiry;
    UPROPERTY(Transient) TArray<FPureMassDeathPresentation> PendingDeaths;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> WarningInstances;
    static constexpr float SnapshotInterval = 1.f / 60.f;
};
