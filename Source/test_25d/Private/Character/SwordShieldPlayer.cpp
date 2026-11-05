#include "Character/SwordShieldPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraFunctionLibrary.h"
ASwordShieldPlayer::ASwordShieldPlayer()
{
 CharacterClass=ECharacterClass::Warrior;
 BaseWalkSpeed=450.f;
 SwordVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwordVisual"));
 SwordVisual->SetupAttachment(GetMesh(),TEXT("Weapon_R"));
 SwordVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 // The source blueprint stores quaternion +X 90 degrees: UE Rotator roll is -90.
 SwordVisual->SetRelativeRotation(FRotator(0,0,-90));
 ShieldVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldVisual"));
 ShieldVisual->SetupAttachment(GetMesh(),TEXT("Weapon_L"));
 ShieldVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 ShieldVisual->SetRelativeRotation(FRotator(0,-90,0));
}
void ASwordShieldPlayer::ShowSwordImpact_Implementation(UNiagaraSystem* Effect,FVector Location)
{
 if(Effect) UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,Effect,Location,FRotator::ZeroRotator,FVector(.35f));
}
