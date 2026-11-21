// 调用链：新星配置 -> Schema2 三片段 -> 同一图执行器；不激活另一 GA，不接管动画/费用。
#include "AbilitySystem/Unified/Nodes/AbilityNovaFragmentNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"

bool UAbilityNovaPayloadConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DamageMultiplier) || DamageMultiplier < 0.f)
    {
        Error = TEXT("Nova payload requires a finite nonnegative damage multiplier.");
        return false;
    }
    return true;
}

bool UAbilityNovaPayloadConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Damage || !FMath::IsFinite(Multiplier) || Multiplier < 0.f ||
        !FMath::IsFinite(DamageMultiplier * Multiplier))
    {
        return false;
    }
    DamageMultiplier *= Multiplier;
    return true;
}

bool UAbilityNovaQueryExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                       const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    FAbilityNodeInput Prepared;
    if (!AbilityNovaNodes::PrepareTargets(Owner, *CastChecked<UAbilityNovaNodeConfig>(&Config), Input, Prepared, Error))
    {
        return false;
    }
    if (!bStopped && !Owner.IsFinished())
    {
        return Owner.Publish(Id, TEXT("Completed"), Prepared);
    }
    return true;
}

bool UAbilityNovaPayloadExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                         const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                         FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityNovaPayloadConfig>(&Config);
    if (!Settings.Validate(Error))
    {
        return false;
    }
    auto Payload = Input;
    Payload.DamageScale *= Settings.DamageMultiplier;
    if (!FMath::IsFinite(Payload.DamageScale) ||
        !FMath::IsFinite(Payload.DamageParams.BaseDamage * Payload.DamageScale) ||
        !FMath::IsFinite(Payload.DamageParams.DebuffDamage * Payload.DebuffDamageScale))
    {
        Error = TEXT("Nova cumulative damage scale overflowed.");
        return false;
    }
    AbilityNovaNodes::ApplyPreparedDamage(Owner, Id, Payload);
    if (!bStopped && !Owner.IsFinished())
    {
        auto Position = Payload;
        Position.TargetSet.Reset();
        return Owner.Publish(Id, TEXT("Completed"), Position);
    }
    return true;
}

bool UAbilityNovaCueExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                     const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    AbilityNovaNodes::PlayPreparedCue(Input, CastChecked<UAbilityNovaCueConfig>(&Config)->CueTag);
    if (!bStopped && !Owner.IsFinished())
    {
        return Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}

UAbilityGraphDefinition* AbilityNovaFragments::Build(UObject* Outer, const UAbilityNovaNodeConfig& Settings)
{
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Query;
    Query.NodeId = FGuid::NewGuid();
    Query.TypeId = TEXT("Query.NovaTargets");
    Query.Config = DuplicateObject<UAbilityNovaNodeConfig>(&Settings, Graph);
    FAbilityGraphNode Payload;
    Payload.NodeId = FGuid::NewGuid();
    Payload.TypeId = TEXT("Effect.NovaDamage");
    Payload.Config = NewObject<UAbilityNovaPayloadConfig>(Graph);
    FAbilityGraphNode Cue;
    Cue.NodeId = FGuid::NewGuid();
    Cue.TypeId = TEXT("Presentation.NovaCue");
    auto* CueConfig = NewObject<UAbilityNovaCueConfig>(Graph);
    CueConfig->CueTag = Settings.CueTag;
    Cue.Config = CueConfig;
    Graph->EntryNodeId = Query.NodeId;
    Graph->Nodes = {Query, Payload, Cue};
    Graph->Edges = {{Query.NodeId, Payload.NodeId, TEXT("Completed")}, {Payload.NodeId, Cue.NodeId, TEXT("Completed")}};
    return Graph;
}

void AbilityNovaFragments::Register(FAbilityNodeRegistry& Registry)
{
    for (const auto& Type :
         {FName(TEXT("Query.NovaTargets")), FName(TEXT("Effect.NovaDamage")), FName(TEXT("Presentation.NovaCue"))})
    {
        if (Registry.Find(Type))
        {
            continue;
        }
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = Type;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
        if (Type == TEXT("Query.NovaTargets"))
        {
            Descriptor.ConfigClass = UAbilityNovaNodeConfig::StaticClass();
            Descriptor.ExecutionClass = UAbilityNovaQueryExecution::StaticClass();
            Descriptor.InputPayload = EAbilityPortPayload::Origin;
            Descriptor.Ports = {{TEXT("Completed"), EAbilityPortPayload::TargetSet, true}};
        }
        else if (Type == TEXT("Effect.NovaDamage"))
        {
            Descriptor.ConfigClass = UAbilityNovaPayloadConfig::StaticClass();
            Descriptor.ExecutionClass = UAbilityNovaPayloadExecution::StaticClass();
            Descriptor.InputPayload = EAbilityPortPayload::TargetSet;
            Descriptor.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                                {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        }
        else
        {
            Descriptor.ConfigClass = UAbilityNovaCueConfig::StaticClass();
            Descriptor.ExecutionClass = UAbilityNovaCueExecution::StaticClass();
            Descriptor.InputPayload = EAbilityPortPayload::Origin;
            Descriptor.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        }
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Nova fragment registration failed: %s"), *Error);
    }
}
