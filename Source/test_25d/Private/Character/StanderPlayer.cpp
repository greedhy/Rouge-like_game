// 主要调用链：BeginPlay -> 装备组件 RefreshWeaponVisuals -> ApplyWeaponSetVisuals；骨骼武器组件替代继承的静态剑盾外观。
#include "Character/StanderPlayer.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
// 创建武器表现组件并设置挂接、碰撞及角色默认参数。
AStanderPlayer::AStanderPlayer()
{
    StanderSword = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("StanderSword"));
    StanderSword->SetupAttachment(GetMesh(), TEXT("ik_hand_r"));
    StanderSword->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StanderShield = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("StanderShield"));
    StanderShield->SetupAttachment(GetMesh(), TEXT("lowerarm_l"));
    StanderShield->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SwordVisual->SetVisibility(false);
    ShieldVisual->SetVisibility(false);
}

#include "Equipment/WeaponSetConfig.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/MyPlayerState.h"
// 角色开始运行后请求装备组件刷新武器外观。
void AStanderPlayer::BeginPlay()
{
    Super::BeginPlay();
    if (auto* State = GetPlayerState<AMyPlayerState>())
        State->GetEquipmentComponent()->RefreshWeaponVisuals();
}
// 首次缓存默认外观，再应用武器套装网格、插槽、变换和动画类；空套装恢复默认值。
void AStanderPlayer::ApplyWeaponSetVisuals(UWeaponSetConfig* Set)
{
    // 先保存角色原始组件配置，卸下武器套装时才能恢复默认表现。
    if (!bWeaponVisualDefaultsCaptured)
    {
        DefaultMainMesh = StanderSword->GetSkeletalMeshAsset();
        DefaultOffhandMesh = StanderShield->GetSkeletalMeshAsset();
        DefaultMainTransform = StanderSword->GetRelativeTransform();
        DefaultOffhandTransform = StanderShield->GetRelativeTransform();
        DefaultMainSocket = StanderSword->GetAttachSocketName();
        DefaultOffhandSocket = StanderShield->GetAttachSocketName();
        DefaultAnimClass = GetMesh()->GetAnimClass();
        bWeaponVisualDefaultsCaptured = true;
    }
    auto Apply =
        [this](USkeletalMeshComponent* Component, USkeletalMesh* WeaponMesh, FName Socket, const FTransform& Transform)
    {
        Component->SetSkeletalMesh(WeaponMesh);
        Component->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, Socket);
        Component->SetRelativeTransform(Transform);
        Component->SetVisibility(WeaponMesh != nullptr);
    };
    Apply(StanderSword, Set ? Set->MainMesh.Get() : DefaultMainMesh.Get(), Set ? Set->MainSocket : DefaultMainSocket,
          Set ? Set->MainTransform : DefaultMainTransform);
    Apply(StanderShield, Set ? Set->OffhandMesh.Get() : DefaultOffhandMesh.Get(),
          Set ? Set->OffhandSocket : DefaultOffhandSocket, Set ? Set->OffhandTransform : DefaultOffhandTransform);
    auto Anim = Set ? Set->AnimClass : DefaultAnimClass;
    if (GetMesh()->GetAnimClass() != Anim)
    {
        GetMesh()->SetAnimInstanceClass(Anim);
    }
    if (auto* State = GetPlayerState<AMyPlayerState>())
    {
        if (auto* ASC = Cast<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent()))
        {
            ASC->RefreshStanderOwnerMontage();
        }
    }
}
