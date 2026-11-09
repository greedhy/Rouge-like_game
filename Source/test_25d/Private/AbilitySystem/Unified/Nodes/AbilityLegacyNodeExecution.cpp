// 调用链：图工厂 -> Start -> Driver Start/Advance -> AdmitChild -> 图队列；Stop 先撤回事件权限再清资源。
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/LegacyActorAbilityDriver.h"
#include "AbilitySystem/Unified/LegacyMixedAbilityDriver.h"
#include "AbilitySystem/Unified/LegacyExtensionAbilityDriver.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "PureMass/PureMassRuntime.h"

bool UAbilityLegacyNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    const auto* Extension = Cast<UExtensionNode>(Parameters);
    if (!Parameters || !PresentationParameters || LegacyDepth < 0 || LegacyDepth > 3 ||
        (Profile != TEXT("LegacyActor.v1") && Profile != TEXT("LegacyMixed.v1") &&
         Profile != TEXT("LegacyExtension.v1")) ||
        ((Profile == TEXT("LegacyExtension.v1")) != (Extension != nullptr)) ||
        (Extension ? !Extension->ValidateGraph() : !Parameters->IsValidConfiguration()) ||
        (bSingleProjectile && (Extension || Parameters->Kind != EAbilityPrototypeKind::Multishot)))
    {
        Error = TEXT("Legacy node has invalid parameters, profile, source asset, depth or single-projectile flag.");
        return false;
    }
    return true;
}

FName AbilityLegacyNodes::TypeId(const UAbilityPrototype& Parameters, FName Profile)
{
    if (const auto* Extension = Cast<UExtensionNode>(&Parameters))
    {
        return FName(TEXT("Legacy.Extension.") +
                     StaticEnum<EExtensionKind>()->GetNameStringByValue(static_cast<int64>(Extension->Behavior)));
    }
    const FString Prefix = Profile == TEXT("LegacyActor.v1") ? TEXT("Legacy.Actor.") : TEXT("Legacy.Mixed.");
    return FName(Prefix +
                 StaticEnum<EAbilityPrototypeKind>()->GetNameStringByValue(static_cast<int64>(Parameters.Kind)));
}

bool UAbilityLegacyNodeConfig::MatchesType(FName RegisteredType) const
{
    return Parameters && AbilityLegacyNodes::TypeId(*Parameters, Profile) == RegisteredType;
}

void AbilityLegacyNodes::Register(FAbilityNodeRegistry& Registry)
{
    auto Add = [&](const FString& Name)
    {
        if (Registry.Find(FName(Name)))
        {
            return;
        }
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = FName(Name);
        Descriptor.ConfigClass = UAbilityLegacyNodeConfig::StaticClass();
        Descriptor.ExecutionClass = UAbilityLegacyNodeExecution::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::Origin;
        Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
        Descriptor.bSupportsActorTargets = true;
        Descriptor.bSupportsMassTargets = !Name.StartsWith(TEXT("Legacy.Actor."));
        Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bIsAsync = true;
        Descriptor.Ports = {{TEXT("OnHit"), EAbilityPortPayload::ConfirmedImpact, false},
                            {TEXT("OnPeriod"), EAbilityPortPayload::Origin, false},
                            {TEXT("OnExpire"), EAbilityPortPayload::Origin, false},
                            {TEXT("Multishot"), EAbilityPortPayload::Origin, false},
                            {TEXT("Completed"), EAbilityPortPayload::Origin, true},
                            {TEXT("Cancelled"), EAbilityPortPayload::Origin, true},
                            {TEXT("Failed"), EAbilityPortPayload::Origin, true}};
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Legacy node registration: %s"), *Error);
    };
    for (int64 Kind = 0; Kind <= static_cast<int64>(EAbilityPrototypeKind::Line); ++Kind)
    {
        const FString Name = StaticEnum<EAbilityPrototypeKind>()->GetNameStringByValue(Kind);
        Add(TEXT("Legacy.Actor.") + Name);
        Add(TEXT("Legacy.Mixed.") + Name);
    }
    for (int64 Kind = 0; Kind <= static_cast<int64>(EExtensionKind::Surface); ++Kind)
    {
        Add(TEXT("Legacy.Extension.") + StaticEnum<EExtensionKind>()->GetNameStringByValue(Kind));
    }
}

bool UAbilityLegacyNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                        const UAbilityNodeConfig& Configuration, const FAbilityNodeInput& InInput,
                                        FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    const auto* Config = Cast<UAbilityLegacyNodeConfig>(&Configuration);
    CastContext = Cast<UPrototypeCastContext>(Input.Context.Get());
    if (!Config || !Config->Validate(Error) || !CastContext || !CastContext->ValidateSourceLife())
    {
        Error = TEXT("Legacy execution requires a validated snapshot and its captured cast context.");
        return false;
    }
    FPureMassCombatTarget Target;
    if (Input.LiveTargetToken.IsSet())
    {
        Target = Input.LiveTargetToken->Target;
    }
    else if (Input.Impact.IsSet())
    {
        Target = Input.Impact->ConfirmedTarget.Target;
    }
    const TWeakObjectPtr<UAbilityLegacyNodeExecution> WeakThis(this);
    FLegacyAbilityChildAdmission Admission = [WeakThis](const FLegacyAbilityChildRequest& Request)
    {
        auto* Self = WeakThis.Get();
        return Self && Self->AdmitChild(Request);
    };
    bStarting = true;
    if (Config->Profile == TEXT("LegacyActor.v1"))
    {
        ActorHost = APrototypeRuntime::SpawnDriven(CastContext, Config->Parameters, Input.Origin, Input.Direction,
                                                   Target.Actor.Get(), Config->LegacyDepth, Config->bSingleProjectile,
                                                   MoveTemp(Admission), Config->PresentationParameters);
    }
    else if (Config->Profile == TEXT("LegacyMixed.v1"))
    {
        MixedHost = APureMassRuntime::SpawnDriven(CastContext, Config->Parameters, Input.Origin, Input.Direction,
                                                  Target, Config->LegacyDepth, Config->bSingleProjectile,
                                                  MoveTemp(Admission), Config->PresentationParameters);
    }
    else
    {
        ExtensionHost = AExtensionRuntime::SpawnDriven(CastContext, Cast<UExtensionNode>(Config->Parameters),
                                                       Input.Origin, Input.Direction, Target, Config->LegacyDepth,
                                                       Input.EventCenter, Input.bConfirmedHit, MoveTemp(Admission),
                                                       Cast<UExtensionNode>(Config->PresentationParameters));
    }
    bStarting = false;
    if (bStopped || Owner.IsFinished())
    {
        // Spawn 的同步命中可能通过观察者取消图；Stop 当时尚未收到返回的 Host，必须在此补清理。
        if (ActorHost && ActorHost->GetDriver())
        {
            ActorHost->GetDriver()->Finish(true);
        }
        if (MixedHost && MixedHost->GetDriver())
        {
            MixedHost->GetDriver()->Finish(true);
        }
        if (ExtensionHost && ExtensionHost->GetDriver())
        {
            ExtensionHost->GetDriver()->Finish(true);
        }
        ActorHost = nullptr;
        MixedHost = nullptr;
        ExtensionHost = nullptr;
        return true;
    }
    if (!ActorHost && !MixedHost && !ExtensionHost)
    {
        Error = TEXT("Legacy driver could not acquire its presentation resources.");
        return false;
    }
    // 工具测试世界尚未自动 BeginPlay；策略仍需启动行为一次，正常游戏生成后此分支为空。
    AActor* Host = ActorHost   ? static_cast<AActor*>(ActorHost.Get())
                   : MixedHost ? static_cast<AActor*>(MixedHost.Get())
                               : static_cast<AActor*>(ExtensionHost.Get());
    if (Host && !Host->HasActorBegunPlay())
    {
        Host->DispatchBeginPlay();
    }
    CheckFinished();
    return true;
}

bool UAbilityLegacyNodeExecution::AdmitChild(const FLegacyAbilityChildRequest& Request)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !CastContext || !CastContext->ValidateSourceLife())
    {
        return false;
    }
    FAbilityNodeInput Payload = Input;
    Payload.Origin = Request.Position;
    Payload.Direction = Request.Direction;
    Payload.EventCenter = Request.EventCenter;
    Payload.bConfirmedHit = Request.bConfirmedHit;
    Payload.LiveTargetToken.Reset();
    Payload.Impact.Reset();
    if (Request.Target.IsSet())
    {
        FVector Location;
        const bool bLive = CastContext->ResolveTargetLife(Request.Target, Location);
        if (const auto* Token = CastContext->FindTargetLife(Request.Target))
        {
            if (bLive)
            {
                Payload.LiveTargetToken = *Token;
            }
            if (Request.bConfirmedHit || Request.Event == TEXT("OnHit"))
            {
                FAbilityImpactSnapshot Snapshot;
                Snapshot.Location = Request.Position;
                Snapshot.Direction = Request.Direction;
                Snapshot.ConfirmedTarget = *Token;
                Payload.Impact = MoveTemp(Snapshot);
            }
        }
    }
    return Owner->RequestChild(InvocationId, Request.Event, Payload);
}

void UAbilityLegacyNodeExecution::Advance(float DeltaSeconds)
{
    if (bStopped)
    {
        return;
    }
    if (ActorHost && ActorHost->GetDriver())
    {
        ActorHost->GetDriver()->Advance(DeltaSeconds);
    }
    if (MixedHost && MixedHost->GetDriver())
    {
        MixedHost->GetDriver()->Advance(DeltaSeconds);
    }
    if (ExtensionHost && ExtensionHost->GetDriver())
    {
        ExtensionHost->GetDriver()->Advance(DeltaSeconds);
    }
    CheckFinished();
}

void UAbilityLegacyNodeExecution::CheckFinished()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bStarting || !Owner)
    {
        return;
    }
    if (CastContext && CastContext->bCancelled)
    {
        Owner->Cancel(EAbilityGraphEndReason::Cancelled);
        return;
    }
    const bool bDone = ActorHost   ? ActorHost->IsFinished()
                       : MixedHost ? MixedHost->IsFinished()
                                   : !ExtensionHost || ExtensionHost->bFinished;
    if (bDone)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilityLegacyNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (bStopped)
    {
        return;
    }
    const bool bCancelled = Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired;
    Super::Stop(Reason);
    if (bCancelled && CastContext)
    {
        CastContext->bCancelled = true;
    }
    if (ActorHost && ActorHost->GetDriver() && !ActorHost->IsFinished())
    {
        ActorHost->GetDriver()->Finish(bCancelled);
    }
    if (MixedHost && MixedHost->GetDriver() && !MixedHost->IsFinished())
    {
        MixedHost->GetDriver()->Finish(bCancelled);
    }
    if (ExtensionHost && ExtensionHost->GetDriver() && !ExtensionHost->bFinished)
    {
        ExtensionHost->GetDriver()->Finish(bCancelled);
    }
    ActorHost = nullptr;
    MixedHost = nullptr;
    ExtensionHost = nullptr;
    CastContext = nullptr;
}
