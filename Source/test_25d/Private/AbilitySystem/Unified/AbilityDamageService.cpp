// 调用链：统一伤害动作 -> 生命令牌校验 -> 原 ApplyDamageEffect/ApplyDamage -> 可观测结果。
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Interface/CombatInterface.h"
#include "PureMass/PureMassSubsystem.h"
#include "Engine/World.h"

bool AbilityDamageService::PrepareContactDamage(const FDamageEffectParams& Original, FVector Direction,
                                                float DamageScale, float DebuffDamageScale,
                                                FDamageEffectParams& OutPrepared, EAbilityContactDamageProfile Profile)
{
    OutPrepared = Original;
    if (Profile != EAbilityContactDamageProfile::Unified &&
        Profile != EAbilityContactDamageProfile::NativeActorProjectile &&
        Profile != EAbilityContactDamageProfile::NativeMassProjectile &&
        Profile != EAbilityContactDamageProfile::ConfiguredForces)
    {
        return false;
    }
    if (Direction.ContainsNaN() || !FMath::IsFinite(DamageScale) || DamageScale < 0.f ||
        !FMath::IsFinite(DebuffDamageScale) || DebuffDamageScale < 0.f || !FMath::IsFinite(Original.BaseDamage) ||
        Original.BaseDamage < 0.f || !FMath::IsFinite(Original.DebuffDamage) || Original.DebuffDamage < 0.f ||
        !FMath::IsFinite(Original.BaseDamage * DamageScale) ||
        !FMath::IsFinite(Original.DebuffDamage * DebuffDamageScale) ||
        !FMath::IsFinite(Original.DeathImpulseMagnitude) || !FMath::IsFinite(Original.KnockbackForceMagnitude) ||
        !FMath::IsFinite(Original.KnockbackChance))
    {
        return false;
    }
    // 只缩放局部结算副本；后继仍传原Params与累计scales，不能把这个副本写回配方输入。
    OutPrepared.BaseDamage *= DamageScale;
    OutPrepared.DebuffDamage *= DebuffDamageScale;
    if (Profile == EAbilityContactDamageProfile::ConfiguredForces)
    {
        // 原Mixed Actor Hit直接消费配置向量；只缩放局部数值，不派生力，也不消耗击退随机数。
        return true;
    }
    if (Profile == EAbilityContactDamageProfile::NativeMassProjectile)
    {
        // 原UPureMassLegacyProjectile只填数值/状态，不传preset死亡力也不掷Actor击退随机数。
        // 实际Apply仍使用同一Mass伤害入口；这里只冻结旧接触合同，不能在纯投递时调用。
        OutPrepared.DeathImpulse = FVector::ZeroVector;
        OutPrepared.KnockbackForce = FVector::ZeroVector;
        return true;
    }
    const FVector ContactDirection = Direction.GetSafeNormal();
    OutPrepared.DeathImpulse = ContactDirection * Original.DeathImpulseMagnitude;
    // 保留原投射物的整数单次判定与45度抬升；只有实际伤害owner调用，纯接触不消耗RNG。
    if (FMath::RandRange(0, 100) < Original.KnockbackChance)
    {
        auto Rotation = ContactDirection.Rotation();
        Rotation.Pitch = 45.f;
        OutPrepared.KnockbackForce = Rotation.Vector() * Original.KnockbackForceMagnitude;
    }
    return true;
}

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
        const auto Context = UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
        if (const auto* Resolved = FMyGameplayEffectContext::GetMutable(Context))
        {
            Result.ActualDamage = Resolved->ResolvedHealthDamage;
        }
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
