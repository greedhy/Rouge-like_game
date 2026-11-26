// 调用链：Developer.ApplyEdit -> 已登记模板的配置构建 -> Compile 逐补丁验证 -> 每次施法冻结。
// UObject 属于服务器组件/后续编译副本；不在 CDO、正式技能资产或客户端持久保存动态图。
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityForkedPropagationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/AbilityRuntimePrototypeFragmentFactory.h"
#include "AbilitySystem/Unified/Nodes/AbilityGraphPassThroughNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "AbilitySystem/Unified/WorkshopDispelAbilities.h"
#include "AbilitySystem/Unified/WorkshopEmberAbilities.h"
#include "AbilitySystem/Unified/WorkshopShatterAbilities.h"
#include "AbilitySystem/Unified/WorkshopRejuvenationAbilities.h"
#include "AbilitySystem/Unified/WorkshopSpatialStormAbilities.h"
#include "AbilitySystem/Unified/WorkshopSurfaceConversionAbilities.h"
#include "AbilitySystem/Unified/WorkshopTerrainCatalystAbilities.h"
#include "AbilitySystem/Unified/Nodes/AbilityTemporalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"

namespace
{
    const FAbilityRuntimeParameterTemplate ParameterTemplates[] = {
        {TEXT("Set.TargetFilter"), EAbilityConfigParameter::TargetFilter,
         TEXT("筛选：0全部 / 1 Actor / 2 Mass / 3低血 / 4灼热 / 5潮湿 / 6感电 / 7奥术 / 8蒸汽 / 9导电 / 10冻僵 / 11冻结"),
         0.f, 11.f, 1.f},
        {TEXT("Set.HealthThreshold"), EAbilityConfigParameter::HealthThreshold, TEXT("低于生命比例（0..1）"), 0.f, 1.f,
         .05f},
        {TEXT("Set.ElementalStatus"), EAbilityConfigParameter::ElementalStatus,
         TEXT("状态：1灼热 / 2潮湿 / 3感电 / 4奥术 / 5蒸汽 / 6导电 / 7冻僵 / 8冻结"), 1.f, 8.f, 1.f},
        {TEXT("Set.TriggerCooldown"), EAbilityConfigParameter::TriggerCooldown, TEXT("同次施法的触发间隔（秒）"), 0.f,
         5.f, .05f},
        {TEXT("Set.ResourceAmount"), EAbilityConfigParameter::ResourceAmount, TEXT("每个恢复节点的资源量"), 1.f, 20.f,
         1.f},
        {TEXT("Set.BarrierCapacity"), EAbilityConfigParameter::BarrierCapacity, TEXT("护盾容量"), 1.f, 250.f, 1.f},
        {TEXT("Set.ElementConversion"), EAbilityConfigParameter::ElementConversion,
         TEXT("结算元素：0原样 / 1火 / 2水 / 3雷 / 4奥术 / 5冰"), 0.f, 5.f, 1.f},
        {TEXT("Set.ExposureElement"), EAbilityConfigParameter::ExposureElement,
         TEXT("状态元素：1火 / 2水 / 3雷 / 4奥术 / 5冰"), 1.f, 5.f, 1.f},
        {TEXT("Set.TerrainSurfaceKind"), EAbilityConfigParameter::TerrainSurfaceKind,
         TEXT("区域类型：0熔岩 / 1霜地 / 2毒地 / 3藤蔓 / 4潮地 / 5通电地 / 6雾云 / 7通电云"), 0.f, 7.f, 1.f},
        {TEXT("Set.TerrainInputKind"), EAbilityConfigParameter::TerrainInputKind,
         TEXT("来源类型：0熔岩 / 1霜地 / 2毒地 / 3藤蔓 / 4潮地 / 5通电地 / 6雾云 / 7通电云"), 0.f, 7.f, 1.f},
        {TEXT("Set.TerrainLayer"), EAbilityConfigParameter::TerrainLayer, TEXT("作用层：0地面 / 1云层"), 0.f, 1.f, 1.f},
        {TEXT("Set.QueryGeometry"), EAbilityConfigParameter::QueryGeometry,
         TEXT("查询形状：0圆形 / 1扇形 / 2圆环 / 3直线"), 0.f, 3.f, 1.f},
        {TEXT("Set.MaxTargets"), EAbilityConfigParameter::PropagationMaxTargets, TEXT("传播总投递上限（含首击）"), 1.f,
         64.f, 1.f},
        {TEXT("Set.JumpDelay"), EAbilityConfigParameter::PropagationDelay, TEXT("每个父命中后的等待（秒）"), .01f, 5.f,
         .01f},
        {TEXT("Set.JumpRadius"), EAbilityConfigParameter::PropagationRadius, TEXT("传播搜索半径（cm）"), 1.f, 3000.f,
         25.f},
        {TEXT("Set.PropagationFalloff"), EAbilityConfigParameter::PropagationFalloff, TEXT("传播每跳/每层伤害保留比例"),
         0.f, 1.f, .05f},
        {TEXT("Set.MaxDepth"), EAbilityConfigParameter::ForkDepth, TEXT("分裂层数（首击为0）"), 0.f, 16.f, 1.f},
        {TEXT("Set.BranchesPerTarget"), EAbilityConfigParameter::ForkBranches, TEXT("每次命中最多分裂几颗"), 1.f, 8.f,
         1.f},
        {TEXT("Set.ForkConcurrency"), EAbilityConfigParameter::ForkConcurrent, TEXT("分叉模式：0兼容串行 / 1并发分裂"),
         0.f, 1.f, 1.f},
        {TEXT("Set.TargetSelection"), EAbilityConfigParameter::TargetSelection,
         TEXT("选敌：0原默认 / 1最近 / 2最远 / 3最低生命比例"), 0.f, 3.f, 1.f},
        {TEXT("Set.ConditionOutcome"), EAbilityConfigParameter::ConditionOutcome, TEXT("条件：0未击杀 / 1击杀"), 0.f,
         1.f, 1.f},
        {TEXT("Set.Chance"), EAbilityConfigParameter::Chance, TEXT("通过概率（0..1）"), 0.f, 1.f, .05f},
        {TEXT("Set.Count"), EAbilityConfigParameter::Count, TEXT("数量（整数；当前片段仍按实际上限验证）"), 1.f, 64.f,
         1.f},
        {TEXT("Set.Spread"), EAbilityConfigParameter::Spread, TEXT("散射/扇形角度（度）"), 0.f, 360.f, 1.f},
        {TEXT("Set.Period"), EAbilityConfigParameter::Period, TEXT("实际周期（秒）"), .1f, 60.f, .1f},
        {TEXT("Set.ArmDelay"), EAbilityConfigParameter::ArmDelay, TEXT("实际布防/延迟（秒）"), .1f, 60.f, .1f},
        {TEXT("Set.Range"), EAbilityConfigParameter::Range, TEXT("实际射程/直线长度（cm）"), 1.f, 10000.f, 25.f},
        {TEXT("Set.CollisionRadius"), EAbilityConfigParameter::CollisionRadius, TEXT("实际碰撞半径（cm）"), .01f, 200.f,
         1.f},
        {TEXT("Set.Duration"), EAbilityConfigParameter::Duration, TEXT("实际有限寿命（秒；持续开关根不支持）"), .01f,
         60.f, .1f},
        {TEXT("Set.Radius"), EAbilityConfigParameter::Radius, TEXT("实际作用/搜索半径（cm；不是碰撞半径）"), .01f,
         10000.f, 25.f},
        {TEXT("Set.InnerRadius"), EAbilityConfigParameter::InnerRadius, TEXT("圆环内半径（cm）"), 0.f, 10000.f, 10.f},
        {TEXT("Set.Width"), EAbilityConfigParameter::Width, TEXT("直线完整宽度（cm）"), 1.f, 10000.f, 10.f},
        {TEXT("Set.HalfHeight"), EAbilityConfigParameter::HalfHeight, TEXT("圆环/直线高度半范围（cm）"), 1.f, 2000.f,
         10.f},
        {TEXT("Set.Speed"), EAbilityConfigParameter::Speed, TEXT("真实飞行/波形速度（cm/s）"), 1.f, 10000.f, 25.f},
        {TEXT("Set.HomingTurnRate"), EAbilityConfigParameter::HomingTurnRate, TEXT("恒转速追踪（度/s）"), 1.f, 720.f,
         5.f},
        {TEXT("Set.Motion"), EAbilityConfigParameter::Motion, TEXT("运动枚举：0直线/1恒转速追踪/2原生追踪/3折返"), 0.f,
         3.f, 1.f},
        {TEXT("Set.ImpactPolicy"), EAbilityConfigParameter::ImpactPolicy, TEXT("接触枚举：0首击停止/1穿透/2往返分阶段"),
         0.f, 2.f, 1.f},
        {TEXT("Set.ImpactLimit"), EAbilityConfigParameter::ImpactLimit, TEXT("唯一接触上限（整数）"), 1.f, 64.f, 1.f},
        {TEXT("Set.DamageFalloff"), EAbilityConfigParameter::DamageFalloff, TEXT("每次追加命中的主伤倍率"), 0.f, 1.f,
         .05f},
        {TEXT("Set.TransferRadius"), EAbilityConfigParameter::TransferRadius, TEXT("附着转移搜索半径（cm）"), 0.f,
         10000.f, 25.f},
        {TEXT("Set.MaxTransfers"), EAbilityConfigParameter::MaxTransfers, TEXT("最多附着转移次数（整数）"), 0.f, 16.f,
         1.f},
        {TEXT("Set.StackThreshold"), EAbilityConfigParameter::StackThreshold, TEXT("叠层触发阈值（整数）"), 1.f, 64.f,
         1.f},
        {TEXT("Set.StackLifetime"), EAbilityConfigParameter::StackLifetime, TEXT("共享叠层寿命（秒）"), .01f, 60.f,
         .1f},
        {TEXT("Set.DetonationDelay"), EAbilityConfigParameter::DetonationDelay, TEXT("叠层触发延迟（秒）"), 0.f, 60.f,
         .1f},
        {TEXT("Set.MoveDistance"), EAbilityConfigParameter::MoveDistance, TEXT("实际位移距离（cm）"), 0.f, 10000.f,
         10.f},
        {TEXT("Set.SafeDistance"), EAbilityConfigParameter::SafeDistance, TEXT("拉近保留安全距离（cm）"), 0.f, 10000.f,
         10.f},
        {TEXT("Set.PelletTargetPolicy"), EAbilityConfigParameter::PelletTargetPolicy,
         TEXT("单弹目标路由：0不继承（原默认）/1保留本次已选原生命"), 0.f, 1.f, 1.f},
    };
    /** 状态模板以秒为单位，不复用数值缩放的 0..16 倍合同。 */
    bool ValidStatusDuration(float Value, FString& Error)
    {
        if (!FMath::IsFinite(Value) || Value < .01f || Value > 60.f)
        {
            Error = TEXT("Status duration must be between 0.01 and 60 seconds.");
            return false;
        }
        return true;
    }

    /** 这些有限时序片段接收位置，应用后继续从各自真实端口连接。 */
    bool IsTemporalFragment(FName Id)
    {
        return Id == TEXT("Control.DelayOrigin") || Id == TEXT("Control.Repeat") || Id == TEXT("Query.WaitForTarget") ||
               Id == TEXT("Spatial.Wavefront") || Id == TEXT("Control.StaggeredPoints");
    }

    /** 地表模块只接收位置，活目标与历史命中不能直接授权世界动作。 */
    bool IsWorkshopTerrainFragment(FName Id)
    {
        return Id == TEXT("World.ElementSurface") || Id == TEXT("World.IgniteSurface") ||
               Id == TEXT("World.DispelSurface") || Id == TEXT("World.ConvertSurface") ||
               Id == TEXT("World.FindOwnedSurface");
    }

    /** 自身资源和状态动作从 Origin 读取施法者身份，无需活目标。 */
    bool IsSelfEffectFragment(FName Id)
    {
        return Id == TEXT("Effect.RestoreSelf") || Id == TEXT("Status.ClearSelfExposure");
    }

    /** 只构造已注册类的私有配置，既有节点负责实际时钟及波前。 */
    UAbilityNodeConfig* BuildTemporalFragment(UAbilityGraphDefinition* Graph, const FAbilityRuntimeDeveloperEdit& Edit)
    {
        if (Edit.FragmentId == TEXT("Control.DelayOrigin"))
        {
            auto* Config = NewObject<UAbilityDelayNodeConfig>(Graph);
            Config->DelaySeconds = Edit.Value;
            return Config;
        }
        if (Edit.FragmentId == TEXT("Control.Repeat"))
        {
            auto* Config = NewObject<UAbilityRepeatNodeConfig>(Graph);
            Config->Duration = Edit.Value;
            Config->Period = Edit.Interval;
            Config->Count = Edit.Count;
            return Config;
        }
        if (Edit.FragmentId == TEXT("Query.WaitForTarget"))
        {
            auto* Config = NewObject<UAbilityWaitTargetNodeConfig>(Graph);
            Config->Duration = Edit.Value;
            Config->ArmDelay = Edit.Interval;
            Config->Radius = Edit.Radius;
            return Config;
        }
        if (Edit.FragmentId == TEXT("Control.StaggeredPoints"))
        {
            auto* Config = NewObject<UAbilityStaggeredPointsNodeConfig>(Graph);
            Config->Count = Edit.Count;
            Config->Radius = Edit.Radius;
            Config->Period = Edit.Interval;
            return Config;
        }
        auto* Config = NewObject<UAbilityWavefrontNodeConfig>(Graph);
        Config->Duration = Edit.Value;
        Config->Radius = Edit.Radius;
        return Config;
    }
} // namespace

const TArray<FName>& AbilityRuntimeModifierTemplates::GetIds()
{
    static const TArray<FName> Ids = []
    {
        TArray<FName> Result = {
            TEXT("Scale.Damage"),           TEXT("Scale.Radius"),       TEXT("Scale.Duration"),
            TEXT("Propagation.Sequential"), TEXT("Propagation.Forked"), TEXT("Append.Status.Petrify"),
            TEXT("Replace.Status.Petrify"), TEXT("Append.Registered"),  TEXT("Replace.RegisteredBranch"),
            TEXT("Insert.Registered")};
        for (const auto& Parameter : ParameterTemplates)
        {
            Result.Add(Parameter.Id);
        }
        return Result;
    }();
    return Ids;
}

const FAbilityRuntimeParameterTemplate* AbilityRuntimeModifierTemplates::FindParameterTemplate(FName Id)
{
    for (const auto& Parameter : ParameterTemplates)
    {
        if (Parameter.Id == Id)
        {
            return &Parameter;
        }
    }
    return nullptr;
}

const TArray<FName>& AbilityRuntimeModifierTemplates::GetFragmentIds()
{
    static const TArray<FName> Ids = {
        TEXT("Target.Petrify"),          TEXT("Area.Petrify"),           TEXT("Target.Slow"),
        TEXT("Query.SelectTargets"),     TEXT("Condition.Killed"),       TEXT("Condition.Chance"),
        TEXT("Control.DelayOrigin"),     TEXT("Control.Repeat"),         TEXT("Query.WaitForTarget"),
        TEXT("Spatial.Wavefront"),       TEXT("Condition.HealthBelow"),  TEXT("Condition.ElementalStatus"),
        TEXT("Condition.DamageApplied"), TEXT("Control.EveryNthDamage"), TEXT("Control.DamageTriggerLimit"),
        TEXT("Effect.RestoreMana"),      TEXT("Effect.RestoreHealth"),   TEXT("Status.ApplyExposure"),
        TEXT("World.ElementSurface"),   TEXT("World.IgniteSurface"),    TEXT("World.DispelSurface"),
        TEXT("Status.ConsumeExposure"), TEXT("Spatial.TargetOrigin"),  TEXT("World.ConvertSurface"),
        TEXT("Control.StaggeredPoints"), TEXT("Effect.RestoreSelf"),    TEXT("Status.ClearSelfExposure"),
        TEXT("World.FindOwnedSurface")};
    return Ids;
}

bool AbilityRuntimeModifierTemplates::IsRuleFragment(FName Id)
{
    return Id == TEXT("Query.SelectTargets") || Id == TEXT("Condition.Killed") || Id == TEXT("Condition.Chance") ||
           IsTemporalFragment(Id) || IsSelfEffectFragment(Id) || AbilityGameplayRuleNodes::FindCombatRule(Id).IsSet();
}

bool AbilityRuntimeModifierTemplates::SupportsFragmentPayload(FName Id, EAbilityPortPayload Payload)
{
    if (Id == TEXT("Status.ApplyExposure") || Id == TEXT("Status.ConsumeExposure") || Id == TEXT("Spatial.TargetOrigin"))
    {
        return Payload == EAbilityPortPayload::LiveTarget;
    }
    if (IsWorkshopTerrainFragment(Id))
    {
        return Payload == EAbilityPortPayload::Origin;
    }
    if (const auto Rule = AbilityGameplayRuleNodes::FindCombatRule(Id); Rule.IsSet())
    {
        return Payload == AbilityGameplayRuleNodes::GetCombatRuleInput(Rule.GetValue());
    }
    if (Id == TEXT("Area.Petrify") || Id == TEXT("Query.SelectTargets") || Id == TEXT("Condition.Chance") ||
        IsTemporalFragment(Id) || IsSelfEffectFragment(Id))
    {
        return Payload == EAbilityPortPayload::Origin;
    }
    if (Id == TEXT("Condition.Killed"))
    {
        return Payload == EAbilityPortPayload::DamageResult;
    }
    return Payload == EAbilityPortPayload::LiveTarget || Payload == EAbilityPortPayload::ConfirmedImpact ||
           Payload == EAbilityPortPayload::DamageResult ||
           (Payload == EAbilityPortPayload::TargetSet && Id == TEXT("Target.Petrify"));
}

const TArray<FName>& AbilityRuntimeModifierTemplates::GetReplacementFragmentIds()
{
    static const TArray<FName> Ids = {TEXT("Prototype.Beam")};
    return Ids;
}

bool AbilityRuntimeModifierTemplates::CanPublishConfiguredPort(const FAbilityGraphNode& Node, FName Port,
                                                               const FAbilityNodeRegistry& Registry, FString& Error)
{
    Error.Reset();
    const auto* Descriptor = Registry.Find(Node.TypeId);
    if (!Descriptor || !IsValid(Node.Config) || !Node.Config->IsA(Descriptor->ConfigClass.Get()) || Port.IsNone() ||
        !Descriptor->Ports.ContainsByPredicate(
            [Port](const FAbilityPortDescriptor& Item)
            {
                return Item.Name == Port;
            }))
    {
        Error = TEXT("The configured registered strategy does not declare this exact output port.");
        return false;
    }
    if (Port == TEXT("Cancelled") || Port == TEXT("Failed"))
    {
        // GraphInstance将这些值保留给终结/清理，取消不能产生正常业务后继。
        Error = TEXT("Cancellation and failure are executor cleanup outcomes, not publishable business ports.");
        return false;
    }
    if (Node.TypeId == TEXT("Delivery.ProjectileAwait") && Port == TEXT("DamageResolved"))
    {
        const auto* Delivery = Cast<UAbilityDeliveryNodeConfig>(Node.Config);
        if (!Delivery || Delivery->bUseNativeLaunchParameters || !Delivery->Delivery.bApplyDamage)
        {
            // 原生准备帧固定使用外置InputDamage；其伤害回执属于后继sink，不属于纯投递。
            Error = TEXT(
                "This delivery uses an external payload and cannot publish DamageResolved; select Hit or the damage sink.");
            return false;
        }
    }
    if (Node.TypeId == TEXT("Propagation.Forked") && Port == TEXT("DamageResolved"))
    {
        // 当前Forked策略只投递外部payload并发布Hit/Completed；兼容描述中的可选名字不代表事实。
        // 类型/配置类已经在上方准确核对，不能按父配置或所有异步策略一概禁止伤害回执。
        Error = TEXT("This external-only Forked strategy publishes Hit and Completed, not DamageResolved.");
        return false;
    }
    if (Node.TypeId == TEXT("Movement.TargetDisplace") && Port == TEXT("Rejected") &&
        !CastChecked<UAbilityTargetDisplaceNodeConfig>(Node.Config)->bContinueOnRejected)
    {
        Error = TEXT("This strict movement node fails the graph on rejection and cannot publish Rejected.");
        return false;
    }
    if (Node.TypeId == TEXT("Movement.Character") && Port == TEXT("Landed") &&
        !CastChecked<UAbilityCharacterMoveNodeConfig>(Node.Config)->UsesGroundTarget())
    {
        Error = TEXT("Only the ground-targeted dash can publish a real Landed position.");
        return false;
    }
    // Completed、Expired等真实终态仍可连接；不能因投递异步就丢弃合法正常收尾。
    return true;
}

bool AbilityRuntimeModifierTemplates::BuildPatch(UObject* Outer, const FAbilityGraphNode& Node,
                                                 const FAbilityRuntimeDeveloperEdit& Edit,
                                                 FAbilityRuntimeRecipePatch& OutPatch, FString& Error,
                                                 const UAbilityGraphDefinition* BaseGraph)
{
    Error.Reset();
    OutPatch = {};
    OutPatch.TargetNodeId = Node.NodeId;
    if (!IsValid(Outer) || !IsValid(Node.Config))
    {
        Error = TEXT("Template requires the authoritative owner and an existing base configuration.");
        return false;
    }
    if (Edit.ModifierId == TEXT("Insert.Registered"))
    {
        if (IsRuleFragment(Edit.FragmentId))
        {
            Error =
                TEXT("Append rule/query modules to an output; insertion always continues the original edge payload.");
            return false;
        }
        if (!BaseGraph || !Edit.TargetEdgeTo.IsValid() ||
            !BaseGraph->Edges.ContainsByPredicate(
                [&Node, &Edit](const FAbilityGraphEdge& Edge)
                {
                    return Edge.From == Node.NodeId && Edge.To == Edit.TargetEdgeTo &&
                           Edge.OutputPort == Edit.OutputPortId;
                }))
        {
            Error = TEXT("Insertion requires one exact accepted edge: source, output port and destination.");
            return false;
        }
        auto Append = Edit;
        Append.ModifierId = TEXT("Append.Registered");
        if (!BuildPatch(Outer, Node, Append, OutPatch, Error, BaseGraph))
        {
            return false;
        }
        FAbilityNodeRegistry Registry;
        Registry.RegisterBuiltins();
        const auto* Descriptor = Registry.Find(Node.TypeId);
        const auto* Port = Descriptor ? Descriptor->Ports.FindByPredicate(
                                            [&Edit](const FAbilityPortDescriptor& Item)
                                            {
                                                return Item.Name == Edit.OutputPortId;
                                            })
                                      : nullptr;
        UAbilityGraphDefinition* Wrapped = nullptr;
        FGuid Continuation;
        FName ContinuationPort;
        if (!Port || !AbilityGraphPassThroughNodes::WrapBranch(Outer, OutPatch.BranchGraph, Port->Payload, Wrapped,
                                                               Continuation, ContinuationPort, Error))
        {
            return false;
        }
        OutPatch.Operation = EAbilityRecipePatchOperation::InsertOnEdge;
        OutPatch.TargetEdgeTo = Edit.TargetEdgeTo;
        OutPatch.BranchGraph = Wrapped;
        OutPatch.ContinuationNodeId = Continuation;
        OutPatch.ContinuationPort = ContinuationPort;
        return true;
    }
    if (Edit.ModifierId == TEXT("Replace.RegisteredBranch"))
    {
        if (!BaseGraph || !GetReplacementFragmentIds().Contains(Edit.FragmentId) || Edit.OutputPortId.IsNone())
        {
            Error = TEXT("Branch replacement requires the exact base graph, trusted fragment and declared port.");
            return false;
        }
        UAbilityGraphDefinition* Branch = nullptr;
        if (!AbilityRuntimePrototypeFragmentFactory::BuildBeamBranch(Outer, *BaseGraph, Node, Edit, Branch, Error))
        {
            return false;
        }
        OutPatch.Operation = EAbilityRecipePatchOperation::ReplaceBranch;
        OutPatch.OutputPort = Edit.OutputPortId;
        OutPatch.BranchGraph = Branch;
        return true;
    }
    if (Edit.ModifierId == TEXT("Append.Registered"))
    {
        if (!GetFragmentIds().Contains(Edit.FragmentId))
        {
            if (Error.IsEmpty())
            {
                Error = TEXT("Unknown trusted fragment ID; arbitrary classes or asset paths are not accepted.");
            }
            return false;
        }
        FAbilityNodeRegistry Registry;
        Registry.RegisterBuiltins();
        const auto* Descriptor = Registry.Find(Node.TypeId);
        const auto* Port = Descriptor ? Descriptor->Ports.FindByPredicate(
                                            [&](const FAbilityPortDescriptor& Item)
                                            {
                                                return Item.Name == Edit.OutputPortId;
                                            })
                                      : nullptr;
        if (!Port || Edit.OutputPortId.IsNone())
        {
            Error = TEXT("The selected registered strategy does not declare this exact output port.");
            return false;
        }
        if (!CanPublishConfiguredPort(Node, Edit.OutputPortId, Registry, Error))
        {
            return false;
        }
        if (IsWorkshopTerrainFragment(Edit.FragmentId))
        {
            if (!SupportsFragmentPayload(Edit.FragmentId, Port->Payload))
            {
                Error = TEXT("A terrain fragment requires an explicit Origin output port.");
                return false;
            }
            auto* Branch = NewObject<UAbilityGraphDefinition>(Outer);
            UAbilityNodeConfig* Surface = nullptr;
            if (Edit.FragmentId == TEXT("World.ElementSurface"))
            {
                auto* Create = NewObject<UAbilityElementSurfaceNodeConfig>(Branch);
                Create->Radius = Edit.Radius;
                Create->Duration = Edit.Value;
                Surface = Create;
            }
            else if (Edit.FragmentId == TEXT("World.IgniteSurface"))
            {
                auto* Ignite = NewObject<UAbilityIgniteSurfaceNodeConfig>(Branch);
                Ignite->Radius = Edit.Radius;
                Surface = Ignite;
            }
            else if (Edit.FragmentId == TEXT("World.ConvertSurface"))
            {
                auto* Convert = NewObject<UAbilityConvertSurfaceNodeConfig>(Branch);
                Convert->Radius = Edit.Radius;
                Convert->bAllowCloud = true;
                Surface = Convert;
            }
            else if (Edit.FragmentId == TEXT("World.FindOwnedSurface"))
            {
                auto* Select = NewObject<UAbilityFindOwnedSurfaceNodeConfig>(Branch);
                Select->Radius = Edit.Radius;
                Surface = Select;
            }
            else
            {
                auto* Dispel = NewObject<UAbilityDispelSurfaceNodeConfig>(Branch);
                Dispel->Radius = Edit.Radius;
                Dispel->MaxTargets = Edit.Count;
                Surface = Dispel;
            }
            if (!Surface->Validate(Error))
            {
                return false;
            }
            Branch->SchemaVersion = 2;
            Branch->CompatibilityProfile = TEXT("UnifiedV2");
            Branch->EntryNodeId = FGuid::NewGuid();
            Branch->Nodes.Add({Branch->EntryNodeId, Edit.FragmentId, Surface});
            OutPatch.Operation = EAbilityRecipePatchOperation::AppendBranch;
            OutPatch.OutputPort = Edit.OutputPortId;
            OutPatch.BranchGraph = Branch;
            return true;
        }
        if (Edit.FragmentId == TEXT("Status.ApplyExposure") || Edit.FragmentId == TEXT("Status.ConsumeExposure") ||
            Edit.FragmentId == TEXT("Spatial.TargetOrigin"))
        {
            if (!SupportsFragmentPayload(Edit.FragmentId, Port->Payload))
            {
                Error = TEXT("This fragment requires a live target; resolve historical impacts first.");
                return false;
            }
            auto* Branch = NewObject<UAbilityGraphDefinition>(Outer);
            UAbilityNodeConfig* Config = nullptr;
            if (Edit.FragmentId == TEXT("Status.ApplyExposure"))
            {
                auto* Exposure = NewObject<UAbilityApplyExposureConfig>(Branch);
                if (!Exposure->TrySetRecipeParameter(EAbilityConfigParameter::ExposureElement, Edit.Value, Error))
                {
                    return false;
                }
                Config = Exposure;
            }
            else if (Edit.FragmentId == TEXT("Status.ConsumeExposure"))
            {
                auto* Consume = NewObject<UAbilityConsumeExposureNodeConfig>(Branch);
                if (!Consume->TrySetRecipeParameter(EAbilityConfigParameter::ElementalStatus, Edit.Value, Error))
                {
                    return false;
                }
                Config = Consume;
            }
            else
            {
                Config = NewObject<UAbilityTargetOriginConfig>(Branch);
            }
            Branch->SchemaVersion = 2;
            Branch->CompatibilityProfile = TEXT("UnifiedV2");
            Branch->EntryNodeId = FGuid::NewGuid();
            Branch->Nodes.Add({Branch->EntryNodeId, Edit.FragmentId, Config});
            OutPatch.Operation = EAbilityRecipePatchOperation::AppendBranch;
            OutPatch.OutputPort = Edit.OutputPortId;
            OutPatch.BranchGraph = Branch;
            return true;
        }
        if (IsRuleFragment(Edit.FragmentId))
        {
            if (!SupportsFragmentPayload(Edit.FragmentId, Port->Payload))
            {
                Error = TEXT("Rule/query fragment requires its exact registered input payload.");
                return false;
            }
            auto* Branch = NewObject<UAbilityGraphDefinition>(Outer);
            Branch->SchemaVersion = 2;
            Branch->CompatibilityProfile = TEXT("UnifiedV2");
            UAbilityNodeConfig* Config = nullptr;
            if (IsTemporalFragment(Edit.FragmentId))
            {
                if (Edit.FragmentId != TEXT("Control.StaggeredPoints") && !ValidStatusDuration(Edit.Value, Error))
                {
                    return false;
                }
                Config = BuildTemporalFragment(Branch, Edit);
            }
            else if (Edit.FragmentId == TEXT("Effect.RestoreSelf"))
            {
                auto* Restore = NewObject<UAbilityRestoreSelfNodeConfig>(Branch);
                Restore->bOncePerCast = true;
                Restore->Amount = Edit.Value;
                Config = Restore;
            }
            else if (Edit.FragmentId == TEXT("Status.ClearSelfExposure"))
            {
                auto* Clear = NewObject<UAbilityClearSelfExposureNodeConfig>(Branch);
                if (!Clear->TrySetRecipeParameter(EAbilityConfigParameter::ElementalStatus, Edit.Value, Error))
                {
                    return false;
                }
                Config = Clear;
            }
            else if (const auto Rule = AbilityGameplayRuleNodes::FindCombatRule(Edit.FragmentId); Rule.IsSet())
            {
                auto* Settings = NewObject<UAbilityCombatRuleConfig>(Branch);
                Settings->Rule = Rule.GetValue();
                Settings->Value = Settings->Rule == EAbilityCombatRule::DamageApplied ? 0.f : Edit.Value;
                Config = Settings;
            }
            else if (Edit.FragmentId == TEXT("Query.SelectTargets"))
            {
                auto* Query = NewObject<UAbilitySelectTargetsConfig>(Branch);
                Query->Radius = Edit.Radius;
                Query->MaxTargets = Edit.Count;
                Config = Query;
            }
            else if (Edit.FragmentId == TEXT("Condition.Killed"))
            {
                auto* Gate = NewObject<UAbilityKilledGateConfig>(Branch);
                if (!Gate->TrySetRecipeParameter(EAbilityConfigParameter::ConditionOutcome, Edit.Value, Error))
                {
                    return false;
                }
                Config = Gate;
            }
            else
            {
                auto* Gate = NewObject<UAbilityChanceGateConfig>(Branch);
                Gate->Chance = Edit.Value;
                Config = Gate;
            }
            if (!Config->Validate(Error))
            {
                return false;
            }
            const FGuid Root = FGuid::NewGuid();
            Branch->EntryNodeId = Root;
            Branch->Nodes.Add({Root, Edit.FragmentId, Config});
            OutPatch.Operation = EAbilityRecipePatchOperation::AppendBranch;
            OutPatch.OutputPort = Edit.OutputPortId;
            OutPatch.BranchGraph = Branch;
            return true;
        }
        if (!ValidStatusDuration(Edit.Value, Error))
        {
            return false;
        }
        const bool bArea = Edit.FragmentId == TEXT("Area.Petrify");
        const bool bSlow = Edit.FragmentId == TEXT("Target.Slow");
        const bool bLiveOrResolved = Port->Payload == EAbilityPortPayload::LiveTarget ||
                                     Port->Payload == EAbilityPortPayload::ConfirmedImpact ||
                                     Port->Payload == EAbilityPortPayload::DamageResult;
        const bool bBatchPetrify = !bSlow && Port->Payload == EAbilityPortPayload::TargetSet;
        if ((bArea && (Port->Payload != EAbilityPortPayload::Origin || !FMath::IsFinite(Edit.Radius) ||
                       Edit.Radius <= 0.f || Edit.Radius > 2000.f)) ||
            (!bArea && !bLiveOrResolved && !bBatchPetrify))
        {
            Error = TEXT("The trusted fragment has no implementation for that payload type or bounded geometry.");
            return false;
        }
        auto* Branch = NewObject<UAbilityGraphDefinition>(Outer);
        Branch->SchemaVersion = 2;
        Branch->CompatibilityProfile = TEXT("UnifiedV2");
        auto Add = [Branch](FName Type, UAbilityNodeConfig* Config)
        {
            const FGuid Id = FGuid::NewGuid();
            Branch->Nodes.Add({Id, Type, Config});
            return Id;
        };
        FGuid Resolve;
        FName ResolvePort;
        if (bArea)
        {
            auto* Query = NewObject<UAbilityPetrifyAreaQueryConfig>(Branch);
            Query->Radius = Edit.Radius;
            Resolve = Add(TEXT("Query.PetrifyArea"), Query);
            ResolvePort = TEXT("Completed");
        }
        else if (Port->Payload == EAbilityPortPayload::ConfirmedImpact ||
                 Port->Payload == EAbilityPortPayload::DamageResult)
        {
            Resolve = Add(Port->Payload == EAbilityPortPayload::ConfirmedImpact ? TEXT("Target.ResolveImpact")
                                                                                : TEXT("Target.ResolveDamageTarget"),
                          NewObject<UAbilityResolveImpactNodeConfig>(Branch));
            ResolvePort = TEXT("TargetFound");
        }
        FGuid Apply;
        if (bArea || Port->Payload == EAbilityPortPayload::TargetSet)
        {
            auto* Batch = NewObject<UAbilityPetrifyBatchConfig>(Branch);
            Batch->Duration = Edit.Value;
            Apply = Add(TEXT("Status.PetrifyBatch"), Batch);
        }
        else if (bSlow)
        {
            auto* Slow = NewObject<UAbilitySlowNodeConfig>(Branch);
            Slow->Duration = Edit.Value;
            Apply = Add(TEXT("Status.Slow"), Slow);
        }
        else
        {
            auto* Petrify = NewObject<UAbilityPetrifyNodeConfig>(Branch);
            Petrify->DurationSeconds = Edit.Value;
            Apply = Add(TEXT("Status.Petrify"), Petrify);
        }
        Branch->EntryNodeId = Resolve.IsValid() ? Resolve : Apply;
        if (Resolve.IsValid())
        {
            Branch->Edges.Add({Resolve, Apply, ResolvePort});
        }
        // 只有真实声明的事实端口进入相应解析器；状态仍逐次复核原生命，库存从不被普通分支接管。
        OutPatch.Operation = EAbilityRecipePatchOperation::AppendBranch;
        OutPatch.OutputPort = Edit.OutputPortId;
        OutPatch.BranchGraph = Branch;
        return true;
    }
    if (const auto* Parameter = FindParameterTemplate(Edit.ModifierId))
    {
        const bool bInteger = Parameter->Parameter == EAbilityConfigParameter::Count ||
                              Parameter->Parameter == EAbilityConfigParameter::Motion ||
                              Parameter->Parameter == EAbilityConfigParameter::ImpactPolicy ||
                              Parameter->Parameter == EAbilityConfigParameter::ImpactLimit ||
                              Parameter->Parameter == EAbilityConfigParameter::MaxTransfers ||
                              Parameter->Parameter == EAbilityConfigParameter::StackThreshold ||
                              Parameter->Parameter == EAbilityConfigParameter::PelletTargetPolicy ||
                              Parameter->Parameter == EAbilityConfigParameter::PropagationMaxTargets ||
                              Parameter->Parameter == EAbilityConfigParameter::ForkDepth ||
                              Parameter->Parameter == EAbilityConfigParameter::ForkBranches ||
                              Parameter->Parameter == EAbilityConfigParameter::ForkConcurrent ||
                              Parameter->Parameter == EAbilityConfigParameter::TargetSelection ||
                              Parameter->Parameter == EAbilityConfigParameter::ConditionOutcome ||
                              Parameter->Parameter == EAbilityConfigParameter::TargetFilter ||
                              Parameter->Parameter == EAbilityConfigParameter::ElementalStatus ||
                              Parameter->Parameter == EAbilityConfigParameter::ElementConversion ||
                              Parameter->Parameter == EAbilityConfigParameter::ExposureElement ||
                              Parameter->Parameter == EAbilityConfigParameter::TerrainSurfaceKind ||
                              Parameter->Parameter == EAbilityConfigParameter::TerrainInputKind ||
                              Parameter->Parameter == EAbilityConfigParameter::TerrainLayer ||
                              Parameter->Parameter == EAbilityConfigParameter::QueryGeometry;
        if (!FMath::IsFinite(Edit.Value) || Edit.Value < Parameter->Minimum || Edit.Value > Parameter->Maximum ||
            (bInteger && Edit.Value != FMath::RoundToFloat(Edit.Value)))
        {
            Error = TEXT("Typed parameter is outside its registered finite/integer bounds.");
            return false;
        }
        OutPatch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
        OutPatch.Parameter = Parameter->Parameter;
        OutPatch.Scalar = Edit.Value;
        return true;
    }
    const bool bDamage = Edit.ModifierId == TEXT("Scale.Damage");
    const bool bRadius = Edit.ModifierId == TEXT("Scale.Radius");
    const bool bDuration = Edit.ModifierId == TEXT("Scale.Duration");
    if (bDamage || bRadius || bDuration)
    {
        if (!FMath::IsFinite(Edit.Value) || Edit.Value > 16.f || Edit.Value < 0.f || (!bDamage && Edit.Value <= 0.f))
        {
            Error = TEXT("Scale must be finite and at most 16; radius/duration must be positive.");
            return false;
        }
        OutPatch.Operation = bDamage   ? EAbilityRecipePatchOperation::DamageScale
                             : bRadius ? EAbilityRecipePatchOperation::RadiusScale
                                       : EAbilityRecipePatchOperation::DurationScale;
        OutPatch.Scalar = Edit.Value;
        return true;
    }
    if (Edit.ModifierId == TEXT("Propagation.Sequential") || Edit.ModifierId == TEXT("Propagation.Forked"))
    {
        const auto* Delivery = Cast<UAbilityDeliveryNodeConfig>(Node.Config);
        if ((Node.TypeId != TEXT("Delivery.ProjectileAwait") && Node.TypeId != TEXT("Propagation.Sequential") &&
             Node.TypeId != TEXT("Propagation.Forked")) ||
            !Delivery)
        {
            Error = TEXT("Sequential propagation replaces an independent ProjectileAwait delivery axis.");
            return false;
        }
        const bool bForked = Edit.ModifierId == TEXT("Propagation.Forked");
        auto* Settings = bForked ? NewObject<UAbilityForkedPropagationNodeConfig>(Outer)
                                 : NewObject<UAbilityPropagationNodeConfig>(Outer);
        Settings->bUseNativeLaunchParameters = Delivery->bUseNativeLaunchParameters;
        Settings->bOverridePreparedCollisionRadius = Delivery->bOverridePreparedCollisionRadius;
        Settings->Delivery = Delivery->Delivery;
        Settings->MaxTargets = Edit.Count;
        Settings->JumpDelay = Edit.Interval;
        Settings->JumpRadius = Edit.Radius;
        Settings->DamageFalloff = Edit.Falloff;
        if (const auto* Previous = Cast<UAbilityPropagationNodeConfig>(Delivery))
        {
            Settings->TargetSelection = Previous->TargetSelection;
        }
        if (auto* Forked = Cast<UAbilityForkedPropagationNodeConfig>(Settings))
        {
            Forked->MaxDepth = Edit.MaxDepth;
            Forked->BranchesPerTarget = Edit.BranchesPerTarget;
            const auto* Previous = Cast<UAbilityForkedPropagationNodeConfig>(Delivery);
            Forked->bConcurrentBranches = Previous ? Previous->bConcurrentBranches : true;
        }
        if (!Settings->Validate(Error))
        {
            return false;
        }
        OutPatch.Operation = EAbilityRecipePatchOperation::ReplaceNode;
        OutPatch.ReplacementTypeId = bForked ? TEXT("Propagation.Forked") : TEXT("Propagation.Sequential");
        OutPatch.ReplacementConfig = Settings;
        return true;
    }
    const bool bAppend = Edit.ModifierId == TEXT("Append.Status.Petrify");
    const bool bReplace = Edit.ModifierId == TEXT("Replace.Status.Petrify");
    if (!bAppend && !bReplace)
    {
        Error = TEXT("Unknown developer modifier ID; arbitrary property reflection is not allowed.");
        return false;
    }
    if (!ValidStatusDuration(Edit.Value, Error))
    {
        return false;
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    const auto* Descriptor = Registry.Find(Node.TypeId);
    if (!Descriptor)
    {
        Error = TEXT("Selected base node has no actual registered execution contract.");
        return false;
    }
    auto* Status = NewObject<UAbilityPetrifyNodeConfig>(Outer);
    Status->DurationSeconds = Edit.Value;
    if (bReplace)
    {
        if (Descriptor->InputPayload == EAbilityPortPayload::TargetSet)
        {
            auto* Batch = NewObject<UAbilityPetrifyBatchConfig>(Outer);
            Batch->Duration = Edit.Value;
            OutPatch.Operation = EAbilityRecipePatchOperation::ReplaceNode;
            OutPatch.ReplacementTypeId = TEXT("Status.PetrifyBatch");
            OutPatch.ReplacementConfig = Batch;
            return true;
        }
        if (Descriptor->InputPayload != EAbilityPortPayload::LiveTarget)
        {
            Error = TEXT("Replacing a payload with petrify requires an explicit LiveTarget or TargetSet input.");
            return false;
        }
        // 原有出边仍由统一编译器校验；不悄悄删除不兼容的 DamageResolved 或库存归还责任。
        OutPatch.Operation = EAbilityRecipePatchOperation::ReplaceNode;
        OutPatch.ReplacementTypeId = TEXT("Status.Petrify");
        OutPatch.ReplacementConfig = Status;
        return true;
    }
    const FAbilityPortDescriptor* Port = Descriptor->Ports.FindByPredicate(
        [](const FAbilityPortDescriptor& Item)
        {
            return Item.Payload == EAbilityPortPayload::ConfirmedImpact;
        });
    if (!Port)
    {
        Port = Descriptor->Ports.FindByPredicate(
            [](const FAbilityPortDescriptor& Item)
            {
                return Item.Payload == EAbilityPortPayload::DamageResult;
            });
    }
    if (!Port)
    {
        Error = TEXT("Appending petrify requires a declared DamageResult or ConfirmedImpact output.");
        return false;
    }
    // 外部payload投递始终发真实Hit，但其可选DamageResolved只有内部伤害模式才会发。
    // 有确认命中就选该事实端口；纯伤害节点只有DamageResult，继续使用伤害后的原生命复核。
    auto* Branch = NewObject<UAbilityGraphDefinition>(Outer);
    Branch->SchemaVersion = 2;
    Branch->CompatibilityProfile = TEXT("UnifiedV2");
    const FGuid Resolve = FGuid::NewGuid();
    const FGuid Apply = FGuid::NewGuid();
    Branch->EntryNodeId = Resolve;
    const FName ResolveType = Port->Payload == EAbilityPortPayload::DamageResult ? TEXT("Target.ResolveDamageTarget")
                                                                                 : TEXT("Target.ResolveImpact");
    Branch->Nodes = {{Resolve, ResolveType, NewObject<UAbilityResolveImpactNodeConfig>(Branch)},
                     {Apply, TEXT("Status.Petrify"), DuplicateObject<UAbilityPetrifyNodeConfig>(Status, Branch)}};
    Branch->Edges = {{Resolve, Apply, TEXT("TargetFound")}};
    // 历史致死命中只能保留位置；先复核原生命，绝不把池复用后的新目标授予状态。
    OutPatch.Operation = EAbilityRecipePatchOperation::AppendBranch;
    OutPatch.OutputPort = Port->Name;
    OutPatch.BranchGraph = Branch;
    return true;
}
