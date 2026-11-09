// 调用链：统一伤害动作 -> 生命令牌校验 -> 原 ApplyDamageEffect/ApplyDamage -> 可观测结果。
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Interface/CombatInterface.h"
#include "PureMass/PureMassSubsystem.h"
#include "Engine/World.h"

FAbilityDamageResult AbilityDamageService::Apply(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                                 const FDamageEffectParams& Parameters, float Scale,
                                                 bool bCaptureLocationAfterDamage)
{
    FAbilityDamageResult Result;
    AActor* Avatar = nullptr;
    if (!FMath::IsFinite(Scale) || Scale < 0.f || !FMath::IsFinite(Parameters.BaseDamage) ||
        Parameters.BaseDamage < 0.f || !FMath::IsFinite(Parameters.BaseDamage * Scale) ||
        !AbilityExecutionIdentity::ResolveSource(Source, Avatar) ||
        !AbilityExecutionIdentity::ResolveTarget(Source, Target, Result.ImpactLocation))
    {
        return Result;
    }
    FDamageEffectParams Params = Parameters;
    // 输入载荷不能把同一世界的其它 ASC 当作伤害来源；始终使用令牌验证过的当前 ASC。
    if (Params.SourceAbilitySystemComponent && Params.SourceAbilitySystemComponent != Source.ASC.Get())
    {
        return Result;
    }
    Params.SourceAbilitySystemComponent = Source.ASC.Get();
    Params.WorldContextObject = Avatar;
    Params.BaseDamage *= Scale;
    if (Target.Target.IsActor())
    {
        AActor* Actor = Target.Target.Actor.Get();
        Params.TargetAbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
        if (!Params.TargetAbilitySystemComponent || !Params.DamageGameplayEffectClass)
        {
            return Result;
        }
        Result.bAttempted = true;
        UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
        // 不解引用被同步伤害回调销毁的 Actor。致死后仍可输出位置快照，不能复活旧目标令牌。
        if (IsValid(Actor))
        {
            Result.bKilled = Actor->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Actor);
            if (bCaptureLocationAfterDamage)
            {
                Result.ImpactLocation = Actor->GetActorLocation();
            }
        }
    }
    else if (auto* Mass = Source.World->GetSubsystem<UPureMassSubsystem>())
    {
        FPureMassDamage Damage;
        Damage.Source = Avatar;
        Damage.Amount = Params.BaseDamage;
        Damage.DamageType = Params.DamageType;
        Damage.DeathImpulse = Params.DeathImpulse;
        if (Params.DebuffChance > 0.f && FMath::FRandRange(0.f, 100.f) < Params.DebuffChance)
        {
            Damage.StatusDuration = Params.DebuffDuration;
            Damage.DamagePerSecond = Params.DebuffDamage / FMath::Max(.1f, Params.DebuffFrequency);
        }
        Result.bAttempted = true;
        // 致死会立即移除查询快照，不能把 Resolve 失败解释为击杀。只认本次调用期间的 Killed 通知。
        const int32 TargetId = Target.Target.EntityId;
        const FDelegateHandle DeathHandle = Mass->OnMonsterRemoved.AddLambda(
            [&Result, TargetId](int32 RemovedId, FName, EPureMassRemovalReason Reason)
            {
                Result.bKilled |= RemovedId == TargetId && Reason == EPureMassRemovalReason::Killed;
            });
        Result.ActualDamage = Mass->ApplyDamage(TargetId, Damage);
        Mass->OnMonsterRemoved.Remove(DeathHandle);
        // 零伤害仍可能成功应用状态，因此不由伤害值推导 Applied=false。
        if (Result.ActualDamage.GetValue() > 0.f)
        {
            Result.Applied = true;
        }
    }
    return Result;
}
