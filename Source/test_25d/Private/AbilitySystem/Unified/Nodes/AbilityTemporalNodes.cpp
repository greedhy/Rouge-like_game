// 调用链：GraphInstance -> 布防/波形/发射策略 -> 同根类型化事件队列 -> 独立 payload。
#include "AbilitySystem/Unified/Nodes/AbilityTemporalNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace AbilityTemporalNodeDetails
{
    /** 位置端口保留历史事实，但不把某个初始活目标权限传给范围触发/正常到期。 */
    FAbilityNodeInput PositionOnly(const FAbilityNodeInput& InInput)
    {
        FAbilityNodeInput Result = InInput;
        Result.LiveTargetToken.Reset();
        Result.EventCenter = Result.Origin;
        return Result;
    }

    /**
     * 本次第一次观察时捕获，之后只验证原 token；Actor 地址/Mass ID 复用不会被重新授权。
     * 遍历完整半径结果后再交给选择/覆盖策略，保持未被命中候选的原生命缓存合同。
     */
    TArray<FAbilityTargetCandidate> GatherOriginal(const FAbilityNodeInput& InInput, float Radius, bool bActors,
                                                   bool bMass, bool bVisibility,
                                                   TMap<FPureMassCombatTarget, FAbilityTargetToken>& OriginalLives)
    {
        TArray<FAbilityTargetCandidate> Result;
        AActor* Source = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(InInput.SourceToken, Source))
        {
            return Result;
        }
        for (const auto& Candidate :
             PureMassCombat::Gather(InInput.SourceToken.World.Get(), Source, InInput.Origin, Radius))
        {
            if ((Candidate.IsActor() && !bActors) || (!Candidate.IsActor() && !bMass))
            {
                continue;
            }
            FAbilityTargetToken* Token = OriginalLives.Find(Candidate);
            if (!Token)
            {
                FAbilityTargetToken Captured;
                FString Error;
                if (!AbilityExecutionIdentity::CaptureTarget(InInput.SourceToken, Candidate, Captured, Error))
                {
                    continue;
                }
                Token = &OriginalLives.Add(Candidate, MoveTemp(Captured));
            }
            FVector Location;
            if (!AbilityExecutionIdentity::ResolveTarget(InInput.SourceToken, *Token, Location) ||
                (bVisibility &&
                 !PureMassCombat::Visible(InInput.SourceToken.World.Get(), Source, InInput.Origin, Candidate)))
            {
                continue;
            }
            Result.Add({*Token, Location});
        }
        return Result;
    }

    /** 动态节点有限维度上限；旧资产合法半径仍可完整重构，不静默夹取。 */
    bool ValidTargets(float Radius, bool bActors, bool bMass)
    {
        return FMath::IsFinite(Radius) && Radius > 0.f && Radius <= 3000.f && (bActors || bMass);
    }
} // namespace AbilityTemporalNodeDetails

bool UAbilityWaitTargetNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("WaitTarget typed configuration requires a finite value");
        return false;
    }
    const TStrongObjectPtr<UAbilityWaitTargetNodeConfig> Candidate(
        DuplicateObject<UAbilityWaitTargetNodeConfig>(this, GetTransientPackage()));
    switch (Parameter)
    {
    case EAbilityConfigParameter::Radius:
        Candidate->Radius = Value;
        break;
    case EAbilityConfigParameter::ArmDelay:
        Candidate->ArmDelay = Value;
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
    // 总截止仍是原调度器的一个时钟；修改配置不重置任何正在等待的执行实例。
    Radius = Candidate->Radius;
    ArmDelay = Candidate->ArmDelay;
    Duration = Candidate->Duration;
    return true;
}

bool UAbilityWavefrontNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Wavefront typed configuration requires a finite value");
        return false;
    }
    const TStrongObjectPtr<UAbilityWavefrontNodeConfig> Candidate(
        DuplicateObject<UAbilityWavefrontNodeConfig>(this, GetTransientPackage()));
    switch (Parameter)
    {
    case EAbilityConfigParameter::Radius:
        Candidate->Radius = Value;
        break;
    case EAbilityConfigParameter::Speed:
        Candidate->Speed = Value;
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
    // 只提交下一次施法的空间值；profile的几何精度、原生命历史与已经落地状态都不改写。
    Radius = Candidate->Radius;
    Speed = Candidate->Speed;
    Duration = Candidate->Duration;
    return true;
}

bool UAbilityEmitterNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Emitter typed configuration requires a finite value");
        return false;
    }
    const TStrongObjectPtr<UAbilityEmitterNodeConfig> Candidate(
        DuplicateObject<UAbilityEmitterNodeConfig>(this, GetTransientPackage()));
    switch (Parameter)
    {
    case EAbilityConfigParameter::Radius:
        Candidate->Radius = Value;
        break;
    case EAbilityConfigParameter::Count:
        if (Value < 1.f || Value > 64.f || Value != FMath::FloorToFloat(Value))
        {
            Error = TEXT("Emitter count must be an integer from 1 to 64");
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
    // 成功仍只由RequestChild准入决定；配置Count不是已发射数，也不预先分配任何物理资源。
    Radius = Candidate->Radius;
    Count = Candidate->Count;
    Period = Candidate->Period;
    Duration = Candidate->Duration;
    return true;
}

bool UAbilityWaitTargetNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!AbilityTemporalNodeDetails::ValidTargets(Radius, bIncludeActors, bIncludeMass) || !FMath::IsFinite(ArmDelay) ||
        ArmDelay < 0.f || ArmDelay > 60.f || !FMath::IsFinite(Duration) || Duration < .01f || Duration > 60.f)
    {
        Error = TEXT("Wait requires finite radius (0,3000], arm delay 0..60, duration .01..60 and target types.");
    }
    return Error.IsEmpty();
}

bool UAbilityWavefrontNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!AbilityTemporalNodeDetails::ValidTargets(Radius, bIncludeActors, bIncludeMass) || !FMath::IsFinite(Speed) ||
        Speed <= 0.f || Speed > 10000.f || !FMath::IsFinite(Duration) || Duration < .01f || Duration > 60.f ||
        Profile < EAbilityWaveQueryProfile::ActorFront3D || Profile > EAbilityWaveQueryProfile::Batch6Growing3D)
    {
        Error = TEXT("Wave requires finite positive geometry, duration .01..60, target types and an explicit profile.");
    }
    return Error.IsEmpty();
}

bool UAbilityEmitterNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!AbilityTemporalNodeDetails::ValidTargets(Radius, bIncludeActors, bIncludeMass) || Count < 1 || Count > 64 ||
        !FMath::IsFinite(Period) || Period < .01f || Period > 60.f || !FMath::IsFinite(Duration) || Duration < .01f ||
        Duration > 60.f || Profile < EAbilityEmitterProfile::ActorCatchUp ||
        Profile > EAbilityEmitterProfile::MixedEight)
    {
        Error = TEXT("Emitter requires finite Count 1..64, period/duration .01..60, target types and a profile.");
    }
    return Error.IsEmpty();
}

bool UAbilityWaitTargetNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                            const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                            FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = CastChecked<UAbilityWaitTargetNodeConfig>(const_cast<UAbilityNodeConfig*>(&Config));
    if (!Settings->Validate(Error))
    {
        return false;
    }
    Age = 0.f;
    OriginalLives.Reset();
    if (Settings->bQueryOnStart && Settings->ArmDelay <= 0.f)
    {
        TryTrigger();
    }
    return true;
}

void UAbilityWaitTargetNodeExecution::Advance(float DeltaSeconds)
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
    const float Duration = Settings->Duration;
    const bool bIncludeBoundary = Settings->bIncludeFinalBoundary;
    const float ArmDelay = Settings->ArmDelay;
    Age = FMath::Min(Duration, Age + DeltaSeconds);
    // 默认 Trap 与旧实现相同：截止先于触发，即使一帧同时跨过布防和总寿命。
    if (!bIncludeBoundary && Age >= Duration)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), AbilityTemporalNodeDetails::PositionOnly(Input));
        return;
    }
    if (Age >= ArmDelay && TryTrigger())
    {
        return;
    }
    if (!bStopped && !Owner->IsFinished() && Age >= Duration)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), AbilityTemporalNodeDetails::PositionOnly(Input));
    }
}

void UAbilityWaitTargetNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    Settings = nullptr;
    Age = 0.f;
    OriginalLives.Reset();
}

bool UAbilityWaitTargetNodeExecution::TryTrigger()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Settings)
    {
        return false;
    }
    const auto Candidates =
        AbilityTemporalNodeDetails::GatherOriginal(Input, Settings->Radius, Settings->bIncludeActors,
                                                   Settings->bIncludeMass, Settings->bRequireVisibility, OriginalLives);
    if (Candidates.IsEmpty())
    {
        return false;
    }
    const auto Payload = AbilityTemporalNodeDetails::PositionOnly(Input);
    if (Owner->Publish(InvocationId, TEXT("Triggered"), Payload) && !bStopped && !Owner->IsFinished())
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
    return true;
}

bool UAbilityWavefrontNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                           const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                           FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = CastChecked<UAbilityWavefrontNodeConfig>(const_cast<UAbilityNodeConfig*>(&Config));
    Age = Radius = 0.f;
    OriginalLives.Reset();
    Visited.Reset();
    return Settings->Validate(Error);
}

void UAbilityWavefrontNodeExecution::Advance(float DeltaSeconds)
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
    const float Duration = Settings->Duration;
    const float Maximum = Settings->Radius;
    const auto Profile = Settings->Profile;
    const bool bIncludeFinal = Profile != EAbilityWaveQueryProfile::MixedGrowing3D;
    float ActiveDelta = DeltaSeconds;
    if (Profile == EAbilityWaveQueryProfile::ActorFront3D)
    {
        // 原 Actor 先累加 Age，再减回本帧 Delta；float 加减顺序会决定最后片段的真实接触。
        Age += DeltaSeconds;
        ActiveDelta = FMath::Min(DeltaSeconds, FMath::Max(0.f, Duration - (Age - DeltaSeconds)));
    }
    else
    {
        ActiveDelta = bIncludeFinal ? FMath::Min(DeltaSeconds, FMath::Max(0.f, Duration - Age)) : DeltaSeconds;
        Age = FMath::Min(Duration, Age + DeltaSeconds);
    }
    if (!bIncludeFinal && Age >= Duration)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), AbilityTemporalNodeDetails::PositionOnly(Input));
        return;
    }
    const float Previous = Radius;
    Radius = FMath::Min(Maximum, Radius + Settings->Speed * ActiveDelta);
    const bool bFront =
        Profile == EAbilityWaveQueryProfile::ActorFront3D || Profile == EAbilityWaveQueryProfile::Batch6Front3D;
    const auto Candidates = AbilityTemporalNodeDetails::GatherOriginal(
        Input, Maximum, Settings->bIncludeActors, Settings->bIncludeMass, Settings->bRequireVisibility, OriginalLives);
    for (const auto& Candidate : Candidates)
    {
        if (bStopped || Owner->IsFinished())
        {
            break;
        }
        if (Visited.ContainsByPredicate(
                [&](const FAbilityTargetToken& Token)
                {
                    return AbilityBehaviorPrimitives::SameTargetLife(Token, Candidate.Target);
                }))
        {
            continue;
        }
        // Mixed Batch6 先平方外界再 float 前沿；Actor 只有 float 前沿，不合并其精度。
        if ((Profile != EAbilityWaveQueryProfile::ActorFront3D &&
             !AbilityBehaviorPrimitives::ContainsWavePoint(Input.Origin, Candidate.Location, Previous, Radius,
                                                           EAbilityWaveCoverage::GrowingSphere3D)) ||
            (bFront && !AbilityBehaviorPrimitives::ContainsWavePoint(Input.Origin, Candidate.Location, Previous, Radius,
                                                                     EAbilityWaveCoverage::SweptShell3D)))
        {
            continue;
        }
        FAbilityNodeInput Payload = Input;
        Payload.LiveTargetToken = Candidate.Target;
        if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Candidate.Target, Payload.EventCenter))
        {
            continue;
        }
        if (Owner->Publish(InvocationId, TEXT("TargetFound"), Payload))
        {
            Visited.Add(Candidate.Target);
        }
    }
    if (!bStopped && !Owner->IsFinished() && (Radius >= Maximum || Age >= Duration))
    {
        Owner->Publish(InvocationId, TEXT("Completed"), AbilityTemporalNodeDetails::PositionOnly(Input));
    }
}

void UAbilityWavefrontNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    Settings = nullptr;
    Age = Radius = 0.f;
    OriginalLives.Reset();
    Visited.Reset();
}

bool UAbilityEmitterNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                         const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                         FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = CastChecked<UAbilityEmitterNodeConfig>(const_cast<UAbilityNodeConfig*>(&Config));
    Age = Clock = 0.f;
    Successful = 0;
    Direction = Input.Direction;
    OriginalLives.Reset();
    return Settings->Validate(Error);
}

void UAbilityEmitterNodeExecution::Advance(float DeltaSeconds)
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
    // 发布终态会同步 Stop 并清空 Settings；本帧需要的策略先复制到栈上。
    const float Duration = Settings->Duration;
    const float Period = Settings->Period;
    const bool bActor = Settings->Profile == EAbilityEmitterProfile::ActorCatchUp;
    const float ActiveDelta = bActor ? FMath::Min(DeltaSeconds, FMath::Max(0.f, Duration - Age)) : DeltaSeconds;
    Age = FMath::Min(Duration, Age + DeltaSeconds);
    if (!bActor && Age >= Duration)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), AbilityTemporalNodeDetails::PositionOnly(Input));
        return;
    }
    AbilityBehaviorPrimitives::AdvanceCadence(
        Clock, ActiveDelta, Period, bActor ? MAX_int32 : 8, EAbilityCadenceRemainder::Preserve,
        [&]()
        {
            return EmitScheduled();
        },
        bActor ? SMALL_NUMBER : 0.f, bActor);
    if (!bStopped && !Owner->IsFinished() && Age >= Duration)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), AbilityTemporalNodeDetails::PositionOnly(Input));
    }
}

void UAbilityEmitterNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    Settings = nullptr;
    Age = Clock = 0.f;
    Successful = 0;
    OriginalLives.Reset();
}

bool UAbilityEmitterNodeExecution::EmitScheduled()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Settings)
    {
        return false;
    }
    const bool bActor = Settings->Profile == EAbilityEmitterProfile::ActorCatchUp;
    const int32 Limit = Settings->Count;
    const float Period = Settings->Period;
    const auto Candidates =
        AbilityTemporalNodeDetails::GatherOriginal(Input, Settings->Radius, Settings->bIncludeActors,
                                                   Settings->bIncludeMass, Settings->bRequireVisibility, OriginalLives);
    const auto Selected = AbilityBehaviorPrimitives::SelectNearest(Candidates, Input.Origin, Settings->Radius, {},
                                                                   EAbilityCandidateTieBreak::Last);
    if (!Selected.IsSet())
    {
        Clock = bActor ? FMath::Fmod(Clock, Period) : 0.f;
        return false;
    }
    Direction =
        (Selected->Location - Input.Origin).GetSafeNormal(SMALL_NUMBER, bActor ? FVector::ForwardVector : Direction);
    FAbilityNodeInput Payload = Input;
    Payload.LiveTargetToken = Selected->Target;
    Payload.EventCenter = Selected->Location;
    Payload.Direction = Direction;
    if (Owner->RequestChild(InvocationId, TEXT("Launch"), Payload))
    {
        ++Successful;
        if (Successful >= Limit && !bStopped && !Owner->IsFinished())
        {
            Owner->Publish(InvocationId, TEXT("Completed"), AbilityTemporalNodeDetails::PositionOnly(Input));
            return false;
        }
        return !bStopped && !Owner->IsFinished();
    }
    if (bActor)
    {
        // Actor 对空闲和拒绝都只保留取模余量；Mixed 的拒绝继续消费最多八个周期。
        Clock = FMath::Fmod(Clock, Period);
        return false;
    }
    return !bStopped && !Owner->IsFinished();
}

void AbilityTemporalNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Wait;
    Wait.TypeId = TEXT("Query.WaitForTarget");
    Wait.ConfigClass = UAbilityWaitTargetNodeConfig::StaticClass();
    Wait.ExecutionClass = UAbilityWaitTargetNodeExecution::StaticClass();
    Wait.InputPayload = EAbilityPortPayload::Origin;
    Wait.bSupportsActorTargets = Wait.bSupportsMassTargets = Wait.bSupportsSnapshotTargets = true;
    Wait.bIsAsync = true;
    Wait.bRuntimeFiniteSafe = true; // 有限存在检测，无物理生成，目标权限仍按原生命解析。
    Wait.Ports = {{TEXT("Triggered"), EAbilityPortPayload::Origin, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Wave = Wait;
    Wave.TypeId = TEXT("Spatial.Wavefront");
    Wave.ConfigClass = UAbilityWavefrontNodeConfig::StaticClass();
    Wave.ExecutionClass = UAbilityWavefrontNodeExecution::StaticClass();
    Wave.bRuntimeFiniteSafe = true; // 只推进空间/发布原生命，独立后继受同根事件/调用预算。
    Wave.Ports = {{TEXT("TargetFound"), EAbilityPortPayload::LiveTarget, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Emitter = Wait;
    Emitter.TypeId = TEXT("Control.Emitter");
    Emitter.ConfigClass = UAbilityEmitterNodeConfig::StaticClass();
    Emitter.ExecutionClass = UAbilityEmitterNodeExecution::StaticClass();
    Emitter.bRuntimeFiniteSafe = true; // 成功子调用只经RequestChild准入，自身没有raw SpawnActor。
    Emitter.Ports = {{TEXT("Launch"), EAbilityPortPayload::LiveTarget, false},
                     {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Wait, Wave, Emitter})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Temporal node registration: %s"), *Error);
        }
    }
}
