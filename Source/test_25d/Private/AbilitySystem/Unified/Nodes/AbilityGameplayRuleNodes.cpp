// 调用链：GraphInstance → 同步Query/条件策略 → 原生命查询或真实DamageResult → 后继事件。
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "PureMass/PureMassCombatTarget.h"

namespace
{
    constexpr uint8 MaxTargetFilter = 3 + static_cast<uint8>(EAbilityExposure::Frozen);
    /** 每次筛选使用当前生命，查询收集和发布前都执行。 */
    bool MatchesFilter(uint8 Filter, const FAbilitySourceToken& Source, const FAbilityTargetToken& Target)
    {
        switch (Filter)
        {
        case 0:
            return true;
        case 1:
            return Target.Target.IsActor();
        case 2:
            return !Target.Target.IsActor();
        case 3:
            return AbilityTargetSelectionService::ReadHealthRatio(Target).Get(1.f) < .35f;
        default:
            return AbilityElements::Read(Source, Target) == static_cast<EAbilityExposure>(Filter - 3);
        }
    }
} // namespace

bool UAbilitySelectTargetsConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Radius) || Radius <= 0.f || Radius > 3000.f || MaxTargets < 1 || MaxTargets > 16 ||
        static_cast<uint8>(TargetSelection) > static_cast<uint8>(EAbilityTargetSelectionPolicy::LowestHealthRatio) ||
        TargetFilter > MaxTargetFilter)
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
    if ((Parameter == EAbilityConfigParameter::Count || Parameter == EAbilityConfigParameter::TargetSelection ||
         Parameter == EAbilityConfigParameter::TargetFilter) &&
        Value != FMath::RoundToFloat(Value))
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
    else if (Parameter == EAbilityConfigParameter::TargetFilter && Value <= MaxTargetFilter)
    {
        TargetFilter = static_cast<uint8>(Value);
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
            [&](const auto& Target)
            {
                const bool bIsExcludedAnchor = Settings.bExcludeAnchorTarget && Input.ExcludedAnchorTarget.IsSet() &&
                                               Target.Target == Input.ExcludedAnchorTarget.GetValue();
                return bIsExcludedAnchor || !MatchesFilter(Settings.TargetFilter, Input.SourceToken, Target);
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
            !MatchesFilter(Settings.TargetFilter, Input.SourceToken, Target.Target) ||
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
        FAbilityNodeInput Completed = Input;
        Completed.ShapeRadius = Settings.Radius;
        Owner.Publish(Id, TEXT("Completed"), Completed);
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

namespace
{
    struct FCombatRuleEntry
    {
        FName Type;
        EAbilityCombatRule Rule;
        EAbilityPortPayload Payload;
        EAbilityConfigParameter Parameter;
        bool bPure;
    };

    const FCombatRuleEntry CombatRules[] = {
        {TEXT("Condition.HealthBelow"), EAbilityCombatRule::HealthBelow, EAbilityPortPayload::LiveTarget,
         EAbilityConfigParameter::HealthThreshold, true},
        {TEXT("Condition.ElementalStatus"), EAbilityCombatRule::ElementalStatus, EAbilityPortPayload::LiveTarget,
         EAbilityConfigParameter::ElementalStatus, true},
        {TEXT("Condition.DamageApplied"), EAbilityCombatRule::DamageApplied, EAbilityPortPayload::DamageResult,
         EAbilityConfigParameter::ResourceAmount, true},
        {TEXT("Control.EveryNthDamage"), EAbilityCombatRule::EveryNthDamage, EAbilityPortPayload::DamageResult,
         EAbilityConfigParameter::Count, false},
        {TEXT("Control.DamageTriggerLimit"), EAbilityCombatRule::DamageTriggerLimit, EAbilityPortPayload::DamageResult,
         EAbilityConfigParameter::Count, false},
        {TEXT("Effect.RestoreMana"), EAbilityCombatRule::RestoreMana, EAbilityPortPayload::DamageResult,
         EAbilityConfigParameter::ResourceAmount, false},
        {TEXT("Effect.RestoreHealth"), EAbilityCombatRule::RestoreHealth, EAbilityPortPayload::DamageResult,
         EAbilityConfigParameter::ResourceAmount, false}};

    const FCombatRuleEntry* FindRule(EAbilityCombatRule Rule)
    {
        for (const auto& Entry : CombatRules)
        {
            if (Entry.Rule == Rule)
            {
                return &Entry;
            }
        }
        return nullptr;
    }

} // namespace

bool AbilityGameplayRuleNodes::RestoreResource(const FAbilitySourceToken& Source, bool bMana, float Amount)
{
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return false;
    }
    auto* ASC = Source.ASC.Get();
    const auto Current = bMana ? UMyAttributeSet::GetManaAttribute() : UMyAttributeSet::GetHealthAttribute();
    const auto Maximum = bMana ? UMyAttributeSet::GetMaxManaAttribute() : UMyAttributeSet::GetMaxHealthAttribute();
    if (!ASC->HasAttributeSetForAttribute(Current) || !ASC->HasAttributeSetForAttribute(Maximum))
    {
        return false;
    }
    const float Increase = FMath::Min(Amount, ASC->GetNumericAttribute(Maximum) - ASC->GetNumericAttribute(Current));
    if (!FMath::IsFinite(Increase) || Increase <= 0.f)
    {
        return false;
    }
    ASC->ApplyModToAttribute(Current, EGameplayModOp::Additive, Increase);
    return true;
}

TOptional<EAbilityCombatRule> AbilityGameplayRuleNodes::FindCombatRule(FName Type)
{
    for (const auto& Entry : CombatRules)
    {
        if (Entry.Type == Type)
        {
            return Entry.Rule;
        }
    }
    return {};
}

EAbilityPortPayload AbilityGameplayRuleNodes::GetCombatRuleInput(EAbilityCombatRule Rule)
{
    const auto* Entry = FindRule(Rule);
    return Entry ? Entry->Payload : EAbilityPortPayload::Origin;
}

EAbilityConfigParameter AbilityGameplayRuleNodes::GetCombatRuleParameter(EAbilityCombatRule Rule)
{
    const auto* Entry = FindRule(Rule);
    return Entry ? Entry->Parameter : EAbilityConfigParameter::Count;
}

bool UAbilityCombatRuleConfig::Validate(FString& Error) const
{
    Error.Reset();
    const bool bInteger = Rule == EAbilityCombatRule::ElementalStatus || Rule == EAbilityCombatRule::EveryNthDamage ||
                          Rule == EAbilityCombatRule::DamageTriggerLimit;
    const float Min = Rule == EAbilityCombatRule::HealthBelow || Rule == EAbilityCombatRule::DamageApplied ? 0.f : 1.f;
    const float Max = Rule == EAbilityCombatRule::HealthBelow       ? 1.f
                      : Rule == EAbilityCombatRule::ElementalStatus ? static_cast<float>(EAbilityExposure::Frozen)
                      : Rule == EAbilityCombatRule::DamageApplied   ? 0.f
                                                                    : 20.f;
    if (!FindRule(Rule) || !FMath::IsFinite(Value) || Value < Min || Value > Max ||
        (bInteger && Value != FMath::RoundToFloat(Value)) || !FMath::IsFinite(Cooldown) || Cooldown < 0.f ||
        Cooldown > 5.f || (Rule != EAbilityCombatRule::DamageTriggerLimit && Cooldown != 0.f))
    {
        Error = TEXT("Combat rule parameters do not satisfy the registered finite bounds.");
    }
    return Error.IsEmpty();
}

bool UAbilityCombatRuleConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float NewValue, FString& Error)
{
    if (Parameter == EAbilityConfigParameter::TriggerCooldown && Rule == EAbilityCombatRule::DamageTriggerLimit)
    {
        const float Previous = Cooldown;
        Cooldown = NewValue;
        if (Validate(Error))
        {
            return true;
        }
        Cooldown = Previous;
        return false;
    }
    if (Rule != EAbilityCombatRule::DamageApplied &&
        Parameter == AbilityGameplayRuleNodes::GetCombatRuleParameter(Rule))
    {
        const float Previous = Value;
        Value = NewValue;
        if (Validate(Error))
        {
            return true;
        }
        Value = Previous;
        return false;
    }
    return Super::TrySetRecipeParameter(Parameter, NewValue, Error);
}

bool UAbilityCombatRuleConfig::SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Incoming,
                                                          FString& Error) const
{
    const auto* Entry = FindRule(Rule);
    return Entry && Entry->bPure && Type == Entry->Type && Incoming == Entry->Payload && Validate(Error);
}

bool UAbilityCombatRuleConfig::MatchesType(FName Type) const
{
    const auto* Entry = FindRule(Rule);
    return Entry && Entry->Type == Type;
}

bool UAbilityCombatRuleExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                        const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Config.Validate(Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityCombatRuleConfig>(&Config);
    using R = EAbilityCombatRule;
    bool bPassed = false;
    if (AbilityGameplayRuleNodes::GetCombatRuleInput(Settings.Rule) == EAbilityPortPayload::LiveTarget)
    {
        if (!Input.LiveTargetToken.IsSet())
        {
            Error = TEXT("Target condition requires a verified live target.");
            return false;
        }
        const auto& Target = Input.LiveTargetToken.GetValue();
        bPassed = Settings.Rule == R::HealthBelow
                      ? AbilityTargetSelectionService::ReadHealthRatio(Target).Get(1.f) < Settings.Value
                      : AbilityElements::Read(Input.SourceToken, Target) ==
                            static_cast<EAbilityExposure>(static_cast<uint8>(Settings.Value));
    }
    else if (Input.bHasDamageResult && Input.bDamageAttempted && Input.ActualDamage.Get(0.f) > 0.f)
    {
        switch (Settings.Rule)
        {
        case R::DamageApplied:
            bPassed = true;
            break;
        case R::EveryNthDamage:
            bPassed = Owner.AdmitDamageTrigger(Id, static_cast<int32>(Settings.Value), MAX_int32, 0.f);
            break;
        case R::DamageTriggerLimit:
            bPassed = Owner.AdmitDamageTrigger(Id, 1, static_cast<int32>(Settings.Value), Settings.Cooldown);
            break;
        case R::RestoreMana:
        case R::RestoreHealth:
            if (Owner.AdmitDamageTrigger(Id, 1, 1, 0.f))
            {
                bPassed = AbilityGameplayRuleNodes::RestoreResource(Input.SourceToken, Settings.Rule == R::RestoreMana,
                                                                   Settings.Value);
            }
            break;
        default:
            break;
        }
    }
    if (!bStopped && !Owner.IsFinished())
    {
        Owner.Publish(Id, bPassed ? TEXT("Passed") : TEXT("Rejected"), Input);
    }
    return true;
}

void AbilityGameplayRuleNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (const auto& Rule : CombatRules)
    {
        if (Registry.Find(Rule.Type))
        {
            continue;
        }
        FAbilityNodeDescriptor D;
        D.TypeId = Rule.Type;
        D.ConfigClass = UAbilityCombatRuleConfig::StaticClass();
        D.ExecutionClass = UAbilityCombatRuleExecution::StaticClass();
        D.InputPayload = Rule.Payload;
        D.bSupportsActorTargets = D.bSupportsMassTargets = D.bSupportsSnapshotTargets = true;
        D.bRuntimeFiniteSafe = true;
        D.bRuntimePurePayload = Rule.bPure;
        D.Ports = {{TEXT("Passed"), Rule.Payload, true}, {TEXT("Rejected"), Rule.Payload, true}};
        FString Error;
        ensureAlwaysMsgf(Registry.Register(D, Error), TEXT("Combat rule registration: %s"), *Error);
    }
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
