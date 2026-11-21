// 调用链：图调度 -> 捕获敌对接触目标 -> 旧额外击退伤害/基础伤害 -> DamageResolved/Completed。
#include "AbilitySystem/Unified/Nodes/AbilityMeleeNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "PureMass/PureMassCombatTarget.h"
#include "Interface/CombatInterface.h"
#include "AbilitySystem/Defense/PerfectDodgeComponent.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "Engine/World.h"
#include "Combat/BloodFXGameplay.h"

bool UAbilityMeleeNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Radius) || Radius <= 0.f)
    {
        Error = TEXT("Melee contact radius must be finite and positive.");
        return false;
    }
    return true;
}

bool UAbilityMeleeNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                       const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityMeleeNodeConfig>(&Config);
    const FGuid BloodAttackId = FGuid::NewGuid();
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Error = TEXT("Melee source is stale.");
        return false;
    }
    // 实际接触节点才提交见证；使用同一球形查询与胶囊轴，见证不加入伤害目标列表。
    UPerfectDodgeComponent::ObserveActorAttack(Input.SourceToken, Id, Input.Origin,
        [&](const FVector& Center, float Radius, float HalfHeight)
        {
            const float Axis = FMath::Max(0.f, HalfHeight - Radius);
            const FVector Closest = FMath::ClosestPointOnSegment(Input.Origin,
                Center - FVector(0, 0, Axis), Center + FVector(0, 0, Axis));
            return FVector::DistSquared(Input.Origin, Closest) <= FMath::Square(Settings.Radius + Radius);
        });
    // 旧 BP 的 Actor 查询和数组顺序保留；新增 Mass 是显式配置，不改变旧资产命中集合。
    TArray<AActor*> Actors;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Avatar, Actors, {Avatar}, Settings.Radius, Input.Origin);
    TArray<FAbilityTargetToken> Targets;
    for (auto* Actor : Actors)
    {
        FAbilityTargetToken Token;
        FString Ignored;
        if (AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, FPureMassCombatTarget::FromActor(Actor), Token,
                                                    Ignored))
        {
            Targets.Add(Token);
        }
    }
    if (Settings.bIncludeMass)
    {
        const auto Mixed = PureMassCombat::Gather(Input.SourceToken.World.Get(), Avatar, Input.Origin, Settings.Radius);
        for (const auto& Target : Mixed)
        {
            FAbilityTargetToken Token;
            FString Ignored;
            if (!Target.IsActor() && AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Target, Token, Ignored))
            {
                Targets.Add(Token);
            }
        }
    }
    for (const auto& Target : Targets)
    {
        FVector Location;
        if (bStopped || Owner.IsFinished() ||
            !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, Location))
        {
            continue;
        }
        FHitResult DodgeWall;
        FCollisionQueryParams DodgeQuery(SCENE_QUERY_STAT(PerfectDodgeMeleeWall), false, Avatar);
        DodgeQuery.AddIgnoredActor(Target.Target.Actor.Get());
        const bool bPerfectVisible = !Target.Target.IsActor() ||
            !Input.SourceToken.World->LineTraceSingleByObjectType(DodgeWall, Input.Origin, Location,
                FCollisionObjectQueryParams(ECC_WorldStatic), DodgeQuery);
        if (Target.Target.IsActor() &&
            UPerfectDodgeComponent::TryDodgeActorHit(Target.Target.Actor.Get(), Input.SourceToken, Id, bPerfectVisible))
        {
            // 同段的额外击退伤害和基础伤害一起拦截，不施加Debuff、受击或第二份GE。
            continue;
        }
        if (Target.Target.IsActor() && bPerfectVisible &&
            UGuardParryComponent::TryParryMelee(Target.Target.Actor.Get(), Input.SourceToken, Id))
        {
            continue;
        }
        auto Params = Input.DamageParams;
        const FVector Direction = (Location - Avatar->GetActorLocation()).GetSafeNormal();
        Params.KnockbackForce = Direction * Params.KnockbackForceMagnitude;
        Params.DeathImpulse = Direction * Params.DeathImpulseMagnitude;
        const bool bExtra = FMath::FRandRange(0.f, 100.f) < Params.KnockbackChance;
        const FBloodHitSnapshot Blood = BloodFXGameplay::Capture(Target, Input.Origin, Location);
        FAbilityDamageResult ExtraResult;
        if (Settings.bLegacyExtraDamageOnKnockback && bExtra)
        {
            ExtraResult = AbilityDamageService::Apply(Input.SourceToken, Target, Params);
            BloodFXGameplay::Confirm(Blood, ExtraResult, BloodAttackId);
        }
        // GE 可同步触发死亡/回收/取消；第二路径不允许击中刚复用的新生命。
        if (bStopped || Owner.IsFinished())
        {
            return true;
        }
        FVector LiveLocation;
        const bool bCanApplyBase = AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, LiveLocation);
        bool bAttempted = ExtraResult.bAttempted;
        bool bKilled = ExtraResult.bKilled;
        if (bCanApplyBase && Target.Target.IsActor())
        {
            auto* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Target.Actor.Get());
            auto* SourceASC = Input.SourceToken.ASC.Get();
            if (TargetASC && SourceASC && Params.DamageGameplayEffectClass)
            {
                // 对应旧 CauseDamage：等级 1 的 GE，只写基础伤害，不附加第二份 Debuff/冲量元数据。
                const auto Spec =
                    SourceASC->MakeOutgoingSpec(Params.DamageGameplayEffectClass, 1.f, SourceASC->MakeEffectContext());
                if (Spec.IsValid())
                {
                    Spec.Data->SetSetByCallerMagnitude(Params.DamageType, Params.BaseDamage);
                    SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
                    FAbilityDamageResult Result;
                    if (const auto* Context = FMyGameplayEffectContext::GetMutable(Spec.Data->GetContext()))
                    {
                        Result.ActualDamage = Context->ResolvedHealthDamage;
                    }
                    BloodFXGameplay::Confirm(Blood, Result, BloodAttackId);
                    bAttempted = true;
                    if (auto* Actor = Target.Target.Actor.Get();
                        IsValid(Actor) && Actor->Implements<UCombatInterface>())
                    {
                        bKilled |= ICombatInterface::Execute_IsDead(Actor);
                    }
                }
            }
        }
        else if (bCanApplyBase)
        {
            Params.DebuffChance = 0.f;
            Params.DeathImpulse = FVector::ZeroVector;
            const auto BaseResult = AbilityDamageService::Apply(Input.SourceToken, Target, Params);
            BloodFXGameplay::Confirm(Blood, BaseResult, BloodAttackId);
            bAttempted |= BaseResult.bAttempted;
            bKilled |= BaseResult.bKilled;
        }
        if (bStopped || Owner.IsFinished())
        {
            return true;
        }
        FAbilityNodeInput Payload = Input;
        FAbilityImpactSnapshot Impact;
        Impact.Location = Location;
        Impact.Direction = Direction;
        Impact.ConfirmedTarget = Target;
        Payload.Impact = Impact;
        Payload.LiveTargetToken.Reset();
        Payload.bHasDamageResult = true;
        Payload.bDamageAttempted = bAttempted;
        // 额外路径致死也必须保留这次命中的历史结果，否则原 GA 的命中 Cue 会消失。
        Payload.bKilled = bKilled;
        Owner.Publish(Id, TEXT("DamageResolved"), Payload);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}

void AbilityMeleeNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Melee.Contact");
    Descriptor.ConfigClass = UAbilityMeleeNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityMeleeNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Descriptor.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Melee registration: %s"), *Error);
    }
}
