// 调用链：注册表 -> 投递策略 -> 单次回执 -> 图端口；不激活第二个GA。
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityNativeProjectileParameterNodes.h"

bool UAbilityDeliveryNodeConfig::Validate(FString& Error) const
{
    if (bUseNativeLaunchParameters)
    {
        Error.Reset(); // 实际class/参数来自同根只读帧，在Start再验证，不能校验未消费的本地默认配置。
        if (bOverridePreparedCollisionRadius && (!FMath::IsFinite(Delivery.CollisionRadius) ||
                                                 Delivery.CollisionRadius <= 0.f || Delivery.CollisionRadius > 200.f))
        {
            Error = TEXT("Selected prepared collision radius must be finite world cm in (0,200].");
        }
        return Error.IsEmpty();
    }
    return Delivery.Validate(Error);
}

bool UAbilityDeliveryNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::CollisionRadius || !FMath::IsFinite(Value) || Value <= 0.f ||
        Value > 200.f)
    {
        Error = TEXT("This delivery currently consumes only a bounded CollisionRadius override.");
        return false;
    }
    Delivery.CollisionRadius = Value;
    bOverridePreparedCollisionRadius = bUseNativeLaunchParameters;
    return Validate(Error);
}

bool AbilityDeliveryNodes::ResolveDeliverySettings(const UAbilityDeliveryNodeConfig& Config,
                                                   const FAbilityNodeInput& Input, FGuid Root,
                                                   FAbilityDeliverySettings& OutSettings, FString& Error)
{
    if (Config.bUseNativeLaunchParameters)
    {
        const auto* Frame = Cast<UAbilityNativeProjectileParameterFrame>(Input.ParameterFrame.Get());
        if (!Frame || !Frame->BelongsTo(Root, Input.SourceToken))
        {
            Error = TEXT("Native delivery requires its original root's prepared launch frame.");
            return false;
        }
        OutSettings = Frame->GetDeliverySettings();
        if (Config.bOverridePreparedCollisionRadius)
        {
            OutSettings.CollisionRadius = Config.Delivery.CollisionRadius;
            OutSettings.bOverrideNativeCollisionRadius = true;
        }
    }
    else
    {
        OutSettings = Config.Delivery;
    }
    return OutSettings.Validate(Error);
}

bool UAbilityDeliveryNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                          const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                          FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    if (!Owner.ReservePhysicalSpawn(InvocationId))
    {
        Error = TEXT("Root physical delivery budget exhausted.");
        return false;
    }
    FAbilityDeliveryRequest Request;
    Request.Source = Input.SourceToken;
    Request.Target = Input.LiveTargetToken;
    Request.Origin = Input.Origin;
    Request.Direction = Input.Direction;
    Request.DamageParams = Input.DamageParams;
    Request.DamageScale = Input.DamageScale;
    Request.DebuffDamageScale = Input.DebuffDamageScale;
    const auto* Settings = CastChecked<UAbilityDeliveryNodeConfig>(&Config);
    if (!AbilityDeliveryNodes::ResolveDeliverySettings(*Settings, Input, Owner.GetExecutionId(), Request.Settings,
                                                       Error))
    {
        return false;
    }
    if (Request.Settings.FlightProfile == EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults ||
        Request.Settings.NativeConfigureHoming)
    {
        Request.AimPoint = Input.EventCenter;
    }
    Request.RootCastId = Owner.GetExecutionId();
    Request.StepId = Id;
    Delivery = NewObject<UAbilityDeliveryInstance>(this);
    Delivery->OnImpact.AddUObject(this, &ThisClass::ImpactReceived);
    Delivery->OnFinished.AddUObject(this, &ThisClass::Delivered);
    return Delivery->Begin(Request, Error);
}
void UAbilityDeliveryNodeExecution::Advance(float DeltaSeconds)
{
    if (!bStopped && Delivery)
    {
        Delivery->Advance(DeltaSeconds);
    }
}
void UAbilityDeliveryNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    if (Delivery)
    {
        Delivery->OnImpact.RemoveAll(this);
        Delivery->OnFinished.RemoveAll(this);
        Delivery->Cancel();
        Delivery = nullptr;
    }
}
void UAbilityDeliveryNodeExecution::ImpactReceived(const FAbilityDeliveryImpact& Impact)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return;
    }
    auto Payload = Input;
    Payload.Origin = Payload.EventCenter = Impact.Impact.Location;
    Payload.Direction = Impact.Impact.Direction;
    Payload.LiveTargetToken.Reset();
    Payload.Impact = Impact.Impact;
    // Params始终保留原值；本次接触的完整倍率由唯一实际伤害owner消费。
    Payload.DamageScale = Impact.DamageScale;
    Payload.DebuffDamageScale = Impact.DebuffDamageScale;
    Payload.bConfirmedHit = true;
    Owner->Publish(InvocationId, TEXT("Hit"), Payload);
    if (Impact.bRequireImmediatePayload && !bStopped && !Owner->IsFinished())
    {
        // 仍使用同一迭代drainer；不推进时钟。首GE的取消/池唤醒必须先于下一物理接触。
        Owner->FlushPublishedEvents(true);
    }
    if (!bStopped && !Owner->IsFinished() && Impact.Damage.bAttempted)
    {
        Payload.bHasDamageResult = true;
        Payload.bDamageAttempted = Impact.Damage.bAttempted;
        Payload.bKilled = Impact.Damage.bKilled;
        Payload.DamageApplied = Impact.Damage.Applied;
        Payload.ActualDamage = Impact.Damage.ActualDamage;
        Owner->Publish(InvocationId, TEXT("DamageResolved"), Payload);
    }
}
void UAbilityDeliveryNodeExecution::Delivered(const FAbilityDeliveryResult& Result)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return;
    }
    auto Payload = Input;
    Payload.Origin = Payload.EventCenter = Result.Location;
    Payload.Direction = Result.Direction;
    Payload.LiveTargetToken.Reset();
    Payload.Impact = Result.Impact;
    if (Result.Outcome == EAbilityDeliveryOutcome::Cancelled)
    {
        // 取消只释放当前调用；Cancelled不是业务事件，不能发布后继，也不能终止独立兄弟分支。
        Owner->FinishInvocation(InvocationId, EAbilityGraphEndReason::Cancelled);
        return;
    }
    else if (Result.Outcome != EAbilityDeliveryOutcome::Hit)
    {
        const FName Port = Result.Outcome == EAbilityDeliveryOutcome::Blocked      ? TEXT("Blocked")
                           : Result.Outcome == EAbilityDeliveryOutcome::TargetLost ? TEXT("TargetLost")
                           : Result.Outcome == EAbilityDeliveryOutcome::Returned   ? TEXT("Returned")
                                                                                   : TEXT("Expired");
        Owner->Publish(InvocationId, Port, Payload);
    }
    if (!bStopped && !Owner->IsFinished())
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
}
void AbilityDeliveryNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Delivery.ProjectileAwait")))
    {
        return;
    }
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Delivery.ProjectileAwait");
    Descriptor.ConfigClass = UAbilityDeliveryNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityDeliveryNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = true;
    // 每次真实弹体先向所属invocation预留物理额度，取消/结束释放其活动资源。
    Descriptor.bRuntimeFiniteSafe = true;
    Descriptor.Ports = {{TEXT("Hit"), EAbilityPortPayload::ConfirmedImpact, false},
                        {TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                        {TEXT("Blocked"), EAbilityPortPayload::Origin, false},
                        {TEXT("TargetLost"), EAbilityPortPayload::Origin, false},
                        {TEXT("Expired"), EAbilityPortPayload::Origin, false},
                        {TEXT("Returned"), EAbilityPortPayload::Origin, false},
                        {TEXT("Cancelled"), EAbilityPortPayload::Origin, true},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Delivery node registration: %s"), *Error);
}
