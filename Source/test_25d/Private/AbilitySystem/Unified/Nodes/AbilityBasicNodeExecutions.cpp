// 调用链：图调用注册策略 -> Delay 推进/Petrify 服务 -> Completed 入队；共享配置保持只读。
#include "AbilitySystem/Unified/Nodes/AbilityBasicNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityStatusService.h"

bool UAbilityDelayNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DelaySeconds) || DelaySeconds <= 0.f)
    {
        Error = TEXT("DelaySeconds must be finite and positive.");
    }
    return Error.IsEmpty();
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
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Location) ||
        !AbilityStatusService::ApplyPetrify(Source, Input.LiveTargetToken->Target, Petrify.DurationSeconds,
                                            Petrify.StoneMaterial))
    {
        Error = TEXT("Petrify requires a valid live target and successful state application.");
        return false;
    }
    Input.Origin = Location;
    return Owner.Publish(Id, TEXT("Completed"), Input);
}
