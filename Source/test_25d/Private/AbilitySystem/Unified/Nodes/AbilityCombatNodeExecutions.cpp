// 调用链：注册策略 -> 查询/伤害服务 -> 类型化事件队列；每个目标事件有自己的生命令牌和调用身份。
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"

bool UAbilityResolveImpactNodeConfig::SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                                                 FString& Error) const
{
    Error.Reset();
    const bool bExactContract =
        (RegisteredType == TEXT("Target.ResolveImpact") && Incoming == EAbilityPortPayload::ConfirmedImpact) ||
        (RegisteredType == TEXT("Target.ResolveDamageTarget") && Incoming == EAbilityPortPayload::DamageResult);
    if (!bExactContract || !Validate(Error))
    {
        Error =
            TEXT("Repeated target resolution requires its exact registered historical impact or damage-result input.");
        return false;
    }
    return true;
}

bool UAbilityResolveImpactNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                               FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Input.Impact.IsSet())
    {
        Error = TEXT("Resolving an impact requires an explicit historical snapshot.");
        return false;
    }
    FVector Location;
    if (AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.Impact->ConfirmedTarget, Location))
    {
        FAbilityNodeInput Payload = Input;
        Payload.LiveTargetToken = Input.Impact->ConfirmedTarget;
        Payload.Origin = Location;
        Owner.Publish(Id, TEXT("TargetFound"), Payload);
    }
    else
    {
        // 目标死亡、回收或被替换只跳过该后续效果，绝不能控制同地址上的新生命。
        Owner.Publish(Id, TEXT("NoTarget"), Input);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}

bool UAbilityQueryNodeConfig::Validate(FString& Error) const
{
    return Query.Validate(Error);
}

bool UAbilityDamageNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Damage) || !FMath::IsFinite(DamagePerLevel) || Damage < 0.f || DamagePerLevel < 0.f ||
        !DamageType.IsValid())
    {
        Error = TEXT("Damage requires finite nonnegative values and a registered damage type.");
        return false;
    }
    return true;
}

bool UAbilityQueryNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                       const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    FAbilityQueryRequest Query = CastChecked<UAbilityQueryNodeConfig>(&Config)->Query;
    Query.Origin = Input.Origin;
    Query.Direction = Input.Direction;
    TArray<FAbilityTargetToken> Targets;
    if (!AbilityTargetQueryService::Gather(Input.SourceToken, Query, Targets, Error))
    {
        return false;
    }
    for (const auto& Target : Targets)
    {
        if (bStopped || Owner.IsFinished())
        {
            break;
        }
        FAbilityNodeInput Payload = Input;
        Payload.LiveTargetToken = Target;
        if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, Payload.Origin))
        {
            continue;
        }
        Owner.Publish(Id, TEXT("TargetFound"), Payload);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        if (Targets.IsEmpty())
        {
            Owner.Publish(Id, TEXT("NoTarget"), Input);
        }
        Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}

bool UAbilityDamageNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                        const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Input.LiveTargetToken.IsSet())
    {
        Error = TEXT("Damage node requires a live target payload.");
        return false;
    }
    const auto* Damage = CastChecked<UAbilityDamageNodeConfig>(&Config);
    auto Params = Input.DamageParams;
    Params.BaseDamage = Damage->Damage + Damage->DamagePerLevel * FMath::Clamp(Input.Level - 1.f, 0.f, 99.f);
    Params.DamageType = Damage->DamageType;
    const auto Result = AbilityDamageService::Apply(Input.SourceToken, Input.LiveTargetToken.GetValue(), Params);
    if (bStopped || Owner.IsFinished())
    {
        return true;
    }
    FAbilityNodeInput Payload = Input;
    FAbilityImpactSnapshot Impact;
    Impact.Location = Result.ImpactLocation;
    Impact.Direction = Input.Direction;
    Impact.ConfirmedTarget = Input.LiveTargetToken.GetValue();
    Payload.Impact = MoveTemp(Impact);
    Payload.bHasDamageResult = true;
    Payload.bDamageAttempted = Result.bAttempted;
    Payload.bKilled = Result.bKilled;
    Payload.DamageApplied = Result.Applied;
    Payload.ActualDamage = Result.ActualDamage;
    Payload.LiveTargetToken.Reset();
    Owner.Publish(Id, TEXT("DamageResolved"), Payload);
    Owner.Publish(Id, TEXT("Completed"), Payload);
    return true;
}

void AbilityCombatNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (const bool bDamage : {false, true})
    {
        FAbilityNodeDescriptor Resolve;
        Resolve.TypeId = bDamage ? TEXT("Target.ResolveDamageTarget") : TEXT("Target.ResolveImpact");
        Resolve.ConfigClass = UAbilityResolveImpactNodeConfig::StaticClass();
        Resolve.ExecutionClass = UAbilityResolveImpactNodeExecution::StaticClass();
        Resolve.InputPayload = bDamage ? EAbilityPortPayload::DamageResult : EAbilityPortPayload::ConfirmedImpact;
        Resolve.bSupportsActorTargets = Resolve.bSupportsMassTargets = Resolve.bSupportsSnapshotTargets = true;
        // 单次原生命权限转换，无资源、定时器或累计历史；仍消费所属根的事件/调用额度。
        Resolve.bSustainedSafe = true;
        Resolve.bRuntimeFiniteSafe = true;
        Resolve.bRuntimePurePayload = true;
        // 两种已审输入都只同步重验历史原生命；重复DamageResolved分支也不能重捕同Actor新生命。
        // pure配置证明仍逐模式校验，不能把此helper标记当作任意历史输入的准入。
        Resolve.bPrototypeCompatibilitySafe = true;
        Resolve.Ports = {{TEXT("TargetFound"), EAbilityPortPayload::LiveTarget, false},
                         {TEXT("NoTarget"), EAbilityPortPayload::Origin, false},
                         {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        if (!Registry.Find(Resolve.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Resolve, Error), TEXT("Target bridge registration: %s"), *Error);
        }
    }
    FAbilityNodeDescriptor Query;
    Query.TypeId = TEXT("Query.Shape");
    Query.ConfigClass = UAbilityQueryNodeConfig::StaticClass();
    Query.ExecutionClass = UAbilityQueryNodeExecution::StaticClass();
    Query.InputPayload = EAbilityPortPayload::Origin;
    Query.bSupportsActorTargets = Query.bSupportsMassTargets = Query.bSupportsSnapshotTargets = true;
    Query.bRuntimeFiniteSafe = true;
    Query.Ports = {{TEXT("TargetFound"), EAbilityPortPayload::LiveTarget, false},
                   {TEXT("NoTarget"), EAbilityPortPayload::Origin, false},
                   {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Damage;
    Damage.TypeId = TEXT("Effect.Damage");
    Damage.ConfigClass = UAbilityDamageNodeConfig::StaticClass();
    Damage.ExecutionClass = UAbilityDamageNodeExecution::StaticClass();
    Damage.InputPayload = EAbilityPortPayload::LiveTarget;
    Damage.bSupportsActorTargets = Damage.bSupportsMassTargets = true;
    Damage.bRuntimeFiniteSafe = true;
    Damage.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                    {TEXT("Completed"), EAbilityPortPayload::DamageResult, true}};
    for (const auto& Descriptor : {Query, Damage})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Combat node registration: %s"), *Error);
        }
    }
}
