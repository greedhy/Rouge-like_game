#pragma once

// 调用链：角色的攻击阶段或状态通知 → 表现蓝图配置 → 本地粒子和声音 → 按槽停止。
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatPresentationComponent.generated.h"

class UAudioComponent;
class UParticleSystem;
class UParticleSystemComponent;
class USceneComponent;
class USoundBase;

USTRUCT(BlueprintType)
struct TEST_25D_API FCombatPresentationPreset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    TObjectPtr<UParticleSystem> Particle;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    TObjectPtr<USoundBase> Sound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    bool bAttachParticle = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (EditCondition = "bAttachParticle"))
    FName ParticleSocket;

    // 挂接时相对插槽，否则相对传入的世界位置和朝向。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    FVector ParticleOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = "0.01"))
    float ParticleScale = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = "0"))
    float SoundVolume = 1.f;
};

UCLASS(Blueprintable, ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class TEST_25D_API UCombatPresentationComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
    TMap<FName, FCombatPresentationPreset> Presets;

    // 槽和预设分开，使两个独立生命周期可以同时使用同一套蓝图配置。
    UFUNCTION(BlueprintCallable, Category = "Combat Presentation")
    bool PlayPresentation(FName Slot, FName Preset, const FTransform& WorldPose, USceneComponent* AttachTo);

    UFUNCTION(BlueprintCallable, Category = "Combat Presentation")
    void StopPresentation(FName Slot);

    UFUNCTION(BlueprintCallable, Category = "Combat Presentation")
    void StopAllPresentations();

    UFUNCTION(BlueprintPure, Category = "Combat Presentation")
    bool HasPresentation(FName Slot) const;

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    struct FActivePresentation
    {
        TWeakObjectPtr<UParticleSystemComponent> Particle;
        TWeakObjectPtr<UAudioComponent> Audio;
    };
    TMap<FName, FActivePresentation> ActivePresentations;
};
