// 混合目标入口：控制器和技能运行时共用身份解析、范围收集与扫掠；角色走敌对接口，实体走子系统，墙体优先阻断命中。
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "Roguelike/DungeonRunDirector.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "Interface/CombatInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "test_25d/test_25d.h"

namespace
{
    // 当前实体群体均属敌方，禁止敌方角色将其选为攻击目标。
    bool CanTargetMass(AActor* Caster)
    {
        // Current Mass population is the enemy faction. Actor enemies must not damage allies.
        return IsValid(Caster) && !Caster->ActorHasTag(TEXT("Enemy"));
    }

    // 世界遮挡查询忽略战斗角色，由独立目标几何检测处理命中。
    void IgnoreCombatActors(UWorld* World, FCollisionQueryParams& Query)
    {
        // Combatants are tested separately from world obstruction, exactly like non-colliding Mass instances.
        // This also prevents friendly characters and corpses from becoming projectile walls.
        for (TActorIterator<AActor> It(World); It; ++It)
            if (It->Implements<UCombatInterface>())
                Query.AddIgnoredActor(*It);
    }

    // 忽略投射物碰撞响应，避免技能体积成为世界遮挡。
    FCollisionResponseParams ObstructionResponses()
    {
        FCollisionResponseParams Responses;
        Responses.CollisionResponse.SetResponse(ECC_Projectile, ECR_Ignore);
        return Responses;
    }
} // namespace

// 校验目标身份与存活状态并返回当前位置或快照，失败时清空输出。
bool PureMassCombat::Resolve(UWorld* World, AActor* Caster, const FPureMassCombatTarget& Target, FVector& OutLocation)
{
    OutLocation = FVector::ZeroVector;
    if (!World || !IsValid(Caster) || Caster->GetWorld() != World)
        return false;
    if (Target.IsActor())
    {
        AActor* Actor = Target.Actor.Get();
        if (Target.EntityId != INDEX_NONE || !IsValid(Actor) || Actor->GetWorld() != World ||
            !APrototypeRuntime::IsEnemy(Caster, Actor) || !ADungeonRunDirector::AllowsCombat(Caster, Actor))
            return false;
        OutLocation = Actor->GetActorLocation();
        return !OutLocation.ContainsNaN();
    }
    if (Target.EntityId <= 0 || !CanTargetMass(Caster))
        return false;
    auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
    FPureMassView View;
    if (!Mass || !Mass->Resolve(Target.EntityId, View) || View.Health <= 0.f || View.State == 3)
        return false;
    OutLocation = View.Location;
    return !OutLocation.ContainsNaN();
}

// 合并实体与角色候选，角色需通过敌对、存活及遭遇规则校验。
TArray<FPureMassCombatTarget> PureMassCombat::Gather(UWorld* World, AActor* Caster, FVector Origin, float Radius)
{
    TArray<FPureMassCombatTarget> Result;
    if (!World || !IsValid(Caster) || Caster->GetWorld() != World || Origin.ContainsNaN() || !FMath::IsFinite(Radius) ||
        Radius < 0.f)
        return Result;
    if (CanTargetMass(Caster))
        if (auto* Mass = World->GetSubsystem<UPureMassSubsystem>())
            for (int32 Id : Mass->QueryRadius(Origin, Radius))
                Result.Emplace(Id);
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->Implements<UCombatInterface>())
            continue;
        const auto Target = FPureMassCombatTarget::FromActor(*It);
        FVector Location;
        if (Resolve(World, Caster, Target, Location) && FVector::DistSquared(Origin, Location) <= FMath::Square(Radius))
            Result.Add(Target);
    }
    return Result;
}

// 通过统一遮挡查询判断目标是否可见。
bool PureMassCombat::Visible(UWorld* World, AActor* Caster, FVector Origin, const FPureMassCombatTarget& Target,
                             AActor* IgnoredEffect)
{
    FVector Location;
    if (Origin.ContainsNaN() || !Resolve(World, Caster, Target, Location))
        return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MixedCombatLOS), false, Caster);
    if (IgnoredEffect)
        Query.AddIgnoredActor(IgnoredEffect);
    IgnoreCombatActors(World, Query);
    FHitResult Wall;
    return !World->LineTraceSingleByChannel(Wall, Origin, Location, ECC_Visibility, Query, ObstructionResponses());
}

// 检查地面目标可见性，允许射线命中目标附近地面。
bool PureMassCombat::GroundVisible(UWorld* World, AActor* Caster, FVector Origin, FVector Point)
{
    if (!World || !IsValid(Caster) || Caster->GetWorld() != World || Origin.ContainsNaN() || Point.ContainsNaN())
        return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MixedGroundLOS), false, Caster);
    IgnoreCombatActors(World, Query);
    FHitResult Wall;
    return !World->LineTraceSingleByChannel(Wall, Origin, Point, ECC_Visibility, Query, ObstructionResponses()) ||
           FVector::DistSquared(Wall.ImpactPoint, Point) <= FMath::Square(15.f);
}

// 沿线段检测目标胶囊，选择最早有效命中并返回命中时间。
FPureMassCombatTarget PureMassCombat::Sweep(UWorld* World, AActor* Caster, FVector Start, FVector End, float Radius,
                                            float& OutTime, FHitResult& OutWall,
                                            const TSet<FPureMassCombatTarget>* Ignored, AActor* IgnoredEffect)
{
    OutTime = 1.f;
    OutWall = FHitResult();
    FPureMassCombatTarget Best;
    if (!World || !IsValid(Caster) || Caster->GetWorld() != World || Start.ContainsNaN() || End.ContainsNaN() ||
        !FMath::IsFinite(Radius) || Radius < 0.f)
        return Best;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MixedCombatSweep), false, Caster);
    if (IgnoredEffect)
        Query.AddIgnoredActor(IgnoredEffect);
    IgnoreCombatActors(World, Query);
    const bool bWall =
        Radius > 0.f
            ? World->SweepSingleByChannel(OutWall, Start, End, FQuat::Identity, ECC_Visibility,
                                          FCollisionShape::MakeSphere(Radius), Query, ObstructionResponses())
            : World->LineTraceSingleByChannel(OutWall, Start, End, ECC_Visibility, Query, ObstructionResponses());
    if (CanTargetMass(Caster))
        if (auto* Mass = World->GetSubsystem<UPureMassSubsystem>())
        {
            TSet<int32> IgnoredEntities;
            if (Ignored)
                for (const auto& Target : *Ignored)
                    if (!Target.IsActor())
                        IgnoredEntities.Add(Target.EntityId);
            float Time = 1.f;
            const int32 Id = Mass->Sweep(Start, End, Radius, Time, &IgnoredEntities);
            if (Id > 0 && (!bWall || Time < OutWall.Time))
            {
                Best = Id;
                OutTime = Time;
            }
        }
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->Implements<UCombatInterface>())
            continue;
        const auto Target = FPureMassCombatTarget::FromActor(*It);
        FVector Location;
        if ((Ignored && Ignored->Contains(Target)) || !Resolve(World, Caster, Target, Location))
            continue;
        float ActorRadius = 0.f, HalfHeight = 0.f;
        It->GetSimpleCollisionCylinder(ActorRadius, HalfHeight);
        if (!FMath::IsFinite(ActorRadius) || !FMath::IsFinite(HalfHeight) || ActorRadius <= 0.f)
            continue;
        float Time = 1.f;
        if (PureMassGeometry::SweepCapsule(Start, End, Radius, Location, ActorRadius,
                                           FMath::Max(ActorRadius, HalfHeight), Time) &&
            (!bWall || Time < OutWall.Time) && (!Best.IsSet() || Time < OutTime))
        {
            Best = Target;
            OutTime = Time;
        }
    }
    return Best;
}
