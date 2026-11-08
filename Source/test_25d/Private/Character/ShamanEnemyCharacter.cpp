// 调用链：萨满 GA 命中事件 -> Summon -> 导演登记成功召唤物；死亡/休眠/EndPlay -> ClearSummons -> 回收结算。
#include "Character/ShamanEnemyCharacter.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

bool UShamanSummonConfig::ValidateSummon() const
{
    return MinionClass && !MinionClass->IsChildOf(AShamanEnemyCharacter::StaticClass()) && MaxAlive >= 1 &&
           MaxAlive <= 5 && PerCast >= 1 && PerCast <= 5 && FMath::IsFinite(MinDistance) &&
           FMath::IsFinite(MaxDistance) && FMath::IsFinite(Spread) && MinDistance >= 50 && MaxDistance >= MinDistance &&
           MaxDistance <= 600 && Spread >= 0 && Spread <= 360;
}
int32 AShamanEnemyCharacter::GetOwnedMinionCount() const
{
    int32 Count = 0;
    for (const auto& Entry : Minions)
        if (Entry.Value == GetDungeonPoolLease() && Entry.Key.IsValid() && !Entry.Key->IsDead_Implementation())
            ++Count;
    return Count;
}
int32 AShamanEnemyCharacter::Summon(const UShamanSummonConfig* Config, int32 ExpectedLease)
{
    if (!HasAuthority() || bDead || IsDungeonPoolSleeping() || GetDungeonPoolLease() != ExpectedLease || !Config ||
        !Config->ValidateSummon())
        return 0;
    const int32 Count = FMath::Min(Config->PerCast, Config->MaxAlive - GetOwnedMinionCount());
    const auto* CDO = Config->MinionClass->GetDefaultObject<AEnemyCharacter>();
    const float HalfHeight = CDO->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    auto* Director = ADungeonRunDirector::Find(GetWorld());
    int32 Spawned = 0;
    for (int32 Attempt = 0; Attempt < Count * 4 && Spawned < Count; ++Attempt)
    {
        if (bDead || IsDungeonPoolSleeping() || GetDungeonPoolLease() != ExpectedLease)
            break;
        const float Angle = Count <= 1 ? 0 : -Config->Spread * .5f + Config->Spread * (Attempt % Count) / (Count - 1.f);
        FVector Point = GetActorLocation() + GetActorForwardVector().RotateAngleAxis(Angle, FVector::UpVector) *
                                                 FMath::FRandRange(Config->MinDistance, Config->MaxDistance);
        if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {
            FNavLocation Projected;
            if (!Nav->ProjectPointToNavigation(Point, Projected, FVector(60, 60, 200)))
                continue;
            const auto* Data = Nav->GetNavDataForProps(GetNavAgentPropertiesRef(), GetActorLocation());
            if (!Data)
                continue;
            FPathFindingQuery Query(this, *Data, GetNavAgentLocation(), Projected.Location);
            Query.SetAllowPartialPaths(false);
            if (!Nav->TestPathSync(Query))
                continue;
            Point = Projected.Location;
        }
        FVector Ground;
        if (!StoneGolemGeometry::Project(GetWorld(), this, Point, 200, Ground) ||
            !StoneGolemGeometry::Visible(GetWorld(), this, GetActorLocation(), Ground))
            continue;
        const FTransform Transform(GetActorRotation(), Ground + FVector(0, 0, HalfHeight + 2));
        AEnemyCharacter* Minion = nullptr;
        if (Director)
            Minion = Director->SpawnSummonedEnemy(this, Config->MinionClass, Transform);
        else
        {
            Minion = GetWorld()->SpawnActorDeferred<AEnemyCharacter>(
                Config->MinionClass, Transform, this, nullptr,
                ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
            if (Minion)
            {
                Minion->MonsterRoomId = MonsterRoomId;
                Minion->SummoningSource = this;
                Minion->FinishSpawning(Transform);
            }
        }
        // 普通地图也可能在 FinishSpawning 的碰撞检查中销毁对象，不能将悬空结果计作成功召唤。
        if (!IsValid(Minion) || Minion->IsActorBeingDestroyed())
        {
            continue;
        }
        Minions.Add(Minion, ExpectedLease);
        Minion->FOnDeath.AddUniqueDynamic(this, &AShamanEnemyCharacter::OnMinionRemoved);
        Minion->OnDestroyed.AddUniqueDynamic(this, &AShamanEnemyCharacter::OnMinionRemoved);
        ++Spawned;
    }
    MinionCount = GetOwnedMinionCount();
    return Spawned;
}
void AShamanEnemyCharacter::OnMinionRemoved(AActor* Actor)
{
    auto* Enemy = Cast<AEnemyCharacter>(Actor);
    if (!Enemy)
        return;
    int32 Lease;
    if (!Minions.RemoveAndCopyValue(Enemy, Lease))
        return;
    Enemy->FOnDeath.RemoveDynamic(this, &AShamanEnemyCharacter::OnMinionRemoved);
    Enemy->OnDestroyed.RemoveDynamic(this, &AShamanEnemyCharacter::OnMinionRemoved);
    if (Lease == GetDungeonPoolLease())
        MinionCount = GetOwnedMinionCount();
}
void AShamanEnemyCharacter::ClearSummons()
{
    if (!HasAuthority())
        return;
    auto Owned = MoveTemp(Minions);
    Minions.Reset();
    MinionCount = 0;
    auto* Director = ADungeonRunDirector::Find(GetWorld());
    for (const auto& Entry : Owned)
        if (auto* Enemy = Entry.Key.Get())
        {
            Enemy->FOnDeath.RemoveDynamic(this, &AShamanEnemyCharacter::OnMinionRemoved);
            Enemy->OnDestroyed.RemoveDynamic(this, &AShamanEnemyCharacter::OnMinionRemoved);
            if (Director)
                Director->RetireSummonedEnemy(this, Enemy);
            else
                Enemy->Destroy();
        }
}
void AShamanEnemyCharacter::Die(const FVector& Impulse)
{
    if (HasAuthority() && !bDead)
        ClearSummons();
    Super::Die(Impulse);
}
void AShamanEnemyCharacter::InvalidateEnemyAttack()
{
    Super::InvalidateEnemyAttack();
    if (bDead || IsDungeonPoolSleeping())
        ClearSummons();
}
void AShamanEnemyCharacter::EndPlay(EEndPlayReason::Type Reason)
{
    ClearSummons();
    Super::EndPlay(Reason);
}
