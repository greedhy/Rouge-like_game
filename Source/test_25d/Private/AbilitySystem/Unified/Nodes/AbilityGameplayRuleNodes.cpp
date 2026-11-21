// 调用链：GraphInstance → 同步Query/条件策略 → 原生命查询或真实DamageResult → 后继事件。
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "PureMass/PureMassCombatTarget.h"

bool UAbilitySelectTargetsConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Radius) || Radius <= 0.f || Radius > 3000.f || MaxTargets < 1 || MaxTargets > 16 ||
        static_cast<uint8>(TargetSelection) > static_cast<uint8>(EAbilityTargetSelectionPolicy::LowestHealthRatio))
    {
        Error = TEXT("SelectTargets requires radius (0,3000], targets1..16 and a registered selection policy.");
    }
    return Error.IsEmpty();
}

bool UAbilitySelectTargetsConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (!FMath::IsFinite(Value) || Value < 0.f || Value > 3000.f)
    {
        Error = TEXT("Query parameters must be finite and bounded.");
        return false;
    }
    if ((Parameter == EAbilityConfigParameter::Count || Parameter == EAbilityConfigParameter::TargetSelection) &&
        !FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value)))
    {
        Error = TEXT("Query counts and policy are integers.");
        return false;
    }
    if (Parameter == EAbilityConfigParameter::Radius)
    {
        Radius = Value;
    }
    else if (Parameter == EAbilityConfigParameter::Count)
    {
        MaxTargets = FMath::RoundToInt(Value);
    }
    else if (Parameter == EAbilityConfigParameter::TargetSelection)
    {
        if (Value > 3.f)
        {
            Error = TEXT("Unknown target selection policy.");
            return false;
        }
        TargetSelection = static_cast<EAbilityTargetSelectionPolicy>(FMath::RoundToInt(Value));
    }
    else
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    return Validate(Error);
}

bool UAbilitySelectTargetsConfig::SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Incoming,
                                                             FString& Error) const
{
    Error.Reset();
    return Type == TEXT("Query.SelectTargets") && Incoming == EAbilityPortPayload::Origin && Validate(Error);
}

bool UAbilitySelectTargetsExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                           const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                           FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilitySelectTargetsConfig>(&Config);
    if (!Settings.Validate(Error))
    {
        return false;
    }
    TArray<FAbilitySelectedCandidate> Targets;
    const auto Policy = Settings.TargetSelection == EAbilityTargetSelectionPolicy::Original
                            ? EAbilityTargetSelectionPolicy::Nearest
                            : Settings.TargetSelection;
    if (!AbilityTargetSelectionService::Select(
            Input.SourceToken, Input.Origin, Settings.Radius, Settings.MaxTargets, Policy,
            [](const auto&)
            {
                return false;
            },
            Targets, Error))
    {
        return false;
    }
    int32 Published = 0;
    for (const auto& Target : Targets)
    {
        AActor* Caster = nullptr;
        FVector Position;
        if (bStopped || Owner.IsFinished())
        {
            return true;
        }
        if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster) ||
            !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target.Target, Position) ||
            FVector::DistSquared(Input.Origin, Position) > FMath::Square(Settings.Radius) ||
            !PureMassCombat::Visible(Input.SourceToken.World.Get(), Caster, Input.Origin, Target.Target.Target))
        {
            continue;
        }
        auto Payload = Input;
        Payload.LiveTargetToken = Target.Target;
        Payload.Origin = Position;
        if (!Owner.Publish(Id, TEXT("TargetFound"), Payload))
        {
            return true;
        }
        ++Published;
        Owner.FlushPublishedEvents(true);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        if (Published == 0)
        {
            Owner.Publish(Id, TEXT("NoTarget"), Input);
        }
        Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}

bool UAbilityKilledGateConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::ConditionOutcome)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    Error.Reset();
    if (!FMath::IsFinite(Value) || (Value != 0.f && Value != 1.f))
    {
        Error = TEXT("Damage outcome is 0=not killed or 1=killed.");
        return false;
    }
    bExpectedKilled = Value > 0.f;
    return true;
}

bool UAbilityKilledGateConfig::SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Incoming,
                                                          FString& Error) const
{
    Error.Reset();
    return Type == TEXT("Condition.Killed") && Incoming == EAbilityPortPayload::DamageResult;
}

bool UAbilityChanceGateConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Chance) || Chance < 0.f || Chance > 1.f)
    {
        Error = TEXT("Chance must be in [0,1].");
    }
    return Error.IsEmpty();
}

bool UAbilityChanceGateConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::Chance)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    Chance = Value;
    return Validate(Error);
}

bool UAbilityChanceGateConfig::SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Incoming,
                                                          FString& Error) const
{
    Error.Reset();
    return Type == TEXT("Condition.Chance") && Incoming == EAbilityPortPayload::Origin && Validate(Error);
}

bool UAbilityGameplayGateExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                          const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                          FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Config.Validate(Error))
    {
        return false;
    }
    bool bPassed = false;
    if (const auto* Killed = Cast<UAbilityKilledGateConfig>(&Config))
    {
        if (!Input.bHasDamageResult || !Input.bDamageAttempted)
        {
            Error = TEXT("Killed condition requires an actual attempted damage result.");
            return false;
        }
        bPassed = Input.bKilled == Killed->bExpectedKilled;
    }
    else if (const auto* Chance = Cast<UAbilityChanceGateConfig>(&Config))
    {
        // 同一次服务器调用确定性，新的Invocation/施法重新取样；不在客户端独立掷骰。
        FRandomStream Random(static_cast<int32>(HashCombine(GetTypeHash(Owner.GetExecutionId()), GetTypeHash(Id))));
        bPassed = Chance->Chance >= 1.f || (Chance->Chance > 0.f && Random.GetFraction() < Chance->Chance);
    }
    else
    {
        Error = TEXT("Unsupported gate configuration.");
        return false;
    }
    auto Payload = Input;
    if (Input.Impact.IsSet())
    {
        Payload.Origin = Payload.EventCenter = Input.Impact->Location;
    }
    Payload.LiveTargetToken.Reset();
    Payload.TargetSet.Reset();
    Payload.Impact.Reset();
    Payload.bHasDamageResult = false;
    Payload.bConfirmedHit = false;
    Owner.Publish(Id, bPassed ? TEXT("Passed") : TEXT("Rejected"), Payload);
    return true;
}

void AbilityGameplayRuleNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (const FName Type :
         {FName(TEXT("Query.SelectTargets")), FName(TEXT("Condition.Killed")), FName(TEXT("Condition.Chance"))})
    {
        if (Registry.Find(Type))
        {
            continue;
        }
        FAbilityNodeDescriptor D;
        D.TypeId = Type;
        D.bSupportsActorTargets = D.bSupportsMassTargets = D.bSupportsSnapshotTargets = true;
        D.bRuntimeFiniteSafe = D.bRuntimePurePayload = true;
        D.InputPayload =
            Type == TEXT("Condition.Killed") ? EAbilityPortPayload::DamageResult : EAbilityPortPayload::Origin;
        if (Type == TEXT("Query.SelectTargets"))
        {
            D.ConfigClass = UAbilitySelectTargetsConfig::StaticClass();
            D.ExecutionClass = UAbilitySelectTargetsExecution::StaticClass();
            D.Ports = {{TEXT("TargetFound"), EAbilityPortPayload::LiveTarget, false},
                       {TEXT("NoTarget"), EAbilityPortPayload::Origin, false},
                       {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        }
        else
        {
            D.ConfigClass = Type == TEXT("Condition.Killed") ? UAbilityKilledGateConfig::StaticClass()
                                                             : UAbilityChanceGateConfig::StaticClass();
            D.ExecutionClass = UAbilityGameplayGateExecution::StaticClass();
            D.Ports = {{TEXT("Passed"), EAbilityPortPayload::Origin, true},
                       {TEXT("Rejected"), EAbilityPortPayload::Origin, true}};
        }
        FString Error;
        ensureAlwaysMsgf(Registry.Register(D, Error), TEXT("Gameplay rule registration: %s"), *Error);
    }
}
