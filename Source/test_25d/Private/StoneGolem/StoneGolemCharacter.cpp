#include "StoneGolem/StoneGolemCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
AStoneGolemCharacter::AStoneGolemCharacter()
{
    Hammer = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StoneHammer"));
    Shield = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StoneShield"));
    Hammer->SetupAttachment(GetMesh(), TEXT("weapon_rSocket"));
    Shield->SetupAttachment(GetMesh(), TEXT("weapon_lSocket"));
    Hammer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shield->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void AStoneGolemCharacter::ApplyConfig()
{
    Super::ApplyConfig();
    if (MonsterConfig)
    {
        Hammer->SetStaticMesh(MonsterConfig->Hammer);
        Shield->SetStaticMesh(MonsterConfig->Shield);
    }
}
