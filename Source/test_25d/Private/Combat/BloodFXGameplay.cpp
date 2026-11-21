// 调用链：共享近战动作 → 攻击前位置与身份快照 → ActualDamage 成功门槛 → 既有复制宿主。
#include "Combat/BloodFXGameplay.h"
#include "Combat/BloodFXComponent.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "Character/EnemyCharacter.h"
#include "EngineUtils.h"
#include "PureMass/PureMassCrowd.h"
#include "PureMass/PureMassSubsystem.h"

FBloodHitSnapshot BloodFXGameplay::Capture(const FAbilityTargetToken& Target, FVector Origin, FVector TargetLocation)
{
    FBloodHitSnapshot Snapshot;
    Snapshot.Location = TargetLocation;
    Snapshot.Direction = (TargetLocation - Origin).GetSafeNormal();
    if (AActor* Actor = Target.Target.Actor.Get(); Target.Target.IsActor() && IsValid(Actor))
    {
        Snapshot.Component = Actor->FindComponentByClass<UBloodFXComponent>();
        Snapshot.Life = FMath::Abs(Target.ActorLease.Get(0));
        if (const auto* Character = Cast<ACharacter>(Actor))
        {
            const float Radius = Character->GetSimpleCollisionRadius();
            Snapshot.Location -= Snapshot.Direction * Radius * .65f;
        }
    }
    else if (UWorld* World = Target.World.Get())
    {
        FPureMassView View;
        auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
        if (Mass && Mass->Resolve(Target.Target.EntityId, View))
        {
            Snapshot.MassType = View.Type;
            Snapshot.MassId = View.Id;
            for (TActorIterator<APureMassCrowd> It(World); It; ++It)
            {
                Snapshot.Crowd = *It;
                break;
            }
        }
    }
    return Snapshot;
}

void BloodFXGameplay::Confirm(const FBloodHitSnapshot& Snapshot, const FAbilityDamageResult& Result, FGuid AttackId)
{
    if (Result.ActualDamage.Get(0.f) <= 0.f)
    {
        return;
    }
    if (UBloodFXComponent* Blood = Snapshot.Component.Get())
    {
        Blood->NotifyHit(AttackId, Snapshot.Life, Snapshot.Location, Snapshot.Direction);
    }
    else if (APureMassCrowd* Crowd = Snapshot.Crowd.Get(); Crowd && Snapshot.MassType.IsValid())
    {
        Crowd->NotifyBloodHit(Snapshot.MassId, AttackId, Snapshot.MassType.Get(), Snapshot.Location,
                              Snapshot.Direction);
    }
}
