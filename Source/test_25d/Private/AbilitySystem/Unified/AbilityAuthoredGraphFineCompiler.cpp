// 调用链：运行时/开发默认规范化 -> isolated fragment expansion；在真实RED后实现，尚不接全局出口。
#include "AbilitySystem/Unified/AbilityAuthoredGraphFineCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/Nodes/AbilityNormalizationNodes.h"
#include "Misc/SecureHash.h"

namespace
{
    FName AuthoredFineProfile(FName Profile)
    {
        if (Profile == TEXT("LegacyActor.v1"))
        {
            return TEXT("PrototypeFine.Actor.v1");
        }
        if (Profile == TEXT("LegacyMixed.v1"))
        {
            return TEXT("PrototypeFine.Mixed.v1");
        }
        if (Profile == TEXT("LegacyExtension.v1"))
        {
            return TEXT("PrototypeFine.Extension.v1");
        }
        return NAME_None;
    }

    FGuid AuthoredBridgeIdentity(FGuid Node, FName Port, FName Kind)
    {
        FGuid Result;
        const auto Key =
            Node.ToString(EGuidFormats::Digits) + TEXT("|") + Port.ToString() + TEXT("|") + Kind.ToString();
        FGuid::ParseExact(FMD5::HashAnsiString(*Key), EGuidFormats::Digits, Result);
        return Result;
    }

    void ConnectAuthored(UAbilityGraphDefinition& Graph, FAbilityFineFragmentPort From, FGuid To)
    {
        FAbilityGraphEdge Edge;
        Edge.From = From.Node;
        Edge.OutputPort = From.Port;
        Edge.To = To;
        Graph.Edges.Add(Edge);
    }
} // namespace

UAbilityGraphDefinition* AbilityAuthoredGraphFineCompiler::Compile(UObject* Outer,
                                                                   const UAbilityGraphDefinition* Authored,
                                                                   FString& Error)
{
    Error.Reset();
    if (!Authored)
    {
        Error = TEXT("Authored expansion requires a real definition.");
        return nullptr;
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    TArray<FString> Errors;
    if (!Authored->Validate(Registry, Errors))
    {
        Error = FString::Join(Errors, TEXT("; "));
        return nullptr;
    }
    const bool bHasLegacy = Authored->Nodes.ContainsByPredicate(
        [](const auto& Node)
        {
            return Node.Config->IsA<UAbilityLegacyNodeConfig>();
        });
    if (!bHasLegacy)
    {
        return DuplicateObject(Authored, Outer ? Outer : GetTransientPackage());
    }
    auto* Result = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Result->SchemaVersion = 2;
    Result->Budget = Authored->Budget;
    Result->PrototypeHelperBudget = Authored->PrototypeHelperBudget;
    Result->CancelScope = Authored->CancelScope;
    Result->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
    const int32 RootIndex = Authored->Nodes.IndexOfByPredicate(
        [&](const auto& Node)
        {
            return Node.NodeId == Authored->EntryNodeId;
        });
    const auto* RootLegacy =
        RootIndex != INDEX_NONE ? Cast<UAbilityLegacyNodeConfig>(Authored->Nodes[RootIndex].Config) : nullptr;
    Result->CompatibilityProfile =
        AuthoredFineProfile(RootLegacy ? RootLegacy->Profile : Authored->CompatibilityProfile);
    if (Result->CompatibilityProfile.IsNone())
    {
        Error = TEXT("No explicit fine compatibility contract exists for this authored root profile.");
        return nullptr;
    }
    Result->OriginalProjection.bEnabled = true;
    Result->OriginalProjection.SchemaVersion = Authored->SchemaVersion;
    Result->OriginalProjection.CompatibilityProfile = Authored->CompatibilityProfile;
    Result->OriginalProjection.BudgetPolicy = Authored->BudgetPolicy;
    Result->OriginalProjection.EntryIndex = RootIndex;
    for (const auto& Node : Authored->Nodes)
    {
        FAbilityOriginalNodeDefinition Definition;
        Definition.TypeId = Node.TypeId;
        if (const auto* Legacy = Cast<UAbilityLegacyNodeConfig>(Node.Config))
        {
            Definition.bLegacyMacro = true;
            Definition.LegacyProfile = Legacy->Profile;
        }
        Result->OriginalProjection.Definitions.Add(Definition);
    }
    for (const auto& Edge : Authored->Edges)
    {
        FAbilityOriginalEdgeDefinition Definition;
        Definition.FromIndex = Authored->Nodes.IndexOfByPredicate(
            [&](const auto& Node)
            {
                return Node.NodeId == Edge.From;
            });
        Definition.ToIndex = Authored->Nodes.IndexOfByPredicate(
            [&](const auto& Node)
            {
                return Node.NodeId == Edge.To;
            });
        Definition.OutputPort = Edge.OutputPort;
        Result->OriginalProjection.Edges.Add(Definition);
    }
    TMap<FGuid, FAbilityFineFragmentBindings> Bindings;
    for (int32 OriginalIndex = 0; OriginalIndex < Authored->Nodes.Num(); ++OriginalIndex)
    {
        const auto& Node = Authored->Nodes[OriginalIndex];
        FAbilityFineFragmentBindings Binding;
        if (const auto* Legacy = Cast<UAbilityLegacyNodeConfig>(Node.Config))
        {
            const int32 First = Result->Nodes.Num();
            FAbilityFineFragmentSource Source;
            Source.Parameters = Legacy->Parameters;
            Source.Presentation = Legacy->PresentationParameters;
            Source.Profile = Legacy->Profile;
            Source.LogicalDepth = Legacy->LegacyDepth;
            Source.bSingleProjectile = Legacy->bSingleProjectile;
            Source.StableNamespace = Node.NodeId;
            if (!AbilityPrototypeFineCompiler::AppendIsolated(*Result, Source, Binding, Error))
            {
                return nullptr;
            }
            const auto* Entry = Result->Nodes.FindByPredicate(
                [&](const auto& Candidate)
                {
                    return Candidate.NodeId == Binding.Entry;
                });
            for (int32 Index = First; Index < Result->Nodes.Num(); ++Index)
            {
                auto& Expanded = Result->Nodes[Index];
                Expanded.OriginalIndex = OriginalIndex;
                Expanded.OriginalRole = Expanded.NodeId == Binding.Entry ? EAbilityOriginalNodeRole::Entry
                                                                         : EAbilityOriginalNodeRole::Helper;
                FString ProofError;
                if (Entry &&
                    Entry->Config->SupportsOriginalLifetime(Node.TypeId, Legacy->Profile, Expanded.TypeId, ProofError))
                {
                    Expanded.OriginalRole = EAbilityOriginalNodeRole::Lifetime;
                }
            }
        }
        else
        {
            const auto* Descriptor = Registry.Find(Node.TypeId);
            if (!Descriptor || !Descriptor->bPrototypeCompatibilitySafe)
            {
                Error =
                    TEXT("Authored extra node has no verified fine compatibility contract: ") + Node.TypeId.ToString();
                return nullptr;
            }
            auto Copy = Node;
            Copy.Config = DuplicateObject(Node.Config.Get(), Result);
            Copy.OriginalIndex = OriginalIndex;
            Copy.OriginalRole = EAbilityOriginalNodeRole::EntryAndLifetime;
            Copy.OriginalOutputs.Reset();
            for (const auto& Port : Descriptor->Ports)
            {
                if (Port.Name != TEXT("Failed") && Port.Name != TEXT("Cancelled"))
                {
                    FAbilityOriginalOutputMapping Mapping;
                    Mapping.OutputPort = Mapping.OriginalPort = Port.Name;
                    Copy.OriginalOutputs.Add(Mapping);
                }
            }
            Result->Nodes.Add(Copy);
            Binding.Entry = Copy.NodeId;
            for (const auto& Port : Descriptor->Ports)
            {
                Binding.Outputs.FindOrAdd(Port.Name).Add({Copy.NodeId, Port.Name, Port.Payload});
            }
        }
        // State.Completed 同时承担旧到期/完成时，以两个有序事件保持实际额外边顺序。
        auto* Expiry = Binding.Outputs.Find(TEXT("OnExpire"));
        auto* Completion = Binding.Outputs.Find(TEXT("Completed"));
        const bool bNeedsBoth = Authored->Edges.ContainsByPredicate(
                                    [&](const auto& Edge)
                                    {
                                        return Edge.From == Node.NodeId && Edge.OutputPort == TEXT("OnExpire");
                                    }) &&
                                (Node.Config->IsA<UAbilityLegacyNodeConfig>() ||
                                 Authored->Edges.ContainsByPredicate(
                                     [&](const auto& Edge)
                                     {
                                         return Edge.From == Node.NodeId && Edge.OutputPort == TEXT("Completed");
                                     }));
        if (bNeedsBoth && Expiry && Completion)
        {
            for (auto& ExpiredPort : *Expiry)
            {
                auto* CompletedPort = Completion->FindByPredicate(
                    [&](const auto& Candidate)
                    {
                        return Candidate.Node == ExpiredPort.Node && Candidate.Port == ExpiredPort.Port;
                    });
                if (!CompletedPort)
                {
                    continue;
                }
                const auto Source = ExpiredPort;
                FAbilityGraphNode Bridge;
                Bridge.NodeId = AuthoredBridgeIdentity(Node.NodeId, Source.Port, TEXT("Ordered"));
                Bridge.TypeId = TEXT("Event.OrderedCompletion");
                Bridge.Config = NewObject<UAbilityOrderedCompletionNodeConfig>(Result);
                Bridge.OriginalIndex = OriginalIndex;
                Result->Nodes.Add(Bridge);
                ConnectAuthored(*Result, Source, Bridge.NodeId);
                ExpiredPort = {Bridge.NodeId, TEXT("Expired"), EAbilityPortPayload::Origin};
                *CompletedPort = {Bridge.NodeId, TEXT("Completed"), EAbilityPortPayload::Origin};
            }
        }
        const bool bHasLegacyCompletion =
            Node.Config->IsA<UAbilityLegacyNodeConfig>() && Completion && !Completion->IsEmpty();
        if (bHasLegacyCompletion)
        {
            // 只有原实际图的Legacy.Completed恢复原输入；状态默认Completed/OnExpire仍可携带当前终点。
            FAbilityGraphNode Adapter;
            Adapter.NodeId = AuthoredBridgeIdentity(Node.NodeId, TEXT("Completed"), TEXT("OriginalInput"));
            Adapter.TypeId = TEXT("Event.PrototypeCompletedInput");
            Adapter.Config = NewObject<UAbilityPrototypeCompletedInputNodeConfig>(Result);
            Adapter.OriginalIndex = OriginalIndex;
            FAbilityOriginalOutputMapping Mapping;
            Mapping.OutputPort = Mapping.OriginalPort = TEXT("Completed");
            Mapping.Publication = EAbilityOriginalPublication::Publish;
            Adapter.OriginalOutputs.Add(Mapping);
            Result->Nodes.Add(Adapter);
            for (const auto& Port : *Completion)
            {
                ConnectAuthored(*Result, Port, Adapter.NodeId);
            }
            *Completion = {{Adapter.NodeId, TEXT("Completed"), EAbilityPortPayload::Origin}};
        }
        Bindings.Add(Node.NodeId, MoveTemp(Binding));
    }
    if (const auto* Entry = Bindings.Find(Authored->EntryNodeId))
    {
        Result->EntryNodeId = Entry->Entry;
    }
    TMap<FGuid, FAbilityFineFragmentPort> ImpactAdapters;
    TMap<FString, FAbilityFineFragmentPort> OriginalEndpoints;
    for (const auto& Edge : Authored->Edges)
    {
        const auto* Source = Bindings.Find(Edge.From);
        const auto* Target = Bindings.Find(Edge.To);
        const auto* Ports = Source ? Source->Outputs.Find(Edge.OutputPort) : nullptr;
        if (!Target || !Ports || Ports->IsEmpty())
        {
            Error = TEXT("Actual authored edge has no verified fine output binding: ") + Edge.OutputPort.ToString();
            return nullptr;
        }
        const auto* OriginalNode = Authored->Nodes.FindByPredicate(
            [&](const auto& Node)
            {
                return Node.NodeId == Edge.From;
            });
        const auto* OriginalDescriptor = OriginalNode ? Registry.Find(OriginalNode->TypeId) : nullptr;
        const auto* OriginalPort = OriginalDescriptor ? OriginalDescriptor->Ports.FindByPredicate(
                                                            [&](const auto& Port)
                                                            {
                                                                return Port.Name == Edge.OutputPort;
                                                            })
                                                      : nullptr;
        if (!OriginalPort)
        {
            Error = TEXT("Original edge output descriptor disappeared.");
            return nullptr;
        }
        const int32 OriginalIndex = Authored->Nodes.IndexOfByPredicate(
            [&](const auto& Node)
            {
                return Node.NodeId == Edge.From;
            });
        const bool bLegacySource = OriginalNode->Config->IsA<UAbilityLegacyNodeConfig>();
        const FString EndpointKey = FString::Printf(TEXT("%d:%s"), OriginalIndex, *Edge.OutputPort.ToString());
        if (bLegacySource && Edge.OutputPort != TEXT("Completed"))
        {
            if (const auto* Existing = OriginalEndpoints.Find(EndpointKey))
            {
                ConnectAuthored(*Result, *Existing, Target->Entry);
                continue;
            }
        }
        FAbilityFineFragmentPort Endpoint;
        if (bLegacySource && Edge.OutputPort != TEXT("Completed"))
        {
            FAbilityGraphNode Adapter;
            Adapter.NodeId = AuthoredBridgeIdentity(Edge.From, Edge.OutputPort, TEXT("OriginalPort"));
            Adapter.TypeId = AbilityNormalizationNodes::OriginalPortType(OriginalPort->Payload);
            auto* Config = NewObject<UAbilityOriginalPortNodeConfig>(Result);
            Config->Payload = OriginalPort->Payload;
            Adapter.Config = Config;
            Adapter.OriginalIndex = OriginalIndex;
            FAbilityOriginalOutputMapping Mapping;
            Mapping.OutputPort = TEXT("Forward");
            Mapping.OriginalPort = Edge.OutputPort;
            Mapping.Publication = EAbilityOriginalPublication::LegacyChildRequest;
            Adapter.OriginalOutputs.Add(Mapping);
            Endpoint = {Adapter.NodeId, TEXT("Forward"), OriginalPort->Payload};
            Result->Nodes.Add(Adapter);
            OriginalEndpoints.Add(EndpointKey, Endpoint);
        }
        for (auto Port : *Ports)
        {
            if (Port.Payload == EAbilityPortPayload::DamageResult &&
                OriginalPort->Payload == EAbilityPortPayload::ConfirmedImpact)
            {
                const FGuid Id = AuthoredBridgeIdentity(Port.Node, Port.Port, TEXT("ImpactFact"));
                if (const auto* Existing = ImpactAdapters.Find(Id))
                {
                    Port = *Existing;
                }
                else
                {
                    FAbilityGraphNode Adapter;
                    Adapter.NodeId = Id;
                    Adapter.TypeId = TEXT("Event.ImpactFact");
                    Adapter.Config = NewObject<UAbilityImpactFactNodeConfig>(Result);
                    Adapter.OriginalIndex = OriginalIndex;
                    Result->Nodes.Add(Adapter);
                    ConnectAuthored(*Result, Port, Adapter.NodeId);
                    Port = {Adapter.NodeId, TEXT("Completed"), EAbilityPortPayload::ConfirmedImpact};
                    ImpactAdapters.Add(Id, Port);
                }
            }
            ConnectAuthored(*Result, Port, Endpoint.Node.IsValid() ? Endpoint.Node : Target->Entry);
        }
        if (Endpoint.Node.IsValid())
        {
            ConnectAuthored(*Result, Endpoint, Target->Entry);
        }
    }
    Errors.Reset();
    if (!Result->Validate(Registry, Errors))
    {
        Error = FString::Join(Errors, TEXT("; "));
        return nullptr;
    }
    return Result;
}
