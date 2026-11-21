// 调用链：细图Acquire资源 -> 原地表工厂 -> 正常Release/取消Stop -> 精确Actor销毁。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeSurfaceNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityPrototypeSurfaceService.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "Terrain/TerrainSurface.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    const FName SurfaceSlot(TEXT("PrototypeSurface"));
}

bool UAbilityPrototypeSurfaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                  const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                  FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    if (!Frame || Frame->GetParameters().Kind != EAbilityPrototypeKind::Surface ||
        Frame->GetParameters().IsA<UExtensionNode>() ||
        (Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1") &&
         Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1")))
    {
        Error = TEXT("World surface requires its own ordinary Actor/Mixed Surface frame.");
        return false;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        return false;
    }
    Surface = AbilityPrototypeSurfaceService::Spawn(*Caster, Frame->GetParameters(), Input.Origin,
                                                    Frame->GetProfile() == TEXT("PrototypeFine.Actor.v1")
                                                        ? EAbilityPrototypeSurfaceProfile::Actor
                                                        : EAbilityPrototypeSurfaceProfile::Mixed);
    if (!Surface)
    {
        // 对应旧宿主Start取得资源失败的取消；不是伪造一次正常到期或改用替代Actor。
        if (auto* Context = Cast<UPrototypeCastContext>(Input.Context.Get()))
        {
            Context->bCancelled = true;
        }
        Owner.Cancel();
        return true;
    }
    if (!Owner.RegisterRuntimeResource(*Frame, SurfaceSlot, this))
    {
        Error = TEXT("World surface could not bind its exact frame-owned resource.");
        return false;
    }
    return Owner.Publish(Id, TEXT("Acquired"), Input);
}

void UAbilityPrototypeSurfaceNodeExecution::Release()
{
    const TStrongObjectPtr<UAbilityPrototypeSurfaceNodeExecution> Guard(this);
    if (auto* Owner = GraphOwner.Get(); Owner && !bStopped)
    {
        Owner->FinishInvocation(InvocationId);
    }
}

void UAbilityPrototypeSurfaceNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (GraphOwner.IsValid() && Frame)
    {
        GraphOwner->UnregisterRuntimeResource(*Frame, SurfaceSlot, this);
    }
    if (IsValid(Surface))
    {
        Surface->Destroy();
    }
    Surface = nullptr;
    Frame = nullptr;
    Super::Stop(Reason);
}

bool UAbilityPrototypeSurfaceReleaseNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                         const UAbilityNodeConfig& Config,
                                                         const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    auto* Resource =
        Frame ? Cast<UAbilityPrototypeSurfaceNodeExecution>(Owner.FindRuntimeResource(*Frame, SurfaceSlot)) : nullptr;
    if (!Resource)
    {
        Error = TEXT("Surface release lost its own exact live resource owner.");
        return false;
    }
    Resource->Release();
    // optional旧Emit预拒不阻止此release节点本身结束。
    if (!Owner.HasSuccessor(Id, TEXT("Released")) || !Owner.RequestChild(Id, TEXT("Released"), Input))
    {
        Owner.FinishInvocation(Id);
    }
    return true;
}

void AbilityPrototypeSurfaceNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Acquire;
    Acquire.TypeId = TEXT("World.PrototypeSurface");
    Acquire.ConfigClass = UAbilityPrototypeSurfaceNodeConfig::StaticClass();
    Acquire.ExecutionClass = UAbilityPrototypeSurfaceNodeExecution::StaticClass();
    Acquire.InputPayload = EAbilityPortPayload::Origin;
    Acquire.bSupportsActorTargets = Acquire.bSupportsMassTargets = Acquire.bSupportsSnapshotTargets = true;
    Acquire.bIsAsync = Acquire.bPrototypeCompatibilitySafe = true;
    Acquire.Ports = {{TEXT("Acquired"), EAbilityPortPayload::Origin, false}};
    auto Release = Acquire;
    Release.TypeId = TEXT("World.ReleasePrototypeSurface");
    Release.ConfigClass = UAbilityPrototypeSurfaceReleaseNodeConfig::StaticClass();
    Release.ExecutionClass = UAbilityPrototypeSurfaceReleaseNodeExecution::StaticClass();
    Release.bIsAsync = false;
    Release.Ports = {{TEXT("Released"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Acquire, Release})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Surface resource registration: %s"), *Error);
        }
    }
}
