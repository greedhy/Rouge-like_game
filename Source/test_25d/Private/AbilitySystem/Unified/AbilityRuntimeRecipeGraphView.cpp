// 调用链：开发组件PublishState/草稿编译 -> Build -> owner-only纯值图 -> 运行时画布。
// 此文件只读取已编译快照；不调用动作、不改参数、不授予技能，也不把配置对象复制给客户端。
#include "AbilitySystem/Unified/AbilityRuntimeRecipeGraphView.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
    /** 在隔离配置上查询实际参数消费能力；不凭TypeId前缀把全部Set项显示成可用。 */
    TArray<FName> DescribeModifiers(UAbilityGraphDefinition& Graph, const FAbilityGraphNode& Node)
    {
        TArray<FName> Result;
        if (!Node.Config || Node.TypeId.ToString().StartsWith(TEXT("PassThrough.")))
        {
            return Result;
        }
        for (const FName Id : AbilityRuntimeModifierTemplates::GetIds())
        {
            if (Id == TEXT("Append.Registered") || Id == TEXT("Insert.Registered") ||
                Id == TEXT("Replace.RegisteredBranch"))
            {
                // 这些操作必须另外选择准确输出口/边，节点本体不承诺某个口一定适用。
                continue;
            }
            const TStrongObjectPtr<UAbilityNodeConfig> Copy(DuplicateObject<UAbilityNodeConfig>(Node.Config, &Graph));
            bool bSupported = false;
            if (Id == TEXT("Scale.Damage") || Id == TEXT("Scale.Radius") || Id == TEXT("Scale.Duration"))
            {
                const auto Scalar = Id == TEXT("Scale.Damage")   ? EAbilityConfigScalar::Damage
                                    : Id == TEXT("Scale.Radius") ? EAbilityConfigScalar::Radius
                                                                 : EAbilityConfigScalar::Duration;
                bSupported = Copy->TryScaleRecipeParameter(Scalar, 1.f);
            }
            else if (const auto* Parameter = AbilityRuntimeModifierTemplates::FindParameterTemplate(Id))
            {
                FString Error;
                bSupported = Copy->TrySetRecipeParameter(Parameter->Parameter, Parameter->Minimum, Error);
                // 全局端点可能等于原地表，或超出本策略的局部界限，再探测另一个端点和常用范围。
                if (!bSupported)
                {
                    bSupported = Copy->TrySetRecipeParameter(Parameter->Parameter, Parameter->Maximum, Error);
                }
                if (!bSupported && Parameter->Parameter == EAbilityConfigParameter::Radius)
                {
                    bSupported = Copy->TrySetRecipeParameter(Parameter->Parameter, 350.f, Error);
                }
                if (!bSupported && Parameter->Parameter == EAbilityConfigParameter::Duration)
                {
                    bSupported = Copy->TrySetRecipeParameter(Parameter->Parameter, .3f, Error);
                }
            }
            else
            {
                FAbilityRuntimeDeveloperEdit Edit;
                Edit.ModifierId = Id;
                Edit.Value = 1.f;
                FAbilityRuntimeRecipePatch Patch;
                FString Error;
                bSupported = AbilityRuntimeModifierTemplates::BuildPatch(&Graph, Node, Edit, Patch, Error, &Graph);
            }
            if (bSupported)
            {
                Result.Add(Id);
            }
        }
        return Result;
    }
    /** 显示有限的已存在配置标量；不是客户端可提交的反射写入接口，也不是释放时装备结算结果。 */
    FString DescribeParameters(const UAbilityNodeConfig* Config, EAbilityPortPayload Input)
    {
        FString Result = FString::Printf(TEXT("Input: %d"), static_cast<int32>(Input));
        if (!Config)
        {
            return Result;
        }
        if (const auto* Shape = Cast<UAbilityQueryNodeConfig>(Config))
        {
            const auto& Query = Shape->Query;
            Result += FString::Printf(TEXT(" | Geometry: %s | MaxTargets: %d"),
                                      *StaticEnum<EAbilityQueryGeometry>()->GetNameStringByValue(
                                          static_cast<int64>(Query.Geometry)), Query.MaxTargets);
            if (Query.Geometry == EAbilityQueryGeometry::Line)
            {
                Result += FString::Printf(TEXT(" | Range: %.3g | Width: %.3g"), Query.Range, Query.Width);
            }
            else
            {
                Result += FString::Printf(TEXT(" | Radius: %.3g"), Query.Radius);
            }
            if (Query.Geometry == EAbilityQueryGeometry::Cone)
            {
                Result += FString::Printf(TEXT(" | Spread: %.3g"), Query.HalfAngleDegrees * 2.f);
            }
            else if (Query.Geometry == EAbilityQueryGeometry::Ring)
            {
                Result += FString::Printf(TEXT(" | InnerRadius: %.3g"), Query.InnerRadius);
            }
        }
        const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Config);
        const UObject* Values = Scope && Scope->Parameters ? static_cast<const UObject*>(Scope->Parameters.Get())
                                                           : static_cast<const UObject*>(Config);
        for (const FName Name : {FName(TEXT("Duration")),
                                 FName(TEXT("DurationSeconds")),
                                 FName(TEXT("Radius")),
                                 FName(TEXT("Period")),
                                 FName(TEXT("Count")),
                                 FName(TEXT("MaxTargets")),
                                 FName(TEXT("JumpDelay")),
                                 FName(TEXT("JumpRadius")),
                                 FName(TEXT("DamageFalloff")),
                                 FName(TEXT("MaxDepth")),
                                 FName(TEXT("BranchesPerTarget")),
                                 FName(TEXT("bConcurrentBranches")),
                                 FName(TEXT("TargetSelection")),
                                 FName(TEXT("Chance")),
                                 FName(TEXT("bExpectedKilled")),
                                 FName(TEXT("DelaySeconds")),
                                 FName(TEXT("Distance")),
                                 FName(TEXT("ArmDelay")),
                                 FName(TEXT("TargetFilter")),
                                 FName(TEXT("ElementOverride")),
                                 FName(TEXT("Element")),
                                 FName(TEXT("Kind")),
                                 FName(TEXT("InputKind")),
                                 FName(TEXT("ResultKind")),
                                 FName(TEXT("bCloudLayer")),
                                 FName(TEXT("bAllowCloud")),
                                 FName(TEXT("Expected")),
                                 FName(TEXT("Amount")),
                                 FName(TEXT("bMana")),
                                 FName(TEXT("Speed")),
                                 FName(TEXT("DurationScale")),
                                 FName(TEXT("Damage")),
                                 FName(TEXT("Value")),
                                 FName(TEXT("Cooldown"))})
        {
            if (const auto* Property = FindFProperty<FNumericProperty>(Values->GetClass(), Name))
            {
                const void* Value = Property->ContainerPtrToValuePtr<void>(Values);
                const double Number = Property->IsFloatingPoint() ? Property->GetFloatingPointPropertyValue(Value)
                                                                  : Property->GetSignedIntPropertyValue(Value);
                Result += FString::Printf(TEXT(" | %s: %.3g"), *Name.ToString(), Number);
            }
            else if (const auto* BoolProperty = FindFProperty<FBoolProperty>(Values->GetClass(), Name))
            {
                Result += FString::Printf(TEXT(" | %s: %d"), *Name.ToString(),
                                          BoolProperty->GetPropertyValue_InContainer(Values) ? 1 : 0);
            }
            else if (const auto* EnumProperty = FindFProperty<FEnumProperty>(Values->GetClass(), Name))
            {
                const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(
                    EnumProperty->ContainerPtrToValuePtr<void>(Values));
                Result += FString::Printf(TEXT(" | %s: %lld"), *Name.ToString(), Value);
            }
        }
        return Result;
    }
} // namespace

bool AbilityRuntimeGraphViews::Build(const FAbilityRuntimeRecipeSnapshot& Snapshot, FAbilityRuntimeGraphView& Out,
                                     FString& Error)
{
    Out = {};
    Error.Reset();
    if (!Snapshot.Graph || Snapshot.Graph->Nodes.Num() > 1024 || Snapshot.Graph->Edges.Num() > 4096)
    {
        Error = TEXT("A bounded accepted graph snapshot is required for the workshop view.");
        Out.Error = Error;
        return false;
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    Out.Revision = Snapshot.Revision;
    Out.ContentHash = Snapshot.ContentHash;
    TSet<FGuid> Ids;
    for (const auto& Node : Snapshot.Graph->Nodes)
    {
        const auto* Descriptor = Registry.Find(Node.TypeId);
        if (!Descriptor || !Node.NodeId.IsValid() || Ids.Contains(Node.NodeId))
        {
            Error = TEXT("The accepted graph contains an unknown or ambiguous node.");
            Out = {};
            Out.Error = Error;
            return false;
        }
        Ids.Add(Node.NodeId);
        FAbilityRuntimeGraphNodeView View;
        View.NodeId = Node.NodeId;
        View.TypeId = Node.TypeId;
        View.Label = Node.TypeId.ToString();
        View.ParameterSummary = DescribeParameters(Node.Config, Descriptor->InputPayload);
        View.SupportedModifiers = DescribeModifiers(*Snapshot.Graph, Node);
        for (const auto& Port : Descriptor->Ports)
        {
            FString PortError;
            if (Node.TypeId == TEXT("Movement.Character") && Port.Name == TEXT("Landed") &&
                !AbilityRuntimeModifierTemplates::CanPublishConfiguredPort(Node, Port.Name, Registry, PortError))
            {
                continue;
            }
            View.Ports.Add({Port.Name, Port.Payload});
        }
        Out.Nodes.Add(MoveTemp(View));
    }
    for (const auto& Edge : Snapshot.Graph->Edges)
    {
        const auto* Node = Out.Nodes.FindByPredicate(
            [&Edge](const FAbilityRuntimeGraphNodeView& Item)
            {
                return Item.NodeId == Edge.From;
            });
        const auto* Port = Node ? Node->Ports.FindByPredicate(
                                      [&Edge](const FAbilityRuntimeGraphPortView& Item)
                                      {
                                          return Item.Name == Edge.OutputPort;
                                      })
                                : nullptr;
        if (!Port || !Ids.Contains(Edge.To))
        {
            Error = TEXT("The accepted graph contains an incomplete edge contract.");
            Out = {};
            Out.Error = Error;
            return false;
        }
        Out.Edges.Add({Edge.From, Edge.To, Edge.OutputPort, Port->Payload});
    }
    Out.bReady = true;
    return true;
}
