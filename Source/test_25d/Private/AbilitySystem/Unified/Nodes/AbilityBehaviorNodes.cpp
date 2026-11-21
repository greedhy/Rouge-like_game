// 调用链：GraphInstance -> 本次查询/有限节拍/位置锚点/减速 -> 类型化事件队列；状态服务独立到期。
// 配方编译器在私有配置副本修改时长，减速执行仍将原移动倍率和编辑后的秒数交给状态服务。
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

bool UAbilityNearestNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Radius) || Radius <= 0.f || Radius > 2000.f || (!bIncludeActors && !bIncludeMass))
    {
        Error = TEXT("Nearest query requires radius in (0, 2000] and at least one target representation.");
    }
    return Error.IsEmpty();
}

bool UAbilityRepeatNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Finite Repeat typed configuration requires a finite value");
        return false;
    }
    const TStrongObjectPtr<UAbilityRepeatNodeConfig> Candidate(
        DuplicateObject<UAbilityRepeatNodeConfig>(this, GetTransientPackage()));
    switch (Parameter)
    {
    case EAbilityConfigParameter::Count:
        if (Value < 1.f || Value > 64.f || Value != FMath::FloorToFloat(Value))
        {
            Error = TEXT("Finite Repeat count must be an integer from 1 to 64");
            return false;
        }
        Candidate->Count = int32(Value);
        break;
    case EAbilityConfigParameter::Period:
        Candidate->Period = Value;
        break;
    case EAbilityConfigParameter::Duration:
        Candidate->Duration = Value;
        break;
    default:
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!Candidate->Validate(Error))
    {
        return false;
    }
    // 仅提交有限数值；即时首发/尾边界与节拍profile保持原显式配置，不能由Count申请无限持续权限。
    Count = Candidate->Count;
    Period = Candidate->Period;
    Duration = Candidate->Duration;
    return true;
}

bool UAbilityRepeatNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (Count < 1 || Count > 64 || !FMath::IsFinite(Period) || Period < .01f || Period > 60.f ||
        !FMath::IsFinite(Duration) || Duration < .01f || Duration > 60.f ||
        CadenceProfile < EAbilityRepeatCadenceProfile::CatchUp ||
        CadenceProfile > EAbilityRepeatCadenceProfile::SingleDiscard)
    {
        Error = TEXT("Finite repeat requires Count 1..64, Period/Duration .01..60, and a registered cadence profile.");
    }
    return Error.IsEmpty();
}

bool UAbilityAnchorNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (Anchor < EAbilitySpatialAnchor::OriginalOrigin || Anchor > EAbilitySpatialAnchor::ConfirmedImpact ||
        Offset.ContainsNaN())
    {
        Error = TEXT("Anchor requires a registered position mode and finite world offset.");
    }
    return Error.IsEmpty();
}

bool UAbilitySlowNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Multiplier) || Multiplier <= 0.f || Multiplier > 1.f || !FMath::IsFinite(Duration) ||
        Duration <= 0.f || Duration > 60.f)
    {
        Error = TEXT("Slow requires multiplier in (0, 1] and finite duration in (0, 60].");
    }
    return Error.IsEmpty();
}

bool UAbilitySlowNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Scale)
{
    if (Parameter != EAbilityConfigScalar::Duration || !FMath::IsFinite(Scale) || Scale <= 0.f)
    {
        return false;
    }
    const float ScaledDuration = Duration * Scale;
    if (!FMath::IsFinite(ScaledDuration) || ScaledDuration <= 0.f || ScaledDuration > 60.f)
    {
        return false;
    }
    Duration = ScaledDuration;
    return true;
}

bool UAbilitySlowNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::Duration)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    Error.Reset();
    if (!FMath::IsFinite(Value) || Value < .01f || Value > 60.f)
    {
        Error = TEXT("Slow duration edit requires a finite value from 0.01 to 60 seconds.");
        return false;
    }
    Duration = Value;
    return true;
}

bool UAbilityNearestNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                         const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                         FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityNearestNodeConfig>(&Config);
    AActor* Source = nullptr;
    if (!Settings.Validate(Error) || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source))
    {
        Error = TEXT("Nearest query requires a current authoritative source and valid settings.");
        return false;
    }
    TArray<FAbilityTargetCandidate> Candidates;
    TArray<FAbilityTargetToken> Excluded;
    if (Settings.bExcludeInputTarget)
    {
        if (Input.LiveTargetToken.IsSet())
        {
            Excluded.Add(Input.LiveTargetToken.GetValue());
        }
        if (Input.Impact.IsSet())
        {
            Excluded.Add(Input.Impact->ConfirmedTarget);
        }
    }
    // 收集整个空间查询结果后再选最近，不先按某个 MaxTargets 前缀截断。
    for (const auto& Candidate :
         PureMassCombat::Gather(Input.SourceToken.World.Get(), Source, Input.Origin, Settings.Radius))
    {
        if ((Candidate.IsActor() && !Settings.bIncludeActors) || (!Candidate.IsActor() && !Settings.bIncludeMass))
        {
            continue;
        }
        FAbilityTargetToken Token;
        FVector Position;
        FString CaptureError;
        if (!AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Candidate, Token, CaptureError) ||
            !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Token, Position) ||
            (Settings.bRequireVisibility &&
             !PureMassCombat::Visible(Input.SourceToken.World.Get(), Source, Input.Origin, Candidate)))
        {
            continue;
        }
        Candidates.Add({Token, Position});
    }
    const auto Selected = AbilityBehaviorPrimitives::SelectNearest(Candidates, Input.Origin, Settings.Radius, Excluded);
    FAbilityNodeInput PositionOnly = Input;
    PositionOnly.LiveTargetToken.Reset();
    if (Selected.IsSet())
    {
        FAbilityNodeInput Found = Input;
        Found.LiveTargetToken = Selected->Target;
        // 选敌不决定投递起点；保留 Origin，瞄准点单独写入 EventCenter。
        Found.EventCenter = Selected->Location;
        Owner.Publish(Id, TEXT("TargetFound"), Found);
    }
    else
    {
        Owner.Publish(Id, TEXT("NoTarget"), PositionOnly);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        return Owner.Publish(Id, TEXT("Completed"), PositionOnly);
    }
    return true;
}

bool UAbilityRepeatNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                        const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = CastChecked<UAbilityRepeatNodeConfig>(const_cast<UAbilityNodeConfig*>(&Config));
    if (!Settings->Validate(Error))
    {
        return false;
    }
    Age = Clock = 0.f;
    Emitted = 0;
    if (Settings->bEmitImmediately)
    {
        EmitPulse();
    }
    return true;
}

void UAbilityRepeatNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Settings)
    {
        return;
    }
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f)
    {
        Owner->Cancel(EAbilityGraphEndReason::Failed);
        return;
    }
    // 在发布前冻结本帧策略；Completed 或预算/取消可能同步 Stop 并清空 Settings。
    const float Period = Settings->Period;
    const float Duration = Settings->Duration;
    const bool bIncludeBoundary = Settings->bIncludeFinalBoundary;
    const auto Profile = Settings->CadenceProfile;
    const float Remaining = FMath::Max(0.f, Duration - Age);
    const float ActiveDelta =
        FMath::Min(DeltaSeconds, FMath::Max(0.f, Remaining - (bIncludeBoundary ? 0.f : 2 * KINDA_SMALL_NUMBER)));
    Age = FMath::Min(Duration, Age + DeltaSeconds);
    const int32 MaxPulses = Profile == EAbilityRepeatCadenceProfile::MixedEightPreserve      ? 8
                            : Profile == EAbilityRepeatCadenceProfile::ExtensionSixteenClamp ? 16
                            : Profile == EAbilityRepeatCadenceProfile::SingleDiscard         ? 1
                                                                                             : 64;
    const EAbilityCadenceRemainder Remainder =
        Profile == EAbilityRepeatCadenceProfile::ExtensionSixteenClamp ? EAbilityCadenceRemainder::ClampToPeriod
        : Profile == EAbilityRepeatCadenceProfile::SingleDiscard       ? EAbilityCadenceRemainder::DiscardAfterPulse
                                                                       : EAbilityCadenceRemainder::Preserve;
    const float Tolerance = Profile == EAbilityRepeatCadenceProfile::MixedEightPreserve ||
                                    Profile == EAbilityRepeatCadenceProfile::SingleDiscard
                                ? 0.f
                                : KINDA_SMALL_NUMBER;
    AbilityBehaviorPrimitives::AdvanceCadence(
        Clock, ActiveDelta, Period, MaxPulses, Remainder,
        [&]()
        {
            return EmitPulse();
        },
        Tolerance);
    if (!bStopped && GraphOwner.IsValid() && !Owner->IsFinished() && Age >= Duration)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilityRepeatNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Settings = nullptr;
    Age = Clock = 0.f;
    Emitted = 0;
    Super::Stop(Reason);
}

bool UAbilityRepeatNodeExecution::EmitPulse()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Settings)
    {
        return false;
    }
    const int32 Limit = Settings->Count;
    // 每次都发布入口快照；前一 Pulse 子图改变位置/目标不会污染下一 Pulse。
    const FAbilityNodeInput Payload = Input;
    if (!Owner->Publish(InvocationId, TEXT("Pulse"), Payload))
    {
        return false;
    }
    ++Emitted;
    if (bStopped || !GraphOwner.IsValid() || Owner->IsFinished())
    {
        return false;
    }
    if (Emitted >= Limit)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
        return false;
    }
    return true;
}

bool UAbilityAnchorNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                        const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityAnchorNodeConfig>(&Config);
    AActor* Source = nullptr;
    if (!Settings.Validate(Error) || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source))
    {
        Error = TEXT("Anchor requires a current source and valid position settings.");
        return false;
    }
    FVector Origin = Input.Origin;
    if (Settings.Anchor == EAbilitySpatialAnchor::Source)
    {
        Origin = Source->GetActorLocation();
    }
    else if (Settings.Anchor == EAbilitySpatialAnchor::ConfirmedImpact)
    {
        if (!Input.Impact.IsSet() || Input.Impact->Location.ContainsNaN())
        {
            Error = TEXT("ConfirmedImpact anchor requires an actual finite historical impact.");
            return false;
        }
        Origin = Input.Impact->Location;
    }
    Origin += Settings.Offset;
    if (Origin.ContainsNaN())
    {
        Error = TEXT("Anchor offset produced invalid position geometry.");
        return false;
    }
    FAbilityNodeInput Payload = Input;
    Payload.Origin = Payload.EventCenter = Origin;
    // 只改变几何；已有活目标权限可原样传递，绝不从 Impact 重新捕获死亡/复用目标。
    return Owner.Publish(Id, TEXT("Completed"), Payload);
}

bool UAbilitySlowNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                      const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilitySlowNodeConfig>(&Config);
    AActor* Source = nullptr;
    FVector Location;
    if (!Settings.Validate(Error) || !Input.LiveTargetToken.IsSet() ||
        !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source) ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Location) ||
        !UBatch6SlowComponent::Apply(Input.SourceToken.World.Get(), Source, Input.LiveTargetToken->Target,
                                     Settings.Multiplier, Settings.Duration))
    {
        Error = TEXT("Slow requires the original live target and successful state application.");
        return false;
    }
    // 服务持有独立到期与租约清理；节点/图结束不撤销已落地状态或其他来源的减速。
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

void AbilityBehaviorNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Nearest;
    Nearest.TypeId = TEXT("Query.Nearest");
    Nearest.ConfigClass = UAbilityNearestNodeConfig::StaticClass();
    Nearest.ExecutionClass = UAbilityNearestNodeExecution::StaticClass();
    Nearest.InputPayload = EAbilityPortPayload::Origin;
    Nearest.bSupportsActorTargets = Nearest.bSupportsMassTargets = Nearest.bSupportsSnapshotTargets = true;
    Nearest.bRuntimeFiniteSafe = true; // 只查询/发布一个原生命，不生成物理对象。
    Nearest.Ports = {{TEXT("TargetFound"), EAbilityPortPayload::LiveTarget, false},
                     {TEXT("NoTarget"), EAbilityPortPayload::Origin, false},
                     {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Repeat = Nearest;
    Repeat.TypeId = TEXT("Control.Repeat");
    Repeat.ConfigClass = UAbilityRepeatNodeConfig::StaticClass();
    Repeat.ExecutionClass = UAbilityRepeatNodeExecution::StaticClass();
    Repeat.bIsAsync = true;
    Repeat.bRuntimeFiniteSafe = true; // 有限时钟，所有payload仍经过根调用/事件额度。
    Repeat.Ports = {{TEXT("Pulse"), EAbilityPortPayload::Origin, false},
                    {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Anchor = Nearest;
    Anchor.TypeId = TEXT("Spatial.Anchor");
    Anchor.ConfigClass = UAbilityAnchorNodeConfig::StaticClass();
    Anchor.ExecutionClass = UAbilityAnchorNodeExecution::StaticClass();
    Anchor.bRuntimeFiniteSafe = true; // 只转换位置，不产生Actor或新的目标权限。
    Anchor.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Slow;
    Slow.TypeId = TEXT("Status.Slow");
    Slow.ConfigClass = UAbilitySlowNodeConfig::StaticClass();
    Slow.ExecutionClass = UAbilitySlowNodeExecution::StaticClass();
    Slow.InputPayload = EAbilityPortPayload::LiveTarget;
    Slow.bSupportsActorTargets = Slow.bSupportsMassTargets = true;
    // 此节点只同步委托既有 Slow 服务管理目标状态，不启动 Driver、定时图或额外物理资源。
    // Completed 仍由同一图有界发布；仅开放这项已验证动作，不扩大其他 generic 策略的兼容权限。
    Slow.bPrototypeCompatibilitySafe = true;
    Slow.bRuntimeFiniteSafe = true;
    Slow.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    Slow.Ports = {{TEXT("Completed"), EAbilityPortPayload::LiveTarget, true}};
    for (const auto& Descriptor : {Nearest, Repeat, Anchor, Slow})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Behavior node registration: %s"), *Error);
        }
    }
}
