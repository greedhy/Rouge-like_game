// 调用链：统一 GA 捕获弱生命令牌 -> 图异步恢复时 Resolve -> 当前 ASC/池/Run/Mass Handle -> PureMassCombat 权限。
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/World.h"
#include "Interface/CombatInterface.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "Roguelike/DungeonRunDirector.h"

namespace
{
    /** 普通世界没有 Run 身份；有导演时保存其真实 RunId，包括尚未初始化的值。 */
    TOptional<FGuid> CurrentRun(UWorld* World)
    {
        if (const auto* Director = ADungeonRunDirector::Find(World))
        {
            return Director->RunId;
        }
        return {};
    }

    /** 导演出现/消失也改变运行域，避免普通世界令牌跨入新 Run。 */
    bool MatchesRun(UWorld* World, const TOptional<FGuid>& Expected)
    {
        const TOptional<FGuid> Current = CurrentRun(World);
        return Expected.IsSet() == Current.IsSet() && (!Expected.IsSet() || Expected.GetValue() == Current.GetValue());
    }

    /** 源角色死亡或销毁后不能继续发起延迟行为；普通非战斗 Actor 只做 UObject 生命周期校验。 */
    bool IsLiving(AActor* Actor)
    {
        return IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
               (!Actor->Implements<UCombatInterface>() || !ICombatInterface::Execute_IsDead(Actor));
    }

    /** 只保存真实正租约；0 表示未入池，不能给普通 Actor 编造生命序号。 */
    TOptional<int32> CurrentLease(AActor* Actor)
    {
        if (const auto* Enemy = Cast<AEnemyCharacter>(Actor); Enemy && Enemy->GetDungeonPoolLease() > 0)
        {
            return Enemy->GetDungeonPoolLease();
        }
        return {};
    }

    /** 拒绝休眠与下一租约；捕获时未入池的角色若随后入池，同样不再是原身份。 */
    bool MatchesLease(AActor* Actor, const TOptional<int32>& Expected)
    {
        const auto* Enemy = Cast<AEnemyCharacter>(Actor);
        if (!Enemy)
        {
            return !Expected.IsSet();
        }
        const int32 Current = Enemy->GetDungeonPoolLease();
        return !Enemy->IsDungeonPoolSleeping() &&
               (Expected.IsSet() ? Expected.GetValue() > 0 && Expected.GetValue() == Current : Current == 0);
    }

    /** 使用 EntityManager 验证真实句柄，避免外部销毁后查询快照/ID 映射尚未清理的窗口。 */
    bool MatchesMass(UWorld* World, int32 Id, FMassEntityHandle Expected)
    {
        const auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
        const auto* Entities = World->GetSubsystem<UMassEntitySubsystem>();
        return Mass && Entities && Expected.IsSet() && Mass->GetEntity(Id) == Expected &&
               Entities->GetEntityManager().IsEntityValid(Expected);
    }
} // namespace

/** 捕获一次服务器生命快照；失败不泄露部分填充的令牌。 */
bool AbilityExecutionIdentity::CaptureSource(AActor* Source, FAbilitySourceToken& OutToken, FString& OutError)
{
    OutToken = {};
    OutError.Reset();
    UWorld* World = IsValid(Source) ? Source->GetWorld() : nullptr;
    auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source);
    if (!World || !World->IsInitialized() || World->bIsTearingDown || World->GetNetMode() == NM_Client ||
        !IsLiving(Source) || !Source->HasAuthority() || !IsValid(ASC) || !ASC->AbilityActorInfo.IsValid() ||
        ASC->GetWorld() != World || ASC->GetAvatarActor() != Source)
    {
        OutError = TEXT("Source requires a live authoritative Avatar bound to its current ASC and world.");
        return false;
    }
    const TOptional<int32> Lease = CurrentLease(Source);
    if (!MatchesLease(Source, Lease))
    {
        OutError = TEXT("Source enemy lease is retired or sleeping.");
        return false;
    }
    OutToken.World = World;
    OutToken.Avatar = Source;
    OutToken.ASC = ASC;
    OutToken.DungeonRunId = CurrentRun(World);
    OutToken.EnemyLease = Lease;
    return true;
}

/** 先走现有战斗过滤，再把目标此刻的实际租约或 Mass 句柄绑定到来源世界。 */
bool AbilityExecutionIdentity::CaptureTarget(const FAbilitySourceToken& Source, const FPureMassCombatTarget& Target,
                                             FAbilityTargetToken& OutToken, FString& OutError)
{
    OutToken = {};
    OutError.Reset();
    AActor* Avatar = nullptr;
    if (!ResolveSource(Source, Avatar))
    {
        OutError = TEXT("Target capture requires a valid authoritative source token.");
        return false;
    }
    UWorld* World = Source.World.Get();
    FVector Location;
    if (!PureMassCombat::Resolve(World, Avatar, Target, Location))
    {
        OutError = TEXT("Target is not a live hostile permitted by the current world and combat rules.");
        return false;
    }
    FAbilityTargetToken Captured;
    Captured.World = World;
    Captured.Target = Target;
    Captured.DungeonRunId = CurrentRun(World);
    if (Target.IsActor())
    {
        AActor* Actor = Target.Actor.Get();
        Captured.ActorLease = CurrentLease(Actor);
        if (!IsLiving(Actor) || !Actor->HasAuthority() || !MatchesLease(Actor, Captured.ActorLease))
        {
            OutError = TEXT("Target Actor is not authoritative or its enemy lease is retired.");
            return false;
        }
    }
    else
    {
        if (const auto* Mass = World->GetSubsystem<UPureMassSubsystem>())
        {
            Captured.MassEntity = Mass->GetEntity(Target.EntityId);
        }
        if (!MatchesMass(World, Target.EntityId, Captured.MassEntity))
        {
            OutError = TEXT("Target Mass ID has no live entity handle in this world.");
            return false;
        }
    }
    OutToken = Captured;
    return true;
}

/** 验证常驻订阅所属生命；世界退出、死亡、换 Avatar 或池复用均使旧订阅失效。 */
bool AbilityExecutionIdentity::ResolveSourceAvatarLifetime(const FAbilitySourceToken& Source, AActor*& OutAvatar)
{
    OutAvatar = nullptr;
    UWorld* World = Source.World.Get();
    AActor* Avatar = Source.Avatar.Get();
    auto* ASC = Source.ASC.Get();
    if (!World || !World->IsInitialized() || World->bIsTearingDown || World->GetNetMode() == NM_Client ||
        !IsLiving(Avatar) || !Avatar->HasAuthority() || Avatar->GetWorld() != World || !IsValid(ASC) ||
        !ASC->AbilityActorInfo.IsValid() || ASC->GetWorld() != World || ASC->GetAvatarActor() != Avatar ||
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Avatar) != ASC ||
        !MatchesLease(Avatar, Source.EnemyLease))
    {
        return false;
    }
    OutAvatar = Avatar;
    return true;
}

/** 实际动作额外绑定捕获时的 Run，旧延迟伤害与图执行不能跨入新局。 */
bool AbilityExecutionIdentity::ResolveSource(const FAbilitySourceToken& Source, AActor*& OutAvatar)
{
    if (!ResolveSourceAvatarLifetime(Source, OutAvatar))
    {
        return false;
    }
    if (!MatchesRun(Source.World.Get(), Source.DungeonRunId))
    {
        OutAvatar = nullptr;
        return false;
    }
    return true;
}

/** 目标身份匹配后仍复核当前位置的敌对/房间权限，不能把捕获时的许可缓存成永久许可。 */
bool AbilityExecutionIdentity::ResolveTarget(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                             FVector& OutLocation)
{
    OutLocation = FVector::ZeroVector;
    AActor* Avatar = nullptr;
    if (!ResolveSource(Source, Avatar))
    {
        return false;
    }
    UWorld* World = Target.World.Get();
    if (!World || World != Source.World.Get() || !MatchesRun(World, Target.DungeonRunId))
    {
        return false;
    }
    if (Target.Target.IsActor())
    {
        AActor* Actor = Target.Target.Actor.Get();
        if (!IsLiving(Actor) || !Actor->HasAuthority() || !MatchesLease(Actor, Target.ActorLease))
        {
            return false;
        }
    }
    else if (!MatchesMass(World, Target.Target.EntityId, Target.MassEntity))
    {
        return false;
    }
    return PureMassCombat::Resolve(World, Avatar, Target.Target, OutLocation);
}
