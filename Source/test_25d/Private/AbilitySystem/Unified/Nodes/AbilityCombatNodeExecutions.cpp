// 调用链：注册策略 -> 查询/伤害服务 -> 类型化事件队列；每个目标事件有自己的生命令牌和调用身份。
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"

bool UAbilityResolveImpactNodeConfig::MatchesType(FName TypeId) const
{
    return !bRequirePositiveDamage || TypeId == TEXT("Target.ResolveDamageTarget");
}

bool UAbilityResolveImpactNodeConfig::SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                                                 FString& Error) const
{
    Error.Reset();
    const bool bExactContract =
        (RegisteredType == TEXT("Target.ResolveImpact") && Incoming == EAbilityPortPayload::ConfirmedImpact) ||
        (RegisteredType == TEXT("Target.ResolveDamageTarget") && Incoming == EAbilityPortPayload::DamageResult);
    if (!bExactContract || !MatchesType(RegisteredType) || !Validate(Error))
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
    const auto* Settings = CastChecked<UAbilityResolveImpactNodeConfig>(&Config);
    const bool bAdmitted = !Settings->bRequirePositiveDamage ||
                           (Input.bHasDamageResult && Input.bDamageAttempted && Input.ActualDamage.Get(0.f) > 0.f);
    FVector Location;
    if (bAdmitted && AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.Impact->ConfirmedTarget, Location))
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

bool UAbilityQueryNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Radius || Query.Geometry == EAbilityQueryGeometry::Line ||
        !FMath::IsFinite(Multiplier) || Multiplier <= 0.f)
    {
        return false;
    }
    auto Updated = Query;
    Updated.Radius *= Multiplier;
    FString Error;
    return ApplyQueryEdit(Updated, Error);
}

bool UAbilityQueryNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (!FMath::IsFinite(Value) || Value < 0.f || Value > 10000.f)
    {
        Error = TEXT("Query parameters must be finite and within the author's supported range.");
        return false;
    }
    using G = EAbilityQueryGeometry;
    auto Updated = Query;
    const bool bLine = Query.Geometry == G::Line;
    const bool bCone = Query.Geometry == G::Cone;
    const bool bHeight = bLine || Query.Geometry == G::Ring || Query.Geometry == G::WaveFront ||
                         (bCone && Query.Profile == EAbilityShapeQueryProfile::Generic);
    if (Parameter == EAbilityConfigParameter::Radius && !bLine)
    {
        Updated.Radius = Value;
    }
    else if (Parameter == EAbilityConfigParameter::InnerRadius && Query.Geometry == G::Ring)
    {
        Updated.InnerRadius = Value;
    }
    else if (Parameter == EAbilityConfigParameter::Range && bLine)
    {
        Updated.Range = Value;
    }
    else if (Parameter == EAbilityConfigParameter::Width && bLine)
    {
        Updated.Width = Value;
    }
    else if (Parameter == EAbilityConfigParameter::HalfHeight && bHeight && Value <= 2000.f)
    {
        Updated.HalfHeight = Value;
    }
    else if (Parameter == EAbilityConfigParameter::Spread && bCone && Value <= 360.f)
    {
        Updated.HalfAngleDegrees = Value * .5f;
    }
    else if (Parameter == EAbilityConfigParameter::Count && Value >= 1.f && Value <= 64.f &&
             Value == FMath::RoundToFloat(Value))
    {
        Updated.MaxTargets = static_cast<int32>(Value);
    }
    else if (Parameter == EAbilityConfigParameter::QueryGeometry && Query.Profile == EAbilityShapeQueryProfile::Generic &&
             Value <= static_cast<float>(G::Line) && Value == FMath::RoundToFloat(Value))
    {
        Updated.Geometry = static_cast<G>(static_cast<uint8>(Value));
    }
    else
    {
        Error = TEXT("The requested axis is not consumed by this query geometry or exceeds its limits.");
        return false;
    }
    return ApplyQueryEdit(Updated, Error);
}

bool UAbilityQueryNodeConfig::ApplyQueryEdit(const FAbilityQueryRequest& Edited, FString& Error)
{
    const auto Previous = Query;
    Query = Edited;
    if (!Validate(Error))
    {
        Query = Previous;
        return false;
    }
    return true;
}

bool UAbilityDamageNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Damage) || !FMath::IsFinite(DamagePerLevel) || Damage < 0.f || DamagePerLevel < 0.f ||
        !ResolveDamageType().IsValid())
    {
        Error = TEXT("Damage requires finite nonnegative values and a registered damage type.");
        return false;
    }
    return true;
}

FGameplayTag UAbilityDamageNodeConfig::ResolveDamageType() const
{
    return ElementOverride == EAbilityElement::Original ? DamageType : AbilityElements::ToDamageTag(ElementOverride);
}

bool UAbilityDamageNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Damage || !FMath::IsFinite(Multiplier) || Multiplier < 0.f ||
        !FMath::IsFinite(Damage * Multiplier) || !FMath::IsFinite(DamagePerLevel * Multiplier))
    {
        return false;
    }
    Damage *= Multiplier;
    DamagePerLevel *= Multiplier;
    return true;
}

bool UAbilityDamageNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::ElementConversion)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!FMath::IsFinite(Value) || Value < 0.f || Value > static_cast<float>(EAbilityElement::Ice) ||
        Value != FMath::RoundToFloat(Value))
    {
        Error = TEXT("Damage element conversion requires an integer from 0 to 5.");
        return false;
    }
    // 0 保留当前类型；移除这项贡献时由配方编译器重放原图。
    if (Value > 0.f)
    {
        ElementOverride = static_cast<EAbilityElement>(static_cast<uint8>(Value));
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
        auto Completed = Input;
        Completed.ShapeRadius.Reset();
        if (Query.Geometry == EAbilityQueryGeometry::Sphere)
        {
            Completed.ShapeRadius = Query.Radius;
        }
        Owner.Publish(Id, TEXT("Completed"), Completed);
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
    Params.DamageType = Damage->ResolveDamageType();
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
