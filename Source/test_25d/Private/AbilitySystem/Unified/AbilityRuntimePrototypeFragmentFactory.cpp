// 调用链：RuntimeModifierTemplates -> BuildBeamBranch -> AppendIsolated -> 配方事务验证/冻结 -> 原GA前端。
// 可信DA仅提供资源和默认数值；私有参数只覆盖显式Duration/Period，不借Radius改Range或伪造原图ordinal。
#include "AbilitySystem/Unified/AbilityRuntimePrototypeFragmentFactory.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeEmitterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"

bool AbilityRuntimePrototypeFragmentFactory::BuildBeamBranch(UObject* Outer, const UAbilityGraphDefinition& BaseGraph,
                                                             const FAbilityGraphNode& Selected,
                                                             const FAbilityRuntimeDeveloperEdit& Edit,
                                                             UAbilityGraphDefinition*& Out, FString& Error)
{
    Out = nullptr;
    Error.Reset();
    const auto* Exact = BaseGraph.Nodes.FindByPredicate(
        [&](const FAbilityGraphNode& Node)
        {
            return Node.NodeId == Selected.NodeId;
        });
    if (!Exact || Exact->TypeId != Selected.TypeId || Exact->Config != Selected.Config ||
        Selected.TypeId != TEXT("Control.PrototypeEmitter") ||
        !Cast<UAbilityPrototypeEmitterNodeConfig>(Selected.Config) || Edit.OutputPortId != TEXT("Launch") ||
        !FMath::IsFinite(Edit.Value) || Edit.Value < .1f || Edit.Value > 60.f || !FMath::IsFinite(Edit.Interval) ||
        Edit.Interval < .1f || Edit.Interval > 60.f)
    {
        Error = TEXT("Trusted Beam replacement requires the exact Emitter.Launch and finite Duration/Period.");
        return false;
    }

    // 仅接受唯一直接来源；配置Outer可以是持久资产或复制容器，不能证明本节点的profile/深度。
    const UAbilityPrototypeScopeNodeConfig* SourceScope = nullptr;
    int32 Incoming = 0;
    for (const auto& Edge : BaseGraph.Edges)
    {
        if (Edge.To != Selected.NodeId)
        {
            continue;
        }
        ++Incoming;
        const auto* Parent = BaseGraph.Nodes.FindByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                return Node.NodeId == Edge.From;
            });
        if (Parent && Parent->TypeId == TEXT("Scope.Prototype") && Edge.OutputPort == TEXT("Ready"))
        {
            SourceScope = Cast<UAbilityPrototypeScopeNodeConfig>(Parent->Config);
        }
    }
    if (Incoming != 1 || !SourceScope || !SourceScope->Parameters ||
        SourceScope->Parameters->Kind != EAbilityPrototypeKind::Emitter || SourceScope->LogicalDepth < 0 ||
        SourceScope->LogicalDepth >= 3 ||
        (SourceScope->Profile != TEXT("PrototypeFine.Actor.v1") &&
         SourceScope->Profile != TEXT("PrototypeFine.Mixed.v1")) ||
        !SourceScope->Validate(Error))
    {
        Error = TEXT("Trusted Beam replacement requires one unambiguous supported Emitter parameter scope.");
        return false;
    }
    auto* Canonical = LoadObject<UAbilityPrototype>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Beam.DA_Prototype_Beam"));
    if (!Canonical || Canonical->Kind != EAbilityPrototypeKind::Beam)
    {
        Error = TEXT("The trusted canonical Beam definition is unavailable or has an incompatible kind.");
        return false;
    }
    auto* Branch = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Branch->SchemaVersion = 2;
    Branch->CompatibilityProfile = TEXT("UnifiedV2");
    auto* Parameters = DuplicateObject<UAbilityPrototype>(Canonical, Branch);
    Parameters->Duration = Edit.Value;
    Parameters->Period = Edit.Interval;
    FAbilityFineFragmentSource Source;
    Source.Parameters = Parameters;
    Source.Presentation = Canonical;
    Source.Profile = SourceScope->Profile;
    Source.LogicalDepth = SourceScope->LogicalDepth + 1;
    Source.StableNamespace = Selected.NodeId;
    FAbilityFineFragmentBindings Bindings;
    if (!AbilityPrototypeFineCompiler::AppendIsolated(*Branch, Source, Bindings, Error))
    {
        return false;
    }
    Branch->EntryNodeId = Bindings.Entry;
    TArray<FString> Errors;
    if (!Branch->ValidateForAuthoring(Errors))
    {
        Error = FString::Join(Errors, TEXT("\n"));
        return false;
    }
    // 事务复制/重映射新片段身份并统一标记runtime-finite；此处不授予任何原图辅助预算身份。
    Out = Branch;
    return true;
}
