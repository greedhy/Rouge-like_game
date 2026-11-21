// 调用链：PlayPresentation → 创建本地组件 → StopPresentation / EndPlay → 释放组件。
#include "Character/CombatPresentationComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundBase.h"

bool UCombatPresentationComponent::PlayPresentation(FName Slot, FName Preset, const FTransform& WorldPose,
                                                    USceneComponent* AttachTo)
{
    if (!GetWorld() || GetNetMode() == NM_DedicatedServer || Slot.IsNone())
    {
        return false;
    }
    const FCombatPresentationPreset* Settings = Presets.Find(Preset);
    if (!Settings)
    {
        return false;
    }

    StopPresentation(Slot);
    FActivePresentation Active;
    if (Settings->Particle)
    {
        const FVector Scale(Settings->ParticleScale);
        if (Settings->bAttachParticle && IsValid(AttachTo))
        {
            Active.Particle =
                UGameplayStatics::SpawnEmitterAttached(Settings->Particle, AttachTo, Settings->ParticleSocket,
                                                       Settings->ParticleOffset, FRotator::ZeroRotator, Scale);
        }
        else
        {
            Active.Particle = UGameplayStatics::SpawnEmitterAtLocation(
                this, Settings->Particle, WorldPose.TransformPositionNoScale(Settings->ParticleOffset),
                WorldPose.Rotator(), Scale);
        }
    }
    if (Settings->Sound && Settings->SoundVolume > 0.f)
    {
        // 传入挂点时声音在角色处播放，随冲锋移动；否则使用世界坐标。
        if (IsValid(AttachTo))
        {
            Active.Audio = UGameplayStatics::SpawnSoundAttached(
                Settings->Sound, AttachTo, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
                EAttachLocation::KeepRelativeOffset, true, Settings->SoundVolume);
        }
        else
        {
            Active.Audio = UGameplayStatics::SpawnSoundAtLocation(this, Settings->Sound, WorldPose.GetLocation(),
                                                                  WorldPose.Rotator(), Settings->SoundVolume);
        }
    }
    if (!Active.Particle.IsValid() && !Active.Audio.IsValid())
    {
        return false;
    }
    ActivePresentations.Add(Slot, Active);
    return true;
}

void UCombatPresentationComponent::StopPresentation(FName Slot)
{
    // 先移出记录，原生组件的停止与销毁可以同步通知其他监听者。
    FActivePresentation Active;
    if (!ActivePresentations.RemoveAndCopyValue(Slot, Active))
    {
        return;
    }
    if (UParticleSystemComponent* Particle = Active.Particle.Get())
    {
        Particle->DestroyComponent();
    }
    if (UAudioComponent* Audio = Active.Audio.Get())
    {
        Audio->Stop();
        if (Active.Audio.IsValid())
        {
            Audio->DestroyComponent();
        }
    }
}

void UCombatPresentationComponent::StopAllPresentations()
{
    TArray<FName> Slots;
    ActivePresentations.GenerateKeyArray(Slots);
    for (FName Slot : Slots)
    {
        StopPresentation(Slot);
    }
}

bool UCombatPresentationComponent::HasPresentation(FName Slot) const
{
    const FActivePresentation* Active = ActivePresentations.Find(Slot);
    return Active && (Active->Particle.IsValid() || Active->Audio.IsValid());
}

void UCombatPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopAllPresentations();
    Super::EndPlay(Reason);
}
