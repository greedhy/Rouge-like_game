// 主要调用链：构造角色剑盾组件 -> 剑盾技能命中调用 ShowSwordImpact -> 多播实现生成 Niagara 特效。
#include "Character/SwordShieldPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraFunctionLibrary.h"
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
// 有特效资源时在指定位置生成缩放后的 Niagara 表现。
void ASwordShieldPlayer::ShowSwordImpact_Implementation(UNiagaraSystem* Effect, FVector Location)
{
    if (Effect)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Effect, Location, FRotator::ZeroRotator, FVector(.35f));
}
