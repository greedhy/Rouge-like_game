// 调用链：GA / 图节点 -> 权威与目标校验 -> UBatch6PetrifyComponent::Apply / UPureMassSubsystem::ApplyPetrify；状态由原持有者收尾。
#include "AbilitySystem/Unified/AbilityStatusService.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "Character/EnemyCharacter.h"
#include "Engine/World.h"
#include "Interface/CombatInterface.h"
#include "PureMass/PureMassSubsystem.h"

/** 只转发经过权威、持续时间和活目标验证的效果请求，复用原状态持有者及延长期限规则。 */
bool AbilityStatusService::ApplyPetrify(AActor* Source, const FPureMassCombatTarget& Target, float Duration,
                                        UMaterialInterface* Material)
{
    UWorld* World = IsValid(Source) ? Source->GetWorld() : nullptr;
    if (!World || World->bIsTearingDown || World->GetNetMode() == NM_Client || !Source->HasAuthority() ||
        Source->IsActorBeingDestroyed() || !FMath::IsFinite(Duration) || Duration <= 0.f ||
        (Source->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Source)))
    {
        return false;
    }
    if (const auto* EnemySource = Cast<AEnemyCharacter>(Source); EnemySource && EnemySource->IsDungeonPoolSleeping())
    {
        return false;
    }
    FVector Location;
    if (!PureMassCombat::Resolve(World, Source, Target, Location))
    {
        return false;
    }
    if (!Target.IsActor())
    {
        auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
        return Mass && Mass->ApplyPetrify(Target.EntityId, Duration, Source);
    }
    auto* Enemy = Cast<AEnemyCharacter>(Target.Actor.Get());
    if (!IsValid(Enemy) || !Enemy->HasAuthority() || Enemy->IsActorBeingDestroyed() || Enemy->IsDungeonPoolSleeping())
    {
        return false;
    }
    // 只在首次状态应用时注册原复制组件；刷新、GE 清除、死亡及池生命收尾仍由原组件处理。
    auto* Component = Enemy->FindComponentByClass<UBatch6PetrifyComponent>();
    if (!Component)
    {
        Component = NewObject<UBatch6PetrifyComponent>(Enemy);
        Enemy->AddInstanceComponent(Component);
        Component->RegisterComponent();
    }
    return Component->Apply(Duration, Material);
}
