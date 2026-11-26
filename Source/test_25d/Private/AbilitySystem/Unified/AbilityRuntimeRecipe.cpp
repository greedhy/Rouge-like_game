// 调用链：配方组件 -> 只读编译/摘要 -> 冻结的本次图；编译器没有游戏 Tick 或伤害权限。
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityGraphPassThroughNodes.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityAuthoredGraphFineCompiler.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityRuntimePayloadPolicy.h"
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityLightningFragments.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionAbility.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "PureMass/PureMassAbility.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace
{
    /** 文本采用长度前缀，避免字段/数组边界不同却串联成相同摘要输入。 */
    FString Framed(const FString& Value)
    {
        return FString::FromInt(Value.Len()) + TEXT(":") + Value;
    }

    /** 新增/替换的执行策略独立证明有限安全；旧正式基图不追溯改变行为。 */
    bool ValidateRuntimeAddition(const FAbilityNodeRegistry& Registry, FName Type, FString& Error)
    {
        const auto* Descriptor = Registry.Find(Type);
        if (!Descriptor || !Descriptor->bRuntimeFiniteSafe)
        {
            Error = TEXT("Runtime additions require an audited finite action contract: ") + Type.ToString();
            return false;
        }
        return true;
    }

    /** 仅计算配置值；瞬态对象名、节点 GUID 与内存地址不参与配方内容身份。 */
    class FRecipeFingerprint
    {
    public:
        explicit FRecipeFingerprint(const UAbilityGraphDefinition& InGraph) : Graph(InGraph)
        {
        }

        /** 保留节点/边声明顺序，因为同时触发的动作仍按该顺序入队；GUID 只用于解析拓扑。 */
        FString Build()
        {
            FString Result =
                FString::Printf(TEXT("Schema=%d|Profile=%s|Policy=%d|Cancel=%d|"), Graph.SchemaVersion,
                                *Graph.CompatibilityProfile.ToString(), static_cast<int32>(Graph.BudgetPolicy),
                                static_cast<int32>(Graph.CancelScope));
            Result +=
                FString::Printf(TEXT("Budget=%d,%d,%d,%d,%d|"), Graph.Budget.MaxInvocations, Graph.Budget.MaxEvents,
                                Graph.Budget.MaxActiveNodes, Graph.Budget.MaxDepth, Graph.Budget.MaxEventsPerTick);
            Result +=
                FString::Printf(TEXT("Sustained=%d,%d,%d|"), Graph.SustainedBudget.InvocationStartsPerSecond,
                                Graph.SustainedBudget.PhysicalStartsPerSecond, Graph.SustainedBudget.MaxActivePhysical);
            Result += FString::Printf(
                TEXT("PrototypeHelpers=%d,%d,%d,%d,%d|"), Graph.PrototypeHelperBudget.MaxRetainedInvocations,
                Graph.PrototypeHelperBudget.MaxActiveNodes, Graph.PrototypeHelperBudget.MaxEventsPerTick,
                Graph.PrototypeHelperBudget.MaxActivePhysical, Graph.PrototypeHelperBudget.PhysicalStartsPerSecond);
            const auto& Original = Graph.OriginalProjection;
            Result += FString::Printf(TEXT("OriginalProjection=%d|"), Original.bEnabled);
            if (Original.bEnabled)
            {
                Result += FString::Printf(TEXT("%d|%s|%d|%d|"), Original.SchemaVersion,
                                          *Original.CompatibilityProfile.ToString(),
                                          static_cast<int32>(Original.BudgetPolicy), Original.EntryIndex);
                for (const auto& Definition : Original.Definitions)
                {
                    Result += Framed(Definition.TypeId.ToString()) + Framed(Definition.LegacyProfile.ToString()) +
                              FString::Printf(TEXT("%d|"), Definition.bLegacyMacro);
                }
                for (const auto& Edge : Original.Edges)
                {
                    Result += FString::Printf(TEXT("OriginalEdge=%d>%d|"), Edge.FromIndex, Edge.ToIndex) +
                              Framed(Edge.OutputPort.ToString());
                }
            }
            Result += FString::Printf(TEXT("RuntimeFinite=%d|"), Graph.bHasRuntimeFinite);
            if (Graph.bHasRuntimeFinite)
            {
                const auto& Finite = Graph.RuntimeFiniteBudget;
                Result += FString::Printf(TEXT("%d,%d,%d,%d,%d|"), Finite.MaxInvocations, Finite.MaxEvents,
                                          Finite.MaxActiveNodes, Finite.MaxDepth, Finite.MaxEventsPerTick);
            }
            TMap<FGuid, int32> Indices;
            for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
            {
                Indices.Add(Graph.Nodes[Index].NodeId, Index);
                Result += Framed(Graph.Nodes[Index].TypeId.ToString()) + Object(Graph.Nodes[Index].Config);
                Result += FString::Printf(TEXT("Original=%d,%d|"), Graph.Nodes[Index].OriginalIndex,
                                          static_cast<int32>(Graph.Nodes[Index].OriginalRole));
                Result += FString::Printf(TEXT("FiniteNode=%d|"), Graph.Nodes[Index].bRuntimeFinite);
                Result += FString::Printf(TEXT("RepeatedPayload=%d|"), Graph.Nodes[Index].bRuntimeRepeatedPayload);
                for (const auto& Mapping : Graph.Nodes[Index].OriginalOutputs)
                {
                    Result += Framed(Mapping.OutputPort.ToString()) + Framed(Mapping.OriginalPort.ToString()) +
                              FString::Printf(TEXT("%d|"), static_cast<int32>(Mapping.Publication));
                }
            }
            const int32* Entry = Indices.Find(Graph.EntryNodeId);
            Result += FString::Printf(TEXT("Entry=%d|"), Entry ? *Entry : INDEX_NONE);
            for (const auto& Edge : Graph.Edges)
            {
                const int32* From = Indices.Find(Edge.From);
                const int32* To = Indices.Find(Edge.To);
                Result += FString::Printf(TEXT("%d>%d|"), From ? *From : INDEX_NONE, To ? *To : INDEX_NONE);
                Result += Framed(Edge.OutputPort.ToString());
            }
            return FMD5::HashAnsiString(*Result);
        }

    private:
        /** 配置对象递归按字段名序列化；稳定资源/类只保存资源路径，引用环使用遍历序号。 */
        FString Object(const UObject* Value)
        {
            if (!Value)
            {
                return TEXT("null;");
            }
            const bool bPersistentAsset =
                Value->IsAsset() && !Value->HasAnyFlags(RF_Transient) && Value->GetOutermost() != GetTransientPackage();
            // StableReference也可能是服务器本次生成的数据对象；临时世界/对象名不构成配方身份。
            // 只有真正持久资源/类按路径，临时参数和引用环继续按字段值/Seen序号计算。
            if (!Value->IsIn(&Graph) && !Value->IsA<UAbilityNodeConfig>() && (Value->IsA<UClass>() || bPersistentAsset))
            {
                return TEXT("asset;") + Framed(Value->GetPathName());
            }
            if (const int32* Existing = Seen.Find(Value))
            {
                return FString::Printf(TEXT("ref;%d;"), *Existing);
            }
            Seen.Add(Value, Seen.Num());
            return Framed(Value->GetClass()->GetPathName()) + Fields(Value->GetClass(), Value);
        }

        /** 反射仅用于只读内容摘要，不用于接收任意字段名或执行动态修改。 */
        FString Fields(const UStruct* Type, const void* Data)
        {
            TArray<const FProperty*> Properties;
            for (TFieldIterator<FProperty> It(Type); It; ++It)
            {
                // 普通UPROPERTY让封印跟随任意Duplicate；即使将来某配置引用瞬态子图，也不能自哈希封印。
                if (It->GetOwnerStruct() == UAbilityGraphDefinition::StaticClass() &&
                    It->GetFName() == TEXT("RuntimeCompilationSeal"))
                {
                    continue;
                }
                if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_EditorOnly))
                {
                    Properties.Add(*It);
                }
            }
            Properties.Sort(
                [](const FProperty& A, const FProperty& B)
                {
                    return A.GetName() < B.GetName();
                });
            FString Result;
            for (const FProperty* Property : Properties)
            {
                Result += Framed(Property->GetName());
                for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
                {
                    Result += Value(Property, Property->ContainerPtrToValuePtr<void>(Data, Index));
                }
            }
            return Result;
        }

        /** 集合/映射按值排序，避免哈希容器遍历顺序让同一配方产生不同摘要。 */
        FString Value(const FProperty* Property, const void* Data)
        {
            if (const auto* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            {
                return Object(ObjectProperty->GetObjectPropertyValue(Data));
            }
            if (const auto* StructProperty = CastField<FStructProperty>(Property))
            {
                return Framed(StructProperty->Struct->GetPathName()) + Fields(StructProperty->Struct, Data);
            }
            if (const auto* ArrayProperty = CastField<FArrayProperty>(Property))
            {
                FScriptArrayHelper Array(ArrayProperty, Data);
                FString Result = FString::Printf(TEXT("array;%d;"), Array.Num());
                for (int32 Index = 0; Index < Array.Num(); ++Index)
                {
                    Result += Framed(Value(ArrayProperty->Inner, Array.GetRawPtr(Index)));
                }
                return Result;
            }
            if (const auto* SetProperty = CastField<FSetProperty>(Property))
            {
                FScriptSetHelper Set(SetProperty, Data);
                TArray<FString> Items;
                for (int32 Index = 0; Index < Set.GetMaxIndex(); ++Index)
                {
                    if (Set.IsValidIndex(Index))
                    {
                        Items.Add(Value(SetProperty->ElementProp, Set.GetElementPtr(Index)));
                    }
                }
                Items.Sort();
                return TEXT("set;") + Framed(FString::Join(Items, TEXT(";")));
            }
            if (const auto* MapProperty = CastField<FMapProperty>(Property))
            {
                FScriptMapHelper Map(MapProperty, Data);
                TArray<FString> Items;
                for (int32 Index = 0; Index < Map.GetMaxIndex(); ++Index)
                {
                    if (Map.IsValidIndex(Index))
                    {
                        Items.Add(Framed(Value(MapProperty->KeyProp, Map.GetKeyPtr(Index))) +
                                  Framed(Value(MapProperty->ValueProp, Map.GetValuePtr(Index))));
                    }
                }
                Items.Sort();
                return TEXT("map;") + Framed(FString::Join(Items, TEXT(";")));
            }
            FString Text;
            Property->ExportTextItem_Direct(Text, Data, nullptr, nullptr, PPF_None);
            return Framed(Text);
        }

        const UAbilityGraphDefinition& Graph;
        TMap<const UObject*, int32> Seen;
    };

    /** 缩放操作只访问明确声明支持的配置类型；输入 DamageParams 不在图配置中，不能假装已修改。 */
    bool ScaleNode(UAbilityNodeConfig& Config, EAbilityRecipePatchOperation Operation, float Scale)
    {
        const auto Parameter = Operation == EAbilityRecipePatchOperation::DamageScale ? EAbilityConfigScalar::Damage
                               : Operation == EAbilityRecipePatchOperation::RadiusScale
                                   ? EAbilityConfigScalar::Radius
                                   : EAbilityConfigScalar::Duration;
        if (Config.TryScaleRecipeParameter(Parameter, Scale))
        {
            return true;
        }
        if (auto* Legacy = Cast<UAbilityLegacyNodeConfig>(&Config))
        {
            if (!Legacy->Parameters)
            {
                return false;
            }
            if (Operation == EAbilityRecipePatchOperation::DamageScale)
            {
                Legacy->Parameters->Damage *= Scale;
                Legacy->Parameters->DamagePerLevel *= Scale;
                return true;
            }
            if (Operation == EAbilityRecipePatchOperation::RadiusScale)
            {
                Legacy->Parameters->Radius *= Scale;
                return true;
            }
            if (Operation == EAbilityRecipePatchOperation::DurationScale)
            {
                Legacy->Parameters->Duration *= Scale;
                return true;
            }
        }
        if (Operation == EAbilityRecipePatchOperation::RadiusScale)
        {
            if (auto* Nova = Cast<UAbilityNovaNodeConfig>(&Config))
            {
                Nova->Radius *= Scale;
                return true;
            }
            if (auto* Shards = Cast<UAbilityArcaneShardNodeConfig>(&Config))
            {
                Shards->Radius *= Scale;
                return true;
            }
            if (auto* Area = Cast<UAbilityAreaPetrifyNodeConfig>(&Config))
            {
                Area->Radius *= Scale;
                return true;
            }
            if (auto* Fork = Cast<UAbilityForkedChainNodeConfig>(&Config))
            {
                Fork->JumpRadius *= Scale;
                return true;
            }
            if (auto* Lightning = Cast<UAbilityLightningNodeConfig>(&Config))
            {
                Lightning->JumpRadius *= Scale;
                return true;
            }
        }
        if (Operation == EAbilityRecipePatchOperation::DurationScale)
        {
            if (auto* Delay = Cast<UAbilityDelayNodeConfig>(&Config))
            {
                Delay->DelaySeconds *= Scale;
                return true;
            }
            if (auto* Petrify = Cast<UAbilityPetrifyNodeConfig>(&Config))
            {
                Petrify->DurationSeconds *= Scale;
                return true;
            }
            if (auto* Area = Cast<UAbilityAreaPetrifyNodeConfig>(&Config))
            {
                Area->Duration *= Scale;
                return true;
            }
        }
        return false;
    }

    /** 匹配目标时只读取当前已接受拓扑；无选择器明确代表入口而非全图。 */
    TArray<FGuid> SelectNodes(const UAbilityGraphDefinition& Graph, const FAbilityRuntimeRecipePatch& Patch)
    {
        TArray<FGuid> Result;
        for (const auto& Node : Graph.Nodes)
        {
            const bool bIdMatches = !Patch.TargetNodeId.IsValid() || Patch.TargetNodeId == Node.NodeId;
            const bool bTypeMatches = Patch.TargetTypeId.IsNone() || Patch.TargetTypeId == Node.TypeId;
            const bool bEntryMatches =
                Patch.TargetNodeId.IsValid() || !Patch.TargetTypeId.IsNone() || Graph.EntryNodeId == Node.NodeId;
            if (bIdMatches && bTypeMatches && bEntryMatches)
            {
                Result.Add(Node.NodeId);
            }
        }
        return Result;
    }

    /** 片段的节点身份按来源/补丁/附着点分区，避免不同追加复制了同一个 GUID。 */
    FGuid BranchNodeId(FName Source, int32 PatchIndex, FGuid Target, FGuid Original)
    {
        const FString Key = FString::Printf(TEXT("%s|%d|%s|%s"), *Source.ToString(), PatchIndex, *Target.ToString(),
                                            *Original.ToString());
        FGuid Id;
        FGuid::ParseExact(FMD5::HashAnsiString(*Key), EGuidFormats::Digits, Id);
        return Id;
    }

    /** 纯分支逐边证明，不从“同步”或持续Profile推导权限；共享尾部同时检查全部实际入边类型。 */
    bool ValidateRepeatedBranch(const UAbilityGraphDefinition& Branch, EAbilityPortPayload Incoming,
                                const FAbilityNodeRegistry& Registry, FString& Error)
    {
        TArray<TPair<FGuid, EAbilityPortPayload>> Pending;
        Pending.Emplace(Branch.EntryNodeId, Incoming);
        TMap<FGuid, TSet<EAbilityPortPayload>> Checked;
        for (int32 Index = 0; Index < Pending.Num(); ++Index)
        {
            const auto Current = Pending[Index];
            auto& Inputs = Checked.FindOrAdd(Current.Key);
            if (Inputs.Contains(Current.Value))
            {
                continue;
            }
            Inputs.Add(Current.Value);
            const auto* Node = Branch.Nodes.FindByPredicate(
                [&](const FAbilityGraphNode& Item)
                {
                    return Item.NodeId == Current.Key;
                });
            if (!Node || !AbilityRuntimePayloadPolicy::ValidatePureNode(*Node, Current.Value, Registry, Error))
            {
                return false;
            }
            const auto* Descriptor = Registry.Find(Node->TypeId);
            for (const auto& Edge : Branch.Edges)
            {
                if (Edge.From != Current.Key)
                {
                    continue;
                }
                const auto* Port = Descriptor->Ports.FindByPredicate(
                    [&](const FAbilityPortDescriptor& Item)
                    {
                        return Item.Name == Edge.OutputPort;
                    });
                if (!Port)
                {
                    Error = TEXT("Repeated payload branch contains an undeclared output port.");
                    return false;
                }
                Pending.Emplace(Edge.To, Port->Payload);
            }
        }
        if (Checked.Num() != Branch.Nodes.Num())
        {
            Error = TEXT("Every repeated runtime payload node must be reachable and have a typed input proof.");
            return false;
        }
        return true;
    }

    /** 在宿主下复制片段再重映射所有端点；原片段/配置仍然只读。 */
    bool AppendBranch(UAbilityGraphDefinition& Graph, const FAbilityRuntimeRecipePatch& Patch, FName Source,
                      int32 PatchIndex, FGuid Target, const FAbilityNodeRegistry& Registry, FString& Error)
    {
        if (!IsValid(Patch.BranchGraph) || Patch.OutputPort.IsNone() || Patch.BranchGraph->SchemaVersion != 2)
        {
            Error = TEXT("AppendBranch requires a Schema2 fragment and a declared output port.");
            return false;
        }
        TArray<FString> Validation;
        if (!Patch.BranchGraph->Validate(Registry, Validation))
        {
            Error = FString::Join(Validation, TEXT("; "));
            return false;
        }
        auto* Copy = DuplicateObject<UAbilityGraphDefinition>(Patch.BranchGraph, &Graph);
        for (const auto& Node : Copy->Nodes)
        {
            // Bounded宿主也必须检查；一条原始Actor策略能在一次调用中绕开64次/32并发节点预算。
            if (!ValidateRuntimeAddition(Registry, Node.TypeId, Error))
            {
                return false;
            }
        }
        const auto* SourceNode = Graph.Nodes.FindByPredicate(
            [Target](const FAbilityGraphNode& Item)
            {
                return Item.NodeId == Target;
            });
        const auto* SourceDescriptor = SourceNode ? Registry.Find(SourceNode->TypeId) : nullptr;
        const auto* SourcePort = SourceDescriptor ? SourceDescriptor->Ports.FindByPredicate(
                                                        [&](const FAbilityPortDescriptor& Item)
                                                        {
                                                            return Item.Name == Patch.OutputPort;
                                                        })
                                                  : nullptr;
        if (!SourceNode || !SourcePort)
        {
            Error = TEXT("AppendBranch requires an existing typed source port.");
            return false;
        }
        // 精确插入先证明边与保留载荷的适配器合同；宿主原图/预算保持原样。
        const bool bInsert = Patch.Operation == EAbilityRecipePatchOperation::InsertOnEdge;
        int32 EdgeIndex = INDEX_NONE;
        if (bInsert)
        {
            EdgeIndex = Graph.Edges.IndexOfByPredicate(
                [&](const FAbilityGraphEdge& Edge)
                {
                    return Edge.From == Target && Edge.OutputPort == Patch.OutputPort && Edge.To == Patch.TargetEdgeTo;
                });
            if (Patch.TargetNodeId != Target || EdgeIndex == INDEX_NONE)
            {
                Error = TEXT("InsertOnEdge requires one existing exact (source node, port, destination) edge.");
                return false;
            }
            const auto* Destination = Graph.Nodes.FindByPredicate(
                [&](const auto& N)
                {
                    return N.NodeId == Patch.TargetEdgeTo;
                });
            if (Graph.OriginalProjection.bEnabled &&
                (SourceNode->OriginalIndex != INDEX_NONE || (Destination && Destination->OriginalIndex != INDEX_NONE)))
            {
                Error = TEXT("Inserting on a projected original edge requires explicit normalizer remapping.");
                return false;
            }
            const auto* Adapter = Copy->Nodes.FindByPredicate(
                [&](const auto& N)
                {
                    return N.NodeId == Patch.ContinuationNodeId;
                });
            const auto* Config = Adapter ? Cast<UAbilityGraphPassThroughConfig>(Adapter->Config) : nullptr;
            if (!Config || Adapter->NodeId != Copy->EntryNodeId || Config->Payload != SourcePort->Payload ||
                Patch.ContinuationPort != TEXT("Continue") ||
                !Copy->Edges.ContainsByPredicate(
                    [&](const auto& E)
                    {
                        return E.From == Copy->EntryNodeId && E.OutputPort == TEXT("Apply");
                    }) ||
                Copy->Edges.ContainsByPredicate(
                    [&](const auto& E)
                    {
                        return E.From == Copy->EntryNodeId && E.OutputPort == Patch.ContinuationPort;
                    }))
            {
                Error = TEXT(
                    "InsertOnEdge requires a matching typed pass-through entry, its Apply subtree and an unused Continue port.");
                return false;
            }
        }
        FAbilityRuntimeRepeatLineage Lineage;
        AbilityRuntimePayloadPolicy::BuildLineage(Graph, Registry, Lineage);
        const bool bRepeated =
            AbilityRuntimePayloadPolicy::IsRepeatedOutput(*SourceNode, Patch.OutputPort, Registry, Lineage);
        if (bRepeated && !ValidateRepeatedBranch(*Copy, SourcePort->Payload, Registry, Error))
        {
            return false;
        }
        const bool bFiniteOverlay =
            !bRepeated && (Graph.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility ||
                           Graph.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained);
        // Apply 子树开始时适配器仍占一个活动位，两者共享宿主的同一份额度。
        if (bInsert && (bFiniteOverlay ? Graph.RuntimeFiniteBudget : Graph.Budget).MaxActiveNodes < 2)
        {
            Error = TEXT("InsertOnEdge requires at least two shared active-node slots for the adapter and effect.");
            return false;
        }
        if (Graph.BudgetPolicy == EAbilityGraphBudgetPolicy::LegacyStreaming)
        {
            Error = TEXT("Normalize the legacy base before attaching a runtime finite fragment.");
            return false;
        }
        TMap<FGuid, FGuid> Remap;
        for (const auto& Node : Copy->Nodes)
        {
            Remap.Add(Node.NodeId, BranchNodeId(Source, PatchIndex, Target, Node.NodeId));
        }
        FAbilityGraphEdge Connection;
        Connection.From = Target;
        Connection.To = Remap.FindChecked(Copy->EntryNodeId);
        Connection.OutputPort = Patch.OutputPort;
        for (auto Node : Copy->Nodes)
        {
            Node.NodeId = Remap.FindChecked(Node.NodeId);
            // 分支自己的原图证明不能移植到宿主；新增域只由宿主实际端口及纯动作证明决定。
            Node.OriginalIndex = INDEX_NONE;
            Node.OriginalRole = EAbilityOriginalNodeRole::Helper;
            Node.OriginalOutputs.Reset();
            Node.bRuntimeFinite = bFiniteOverlay;
            // 无限端口只接受经逐节点证明的纯动作；保留原速率/活动/队列额度，不消耗累计有限64次。
            Node.bRuntimeRepeatedPayload = bRepeated;
            Graph.Nodes.Add(Node);
        }
        Graph.bHasRuntimeFinite |= bFiniteOverlay;
        for (auto Edge : Copy->Edges)
        {
            Edge.From = Remap.FindChecked(Edge.From);
            Edge.To = Remap.FindChecked(Edge.To);
            Graph.Edges.Add(Edge);
        }
        if (bInsert)
        {
            // 同端口的声明次序就是派发次序，只原位改向该边；同端口兄弟、共享尾部和映射序列不动。
            Graph.Edges[EdgeIndex] = Connection;
            Graph.Edges.Add({Remap.FindChecked(Patch.ContinuationNodeId), Patch.TargetEdgeTo, Patch.ContinuationPort});
        }
        else
        {
            Graph.Edges.Add(Connection);
        }
        return true;
    }

    /** 只读迭代可达性；配置图有512节点硬上限，不递归进入共享尾部或修改任何运行状态。 */
    TSet<FGuid> ReachableNodes(const UAbilityGraphDefinition& Graph, const TArray<FGuid>& Starts)
    {
        TSet<FGuid> Result;
        TArray<FGuid> Pending = Starts;
        for (int32 Index = 0; Index < Pending.Num(); ++Index)
        {
            const FGuid Id = Pending[Index];
            if (Result.Contains(Id))
            {
                continue;
            }
            Result.Add(Id);
            for (const auto& Edge : Graph.Edges)
            {
                if (Edge.From == Id && !Result.Contains(Edge.To))
                {
                    Pending.Add(Edge.To);
                }
            }
        }
        return Result;
    }

    /** 私有Working上仅替换准确输出口；失败由外层丢弃整份Working，旧快照/正式资产不变。 */
    bool ReplaceBranch(UAbilityGraphDefinition& Graph, const FAbilityRuntimeRecipePatch& Patch, FName Source,
                       int32 PatchIndex, FGuid Target, const FAbilityNodeRegistry& Registry, FString& Error)
    {
        const auto* Anchor = Graph.Nodes.FindByPredicate(
            [&](const auto& Node)
            {
                return Node.NodeId == Target;
            });
        if (!Patch.TargetNodeId.IsValid() || Patch.TargetNodeId != Target || !Anchor || Patch.OutputPort.IsNone())
        {
            Error = TEXT("ReplaceBranch requires one exact target node identity and output port.");
            return false;
        }
        const auto* Descriptor = Registry.Find(Anchor->TypeId);
        if (!Descriptor || !Descriptor->OutputPorts.Contains(Patch.OutputPort) || Patch.OutputPort == TEXT("Failed") ||
            Patch.OutputPort == TEXT("Cancelled"))
        {
            Error = TEXT("ReplaceBranch output must be a declared business port.");
            return false;
        }
        TArray<FGuid> OldRoots;
        for (const auto& Edge : Graph.Edges)
        {
            if (Edge.From == Target && Edge.OutputPort == Patch.OutputPort)
            {
                OldRoots.Add(Edge.To);
            }
        }
        const auto OldClosure = ReachableNodes(Graph, OldRoots);
        if (Graph.OriginalProjection.bEnabled &&
            (Anchor->OriginalIndex != INDEX_NONE || Graph.Nodes.ContainsByPredicate(
                                                        [&](const auto& Node)
                                                        {
                                                            return Node.OriginalIndex != INDEX_NONE &&
                                                                   OldClosure.Contains(Node.NodeId);
                                                        })))
        {
            Error = TEXT("Replacing a projected original branch requires explicit normalizer remapping.");
            return false;
        }
        TSet<FGuid> OldIds;
        for (const auto& Node : Graph.Nodes)
        {
            OldIds.Add(Node.NodeId);
        }
        Graph.Edges.RemoveAll(
            [&](const auto& Edge)
            {
                return Edge.From == Target && Edge.OutputPort == Patch.OutputPort;
            });
        if (!AppendBranch(Graph, Patch, Source, PatchIndex, Target, Registry, Error))
        {
            return false;
        }
        const auto Reachable = ReachableNodes(Graph, {Graph.EntryNodeId});
        // 共享尾部只要仍从其他口可达就保留；新片段的有效性由Append及外层完整校验负责。
        TSet<FGuid> Removed;
        Graph.Nodes.RemoveAll(
            [&](const auto& Node)
            {
                const bool bRemove = OldIds.Contains(Node.NodeId) && !Reachable.Contains(Node.NodeId);
                if (bRemove)
                {
                    Removed.Add(Node.NodeId);
                }
                return bRemove;
            });
        Graph.Edges.RemoveAll(
            [&](const auto& Edge)
            {
                return Removed.Contains(Edge.From) || Removed.Contains(Edge.To);
            });
        Graph.bHasRuntimeFinite = Graph.Nodes.ContainsByPredicate(
            [](const auto& Node)
            {
                return Node.bRuntimeFinite;
            });
        return true;
    }
} // namespace

UAbilityGraphDefinition* AbilityRuntimeRecipes::BuildDefaultGraph(UObject* Outer, const UGameplayAbility* Ability,
                                                                  FString& Error)
{
    Error.Reset();
    if (!IsValid(Ability))
    {
        Error = TEXT("A live ability definition is required.");
        return nullptr;
    }
    UObject* Owner = Outer ? Outer : GetTransientPackage();
    if (const auto* Unified = Cast<UUnifiedGraphAbility>(Ability))
    {
        if (!Unified->Graph)
        {
            Error = TEXT("The ability has no base graph definition.");
            return nullptr;
        }
        // 只规范化开发/配方默认导出；正式无修正前端继续直接读取原Graph资产。
        // 原边与预算证明随细图保留，不能靠重编参数DA复活资产中已断开的分支。
        return AbilityAuthoredGraphFineCompiler::Compile(Owner, Unified->Graph, Error);
    }
    // 这两个前端覆写了原型的执行路径。必须先识别它们，不能仅因继承 Prototype 就编译旧宏图。
    if (const auto* Chain = Cast<UBatch6ChainAbility>(Ability))
    {
        UAbilityLightningNodeConfig* Config = nullptr;
        if (Chain->bSequentialChain)
        {
            auto* Sequential = NewObject<UAbilitySequentialChainNodeConfig>(Owner);
            Sequential->JumpDelay = Chain->JumpDelay;
            Config = Sequential;
        }
        else
        {
            Config = NewObject<UAbilityForkedVolleyNodeConfig>(Owner);
        }
        Config->MaxTargets = Chain->MaxTargets;
        Config->JumpRadius = Chain->JumpRadius;
        Config->DamageFalloff = Chain->Prototype ? Chain->Prototype->DamageFalloff : 1.f;
        Config->BeamSystem = Chain->BeamSystem;
        return AbilityLightningFragments::Build(Owner, *Config);
    }
    if (const auto* Petrify = Cast<UBatch6PetrifyAbility>(Ability))
    {
        return AbilityPetrifyFragments::Build(Owner, Petrify->Radius, Petrify->Duration, Petrify->StoneMaterial);
    }
    if (const auto* Prototype = Cast<UPrototypeAbility>(Ability))
    {
        const FName Profile = Ability->IsA<UExtensionAbility>()  ? TEXT("LegacyExtension.v1")
                              : Ability->IsA<UPureMassAbility>() ? TEXT("LegacyMixed.v1")
                                                                 : TEXT("LegacyActor.v1");
        // 逐目录类的原DA/GE/逐帧动作对照见AssetParity测试。这里只导出细默认定义；
        // 正式技能无贡献时仍由原输入壳选择旧路径，不在导出动作中改变费用或激活技能。
        return AbilityPrototypeFineCompiler::Compile(Owner, Prototype->Prototype, Profile, Error);
    }
    if (const auto* Frontend = Cast<UMyGameplayAbility>(Ability))
    {
        return Frontend->BuildRuntimeBaseGraph(Owner, Error);
    }
    Error = TEXT("This frontend does not implement the runtime behavior definition contract.");
    return nullptr;
}

bool AbilityRuntimeRecipes::Compile(UObject* Outer, const UAbilityGraphDefinition* Base,
                                    const TArray<FAbilityRuntimeRecipeContribution>& Contributions,
                                    UAbilityGraphDefinition*& OutGraph, FString& Error)
{
    OutGraph = nullptr;
    Error.Reset();
    if (!IsValid(Base) || Base->SchemaVersion != 2 || Contributions.Num() > 64)
    {
        Error = TEXT("Runtime compilation requires a Schema2 base and at most 64 contribution sources.");
        return false;
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    TArray<FString> Validation;
    if (!Base->Validate(Registry, Validation))
    {
        Error = FString::Join(Validation, TEXT("; "));
        return false;
    }
    TArray<const FAbilityRuntimeRecipeContribution*> Ordered;
    TSet<FName> Sources;
    for (const auto& Contribution : Contributions)
    {
        if (Contribution.SourceId.IsNone() || Sources.Contains(Contribution.SourceId) ||
            Contribution.Patches.Num() > 64)
        {
            Error = TEXT("Contribution sources must be nonempty and unique, with at most 64 patches each.");
            return false;
        }
        Sources.Add(Contribution.SourceId);
        Ordered.Add(&Contribution);
    }
    Ordered.Sort(
        [](const FAbilityRuntimeRecipeContribution& A, const FAbilityRuntimeRecipeContribution& B)
        {
            return A.Priority != B.Priority ? A.Priority < B.Priority : A.SourceId.LexicalLess(B.SourceId);
        });
    auto* Working = DuplicateObject<UAbilityGraphDefinition>(Base, Outer ? Outer : GetTransientPackage());
    // Base先经过上方Validate，不能重新编译已篡改快照；仅本地未发布Working允许合法补丁重封印。
    Working->RuntimeCompilationSeal.Reset();
    Working->SetFlags(RF_Transient);
    TSet<FGuid> Replaced;
    TSet<FGuid> Configured;
    TSet<FString> ConfiguredAxes;
    TMap<FGuid, const FAbilityRuntimeRecipeContribution*> LastNodeWriter;
    TMap<FString, const FAbilityRuntimeRecipeContribution*> LastAxisWriter;
    // 不能只保留最后写入者：其它轴上的后续工作台编辑不得洗掉普通来源的节点替换保护。
    TSet<FGuid> NodesWithStrictWriters;
    TMap<FGuid, TSet<FName>> ReplacedPorts;
    TMap<FGuid, TSet<FName>> AppendedPorts;
    for (const auto* Contribution : Ordered)
    {
        for (int32 PatchIndex = 0; PatchIndex < Contribution->Patches.Num(); ++PatchIndex)
        {
            const auto& Patch = Contribution->Patches[PatchIndex];
            const TArray<FGuid> Targets = SelectNodes(*Working, Patch);
            if (Targets.IsEmpty())
            {
                Error = FString::Printf(TEXT("Source %s patch %d selects no node."), *Contribution->SourceId.ToString(),
                                        PatchIndex);
                return false;
            }
            for (const FGuid Target : Targets)
            {
                // 工作台是服务器签发的有序编辑历史；装备等并行来源仍不得互相覆盖。
                // 同一贡献/同优先级也不获豁免，撤销仍从不可变Base重放剩余贡献。
                const auto CanFollow = [Contribution](const FAbilityRuntimeRecipeContribution* Previous)
                {
                    return Previous && Previous->bOrderedWorkshopEdit && Contribution->bOrderedWorkshopEdit &&
                           Previous->Priority < Contribution->Priority;
                };
                auto* Node = Working->Nodes.FindByPredicate(
                    [Target](const FAbilityGraphNode& Candidate)
                    {
                        return Candidate.NodeId == Target;
                    });
                if (Patch.Operation == EAbilityRecipePatchOperation::ReplaceNode)
                {
                    if (!ValidateRuntimeAddition(Registry, Patch.ReplacementTypeId, Error))
                    {
                        return false;
                    }
                    if (NodesWithStrictWriters.Contains(Target) ||
                        ((Replaced.Contains(Target) || Configured.Contains(Target)) &&
                         !CanFollow(LastNodeWriter.FindRef(Target))) ||
                        !IsValid(Patch.ReplacementConfig) || Patch.ReplacementTypeId.IsNone())
                    {
                        Error =
                            TEXT("Competing replacements or an incomplete replacement configuration are not allowed.");
                        return false;
                    }
                    Replaced.Add(Target);
                    LastNodeWriter.Add(Target, Contribution);
                    if (!Contribution->bOrderedWorkshopEdit)
                    {
                        NodesWithStrictWriters.Add(Target);
                    }
                    const auto* PreviousDescriptor = Registry.Find(Node->TypeId);
                    const FName PreviousType = Node->TypeId;
                    FAbilityRuntimeRepeatLineage Lineage;
                    AbilityRuntimePayloadPolicy::BuildLineage(*Working, Registry, Lineage);
                    const auto* RepeatedInputs = Lineage.Inputs.Find(Target);
                    FAbilityGraphNode Replacement = *Node;
                    Replacement.TypeId = Patch.ReplacementTypeId;
                    Replacement.Config = Patch.ReplacementConfig;
                    if (RepeatedInputs)
                    {
                        if (!PreviousDescriptor ||
                            (!PreviousDescriptor->bRuntimePayloadReplacementSlot &&
                             !PreviousDescriptor->bRuntimePurePayload && !Node->bRuntimeRepeatedPayload))
                        {
                            Error =
                                TEXT("The repeated original node is not a registered pure-payload replacement slot.");
                            return false;
                        }
                        for (const auto Incoming : *RepeatedInputs)
                        {
                            if (!AbilityRuntimePayloadPolicy::ValidatePureNode(Replacement, Incoming, Registry, Error))
                            {
                                return false;
                            }
                        }
                    }
                    Node->TypeId = Patch.ReplacementTypeId;
                    Node->Config = DuplicateObject<UAbilityNodeConfig>(Patch.ReplacementConfig, Working);
                    Node->bRuntimeRepeatedPayload = RepeatedInputs != nullptr;
                    if (RepeatedInputs)
                    {
                        Node->bRuntimeFinite = false;
                    }
                    else if (PreviousType != Node->TypeId &&
                             (Working->BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained ||
                              Working->BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility))
                    {
                        // 单次口的异步新角色也不能直接继承原持续域；普通有限域仍控制其调度和物理生成。
                        Node->bRuntimeFinite = true;
                    }
                    Working->bHasRuntimeFinite = Working->Nodes.ContainsByPredicate(
                        [](const FAbilityGraphNode& Item)
                        {
                            return Item.bRuntimeFinite;
                        });
                }
                else if (Patch.Operation == EAbilityRecipePatchOperation::AppendBranch ||
                         Patch.Operation == EAbilityRecipePatchOperation::InsertOnEdge)
                {
                    if (const auto* Ports = ReplacedPorts.Find(Target);
                        Patch.Operation == EAbilityRecipePatchOperation::AppendBranch && Ports &&
                        Ports->Contains(Patch.OutputPort))
                    {
                        Error = TEXT("AppendBranch conflicts with a replacement on the same output port.");
                        return false;
                    }
                    if (!AppendBranch(*Working, Patch, Contribution->SourceId, PatchIndex, Target, Registry, Error))
                    {
                        return false;
                    }
                    AppendedPorts.FindOrAdd(Target).Add(Patch.OutputPort);
                }
                else if (Patch.Operation == EAbilityRecipePatchOperation::ReplaceBranch)
                {
                    const auto* ReplacedHere = ReplacedPorts.Find(Target);
                    const auto* AppendedHere = AppendedPorts.Find(Target);
                    if ((ReplacedHere && ReplacedHere->Contains(Patch.OutputPort)) ||
                        (AppendedHere && AppendedHere->Contains(Patch.OutputPort)))
                    {
                        Error = TEXT("Competing branch edits on the same output port are not allowed.");
                        return false;
                    }
                    if (!ReplaceBranch(*Working, Patch, Contribution->SourceId, PatchIndex, Target, Registry, Error))
                    {
                        return false;
                    }
                    ReplacedPorts.FindOrAdd(Target).Add(Patch.OutputPort);
                }
                else if (Patch.Operation == EAbilityRecipePatchOperation::ConfigureParameter)
                {
                    const FString AxisKey =
                        Target.ToString() + TEXT(":") + FString::FromInt(static_cast<int32>(Patch.Parameter));
                    if (!FMath::IsFinite(Patch.Scalar) ||
                        static_cast<uint8>(Patch.Parameter) >
                            static_cast<uint8>(EAbilityConfigParameter::TerrainLayer) ||
                        !Node->Config || (Replaced.Contains(Target) && !CanFollow(LastNodeWriter.FindRef(Target))) ||
                        (ConfiguredAxes.Contains(AxisKey) && !CanFollow(LastAxisWriter.FindRef(AxisKey))))
                    {
                        Error = TEXT(
                            "Typed configuration requires a finite known axis without competing assignments or node replacement.");
                        return false;
                    }
                    if (!Node->Config->TrySetRecipeParameter(Patch.Parameter, Patch.Scalar, Error))
                    {
                        Error = FString::Printf(TEXT("Source %s cannot configure this node: %s"),
                                                *Contribution->SourceId.ToString(), *Error);
                        return false;
                    }
                    Configured.Add(Target);
                    ConfiguredAxes.Add(AxisKey);
                    LastNodeWriter.Add(Target, Contribution);
                    LastAxisWriter.Add(AxisKey, Contribution);
                    if (!Contribution->bOrderedWorkshopEdit)
                    {
                        NodesWithStrictWriters.Add(Target);
                    }
                }
                else
                {
                    const bool bScaleOperation = Patch.Operation == EAbilityRecipePatchOperation::DamageScale ||
                                                 Patch.Operation == EAbilityRecipePatchOperation::RadiusScale ||
                                                 Patch.Operation == EAbilityRecipePatchOperation::DurationScale;
                    const float Minimum =
                        Patch.Operation == EAbilityRecipePatchOperation::DamageScale ? 0.f : UE_SMALL_NUMBER;
                    if (!bScaleOperation || !FMath::IsFinite(Patch.Scalar) || Patch.Scalar < Minimum ||
                        Patch.Scalar > 16.f || !Node->Config ||
                        !ScaleNode(*Node->Config, Patch.Operation, Patch.Scalar))
                    {
                        Error = FString::Printf(
                            TEXT(
                                "Source %s requests an unsupported typed operation or scalar outside its [0,16] contract."),
                            *Contribution->SourceId.ToString());
                        return false;
                    }
                }
            }
            // 单条补丁完成后校验，避免让下一条补丁在非法拓扑或错误配置上继续工作。
            if (!Working->Validate(Registry, Validation))
            {
                Error = FString::Printf(TEXT("Source %s patch %d: %s"), *Contribution->SourceId.ToString(), PatchIndex,
                                        *FString::Join(Validation, TEXT("; ")));
                return false;
            }
        }
    }
    // 零补丁也发布独立只读快照；私有封印不进入Fingerprint，不受运行RootId/OwnerLease影响。
    if (!Working->Validate(Registry, Validation))
    {
        Error = TEXT("Final runtime definition is invalid: ") + FString::Join(Validation, TEXT("; "));
        return false;
    }
    Working->RuntimeCompilationSeal = Fingerprint(*Working);
    OutGraph = Working;
    return true;
}

FString AbilityRuntimeRecipes::Fingerprint(const UAbilityGraphDefinition& Graph)
{
    return FRecipeFingerprint(Graph).Build();
}
