// 调用链：原环绕配置 -> 原库存宿主 -> PreparedInventory发射 -> 原Actor碰撞 -> 独立外部效果。
#include "AbilitySystem/Unified/Nodes/AbilityOrbitFragments.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/AbilityProjectileMotion.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    const FName BoltSlot(TEXT("NativeOrbitBolt"));
    const FName HostSlot(TEXT("NativeOrbitHost"));
} // namespace

bool UAbilityNativeOrbitConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!Parameters || !Parameters->IsValidConfiguration())
    {
        Error = TEXT("Native inventory requires its own valid frozen orbit configuration.");
        return false;
    }
    // Generic delivery still rejects this native class. Only its own inventory/LaunchAt contract is admitted here.
    for (const UClass* Type = Parameters->FireBoltClass.Get(); Type; Type = Type->GetSuperClass())
    {
        if (Type->HasAnyClassFlags(CLASS_Native))
        {
            if (Type == AMyOrbitingFireBolt::StaticClass())
            {
                return true;
            }
            break;
        }
    }
    Error = TEXT("Native inventory requires the original adapted Orbit native class or its Blueprint asset.");
    return false;
}

bool UAbilityNativeOrbitConfig::SupportsSustainedRoot(FString& Error) const
{
    return Validate(Error);
}

UAbilityNativeOrbitFrame* UAbilityNativeOrbitFrame::Create(UObject* Outer, FGuid Root,
                                                           const FAbilitySourceToken& Source,
                                                           const UOrbitingFireBoltConfig& Settings)
{
    auto* Frame = NewObject<UAbilityNativeOrbitFrame>(Outer);
    if (!Frame->BindExecution(Root, Source))
    {
        return nullptr;
    }
    Frame->FrozenSettings = DuplicateObject<UOrbitingFireBoltConfig>(&Settings, Frame);
    return Frame;
}

bool UAbilityNativeOrbitHostExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    const auto* InventoryConfig = Cast<UAbilityNativeOrbitConfig>(&Config);
    if (!InventoryConfig || !InventoryConfig->Validate(Error))
    {
        return false;
    }
    auto* Original = NewObject<UAbilityOrbitNodeConfig>(this);
    Original->OrbitSettings = InventoryConfig->Parameters;
    return Super::Start(Owner, Id, *Original, InInput, Error);
}

bool UAbilityNativeOrbitHostExecution::PrepareDelivery(AMyOrbitingFireBolt* Bolt)
{
    auto* Owner = GraphOwner.Get();
    FGuid Lease;
    if (!Owner || !Bolt || !GetOrbitSettings() || !Owner->ReservePhysicalSpawnWithLease(InvocationId, Lease))
    {
        if (Owner)
        {
            Owner->Cancel(EAbilityGraphEndReason::BudgetExceeded);
        }
        return false;
    }
    auto* Frame =
        UAbilityNativeOrbitFrame::Create(this, Owner->GetExecutionId(), Input.SourceToken, *GetOrbitSettings());
    if (!Frame || !Owner->RegisterRuntimeResource(*Frame, BoltSlot, Bolt) ||
        !Owner->RegisterRuntimeResource(*Frame, HostSlot, this))
    {
        Owner->ReleasePhysicalSpawnLease(Lease);
        if (Frame)
        {
            Owner->UnregisterRuntimeResource(*Frame, BoltSlot, Bolt);
        }
        return false;
    }
    Frames.Add(Bolt, Frame);
    PhysicalLeases.Add(Bolt, Lease);
    Bolt->OnDestroyed.AddUniqueDynamic(this, &ThisClass::InventoryDestroyed);
    const TWeakObjectPtr<UAbilityNativeOrbitHostExecution> Weak(this);
    const TWeakObjectPtr<AMyOrbitingFireBolt> WeakBolt(Bolt);
    Bolt->SetGraphDeliveryHandlers(
        [Weak, WeakBolt](const FAbilityTargetToken& Target, FVector Position, FVector)
        {
            if (auto* Host = Weak.Get(); Host && WeakBolt.IsValid())
            {
                return Host->Contact(WeakBolt.Get(), Target, Position);
            }
            return EAbilityProjectileContactDisposition::Terminate;
        },
        {});
    return true;
}

void UAbilityNativeOrbitHostExecution::DispatchLaunch(AMyOrbitingFireBolt* Bolt, const FAbilityTargetToken& Target)
{
    auto* Owner = GraphOwner.Get();
    auto* Frame = Frames.FindRef(Bolt).Get();
    if (!Owner || bStopped || !Frame)
    {
        return;
    }
    auto Payload = Input;
    Payload.ParameterFrame = Frame;
    Payload.LiveTargetToken = Target;
    Payload.Origin = Bolt->GetActorLocation();
    if (Owner->Publish(InvocationId, TEXT("Launch"), Payload))
    {
        // 原LaunchTimer在同一边界使原Actor开始移动，不把发射偷偷推迟到下次图Tick。
        Owner->FlushPublishedEvents(true);
    }
}

bool UAbilityNativeOrbitHostExecution::LaunchPrepared(const UAbilityNativeOrbitFrame& Frame,
                                                      const FAbilityTargetToken& Target)
{
    auto* Owner = GraphOwner.Get();
    FVector Location;
    auto* Bolt = Owner ? Cast<AMyOrbitingFireBolt>(Owner->FindRuntimeResource(Frame, BoltSlot)) : nullptr;
    if (bStopped || !Owner || !Bolt || Bolt->IsLaunched() || Frames.FindRef(Bolt).Get() != &Frame ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, Location) || !Target.Target.IsActor())
    {
        return false;
    }
    Bolt->LaunchAt(Target.Target.Actor.Get());
    return true;
}

EAbilityProjectileContactDisposition UAbilityNativeOrbitHostExecution::Contact(AMyOrbitingFireBolt* Bolt,
                                                                               const FAbilityTargetToken& Target,
                                                                               FVector Position)
{
    const TStrongObjectPtr<UAbilityNativeOrbitHostExecution> Self(this);
    const TStrongObjectPtr<AMyOrbitingFireBolt> Shot(Bolt);
    auto* Owner = GraphOwner.Get();
    const TStrongObjectPtr<UAbilityGraphInstance> RootGuard(Owner);
    auto* Frame = Frames.FindRef(Bolt).Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Frame)
    {
        return EAbilityProjectileContactDisposition::Terminate;
    }
    auto Payload = Input;
    Payload.ParameterFrame = Frame;
    Payload.LiveTargetToken.Reset();
    Payload.DamageParams = Bolt->DamageEffectParams;
    Payload.Origin = Payload.EventCenter = Position;
    Payload.Direction = Bolt->GetActorForwardVector();
    FAbilityImpactSnapshot Impact;
    Impact.Location = Position;
    Impact.Direction = Payload.Direction;
    Impact.ConfirmedTarget = Target;
    Impact.Sequence = ++ContactSequence;
    Impact.ContactDamageProfile = Target.Target.IsActor() ? EAbilityContactDamageProfile::NativeActorProjectile
                                                          : EAbilityContactDamageProfile::NativeMassProjectile;
    Payload.Impact = Impact;
    Payload.bConfirmedHit = true;
    // 原弹体先锁终结门/表现，再GE；死亡回调中的ReturnToOrbit不能重新发射已经命中的同一Actor。
    Bolt->PlayGraphDeliveryImpactAt(Position, true);
    if (!bStopped && !Owner->IsFinished() && Owner->Publish(InvocationId, TEXT("Contact"), Payload))
    {
        Owner->FlushPublishedEvents(true);
        if (!bStopped && !Owner->IsFinished() && Owner->HasSuccessor(InvocationId, TEXT("OnHit")))
        {
            Owner->Publish(InvocationId, TEXT("OnHit"), Payload);
        }
    }
    if (IsValid(Bolt))
    {
        Bolt->ClearGraphDeliveryHandlers();
        Bolt->Destroy();
    }
    return EAbilityProjectileContactDisposition::Terminate;
}

void UAbilityNativeOrbitHostExecution::InventoryDestroyed(AActor* Actor)
{
    auto* Bolt = Cast<AMyOrbitingFireBolt>(Actor);
    auto* Frame = Frames.FindRef(Bolt).Get();
    if (auto* Owner = GraphOwner.Get())
    {
        if (Frame)
        {
            Owner->UnregisterRuntimeResource(*Frame, BoltSlot, Bolt);
            Owner->UnregisterRuntimeResource(*Frame, HostSlot, this);
        }
        if (const auto* Lease = PhysicalLeases.Find(Actor))
        {
            Owner->ReleasePhysicalSpawnLease(*Lease);
        }
    }
    Frames.Remove(Bolt);
    PhysicalLeases.Remove(Actor);
}

void UAbilityNativeOrbitHostExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        const auto Owned = Frames;
        for (const auto& Item : Owned)
        {
            if (auto* Bolt = Item.Key.Get())
            {
                Bolt->OnDestroyed.RemoveDynamic(this, &ThisClass::InventoryDestroyed);
                Bolt->ClearGraphDeliveryHandlers();
                InventoryDestroyed(Bolt);
            }
        }
        Frames.Reset();
        PhysicalLeases.Reset();
    }
    Super::Stop(Reason);
}

bool UAbilityNativeOrbitDeliveryExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                 const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                 FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = Cast<UAbilityNativeOrbitFrame>(Input.ParameterFrame.Get());
    auto* Host = Frame ? Cast<UAbilityNativeOrbitHostExecution>(Owner.FindRuntimeResource(*Frame, HostSlot)) : nullptr;
    if (!Frame || !Host || !Input.LiveTargetToken.IsSet())
    {
        Error = TEXT("Native Orbit delivery requires this root's prepared inventory and original target.");
        return false;
    }
    if (!Host->LaunchPrepared(*Frame, Input.LiveTargetToken.GetValue()))
    {
        Owner.FinishInvocation(Id, EAbilityGraphEndReason::TargetLost);
        return true;
    }
    return Owner.IsFinished() ? true : Owner.Publish(Id, TEXT("Completed"), Input);
}

UAbilityGraphDefinition* AbilityOrbitFragments::Build(UObject* Outer, const UOrbitingFireBoltConfig& Settings)
{
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::RegisteredSustained;
    auto Add = [Graph](FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        return Node.NodeId;
    };
    auto* HostConfig = NewObject<UAbilityNativeOrbitConfig>(Graph);
    HostConfig->Parameters = DuplicateObject<UOrbitingFireBoltConfig>(&Settings, HostConfig);
    const auto Host = Add(TEXT("Inventory.NativeOrbit"), HostConfig);
    const auto Launch = Add(TEXT("Delivery.NativeOrbitBolt"), NewObject<UAbilityNativeOrbitDeliveryConfig>(Graph));
    const auto Resolve = Add(TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(Graph));
    const auto Damage = Add(TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(Graph));
    Graph->EntryNodeId = Host;
    Graph->Edges = {
        {Host, Launch, TEXT("Launch")}, {Host, Resolve, TEXT("Contact")}, {Resolve, Damage, TEXT("TargetFound")}};
    return Graph;
}

void AbilityOrbitFragments::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Inventory.NativeOrbit")))
    {
        return;
    }
    FAbilityNodeDescriptor Host;
    Host.TypeId = TEXT("Inventory.NativeOrbit");
    Host.ConfigClass = UAbilityNativeOrbitConfig::StaticClass();
    Host.ExecutionClass = UAbilityNativeOrbitHostExecution::StaticClass();
    Host.InputPayload = EAbilityPortPayload::Origin;
    Host.bSupportsActorTargets = Host.bSupportsSnapshotTargets = true;
    Host.bIsAsync = Host.bSustainedRoot = Host.bSustainedSafe = true;
    Host.PotentiallyUnboundedOutputPorts = {TEXT("Launch"), TEXT("Contact"), TEXT("OnHit"), TEXT("Replenished"),
                                            TEXT("Launched")};
    Host.Ports = {{TEXT("Launch"), EAbilityPortPayload::PreparedInventory, false},
                  {TEXT("Contact"), EAbilityPortPayload::ConfirmedImpact, false},
                  {TEXT("OnHit"), EAbilityPortPayload::ConfirmedImpact, false},
                  {TEXT("Replenished"), EAbilityPortPayload::Origin, false},
                  {TEXT("Launched"), EAbilityPortPayload::LiveTarget, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Delivery;
    Delivery.TypeId = TEXT("Delivery.NativeOrbitBolt");
    Delivery.ConfigClass = UAbilityNativeOrbitDeliveryConfig::StaticClass();
    Delivery.ExecutionClass = UAbilityNativeOrbitDeliveryExecution::StaticClass();
    Delivery.InputPayload = EAbilityPortPayload::PreparedInventory;
    Delivery.bSupportsActorTargets = Delivery.bSupportsSnapshotTargets = Delivery.bSustainedSafe = true;
    Delivery.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Host, Error), TEXT("Native inventory registration: %s"), *Error);
    ensureAlwaysMsgf(Registry.Register(Delivery, Error), TEXT("Native orbit delivery registration: %s"), *Error);
}
