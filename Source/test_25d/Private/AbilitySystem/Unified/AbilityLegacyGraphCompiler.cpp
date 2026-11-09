// 调用链：原 GA 输入/Commit -> 编译快照 -> StartWithInput -> 世界持有 -> 节点 Driver。
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "Misc/SecureHash.h"
#include "Engine/World.h"

namespace
{
    /** 节点身份只取源路径、深度和单发标记；不以进程地址、数组序号或本次随机 GUID 定义资产。 */
    FGuid NodeIdentity(const UAbilityPrototype& Source, int32 Depth, bool bSingle)
    {
        const FString Key = FString::Printf(TEXT("%s|%d|%d"), *Source.GetPathName(), Depth, bSingle);
        const FString Hash = FMD5::HashAnsiString(*Key);
        FGuid Result;
        FGuid::ParseExact(Hash, EGuidFormats::Digits, Result);
        return Result;
    }

    /** Freeze all reachable parameter references before unfolding execution depth. Cycles share one clone;
     * required links beyond depth three remain available to parameter validation but cannot create graph calls. */
    bool FreezeParameters(UAbilityGraphDefinition& Graph, UAbilityPrototype* Root,
                          TMap<const UAbilityPrototype*, UAbilityPrototype*>& Frozen, FString& Error)
    {
        TArray<const UAbilityPrototype*> Pending;
        auto Clone = [&](const UAbilityPrototype* Source) -> UAbilityPrototype*
        {
            if (!Source)
            {
                return nullptr;
            }
            if (auto* Existing = Frozen.Find(Source))
            {
                return *Existing;
            }
            // Configuration traversal is iterative and bounded independently from the old gameplay spawn budget.
            if (Frozen.Num() >= 4096)
            {
                Error = TEXT("Legacy parameter snapshot exceeds 4096 distinct configuration objects.");
                return nullptr;
            }
            const FName Name = MakeUniqueObjectName(&Graph, Source->GetClass(), Source->GetFName());
            auto* Copy = DuplicateObject<UAbilityPrototype>(Source, &Graph, Name);
            Frozen.Add(Source, Copy);
            Pending.Add(Source);
            return Copy;
        };
        Clone(Root);
        for (int32 Index = 0; Index < Pending.Num() && Error.IsEmpty(); ++Index)
        {
            const UAbilityPrototype* Source = Pending[Index];
            UAbilityPrototype* Copy = Frozen.FindChecked(Source);
            Copy->OnHit = Clone(Source->OnHit);
            Copy->OnPeriod = Clone(Source->OnPeriod);
            Copy->OnExpire = Clone(Source->OnExpire);
            if (Source->OrbitSettings)
            {
                Copy->OrbitSettings = DuplicateObject<UOrbitingFireBoltConfig>(Source->OrbitSettings, Copy);
            }
            if (const auto* SourceExtension = Cast<UExtensionNode>(Source))
            {
                auto* CopyExtension = CastChecked<UExtensionNode>(Copy);
                CopyExtension->HitChild = Cast<UExtensionNode>(Clone(SourceExtension->HitChild));
                CopyExtension->PeriodChild = Cast<UExtensionNode>(Clone(SourceExtension->PeriodChild));
                CopyExtension->CompleteChild = Cast<UExtensionNode>(Clone(SourceExtension->CompleteChild));
            }
        }
        return Error.IsEmpty();
    }

    /** 配置引用图只遍历有限的深度；同配置、同深度共享定义但每次调用仍有独立运行状态。 */
    FGuid AddNode(UAbilityGraphDefinition& Graph, UAbilityPrototype* Source, int32 Depth, bool bSingle,
                  TMap<FGuid, bool>& Added, const TMap<const UAbilityPrototype*, UAbilityPrototype*>& Frozen,
                  FString& Error)
    {
        if (!Source || Depth > 3)
        {
            return {};
        }
        const FGuid Id = NodeIdentity(*Source, Depth, bSingle);
        if (Added.Contains(Id))
        {
            return Id;
        }
        if (Added.Num() >= 512)
        {
            Error = TEXT("Expanded legacy configuration exceeds the 512 definition limit.");
            return {};
        }
        Added.Add(Id, true);
        auto* Config = NewObject<UAbilityLegacyNodeConfig>(&Graph);
        Config->Profile = Graph.CompatibilityProfile;
        Config->LegacyDepth = Depth;
        Config->bSingleProjectile = bSingle;
        Config->PresentationParameters = Source;
        Config->Parameters = DuplicateObject<UAbilityPrototype>(Frozen.FindChecked(Source), Config);
        if (!Config->Validate(Error))
        {
            Error = Source->GetPathName() + TEXT(": ") + Error;
            return {};
        }
        FAbilityGraphNode Node;
        Node.NodeId = Id;
        Node.TypeId = AbilityLegacyNodes::TypeId(*Source, Graph.CompatibilityProfile);
        Node.Config = Config;
        Graph.Nodes.Add(Node);
        auto Connect = [&](FName Port, UAbilityPrototype* Child, int32 ChildDepth, bool bChildSingle)
        {
            if (!Child || ChildDepth > 3 || !Error.IsEmpty())
            {
                return;
            }
            const FGuid ChildId = AddNode(Graph, Child, ChildDepth, bChildSingle, Added, Frozen, Error);
            if (ChildId.IsValid())
            {
                FAbilityGraphEdge Edge;
                Edge.From = Id;
                Edge.To = ChildId;
                Edge.OutputPort = Port;
                Graph.Edges.Add(Edge);
            }
        };
        if (const auto* Extension = Cast<UExtensionNode>(Source))
        {
            Connect(TEXT("OnHit"), Extension->HitChild, Depth + 1, false);
            Connect(TEXT("OnPeriod"), Extension->PeriodChild, Depth + 1, false);
            Connect(TEXT("OnExpire"), Extension->CompleteChild, Depth + 1, false);
        }
        else if (Source->Kind == EAbilityPrototypeKind::Multishot && !bSingle)
        {
            // 扇射容器自身不发命中/到期，单弹共用原配置但跳过再次扇射，保持旧生成预算语义。
            Connect(TEXT("Multishot"), Source, Depth, true);
        }
        else
        {
            Connect(TEXT("OnHit"), Source->OnHit, Depth + 1, false);
            Connect(TEXT("OnPeriod"), Source->OnPeriod, Depth + 1, false);
            Connect(TEXT("OnExpire"), Source->OnExpire, Depth + 1, false);
        }
        return Id;
    }
} // namespace

UAbilityGraphDefinition* UAbilityLegacyGraphCompiler::Compile(UObject* Outer, UAbilityPrototype* Root, FName Profile,
                                                              FString& Error)
{
    Error.Reset();
    if (!Root || (Profile != TEXT("LegacyActor.v1") && Profile != TEXT("LegacyMixed.v1") &&
                  Profile != TEXT("LegacyExtension.v1")))
    {
        Error = TEXT("Legacy compilation requires a root and an explicit supported compatibility profile.");
        return nullptr;
    }
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = Profile;
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    Graph->Budget.MaxActiveNodes = 64;
    TMap<const UAbilityPrototype*, UAbilityPrototype*> Frozen;
    if (!FreezeParameters(*Graph, Root, Frozen, Error))
    {
        return nullptr;
    }
    TMap<FGuid, bool> Added;
    Graph->EntryNodeId = AddNode(*Graph, Root, 0, false, Added, Frozen, Error);
    if (!Error.IsEmpty())
    {
        return nullptr;
    }
    TArray<FString> Errors;
    if (!Graph->ValidateForAuthoring(Errors))
    {
        Error = FString::Join(Errors, TEXT("\n"));
        return nullptr;
    }
    return Graph;
}

UAbilityGraphDefinition* UAbilityLegacyGraphCompiler::CompileForAuthoring(UAbilityPrototype* Root, FName Profile,
                                                                          TArray<FString>& Errors)
{
    Errors.Reset();
    FString Error;
    auto* Graph = Compile(GetTransientPackage(), Root, Profile, Error);
    if (!Graph)
    {
        Errors.Add(Error);
    }
    return Graph;
}

UAbilityGraphInstance* UAbilityLegacyGraphCompiler::Execute(UAbilityGraphDefinition* Graph,
                                                            UPrototypeCastContext* Context,
                                                            const FAbilityNodeInput& Input, FString& Error)
{
    if (!Graph || !Context || !Context->CaptureSourceLife(Error))
    {
        return nullptr;
    }
    UWorld* World = Context->GetSourceLife().World.Get();
    auto* Owner = World ? World->GetSubsystem<UAbilityGraphWorldSubsystem>() : nullptr;
    if (!Owner)
    {
        Error = TEXT("Unified execution requires its owning world subsystem.");
        return nullptr;
    }
    auto* Instance = NewObject<UAbilityGraphInstance>(World);
    FAbilityExecutionOptions Options;
    Options.CompatibilityProfile = Graph->CompatibilityProfile;
    Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    if (!Instance->StartWithInput(Graph, Input, Options, Error) || !Owner->Adopt(Instance, Error))
    {
        Instance->Cancel(EAbilityGraphEndReason::Failed);
        return nullptr;
    }
    return Instance;
}
