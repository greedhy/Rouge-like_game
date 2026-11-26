// 调用链：图的活目标输入 -> 原生命复核 -> 共享PrepareContactDamage/Apply -> 唯一DamageResolved。
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"

bool UAbilityInputDamageNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DamageMultiplier) || DamageMultiplier < 0.f || ElementOverride > EAbilityElement::Ice)
    {
        Error = TEXT("Input damage requires a valid element and a finite nonnegative multiplier.");
        return false;
    }
    return true;
}

bool UAbilityInputDamageNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                          FString& Error)
{
    if (Parameter != EAbilityConfigParameter::ElementConversion)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    Error.Reset();
    if (!FMath::IsFinite(Value) || Value < 0.f || Value > static_cast<float>(EAbilityElement::Ice) ||
        Value != FMath::RoundToFloat(Value))
    {
        Error = TEXT("Element conversion requires 0 original, 1 fire, 2 water, 3 thunder, 4 arcane or 5 ice.");
        return false;
    }
    ElementOverride = static_cast<EAbilityElement>(static_cast<uint8>(Value));
    return true;
}

bool UAbilityInputDamageNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Damage || !FMath::IsFinite(Multiplier) || Multiplier < 0.f ||
        !FMath::IsFinite(DamageMultiplier * Multiplier))
    {
        return false;
    }
    DamageMultiplier *= Multiplier;
    return true;
}

bool UAbilityInputDamageNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Input.LiveTargetToken.IsSet())
    {
        Error = TEXT("Input damage requires a live original target token.");
        return false;
    }
    // 每次实际伤害前重新验证源和原目标生命；历史快照不授予池中下一生命的权限。
    AActor* Source = nullptr;
    FVector Location;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source) ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Location))
    {
        Error = TEXT("Input damage source or original target life has retired.");
        return false;
    }
    FDamageEffectParams Prepared;
    const auto* Settings = CastChecked<UAbilityInputDamageNodeConfig>(&Config);
    if (!Settings->Validate(Error))
    {
        return false;
    }
    const auto Profile =
        Input.Impact.IsSet() ? Input.Impact->ContactDamageProfile : EAbilityContactDamageProfile::Unified;
    if (!AbilityDamageService::PrepareContactDamage(Input.DamageParams, Input.Direction,
                                                    Input.DamageScale * Settings->DamageMultiplier,
                                                    Input.DebuffDamageScale, Prepared, Profile))
    {
        Error = TEXT("Input damage requires finite unscaled values and cumulative scales.");
        return false;
    }
    if (const auto Converted = AbilityElements::ToDamageTag(Settings->ElementOverride); Converted.IsValid())
    {
        Prepared.DamageType = Converted;
    }
    const auto Result = AbilityDamageService::Apply(Input.SourceToken, Input.LiveTargetToken.GetValue(), Prepared, 1.f,
                                                    Settings->bCaptureLocationAfterDamage);
    if (bStopped || Owner.IsFinished())
    {
        return true;
    }
    auto Payload = Input;
    FAbilityImpactSnapshot Impact;
    Impact.Location = Result.ImpactLocation;
    Impact.Direction = Input.Direction;
    Impact.ConfirmedTarget = Input.LiveTargetToken.GetValue();
    Impact.Sequence = Input.Impact.IsSet() ? Input.Impact->Sequence : 0;
    Impact.ContactDamageProfile = Profile;
    Payload.Impact = MoveTemp(Impact);
    Payload.bHasDamageResult = true;
    Payload.bDamageAttempted = Result.bAttempted;
    Payload.bKilled = Result.bKilled;
    Payload.DamageApplied = Result.Applied;
    Payload.ActualDamage = Result.ActualDamage;
    Payload.LiveTargetToken.Reset();
    // 原Params完整保留，包括真正的初始preset；本次随机force只进入实际GE，不回流到下一次接触。
    Owner.Publish(Id, TEXT("DamageResolved"), Payload);
    if (!bStopped && !Owner.IsFinished())
    {
        Owner.Publish(Id, TEXT("Completed"), Payload);
    }
    return true;
}
void AbilityInputDamageNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Effect.InputDamage")))
    {
        return;
    }
    FAbilityNodeDescriptor D;
    D.TypeId = TEXT("Effect.InputDamage");
    D.ConfigClass = UAbilityInputDamageNodeConfig::StaticClass();
    D.ExecutionClass = UAbilityInputDamageNodeExecution::StaticClass();
    D.InputPayload = EAbilityPortPayload::LiveTarget;
    D.bSupportsActorTargets = D.bSupportsMassTargets = true;
    // 每调用至多结算一个已捕获生命，完成后不持有资源，持续根仍限制开始速率和帧事件。
    D.bSustainedSafe = true;
    // 经原Actor/Mixed接触profile真实GE、Mass/DOT与力合同对照；无生成/计时器，不借此取得逻辑子根额度。
    D.bPrototypeCompatibilitySafe = true;
    D.bRuntimeFiniteSafe = true;
    // 原接触结算只同步消费一个已经复核的LiveTarget；允许以同输入的已证明纯动作替换。
    // 这不授予库存或投递权限，也不把InputDamage本身声明为可无限追加的新动作。
    D.bRuntimePayloadReplacementSlot = true;
    D.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
               {TEXT("Completed"), EAbilityPortPayload::DamageResult, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(D, Error), TEXT("Input damage registration: %s"), *Error);
}
