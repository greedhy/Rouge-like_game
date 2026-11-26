// 调用链：统一图执行 -> 重验源和目标生命 -> 既有元素状态服务；表现走 ASC 多播，不参与伤害。
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"

#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "NiagaraSystem.h"

bool UAbilityApplyExposureConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!AbilityElements::CanApply(Element, DurationScale))
    {
        Error = TEXT("Exposure requires an actual element and a duration scale within [0.5, 2].");
    }
    return Error.IsEmpty();
}

bool UAbilityApplyExposureConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::ExposureElement)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!FMath::IsFinite(Value) || Value < 1.f || Value > static_cast<float>(EAbilityElement::Ice) ||
        Value != FMath::RoundToFloat(Value))
    {
        Error = TEXT("Exposure element must be an explicitly supported integer value (1 to 5).");
        return false;
    }
    Element = static_cast<EAbilityElement>(static_cast<uint8>(Value));
    return true;
}

bool UAbilityApplyExposureConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    const float NewScale = DurationScale * Multiplier;
    if (Parameter != EAbilityConfigScalar::Duration || !AbilityElements::CanApply(Element, NewScale))
    {
        return false;
    }
    DurationScale = NewScale;
    return true;
}

bool UAbilityApplyExposureConfig::SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Port, FString& Error) const
{
    return Type == TEXT("Status.ApplyExposure") && Port == EAbilityPortPayload::LiveTarget && Validate(Error);
}

bool UAbilityApplyExposureExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                           const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                           FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Input.LiveTargetToken.IsSet())
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityApplyExposureConfig>(&Config);
    if (AbilityElements::Apply(Input.SourceToken, *Input.LiveTargetToken, Settings->Element, Settings->DurationScale) &&
        !bStopped && !Owner.IsFinished())
    {
        Owner.Publish(Id, TEXT("Applied"), Input);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}

bool UAbilityElementPulseVisualConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (System.IsNull())
    {
        Error = TEXT("An elemental pulse visual needs an explicit Niagara asset reference.");
    }
    else if (!FMath::IsFinite(FallbackRadius) || FallbackRadius < 0.f || FallbackRadius > 3000.f ||
             !FMath::IsFinite(ForwardOffset) || ForwardOffset < 0.f || ForwardOffset > 2000.f)
    {
        Error = TEXT("An elemental pulse visual needs a bounded fallback radius and forward offset.");
    }
    return Error.IsEmpty();
}

bool UAbilityElementPulseVisualExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityElementPulseVisualConfig>(&Config);
    AActor* Avatar = nullptr;
    const float Radius = Input.ShapeRadius.Get(Settings->FallbackRadius);
    if (Radius > 0.f && AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        if (auto* ASC = Cast<UMyAbilitySystemComponent>(Input.SourceToken.ASC.Get()))
        {
            const FVector Position = Input.Origin + Input.Direction.GetSafeNormal2D() * Settings->ForwardOffset;
            ASC->MulticastPlayElementPulse(Settings->System.ToSoftObjectPath(), Position, Radius);
        }
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

void AbilityElementalNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Exposure;
    Exposure.TypeId = TEXT("Status.ApplyExposure");
    Exposure.ConfigClass = UAbilityApplyExposureConfig::StaticClass();
    Exposure.ExecutionClass = UAbilityApplyExposureExecution::StaticClass();
    Exposure.InputPayload = EAbilityPortPayload::LiveTarget;
    Exposure.bSupportsActorTargets = Exposure.bSupportsMassTargets = true;
    Exposure.bRuntimeFiniteSafe = Exposure.bRuntimePurePayload = true;
    Exposure.bPrototypeCompatibilitySafe = Exposure.bSustainedSafe = true;
    Exposure.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    Exposure.Ports = {{TEXT("Applied"), EAbilityPortPayload::LiveTarget, false},
                      {TEXT("Completed"), EAbilityPortPayload::LiveTarget, true}};

    FAbilityNodeDescriptor Visual;
    Visual.TypeId = TEXT("Presentation.ElementPulse");
    Visual.ConfigClass = UAbilityElementPulseVisualConfig::StaticClass();
    Visual.ExecutionClass = UAbilityElementPulseVisualExecution::StaticClass();
    Visual.InputPayload = EAbilityPortPayload::Origin;
    Visual.bSupportsActorTargets = Visual.bSupportsMassTargets = true;
    Visual.bRuntimeFiniteSafe = true;
    Visual.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Exposure, Visual})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Elemental node registration: %s"), *Error);
        }
    }
}
