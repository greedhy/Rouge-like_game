// 调用链：共享近战接触动作 -> 命中或装备周身多播 -> 本地 Niagara 播放与清理。
#include "Character/SwordShieldPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"
// 创建武器表现组件并设置挂接、碰撞及角色默认参数。
ASwordShieldPlayer::ASwordShieldPlayer()
{
    CharacterClass = ECharacterClass::Warrior;
    BaseWalkSpeed = 450.f;
    SwordVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwordVisual"));
    SwordVisual->SetupAttachment(GetMesh(), TEXT("Weapon_R"));
    SwordVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // The source blueprint stores quaternion +X 90 degrees: UE Rotator roll is -90.
    SwordVisual->SetRelativeRotation(FRotator(0, 0, -90));
    ShieldVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldVisual"));
    ShieldVisual->SetupAttachment(GetMesh(), TEXT("Weapon_L"));
    ShieldVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ShieldVisual->SetRelativeRotation(FRotator(0, -90, 0));
}

// 一次接触播放一次。特效跟随角色移动，有限时长到期后销毁，即使 Niagara 本身是循环系统。
void ASwordShieldPlayer::ShowEquipmentBodyEffect_Implementation(UNiagaraSystem* Effect, float Duration)
{
    if (!IsValid(Effect) || GetNetMode() == NM_DedicatedServer || !GetRootComponent() ||
        !FMath::IsFinite(Duration) || Duration < .1f || Duration > 3.f)
    {
        return;
    }
    const FVector LocalOffset(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    auto* Visual = UNiagaraFunctionLibrary::SpawnSystemAttached(Effect, GetRootComponent(), NAME_None, LocalOffset,
                                                              FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
                                                              true);
    if (Visual)
    {
        FTimerHandle Lifetime;
        GetWorldTimerManager().SetTimer(
            Lifetime, FTimerDelegate::CreateWeakLambda(Visual, [Visual]()
            {
                Visual->DestroyComponent();
            }), Duration, false);
    }
}
// 有特效资源时在指定位置生成缩放后的 Niagara 表现。
void ASwordShieldPlayer::ShowSwordImpact_Implementation(UNiagaraSystem* Effect, FVector Location)
{
    if (Effect)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Effect, Location, FRotator::ZeroRotator, FVector(.35f));
}
