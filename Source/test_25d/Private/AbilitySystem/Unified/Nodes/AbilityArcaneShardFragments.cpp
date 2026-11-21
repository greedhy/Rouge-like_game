// 调用链：原生碎片导出 -> 细节点定义；实现由真实对照测试驱动，不借原整体策略模拟可组合性。
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardFragments.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"

bool UAbilityShardDamageConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DamageMultiplier) || DamageMultiplier < 0.f)
    {
        Error = TEXT("Shard payload requires a finite nonnegative damage multiplier.");
        return false;
    }
    return true;
}

bool UAbilityShardDamageConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Damage || !FMath::IsFinite(Multiplier) || Multiplier < 0.f ||
        !FMath::IsFinite(DamageMultiplier * Multiplier))
    {
        return false;
    }
    DamageMultiplier *= Multiplier;
    return true;
}

UAbilityShardPointFrame* UAbilityShardPointFrame::Create(UObject* Outer, FGuid Root, const FAbilitySourceToken& Source,
                                                         const UAbilityArcaneShardNodeConfig& InSettings,
                                                         uint64 InSequence,
                                                         const TArray<FAbilityArcaneShardHit>* InHits)
{
    auto* Frame = NewObject<UAbilityShardPointFrame>(Outer);
    if (!Frame->BindExecution(Root, Source))
    {
        return nullptr;
    }
    Frame->Settings = DuplicateObject<UAbilityArcaneShardNodeConfig>(&InSettings, Frame);
    Frame->Sequence = InSequence;
    Frame->bHasCapturedHits = InHits != nullptr;
    if (InHits)
    {
        Frame->Hits = *InHits;
    }
    return Frame;
}

const UAbilityArcaneShardNodeConfig& UAbilityShardPointFrame::GetSettings() const
{
    return *Settings;
}

const TArray<FAbilityArcaneShardHit>& UAbilityShardPointFrame::GetHits() const
{
    return Hits;
}

bool UAbilityShardPointFrame::HasCapturedHits() const
{
    return bHasCapturedHits;
}

uint64 UAbilityShardPointFrame::GetSequence() const
{
    return Sequence;
}

namespace
{
    const UAbilityShardPointFrame* ResolveFrame(UAbilityGraphInstance& Owner, const FAbilityNodeInput& Input,
                                                FString& Error)
    {
        const auto* Frame = Cast<UAbilityShardPointFrame>(Input.ParameterFrame.Get());
        if (!Frame || !Frame->BelongsTo(Owner.GetExecutionId(), Input.SourceToken))
        {
            Error = TEXT("Shard fragment requires this root's original prepared point frame.");
            return nullptr;
        }
        return Frame;
    }
} // namespace

void UAbilityShardPointsExecution::EmitPoint(FVector Location, uint64 Sequence)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return;
    }
    auto* Frame = UAbilityShardPointFrame::Create(this, Owner->GetExecutionId(), Input.SourceToken,
                                                  *GetResolvedSettings(), Sequence);
    if (!Frame)
    {
        Owner->Cancel(EAbilityGraphEndReason::Failed);
        return;
    }
    auto Payload = Input;
    Payload.Origin = Payload.EventCenter = Location;
    Payload.DamageParams = GetResolvedDamage(); // 原Spell装备规则已经由共用Start只解析一次。
    Payload.ParameterFrame = Frame;
    if (Owner->Publish(InvocationId, TEXT("Point"), Payload))
    {
        // Timer回调与原宏同样在本点边界结算；Start内部只请求既有drainer继续，不递归。
        Owner->FlushPublishedEvents(true);
    }
}

bool UAbilityShardTargetsExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                          const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                          FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = ResolveFrame(Owner, Input, Error);
    if (!Frame)
    {
        return false;
    }
    const auto Hits = AbilityArcaneShardNodes::CaptureHits(Input.SourceToken, Input.Origin, Frame->GetSettings());
    auto* Captured = UAbilityShardPointFrame::Create(this, Owner.GetExecutionId(), Input.SourceToken,
                                                     Frame->GetSettings(), Frame->GetSequence(), &Hits);
    if (!Captured)
    {
        Error = TEXT("Shard query could not bind its original-life result frame.");
        return false;
    }
    auto Targets = MakeShared<FAbilityTargetSetSnapshot>();
    Targets->Source = Input.SourceToken;
    for (const auto& Hit : Hits)
    {
        Targets->Targets.Add(Hit.Token);
    }
    auto Payload = Input;
    Payload.TargetSet = Targets;
    Payload.ParameterFrame = Captured;
    return Owner.Publish(Id, TEXT("Targets"), Payload);
}

bool UAbilityShardDamageExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                         const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                         FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = ResolveFrame(Owner, Input, Error);
    if (!Frame || !Frame->HasCapturedHits() || !Input.TargetSet.IsValid())
    {
        Error = TEXT("Shard damage requires a complete original-life query snapshot.");
        return false;
    }
    const auto* PayloadConfig = Cast<UAbilityShardDamageConfig>(&Config);
    if (PayloadConfig && !PayloadConfig->Validate(Error))
    {
        return false;
    }
    // 原generic细图的默认倍率仍为1；只有显式sink参数消费一次局部主伤倍率，原Params/后继不被改写。
    const float DamageScale = Input.DamageScale * (PayloadConfig ? PayloadConfig->DamageMultiplier : 1.f);
    if (!FMath::IsFinite(DamageScale) || !FMath::IsFinite(Input.DamageParams.BaseDamage * DamageScale))
    {
        Error = TEXT("Shard payload cumulative damage scale overflowed.");
        return false;
    }
    const bool bPublishResults =
        Frame->GetSettings().bPublishDamageResults || Owner.HasSuccessor(Id, TEXT("DamageResolved"));
    for (const auto& Hit : Frame->GetHits())
    {
        if (bStopped || Owner.IsFinished())
        {
            return true;
        }
        FVector TargetLocation;
        if (!AbilityArcaneShardNodes::ResolveHitLocation(Input.SourceToken, Hit, TargetLocation))
        {
            continue;
        }
        const auto Result = AbilityArcaneShardNodes::ApplyHit(Input.SourceToken, Hit, Input.DamageParams, Input.Origin,
                                                              DamageScale, Input.DebuffDamageScale);
        if (bStopped || Owner.IsFinished())
        {
            return true;
        }
        if (bPublishResults)
        {
            Owner.Publish(
                Id, TEXT("DamageResolved"),
                AbilityArcaneShardNodes::DamagePayload(Input, Input.Origin, Hit, Result, Frame->GetSequence()));
        }
    }
    return !bStopped && !Owner.IsFinished() ? Owner.Publish(Id, TEXT("Completed"), Input) : true;
}

bool UAbilityShardCueExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                      const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = ResolveFrame(Owner, Input, Error);
    if (!Frame)
    {
        return false;
    }
    AbilityArcaneShardNodes::ExecuteCue(Input.SourceToken, Input.Origin, Frame->GetSettings());
    return !bStopped && !Owner.IsFinished() ? Owner.Publish(Id, TEXT("Completed"), Input) : true;
}

UAbilityGraphDefinition* AbilityArcaneShardFragments::Build(UObject* Outer,
                                                            const UAbilityArcaneShardNodeConfig& Settings)
{
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    auto Add = [&](FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        return Node.NodeId;
    };
    const auto Clock =
        Add(TEXT("Control.ShardPoints"), DuplicateObject<UAbilityArcaneShardNodeConfig>(&Settings, Graph));
    const auto Cue = Add(TEXT("Presentation.ShardCue"), NewObject<UAbilityShardFragmentConfig>(Graph));
    const auto Query = Add(TEXT("Query.ShardTargets"), NewObject<UAbilityShardFragmentConfig>(Graph));
    const auto Damage = Add(TEXT("Effect.ShardDamageBatch"), NewObject<UAbilityShardDamageConfig>(Graph));
    Graph->EntryNodeId = Clock;
    Graph->Edges = {{Clock, Cue, TEXT("Point")}, {Cue, Query, TEXT("Completed")}, {Query, Damage, TEXT("Targets")}};
    return Graph;
}

const UAbilityArcaneShardNodeConfig* AbilityArcaneShardFragments::FindSettings(const UAbilityGraphDefinition& Graph)
{
    for (const auto& Node : Graph.Nodes)
    {
        if (Node.TypeId == TEXT("Control.ShardPoints") || Node.TypeId == TEXT("Shape.ArcaneShards"))
        {
            return Cast<UAbilityArcaneShardNodeConfig>(Node.Config);
        }
    }
    return nullptr;
}

void AbilityArcaneShardFragments::Register(FAbilityNodeRegistry& Registry)
{
    auto Register = [&](FName Type, UClass* Config, UClass* Execution, EAbilityPortPayload Input,
                        TArray<FAbilityPortDescriptor> Ports, bool bAsync = false)
    {
        if (Registry.Find(Type))
        {
            return;
        }
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = Type;
        Descriptor.ConfigClass = Config;
        Descriptor.ExecutionClass = Execution;
        Descriptor.InputPayload = Input;
        Descriptor.bIsAsync = bAsync;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.Ports = MoveTemp(Ports);
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Shard fragment registration: %s"), *Error);
    };
    Register(
        TEXT("Control.ShardPoints"), UAbilityArcaneShardNodeConfig::StaticClass(),
        UAbilityShardPointsExecution::StaticClass(), EAbilityPortPayload::Origin,
        {{TEXT("Point"), EAbilityPortPayload::Origin, false}, {TEXT("Completed"), EAbilityPortPayload::Origin, true}},
        true);
    Register(TEXT("Presentation.ShardCue"), UAbilityShardFragmentConfig::StaticClass(),
             UAbilityShardCueExecution::StaticClass(), EAbilityPortPayload::Origin,
             {{TEXT("Completed"), EAbilityPortPayload::Origin, true}});
    Register(TEXT("Query.ShardTargets"), UAbilityShardFragmentConfig::StaticClass(),
             UAbilityShardTargetsExecution::StaticClass(), EAbilityPortPayload::Origin,
             {{TEXT("Targets"), EAbilityPortPayload::TargetSet, true}});
    Register(TEXT("Effect.ShardDamageBatch"), UAbilityShardFragmentConfig::StaticClass(),
             UAbilityShardDamageExecution::StaticClass(), EAbilityPortPayload::TargetSet,
             {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
              {TEXT("Completed"), EAbilityPortPayload::TargetSet, true}});
}
