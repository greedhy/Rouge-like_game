// 调用链：原FireBolt前端的冻结定义 -> 准备一次装备/等级值 -> 方向分布 -> 真正投递 -> 独立接触效果。
#include "AbilitySystem/Unified/Nodes/AbilityNativeProjectileParameterNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityDistributionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"

UAbilityNativeProjectileParameterFrame* UAbilityNativeProjectileParameterFrame::Create(
    UObject* Outer, FGuid Root, const FAbilitySourceToken& Source, const FAbilityActorProjectileSettings& Launch,
    int32 ActualCount, FString& Error)
{
    if (!Launch.Validate(Error) || ActualCount < 0 || ActualCount > 64 || !FMath::IsFinite(Launch.Spread))
    {
        Error = TEXT("Native launch frame requires validated prepared count and settings.");
        return nullptr;
    }
    auto* Frame = NewObject<UAbilityNativeProjectileParameterFrame>(Outer);
    if (!Frame->BindExecution(Root, Source))
    {
        Error = TEXT("Native launch frame cannot bind another root or retired source.");
        return nullptr;
    }
    Frame->Count = ActualCount;
    Frame->Spread = Launch.Spread;
    auto& Flight = Frame->Delivery;
    Flight.ProjectileClass = Launch.ProjectileClass;
    Flight.FlightProfile = EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults;
    Flight.ContactProfile = EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass;
    Flight.TargetLossPolicy = EAbilityDeliveryTargetLossPolicy::KeepLastAimPoint;
    Flight.NativeConfigureHoming = Launch.bConfigureHoming;
    Flight.Motion = Launch.bLaunchHoming ? EAbilityProjectileMotion::NativeHoming : EAbilityProjectileMotion::Linear;
    Flight.HomingAccelerationMin = Launch.HomingAccelerationMin;
    Flight.HomingAccelerationMax = Launch.HomingAccelerationMax;
    Flight.bApplyDamage = false;
    return Frame;
}

const FAbilityDeliverySettings& UAbilityNativeProjectileParameterFrame::GetDeliverySettings() const
{
    return Delivery;
}

int32 UAbilityNativeProjectileParameterFrame::GetCount() const
{
    return Count;
}

float UAbilityNativeProjectileParameterFrame::GetSpread() const
{
    return Spread;
}

bool UAbilityNativeProjectilePrepareConfig::Validate(FString& Error) const
{
    if (!Settings.Validate(Error) || !FMath::IsFinite(DamageMultiplier) || DamageMultiplier < 0.f)
    {
        Error = TEXT("Native projectile preparation requires original settings and a finite damage multiplier.");
        return false;
    }
    return true;
}

bool UAbilityNativeProjectilePrepareConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Damage || !FMath::IsFinite(Multiplier) || Multiplier < 0.f ||
        !FMath::IsFinite(DamageMultiplier * Multiplier))
    {
        return false;
    }
    DamageMultiplier *= Multiplier;
    return true;
}

bool UAbilityNativeProjectilePrepareExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                     const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                     FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityNativeProjectilePrepareConfig>(&Config);
    if (!Settings.Validate(Error))
    {
        return false;
    }
    FAbilityActorProjectileSettings Launch;
    FDamageEffectParams Damage;
    int32 Count = 0;
    if (!AbilityTraditionalActions::PrepareProjectileLaunch(Input.SourceToken, Input.DamageParams, Settings.Settings,
                                                            Input.Level, Launch, Damage, Count, Error))
    {
        if (Error.IsEmpty())
        {
            Owner.FinishInvocation(Id);
            return true;
        }
        return false;
    }
    auto* Frame = UAbilityNativeProjectileParameterFrame::Create(this, Owner.GetExecutionId(), Input.SourceToken,
                                                                 Launch, Count, Error);
    if (!Frame)
    {
        return false;
    }
    auto Prepared = Input;
    Prepared.ParameterFrame = Frame;
    Prepared.DamageParams = Damage;
    Prepared.DamageScale *= Settings.DamageMultiplier;
    if (!FMath::IsFinite(Prepared.DamageScale) || !FMath::IsFinite(Damage.BaseDamage * Prepared.DamageScale))
    {
        Error = TEXT("Native projectile cumulative damage multiplier overflowed.");
        return false;
    }
    // 实际Notify方向已包含原pitch；准备只固定数量/参数，不重算瞄准也不再覆盖俯仰。
    return Owner.Publish(Id, TEXT("Completed"), Prepared);
}

UAbilityGraphDefinition* AbilityNativeProjectileParameters::Build(UObject* Outer,
                                                                  const FAbilityActorProjectileSettings& Settings)
{
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    auto Add = [Graph](FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        return Node.NodeId;
    };
    auto* Prepare = NewObject<UAbilityNativeProjectilePrepareConfig>(Graph);
    Prepare->Settings = Settings;
    const auto Entry = Add(TEXT("Prepare.NativeFireBolt"), Prepare);
    auto* Scatter = NewObject<UAbilityScatterNodeConfig>(Graph);
    Scatter->bUseNativeLaunchParameters = true;
    const auto Directions = Add(TEXT("Distribution.Scatter"), Scatter);
    auto* Delivery = NewObject<UAbilityDeliveryNodeConfig>(Graph);
    Delivery->bUseNativeLaunchParameters = true;
    const auto Flight = Add(TEXT("Delivery.ProjectileAwait"), Delivery);
    const auto Resolve = Add(TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(Graph));
    const auto Damage = Add(TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(Graph));
    Graph->EntryNodeId = Entry;
    Graph->Edges = {{Entry, Directions, TEXT("Completed")},
                    {Directions, Flight, TEXT("Launch")},
                    {Flight, Resolve, TEXT("Hit")},
                    {Resolve, Damage, TEXT("TargetFound")}};
    return Graph;
}

void AbilityNativeProjectileParameters::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Prepare.NativeFireBolt")))
    {
        return;
    }
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Prepare.NativeFireBolt");
    Descriptor.ConfigClass = UAbilityNativeProjectilePrepareConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityNativeProjectilePrepareExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Native launch preparation registration: %s"), *Error);
}
