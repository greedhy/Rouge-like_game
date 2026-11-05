#include "Character/StanderPlayer.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
AStanderPlayer::AStanderPlayer()
{
 StanderSword=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("StanderSword"));
 StanderSword->SetupAttachment(GetMesh(),TEXT("ik_hand_r"));StanderSword->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 StanderShield=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("StanderShield"));
 StanderShield->SetupAttachment(GetMesh(),TEXT("lowerarm_l"));StanderShield->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 SwordVisual->SetVisibility(false);ShieldVisual->SetVisibility(false);
}

#include "Equipment/WeaponSetConfig.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/MyPlayerState.h"
void AStanderPlayer::BeginPlay()
{
 Super::BeginPlay();
 if(auto* State=GetPlayerState<AMyPlayerState>()) State->GetEquipmentComponent()->RefreshWeaponVisuals();
}
void AStanderPlayer::ApplyWeaponSetVisuals(UWeaponSetConfig* Set)
{
 if(!bWeaponVisualDefaultsCaptured)
 {
  DefaultMainMesh=StanderSword->GetSkeletalMeshAsset(); DefaultOffhandMesh=StanderShield->GetSkeletalMeshAsset();
  DefaultMainTransform=StanderSword->GetRelativeTransform();DefaultOffhandTransform=StanderShield->GetRelativeTransform();
  DefaultMainSocket=StanderSword->GetAttachSocketName();DefaultOffhandSocket=StanderShield->GetAttachSocketName();
  DefaultAnimClass=GetMesh()->GetAnimClass();bWeaponVisualDefaultsCaptured=true;
 }
 auto Apply=[this](USkeletalMeshComponent* Component,USkeletalMesh* WeaponMesh,FName Socket,const FTransform& Transform)
 {
  Component->SetSkeletalMesh(WeaponMesh);Component->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,Socket);
  Component->SetRelativeTransform(Transform);Component->SetVisibility(WeaponMesh!=nullptr);
 };
 Apply(StanderSword,Set?Set->MainMesh.Get():DefaultMainMesh.Get(),Set?Set->MainSocket:DefaultMainSocket,Set?Set->MainTransform:DefaultMainTransform);
 Apply(StanderShield,Set?Set->OffhandMesh.Get():DefaultOffhandMesh.Get(),Set?Set->OffhandSocket:DefaultOffhandSocket,Set?Set->OffhandTransform:DefaultOffhandTransform);
 auto Anim=Set?Set->AnimClass:DefaultAnimClass;
 if(GetMesh()->GetAnimClass()!=Anim) GetMesh()->SetAnimInstanceClass(Anim);
}
