// 调用链：图调用注册策略 -> Delay 推进/Petrify 服务 -> Completed 入队；共享配置保持只读。
// 配方编译器在私有配置副本设置石化时长，执行时将同一字段交给状态服务。
#include "AbilitySystem/Unified/Nodes/AbilityBasicNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityStatusService.h"
#include "Materials/MaterialInterface.h"

bool UAbilityDelayNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DelaySeconds) || DelaySeconds <= 0.f)
    {
        Error = TEXT("DelaySeconds must be finite and positive.");
    }
    return Error.IsEmpty();
}

bool UAbilityDelayNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::Duration)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    Error.Reset();
    if (!FMath::IsFinite(Value) || Value < .01f || Value > 60.f)
    {
        Error = TEXT("Delay requires a finite duration from 0.01 to 60 seconds.");
        return false;
    }
    DelaySeconds = Value;
    return true;
}

bool UAbilityPetrifyNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.f)
    {
        Error = TEXT("DurationSeconds must be finite and positive.");
    }
    return Error.IsEmpty();
}

bool UAbilityPetrifyNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Duration || !FMath::IsFinite(Multiplier) || Multiplier <= 0.f)
    {
        return false;
    }
    const float ScaledDuration = DurationSeconds * Multiplier;
    if (!FMath::IsFinite(ScaledDuration) || ScaledDuration <= 0.f)
    {
        return false;
    }
    DurationSeconds = ScaledDuration;
    return true;
}

bool UAbilityPetrifyNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::Duration)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    Error.Reset();
    if (!FMath::IsFinite(Value) || Value < .01f || Value > 60.f)
    {
        Error = TEXT("Petrify duration edit requires a finite value from 0.01 to 60 seconds.");
        return false;
    }
    DurationSeconds = Value;
    return true;
}

bool UAbilityPetrifyNodeConfig::SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                                           FString& Error) const
{
    if (RegisteredType != TEXT("Status.Petrify") || Incoming != EAbilityPortPayload::LiveTarget || !Validate(Error) ||
        DurationSeconds > 60.f)
    {
        Error = TEXT("Repeated petrify requires its registered LiveTarget input and duration <= 60 seconds.");
        return false;
    }
    return true;
}

bool UAbilityDelayNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                       const FAbilityNodeInput& InInput, FString& Error)
{
    Super::Start(Owner, Id, Config, InInput, Error);
    Remaining = CastChecked<UAbilityDelayNodeConfig>(&Config)->DelaySeconds;
    return true;
}

void UAbilityDelayNodeExecution::Advance(float DeltaSeconds)
{
    if (bStopped || !GraphOwner.IsValid())
    {
        return;
    }
    Remaining -= DeltaSeconds;
    if (Remaining <= 0)
    {
        GraphOwner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

bool UAbilityPetrifyNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                         const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                         FString& Error)
{
    Super::Start(Owner, Id, Config, InInput, Error);
    AActor* Source = nullptr;
    FVector Location;
    const auto& Petrify = *CastChecked<UAbilityPetrifyNodeConfig>(&Config);
    if (!Input.LiveTargetToken.IsSet() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source) ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Location))
    {
        Error = TEXT("Petrify requires a valid live target.");
        return false;
    }
    auto* StoneMaterial = Petrify.StoneMaterial.Get();
    // Mass 的石化材质由渲染批决定。仅 Actor 分支需要解析此可选材质引用。
    if (!StoneMaterial && Input.LiveTargetToken->Target.IsActor() && !Petrify.SoftStoneMaterial.IsNull())
    {
        StoneMaterial = Petrify.SoftStoneMaterial.LoadSynchronous();
    }
    if (!AbilityStatusService::ApplyPetrify(Source, Input.LiveTargetToken->Target, Petrify.DurationSeconds, StoneMaterial))
    {
        Error = TEXT("Petrify requires a valid live target and successful state application.");
        return false;
    }
    Input.Origin = Location;
    return Owner.Publish(Id, TEXT("Completed"), Input);
}
