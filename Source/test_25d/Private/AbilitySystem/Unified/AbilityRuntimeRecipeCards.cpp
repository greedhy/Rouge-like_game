// 调用链：人物技能改装页 -> 服务器卡片 ID -> 类型化配方补丁 -> 草稿 -> 原临时技能试放。
// 位置与来源只在服务器解析，复制给拥有者的是可读卡片和安装状态。
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"

#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Unified/AbilityBarrier.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/WorkshopRejuvenationAbilities.h"
#include "AbilitySystem/Unified/WorkshopSurfaceConversionAbilities.h"
#include "Algo/Find.h"

namespace
{
    /** 每个位置独立替换，其他位置可以同时使用。数值与文案由同一服务器目录定义。 */
    struct FWorkshopCard
    {
        FName Id;
        FName Slot;
        const TCHAR* SlotLabel;
        const TCHAR* Label;
        const TCHAR* Description;
        FName TargetType;
        FName Modifier;
        FName Fragment;
        float Value = 1.f;
        FName ScopeConsumer;
        TOptional<EAbilityPrototypeKind> PrototypeKind;
        FName RequiredGraphType;
        TOptional<EExtensionKind> ExtensionKind;
        EAbilityExposure RequiredExposure = EAbilityExposure::None;
        EAbilityElement BonusElement = EAbilityElement::Original;
        FName RequiredAbility;
    };

    const FWorkshopCard Cards[] = {
        {TEXT("Chain"), TEXT("Propagation"), TEXT("传播方式"), TEXT("逐个弹跳"),
         TEXT("每次命中后朝附近另一个敌人发出一枚弹体，每条弹道最多投递 3 枚，含首发。"),
         TEXT("Delivery.ProjectileAwait"), TEXT("Propagation.Sequential"), NAME_None},
        {TEXT("Fork"), TEXT("Propagation"), TEXT("传播方式"), TEXT("同时分裂"),
         TEXT("首次命中后同时向附近敌人发出最多 2 枚子弹。子弹命中后结束传播。"), TEXT("Delivery.ProjectileAwait"),
         TEXT("Propagation.Forked"), NAME_None},
        {TEXT("WeakestJump"), TEXT("JumpTarget"), TEXT("传播目标"), TEXT("追击残血"),
         TEXT("传播时优先选择搜索范围内生命比例最低的敌人。需先安装逐个弹跳或同时分裂。"),
         TEXT("Propagation.Sequential"), TEXT("Set.TargetSelection"), NAME_None,
         static_cast<float>(EAbilityTargetSelectionPolicy::LowestHealthRatio)},
        {TEXT("FarthestJump"), TEXT("JumpTarget"), TEXT("传播目标"), TEXT("越阵"),
         TEXT("传播时优先选择搜索范围内最远的敌人。需先安装逐个弹跳或同时分裂。"), TEXT("Propagation.Sequential"),
         TEXT("Set.TargetSelection"), NAME_None, static_cast<float>(EAbilityTargetSelectionPolicy::Farthest)},
        {TEXT("Ice"), TEXT("Element"), TEXT("伤害元素"), TEXT("冰霜"),
         TEXT("将主攻击转为冰伤并施加冻僵。冰伤命中潮湿或冻僵的敌人可使其冻结。"), TEXT("Effect.InputDamage"),
         TEXT("Set.ElementConversion"), NAME_None, 5.f},
        {TEXT("Water"), TEXT("Element"), TEXT("伤害元素"), TEXT("潮汐"),
         TEXT("将主攻击转为水伤并施加潮湿，为冰冻和导电反应提供条件。"), TEXT("Effect.InputDamage"),
         TEXT("Set.ElementConversion"), NAME_None, 2.f},
        {TEXT("FireElement"), TEXT("Element"), TEXT("伤害元素"), TEXT("引燃"),
         TEXT("将主攻击转为火伤。命中潮湿目标可产生蒸汽。"), TEXT("Effect.InputDamage"),
         TEXT("Set.ElementConversion"), NAME_None, 1.f},
        {TEXT("ThunderElement"), TEXT("Element"), TEXT("伤害元素"), TEXT("导雷"),
         TEXT("将主攻击转为雷伤。命中潮湿或蒸汽目标可触发导电并增加本次伤害。"),
         TEXT("Effect.InputDamage"), TEXT("Set.ElementConversion"), NAME_None, 3.f},
        {TEXT("FlashFreeze"), TEXT("ExposureElement"), TEXT("状态元素"), TEXT("霜雾替换"),
         TEXT("将雨幕的水元素替换为冰元素。首次命中施加冻僵，后续脉冲可使其冻结。"),
         TEXT("Status.ApplyExposure"), TEXT("Set.ExposureElement"), NAME_None, 5.f},
        {TEXT("LongExposure"), TEXT("ExposureDuration"), TEXT("状态时长"), TEXT("余韵"),
         TEXT("本次脉冲新增的元素状态时长增加 50%。"), TEXT("Status.ApplyExposure"), TEXT("Scale.Duration"),
         NAME_None, 1.5f},
        {TEXT("FastRain"), TEXT("RainRate"), TEXT("雨幕节奏"), TEXT("密雨"),
         TEXT("雨幕的脉冲间隔缩短到 0.5 秒，共扫描 3 次。"), TEXT("Control.Repeat"), TEXT("Set.Period"), NAME_None,
         .5f, TEXT("Query.Shape"), {}, TEXT("Status.ApplyExposure")},
        {TEXT("QuickIgnition"), TEXT("Ignition"), TEXT("涌爆引信"), TEXT("急燃"),
         TEXT("涌爆在落点等待 0.2 秒后结算。"), TEXT("Control.DelayOrigin"), TEXT("Set.Duration"), NAME_None,
         .2f, TEXT("Query.Shape"), {}, TEXT("Presentation.ElementPulse")},
        {TEXT("WideMire"), TEXT("TerrainSize"), TEXT("地块尺寸"), TEXT("蔓生"),
         TEXT("放置地表的宽度和长度增加 30%。"), TEXT("World.ElementSurface"), TEXT("Scale.Radius"), NAME_None, 1.3f},
        {TEXT("LongMire"), TEXT("TerrainLifetime"), TEXT("地块寿命"), TEXT("久留"),
         TEXT("新地表的寿命从 8 秒增加到 12 秒。"), TEXT("World.ElementSurface"), TEXT("Scale.Duration"), NAME_None,
         1.5f},
        {TEXT("FrostMire"), TEXT("TerrainKind"), TEXT("地块元素"), TEXT("霜地替换"),
         TEXT("将所造地表替换为霜地，减慢接触者。熔沼引燃需要毒地。"), TEXT("World.ElementSurface"),
         TEXT("Set.TerrainSurfaceKind"), NAME_None, 1.f},
        {TEXT("DistantSpark"), TEXT("IgniteReach"), TEXT("引燃范围"), TEXT("远距引燃"),
         TEXT("搜索自有毒地的半径从 3.6 米扩大到 5.4 米。"), TEXT("World.IgniteSurface"), TEXT("Scale.Radius"),
         NAME_None, 1.5f},
        {TEXT("RushingFrost"), TEXT("FrontSpeed"), TEXT("波前速度"), TEXT("疾行冰锋"),
         TEXT("冰浪推进速度提高到每秒 15 米。"), TEXT("Spatial.Wavefront"), TEXT("Set.Speed"), NAME_None, 1500.f,
         TEXT("Effect.Damage"), {}, TEXT("Presentation.ElementImpact")},
        {TEXT("FarFrost"), TEXT("FrontReach"), TEXT("波前半径"), TEXT("广域冰锋"),
         TEXT("冰浪最大半径扩大到 6.5 米。"), TEXT("Spatial.Wavefront"), TEXT("Set.Radius"), NAME_None, 650.f,
         TEXT("Effect.Damage"), {}, TEXT("Presentation.ElementImpact")},
        {TEXT("SteamConductors"), TEXT("ConductorFilter"), TEXT("放电条件"), TEXT("蒸汽导体"),
         TEXT("改为寻找蒸汽状态的敌人。"), TEXT("Query.SelectTargets"), TEXT("Set.TargetFilter"), NAME_None, 8.f,
         TEXT("Effect.Damage"), {}, TEXT("Presentation.ElementImpact")},
        {TEXT("ExtraConductors"), TEXT("ConductorCount"), TEXT("放电目标数"), TEXT("多重导体"),
         TEXT("放电目标上限从 3 个增加到 5 个。"), TEXT("Query.SelectTargets"), TEXT("Set.Count"), NAME_None, 5.f,
         TEXT("Effect.Damage"), {}, TEXT("Presentation.ElementImpact")},
        {TEXT("WideDispel"), TEXT("DispelReach"), TEXT("涤地区域"), TEXT("广域涤地"),
         TEXT("地表搜索半径增加 50%。"), TEXT("World.DispelSurface"), TEXT("Scale.Radius"), NAME_None, 1.5f},
        {TEXT("ToxicOnlyDispel"), TEXT("DispelKind"), TEXT("清除对象"), TEXT("优先除毒"),
         TEXT("只清除符合原归属规则的毒地。"), TEXT("World.DispelSurface"), TEXT("Set.TerrainSurfaceKind"),
         NAME_None, 2.f},
        {TEXT("WideShatter"), TEXT("Shape"), TEXT("裂霜范围"), TEXT("宽幅裂霜"),
         TEXT("裂霜的直线完整宽度扩大到 3 米。"), TEXT("Query.Shape"), TEXT("Set.Width"), NAME_None, 300.f,
         NAME_None, {}, TEXT("Status.ConsumeExposure")},
        {TEXT("ChilledShatter"), TEXT("ShatterState"), TEXT("碎裂条件"), TEXT("冻僵碎裂"),
         TEXT("改为打击冻僵目标，并在实际扣血后消耗其冻僵。"), TEXT("Condition.ElementalStatus"),
         TEXT("Set.ElementalStatus"), NAME_None, static_cast<float>(EAbilityExposure::Chilled),
         TEXT("Effect.Damage"), {}, TEXT("Status.ConsumeExposure")},
        {TEXT("WideGale"), TEXT("Shape"), TEXT("风压范围"), TEXT("宽角阵风"),
         TEXT("扇形的完整开角扩大到 80 度。"), TEXT("Query.Shape"), TEXT("Set.Spread"), NAME_None, 80.f, NAME_None, {},
         TEXT("Movement.TargetDisplace")},
        {TEXT("StrongGale"), TEXT("GaleTravel"), TEXT("位移强度"), TEXT("强风推移"),
         TEXT("推移距离上限改为 3.3 米，仍受场景碰撞约束。"), TEXT("Movement.TargetDisplace"), TEXT("Set.MoveDistance"),
         NAME_None, 330.f, NAME_None, {}, TEXT("Spatial.Anchor")},
        {TEXT("SteamGale"), TEXT("GaleFilter"), TEXT("位移条件"), TEXT("蒸汽导流"),
         TEXT("改为推开蒸汽状态的敌人。"), TEXT("Condition.ElementalStatus"), TEXT("Set.ElementalStatus"),
         NAME_None, static_cast<float>(EAbilityExposure::Steam), TEXT("Movement.TargetDisplace")},
        {TEXT("FastEmbers"), TEXT("EmberFuse"), TEXT("灼印引信"), TEXT("急速迸火"),
         TEXT("等待时间缩至 0.12 秒，再筛选爆裂中心。"), TEXT("Control.DelayOrigin"), TEXT("Set.Duration"), NAME_None,
         .12f, TEXT("Query.SelectTargets"), {}, TEXT("Spatial.TargetOrigin")},
        {TEXT("VolatileReach"), TEXT("Shape"), TEXT("局部爆裂范围"), TEXT("灼印溅射"),
         TEXT("每个灼印的局部爆裂半径增加 30%。"), TEXT("Query.Shape"), TEXT("Scale.Radius"), NAME_None, 1.3f,
         NAME_None, {}, TEXT("Spatial.TargetOrigin")},
        {TEXT("ConductiveSnare"), TEXT("ReactionControl"), TEXT("反应控制"), TEXT("导电束缚"),
         TEXT("主攻击实际造成伤害后，减速仍处于导电状态的存活敌人 1.5 秒。"),
         TEXT("Effect.Damage"), NAME_None, NAME_None, 1.5f, NAME_None, {}, TEXT("Presentation.ElementPulse"), {},
         EAbilityExposure::Conductive},
        {TEXT("SteamVeil"), TEXT("ReactionControl"), TEXT("反应控制"), TEXT("蒸汽滞留"),
         TEXT("主攻击实际造成伤害后，减速仍处于蒸汽状态的存活敌人 2 秒。"),
         TEXT("Effect.Damage"), NAME_None, NAME_None, 2.f, NAME_None, {}, TEXT("Presentation.ElementPulse"), {},
         EAbilityExposure::Steam},
        {TEXT("Scatter"), TEXT("Launch"), TEXT("出手方式"), TEXT("三重散射"),
         TEXT("出手时在扇面内发出 3 枚弹体，各自飞行和命中。"), TEXT("Distribution.Scatter"), TEXT("Set.Count"),
         NAME_None, 3.f},
        {TEXT("Homing"), TEXT("Launch"), TEXT("出手方式"), TEXT("追踪"), TEXT("初始弹体在飞行中向锁定目标转向。"),
         TEXT("Delivery.PrototypeFlight"), TEXT("Set.Motion"), NAME_None, 1.f},
        {TEXT("FirstStop"), TEXT("FlightContact"), TEXT("弹体接触"), TEXT("首击停弹"),
         TEXT("原可穿透的弹体命中第一个敌人后即停止飞行，该技能原有的飞行结束效果随即触发。"),
         TEXT("Delivery.PrototypeFlight"), TEXT("Set.ImpactPolicy"), NAME_None, 0.f, NAME_None,
         EAbilityPrototypeKind::Piercing},
        {TEXT("RingCore"), TEXT("Shape"), TEXT("伤害范围"), TEXT("近身震环"),
         TEXT("环形伤害的内半径缩为 30 厘米，扩大向内覆盖的范围。"), TEXT("Scope.Prototype"), TEXT("Set.InnerRadius"),
         NAME_None, 30.f, NAME_None, EAbilityPrototypeKind::Ring},
        {TEXT("WideLane"), TEXT("Shape"), TEXT("伤害范围"), TEXT("宽幅断层"), TEXT("直线攻击的总宽度扩大到 2.6 米。"),
         TEXT("Scope.Prototype"), TEXT("Set.Width"), NAME_None, 260.f, NAME_None, EAbilityPrototypeKind::Line},
        {TEXT("WideArea"), TEXT("Shape"), TEXT("作用范围"), TEXT("扩展范围"), TEXT("主范围的半径或扇面距离扩大 25%。"),
         TEXT("Query.Shape"), TEXT("Scale.Radius"), NAME_None, 1.25f},
        {TEXT("Petrify"), TEXT("Contact"), TEXT("命中效果"), TEXT("石化印记"),
         TEXT("在主攻击结算后，将仍然存活的命中目标石化 1 秒。"), NAME_None, TEXT("Append.Registered"),
         TEXT("Target.Petrify"), 1.f},
        {TEXT("LowHealthStone"), TEXT("Contact"), TEXT("命中效果"), TEXT("残血石化"),
         TEXT("主攻击结算后，仍存活且生命低于 35% 的目标会石化 2 秒。"), NAME_None, NAME_None, NAME_None},
        {TEXT("SlowStrike"), TEXT("Contact"), TEXT("命中效果"), TEXT("迟缓印记"),
         TEXT("主攻击结算后，使仍然存活的命中目标减速 2 秒。"), NAME_None, TEXT("Append.Registered"),
         TEXT("Target.Slow"), 2.f},
        {TEXT("GuardOnHit"), TEXT("BarrierFeedback"), TEXT("护盾反馈"), TEXT("命中护盾"),
         TEXT("主攻击首次实际造成伤害后，获得 25 点护盾，持续 3 秒。每次施法最多触发一次。"),
         NAME_None, NAME_None, NAME_None},
        {TEXT("HeavyBarrier"), TEXT("BarrierCapacity"), TEXT("护盾容量"), TEXT("厚重屏障"),
         TEXT("护盾的授予量提高到 60 点。"), TEXT("Status.Barrier"), TEXT("Set.BarrierCapacity"), NAME_None, 60.f},
        {TEXT("LingeringBarrier"), TEXT("BarrierDuration"), TEXT("护盾时长"), TEXT("延续屏障"),
         TEXT("护盾的持续时间延长到 6 秒。"), TEXT("Status.Barrier"), TEXT("Set.Duration"), NAME_None, 6.f},
        {TEXT("LongStasis"), TEXT("ControlDuration"), TEXT("控制时长"), TEXT("延长凝滞"),
         TEXT("凝滞力场的基础石化时长增加 50%。"), TEXT("Status.Petrify"), TEXT("Scale.Duration"), NAME_None, 1.5f,
         NAME_None, {}, TEXT("Query.Shape")},
        {TEXT("WetStrike"),
         TEXT("ElementReaction"),
         TEXT("元素联动"),
         TEXT("潮湿引雷"),
         TEXT("主攻击结算后，对仍潮湿的存活目标追加基础 12 点雷伤，可触发导电。"),
         NAME_None,
         NAME_None,
         NAME_None,
         12.f,
         NAME_None,
         {},
         NAME_None,
         {},
         EAbilityExposure::Wet,
         EAbilityElement::Thunder},
        {TEXT("FrozenEcho"),
         TEXT("ElementReaction"),
         TEXT("元素联动"),
         TEXT("冻结回响"),
         TEXT("主攻击结算后，对仍在冻结中的存活目标追加基础 14 点奥术伤害。"),
         NAME_None,
         NAME_None,
         NAME_None,
         14.f,
         NAME_None,
         {},
         NAME_None,
         {},
         EAbilityExposure::Frozen,
         EAbilityElement::Arcane},
        {TEXT("Mana"), TEXT("Resource"), TEXT("资源反馈"), TEXT("回流"),
         TEXT("主攻击实际造成伤害后恢复 5 点法力，每次施法最多恢复一次。"), NAME_None, TEXT("Append.Registered"),
         TEXT("Effect.RestoreMana"), 5.f},
        {TEXT("RhythmMana"), TEXT("Resource"), TEXT("资源反馈"), TEXT("三响回流"),
         TEXT("主攻击在同一次施法中第 3 次实际造成伤害时，恢复 12 点法力。同一敌人可计入多次，最多恢复一次。"),
         NAME_None, NAME_None, NAME_None},
        {TEXT("BloodReturn"), TEXT("Resource"), TEXT("资源反馈"), TEXT("汲取"),
         TEXT("主攻击实际造成伤害后恢复 10 点生命，每次施法最多恢复一次。"), NAME_None, TEXT("Append.Registered"),
         TEXT("Effect.RestoreHealth"), 10.f},
        {TEXT("KillBurst"), TEXT("Kill"), TEXT("击杀时"), TEXT("烈火溅射"),
         TEXT("每次主攻击击杀敌人时，对击杀位置 2 米内最多 6 个敌人造成基础 10 点火伤。溅射击杀不会再次触发。"),
         NAME_None, NAME_None, NAME_None},
        {TEXT("QuickTrap"), TEXT("Arming"), TEXT("布防"), TEXT("快速布防"), TEXT("地雷在放置 0.4 秒后即可被敌人触发。"),
         TEXT("Scope.Prototype"), TEXT("Set.ArmDelay"), NAME_None, .4f, TEXT("Control.PrototypeTrap")},
        {TEXT("FastFuse"),
         TEXT("Fuse"),
         TEXT("引信"),
         TEXT("急速涟漪"),
         TEXT("地雷启动引信后仅等待 0.25 秒就放出冲击波，布防时间保持原样。"),
         TEXT("Scope.Prototype"),
         TEXT("Set.ArmDelay"),
         NAME_None,
         .25f,
         TEXT("Control.PrototypeDelay"),
         {},
         TEXT("Control.PrototypeTrap")},
        {TEXT("FastPulse"), TEXT("Cadence"), TEXT("领域节奏"), TEXT("密集脉冲"),
         TEXT("领域每 0.4 秒结算一次伤害，持续时间与到期效果保持原样。"), TEXT("Scope.Prototype"), TEXT("Set.Period"),
         NAME_None, .4f, TEXT("Control.FieldCadence")},
        {TEXT("DeepField"), TEXT("AreaDuration"), TEXT("领域持续"), TEXT("持久领域"),
         TEXT("有限领域的持续时间增加 50%，脉冲间隔保持原样。"), TEXT("Scope.Prototype"), TEXT("Scale.Duration"),
         NAME_None, 1.5f, TEXT("Control.FieldCadence")},
        {TEXT("SentryStock"), TEXT("Sentry"), TEXT("哨塔连射"), TEXT("备用弹"),
         TEXT("哨塔在存续期间最多发射 4 次，仍需在射程内找到敌人。"), TEXT("Scope.Prototype"), TEXT("Set.Count"),
         NAME_None, 4.f, TEXT("Control.PrototypeEmitter")},
        {TEXT("ExpandedNova"), TEXT("NovaShape"), TEXT("新星范围"), TEXT("扩散新星"),
         TEXT("奥术新星的基础伤害半径扩大 30%。"), TEXT("Query.NovaTargets"), TEXT("Scale.Radius"), NAME_None, 1.3f},
        {TEXT("ShardReach"), TEXT("ShardReach"), TEXT("碎片范围"), TEXT("扩散碎片"),
         TEXT("每枚奥术碎片的基础伤害半径扩大 25%。"), TEXT("Control.ShardPoints"), TEXT("Scale.Radius"), NAME_None,
         1.25f},
        {TEXT("LightningReach"), TEXT("LightningReach"), TEXT("闪电搜敌"), TEXT("延伸电弧"),
         TEXT("闪电搜索后续目标的半径扩大 35%。"), TEXT("Query.LightningTargets"), TEXT("Scale.Radius"), NAME_None,
         1.35f},
        {TEXT("FocusedGaze"), TEXT("GazeMode"), TEXT("凝视方式"), TEXT("集中石化"),
         TEXT("优先石化范围内生命比例最低的 3 个敌人，石化时长延长 50%。"), TEXT("Query.PetrifyArea"),
         TEXT("Set.Count"), NAME_None, 3.f},
        {TEXT("WideGaze"), TEXT("GazeRadius"), TEXT("凝视范围"), TEXT("广域凝视"),
         TEXT("石化凝视的基础选择半径扩大 30%。"), TEXT("Query.PetrifyArea"), TEXT("Scale.Radius"), NAME_None, 1.3f},
        {TEXT("StrongDisplace"),
         TEXT("MoveDistance"),
         TEXT("位移距离"),
         TEXT("强力位移"),
         TEXT("单个目标的牵引或击退距离上限设为 3 米，仍受地形碰撞和技能原规则限制。"),
         TEXT("Scope.Prototype"),
         TEXT("Set.MoveDistance"),
         NAME_None,
         300.f,
         NAME_None,
         {},
         NAME_None,
         EExtensionKind::Displace},
        {TEXT("TightPull"),
         TEXT("PullStop"),
         TEXT("聚拢终点"),
         TEXT("紧密聚拢"),
         TEXT("拉近目标时，离聚拢中心的保留距离缩至 0.5 米。"),
         TEXT("Scope.Prototype"),
         TEXT("Set.SafeDistance"),
         NAME_None,
         50.f,
         NAME_None,
         {},
         NAME_None,
         EExtensionKind::Displace},
        {TEXT("RapidWave"), TEXT("WaveSpeed"), TEXT("波前速度"), TEXT("迅疾震波"),
         TEXT("震波的波前速度设为每秒 6.5 米。"), TEXT("Scope.Prototype"), TEXT("Set.Speed"), NAME_None, 650.f,
         TEXT("Spatial.PrototypeWave"), EAbilityPrototypeKind::Wave},
        {TEXT("FarTransfer"),
         TEXT("Transfer"),
         TEXT("附着转移"),
         TEXT("远距转移"),
         TEXT("附着目标死亡时，在 6 米内寻找可转移的目标，转移次数沿用基础技能。"),
         TEXT("Scope.Prototype"),
         TEXT("Set.TransferRadius"),
         NAME_None,
         600.f,
         NAME_None,
         {},
         NAME_None,
         EExtensionKind::Attach},
        {TEXT("FastStack"),
         TEXT("Stack"),
         TEXT("叠层引爆"),
         TEXT("两层引爆"),
         TEXT("标记累计两层即可触发原有爆裂，命中与叠层寿命沿用基础技能。"),
         TEXT("Scope.Prototype"),
         TEXT("Set.StackThreshold"),
         NAME_None,
         2.f,
         NAME_None,
         {},
         NAME_None,
         EExtensionKind::Stack},
        {TEXT("EndBurst"), TEXT("Expiry"), TEXT("结束时"), TEXT("余震"),
         TEXT("领域自然结束时额外爆裂一次，对中心周围 2.5 米内最多 8 个敌人造成基础 12 点雷伤。"),
         TEXT("Control.FieldCadence"), NAME_None, NAME_None},
        {TEXT("DelayedBurst"), TEXT("Expiry"), TEXT("结束时"), TEXT("延时雷爆"),
         TEXT("领域自然结束 0.5 秒后，在原中心周围 2.5 米内对最多 8 个敌人造成基础 12 点雷伤。"),
         TEXT("Control.FieldCadence"), NAME_None, NAME_None, .5f},
        {TEXT("ExtraTides"), TEXT("TidalCadence"), TEXT("恢复节奏"), TEXT("快潮"),
         TEXT("恢复间隔缩短为 0.35 秒，首跳仍然立即发生。"), TEXT("Control.Repeat"), TEXT("Set.Period"),
         NAME_None, .35f, TEXT("Effect.RestoreSelf")},
        {TEXT("DeepMend"), TEXT("RecoveryAmount"), TEXT("恢复量"), TEXT("深潮"),
         TEXT("每跳恢复 15 点生命。"), TEXT("Effect.RestoreSelf"), TEXT("Set.ResourceAmount"), NAME_None, 15.f},
        {TEXT("WarmAegisCleanse"), TEXT("CleanseStatus"), TEXT("净化状态"), TEXT("熄焰"),
         TEXT("霜甲净化改为解除灼热，护盾照常授予。"), TEXT("Status.ClearSelfExposure"), TEXT("Set.ElementalStatus"),
         NAME_None, 1.f},
        {TEXT("FarConvert"), TEXT("SurfaceRange"), TEXT("地表范围"), TEXT("远域转化"),
         TEXT("查找可转化地表的距离增加 50%。"), TEXT("World.ConvertSurface"), TEXT("Scale.Radius"), NAME_None, 1.5f},
        {TEXT("FromVines"), TEXT("SurfaceSource"), TEXT("原地表"), TEXT("藤地媒介"),
         TEXT("改为转化自己生成的藤蔓地表。"), TEXT("World.ConvertSurface"), TEXT("Set.TerrainInputKind"), NAME_None,
         3.f},
        {TEXT("IntoVines"), TEXT("SurfaceResult"), TEXT("转化结果"), TEXT("化为藤地"),
         TEXT("将地表转化为藤蔓。原地表和结果必须不同。"), TEXT("World.ConvertSurface"), TEXT("Set.TerrainSurfaceKind"),
         NAME_None, 3.f},
        {TEXT("WideReactions"), TEXT("SelectionRadius"), TEXT("状态范围"), TEXT("扩展反应"),
         TEXT("在选定落点 5 米内查找具有指定状态的敌人。"), TEXT("Query.SelectTargets"), TEXT("Set.Radius"), NAME_None,
         500.f, TEXT("Status.ConsumeExposure")},
        {TEXT("CondenseIce"), TEXT("ReactionResult"), TEXT("转化状态"), TEXT("凝成寒霜"),
         TEXT("消耗蒸汽后施加冰霜，可与目标已有的元素状态反应。"), TEXT("Status.ApplyExposure"),
         TEXT("Set.ExposureElement"), NAME_None, 5.f, NAME_None, {}, TEXT("Status.ConsumeExposure")},
        {TEXT("DeepRecharge"), TEXT("RecoveryAmount"), TEXT("恢复量"), TEXT("聚导回充"),
         TEXT("消耗导电后恢复 15 点法力，每次施法最多一次。"), TEXT("Effect.RestoreSelf"), TEXT("Set.ResourceAmount"),
         NAME_None, 15.f},
        {TEXT("FastThunderMark"), TEXT("MarkDelay"), TEXT("印记引信"), TEXT("骤雷"),
         TEXT("雷印从锁定地点到爆发只等待 0.18 秒。"), TEXT("Control.DelayOrigin"), TEXT("Set.Duration"), NAME_None,
         .18f, TEXT("Query.Shape"), {}, TEXT("Spatial.TargetOrigin")},
        {TEXT("MarkedConductors"), TEXT("MarkedStatus"), TEXT("印记筛选"), TEXT("搜寻导电"),
         TEXT("雷印优先查找处于导电状态的敌人并锁定其坐标。"), TEXT("Query.SelectTargets"), TEXT("Set.TargetFilter"),
         NAME_None, 9.f, TEXT("Spatial.TargetOrigin"), {}, TEXT("Control.DelayOrigin")},
        {TEXT("WideFrostShower"), TEXT("PointSpread"), TEXT("落点分布"), TEXT("广布霜晶"),
         TEXT("后续霜晶落点分布在准星周围 3 米内，每处冰爆范围沿用当前改装。"), TEXT("Control.StaggeredPoints"),
         TEXT("Set.Radius"), NAME_None, 300.f},
        {TEXT("MoreFrostShower"), TEXT("PointCount"), TEXT("落点数量"), TEXT("密集霜晶"),
         TEXT("持续生成 6 个霜晶落点，落点之间的间隔沿用当前设置。"), TEXT("Control.StaggeredPoints"),
         TEXT("Set.Count"), NAME_None, 6.f},
        {TEXT("WideShallows"), TEXT("TerrainSize"), TEXT("地块尺寸"), TEXT("漫潮"),
         TEXT("地块的半宽和半长增加到 3.2 米。"), TEXT("World.ElementSurface"), TEXT("Set.Radius"), NAME_None, 320.f,
         NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.WaterShallows")},
        {TEXT("LongShallows"), TEXT("TerrainLifetime"), TEXT("地块寿命"), TEXT("留潮"),
         TEXT("潮地的寿命延长到 9 秒。"), TEXT("World.ElementSurface"), TEXT("Set.Duration"), NAME_None, 9.f,
         NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.WaterShallows")},
        {TEXT("ToxicTide"), TEXT("SurfaceResult"), TEXT("转化结果"), TEXT("潮生毒沼"),
         TEXT("把原地表转成毒地，继承原地块的大小和寿命。"), TEXT("World.ConvertSurface"),
         TEXT("Set.TerrainSurfaceKind"), NAME_None, 2.f},
        {TEXT("ThawSurface"), TEXT("SurfaceResult"), TEXT("转化结果"), TEXT("融霜成潮"),
         TEXT("把原地表转成潮地，继承原地块的大小和寿命。"), TEXT("World.ConvertSurface"),
         TEXT("Set.TerrainSurfaceKind"), NAME_None, 4.f},
        {TEXT("WaterSource"), TEXT("SurfaceSource"), TEXT("原地表"), TEXT("潮地媒介"),
         TEXT("改为转化自己生成的潮地。原地表和结果须不同。"), TEXT("World.ConvertSurface"),
         TEXT("Set.TerrainInputKind"), NAME_None, 4.f},
        {TEXT("WideHarvest"), TEXT("HarvestRadius"), TEXT("回收范围"), TEXT("远域回收"),
         TEXT("可用于回收的地表搜索半径增加 50%。"), TEXT("World.DispelSurface"), TEXT("Scale.Radius"), NAME_None, 1.5f},
        {TEXT("HarvestWater"), TEXT("HarvestSource"), TEXT("回收地表"), TEXT("汲潮"),
         TEXT("改为消耗一块自有潮地。成功后仍给予本技能的回馈。"), TEXT("World.DispelSurface"),
         TEXT("Set.TerrainSurfaceKind"), NAME_None, 4.f},
        {TEXT("PhaseChilled"), TEXT("PhaseGate"), TEXT("分流条件"), TEXT("冻僵分流"),
         TEXT("按冻僵状态分流，匹配与未匹配的两路效果保持各自当前改装。"), TEXT("Condition.ElementalStatus"),
         TEXT("Set.ElementalStatus"), NAME_None, static_cast<float>(EAbilityExposure::Chilled)},
        {TEXT("PhaseArcane"), TEXT("PhaseMatchElement"), TEXT("匹配时元素"), TEXT("奥术潮痕"),
         TEXT("只将满足分流条件的那一路改为奥术伤害。"), TEXT("Effect.Damage"), TEXT("Set.ElementConversion"),
         NAME_None, 4.f},
        {TEXT("PhaseFrost"), TEXT("PhaseOtherElement"), TEXT("未匹配时元素"), TEXT("寒潮余响"),
         TEXT("只将未满足分流条件的那一路改为冰伤。"), TEXT("Effect.Damage"), TEXT("Set.ElementConversion"),
         NAME_None, 5.f},
        {TEXT("LongEmberRush"), TEXT("RushDistance"), TEXT("突进距离"), TEXT("长距飞焰"),
         TEXT("突进上限提高到 8.5 米，仍由瞄准距离、地面和阻挡决定实际终点。"), TEXT("Movement.Character"),
         TEXT("Set.MoveDistance"), NAME_None, 850.f, TEXT("World.ElementSurface")},
        {TEXT("QuickEmberRush"), TEXT("RushDuration"), TEXT("突进时长"), TEXT("迅焰"),
         TEXT("突进的计划时长缩短到 0.18 秒。"), TEXT("Movement.Character"), TEXT("Set.Duration"), NAME_None, .18f,
         TEXT("World.ElementSurface")},
        {TEXT("FrostRushFloor"), TEXT("RushFloorKind"), TEXT("落点元素"), TEXT("霜落"),
         TEXT("实际落点改为生成霜地。"), TEXT("World.ElementSurface"), TEXT("Set.TerrainSurfaceKind"), NAME_None, 1.f},
        {TEXT("WideRushFloor"), TEXT("RushFloorRadius"), TEXT("落点范围"), TEXT("焰落扩散"),
         TEXT("实际落点地块的半宽和半长扩大到 2.2 米。"), TEXT("World.ElementSurface"), TEXT("Set.Radius"),
         NAME_None, 220.f},
        {TEXT("LingeringRushFloor"), TEXT("RushFloorDuration"), TEXT("落点寿命"), TEXT("余焰"),
         TEXT("实际落点地块的寿命延长到 6 秒。"), TEXT("World.ElementSurface"), TEXT("Set.Duration"), NAME_None, 6.f},
        {TEXT("FrostMistSource"), TEXT("SurfaceSource"), TEXT("原地表"), TEXT("霜地升雾"),
         TEXT("改为把自有霜地升为雾云。"), TEXT("World.ConvertSurface"), TEXT("Set.TerrainInputKind"), NAME_None, 1.f,
         NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original, TEXT("Abilities.Mage.MistLift")},
        {TEXT("CondenseChargedMist"), TEXT("SurfaceSource"), TEXT("原云层"), TEXT("通电雾凝"),
         TEXT("改为凝结通电云，产物保持当前设置。"), TEXT("World.ConvertSurface"), TEXT("Set.TerrainInputKind"),
         NAME_None, 7.f, NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.MistCondense")},
        {TEXT("GroundStormMist"), TEXT("SurfaceResult"), TEXT("转化结果"), TEXT("沉雷"),
         TEXT("把雾云转化为地面的通电地块。"), TEXT("World.ConvertSurface"), TEXT("Set.TerrainSurfaceKind"),
         NAME_None, 5.f, NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.StormMist")},
        {TEXT("WideMistDispel"), TEXT("DispelReach"), TEXT("驱雾范围"), TEXT("长风驱雾"),
         TEXT("搜索同层自有云的范围扩大 50%。"), TEXT("World.DispelSurface"), TEXT("Scale.Radius"), NAME_None, 1.5f,
         NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.MistDispel")},
        {TEXT("OnlyChargedMist"), TEXT("DispelKind"), TEXT("驱散对象"), TEXT("截雷"),
         TEXT("只驱散通电云。"), TEXT("World.DispelSurface"), TEXT("Set.TerrainSurfaceKind"), NAME_None, 7.f,
         NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.MistDispel")},
        {TEXT("CatalystReach"), TEXT("CatalystReach"), TEXT("供能搜索"), TEXT("远域借势"),
         TEXT("每次脉冲的自有地块搜索范围提高 50%。"), TEXT("World.FindOwnedSurface"), TEXT("Scale.Radius"), NAME_None,
         1.5f},
        {TEXT("CatalystWater"), TEXT("CatalystSource"), TEXT("供能地表"), TEXT("潮地供能"),
         TEXT("每次脉冲改为从附近自有潮地取势。"), TEXT("World.FindOwnedSurface"), TEXT("Set.TerrainInputKind"),
         NAME_None, 4.f},
        {TEXT("FastCatalyst"), TEXT("CatalystCadence"), TEXT("取势节奏"), TEXT("急促借势"),
         TEXT("脉冲间隔缩至 0.38 秒，仍会逐次查询有效地块。"), TEXT("Control.Repeat"), TEXT("Set.Period"), NAME_None,
         .38f, TEXT("World.FindOwnedSurface")},
        {TEXT("WideCatalyst"), TEXT("CatalystArea"), TEXT("脉冲半径"), TEXT("广域借势"),
         TEXT("从实际地块中心触发的脉冲半径提高到 3 米。"), TEXT("Query.Shape"), TEXT("Set.Radius"), NAME_None, 300.f,
         NAME_None, {}, TEXT("World.FindOwnedSurface")},
        {TEXT("LongFrostBinding"), TEXT("CatalystSlow"), TEXT("减速时长"), TEXT("久霜缠足"),
         TEXT("霜地借势的减速时间提高 50%。"), TEXT("Status.Slow"), TEXT("Scale.Duration"), NAME_None, 1.5f,
         NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.FrostCatalyst")},
        {TEXT("RuptureSteam"), TEXT("ComboStatus"), TEXT("裂爆印记"), TEXT("蒸汽裂爆"),
         TEXT("同步改为寻找并消耗蒸汽。"), TEXT("Query.SelectTargets"), TEXT("Set.TargetFilter"), NAME_None,
         3.f + static_cast<float>(EAbilityExposure::Steam), TEXT("Status.ConsumeExposure"), {}, NAME_None, {},
         EAbilityExposure::Steam, EAbilityElement::Original, TEXT("Abilities.Mage.EmberRupture")},
        {TEXT("WideEmberRupture"), TEXT("Shape"), TEXT("裂爆半径"), TEXT("广聚裂爆"),
         TEXT("以印记为中心的爆发半径扩大至 3.1 米。"), TEXT("Query.Shape"), TEXT("Set.Radius"), NAME_None, 310.f,
         NAME_None, {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.EmberRupture")},
        {TEXT("RelayShocked"), TEXT("ComboStatus"), TEXT("传递印记"), TEXT("感电引渡"),
         TEXT("同步改为寻找并消耗感电。"), TEXT("Query.SelectTargets"), TEXT("Set.TargetFilter"), NAME_None,
         3.f + static_cast<float>(EAbilityExposure::Shocked), TEXT("Status.ConsumeExposure"), {}, NAME_None, {},
         EAbilityExposure::Shocked, EAbilityElement::Original, TEXT("Abilities.Mage.ConductiveRelay")},
        {TEXT("WideRelay"), TEXT("RelayRadius"), TEXT("传递半径"), TEXT("远距传雷"),
         TEXT("后续传递从原目标处搜索距离提高到 4.4 米。"), TEXT("Query.SelectTargets"), TEXT("Set.Radius"), NAME_None,
         440.f},
        {TEXT("MoreRelayTargets"), TEXT("RelayCount"), TEXT("传递目标"), TEXT("分流传雷"),
         TEXT("后续传递最多波及五名敌人，仍排除原印记载体。"), TEXT("Query.SelectTargets"), TEXT("Set.Count"), NAME_None,
         5.f},
        {TEXT("QuickRelay"), TEXT("RelayDelay"), TEXT("传递延迟"), TEXT("骤然传雷"),
         TEXT("消耗印记至后续传递的等待缩至 0.12 秒。"), TEXT("Control.DelayOrigin"), TEXT("Set.Duration"), NAME_None,
         .12f, TEXT("Query.SelectTargets"), {}, NAME_None, {}, EAbilityExposure::None, EAbilityElement::Original,
         TEXT("Abilities.Mage.ConductiveRelay")},
    };

    /** 通过技能身份保留水滩专用文案，改装改变地表枚举后仍可撤掉原卡。 */
    bool CardFitsAbility(const FWorkshopCard& Card, FGameplayTag AbilityTag)
    {
        const FName Tag = AbilityTag.GetTagName();
        if (!Card.RequiredAbility.IsNone())
        {
            return Tag == Card.RequiredAbility;
        }
        if (Tag == TEXT("Abilities.Mage.WaterShallows") &&
            (Card.Id == TEXT("WideMire") || Card.Id == TEXT("LongMire") || Card.Id == TEXT("FrostMire")))
        {
            return false;
        }
        if (Tag == TEXT("Abilities.Mage.MistDispel") &&
            (Card.Id == TEXT("WideDispel") || Card.Id == TEXT("ToxicOnlyDispel")))
        {
            return false;
        }
        const bool bWaterConverter = Tag == TEXT("Abilities.Mage.WaterFreeze") ||
                                     Tag == TEXT("Abilities.Mage.WaterElectrify");
        if (Card.Id == TEXT("ToxicTide"))
        {
            return bWaterConverter;
        }
        if (Card.Id == TEXT("WaterSource"))
        {
            return !bWaterConverter;
        }
        if (Tag == TEXT("Abilities.Mage.ConductiveRelay") &&
            (Card.Slot == TEXT("ReactionControl") || Card.Slot == TEXT("ElementReaction") ||
             Card.Id == TEXT("LowHealthStone") || Card.Id == TEXT("KillBurst")))
        {
            // 雷印伤害已在第六级。这些后继至少再展开三层，超过有限图的八层上限。
            return false;
        }
        // 通用命中卡的合同只有一个主伤害节点。两相技能使用精确分支卡，避免悄悄只改其中一路。
        return Tag != TEXT("Abilities.Mage.TidalPhases") ||
               (!Card.TargetType.IsNone() && Card.Slot != TEXT("Element") && Card.Slot != TEXT("ReactionControl"));
    }

    /** 从准确的边找动作阶段，避免误改共享同一配置类的相邻阶段。 */
    bool ConnectsTo(const UAbilityGraphDefinition& Graph, FGuid From, FName ConsumerType)
    {
        return Graph.Edges.ContainsByPredicate(
            [&Graph, From, ConsumerType](const FAbilityGraphEdge& Edge)
            {
                if (Edge.From != From)
                {
                    return false;
                }
                return Graph.Nodes.ContainsByPredicate(
                    [&Edge, ConsumerType](const FAbilityGraphNode& Target)
                    {
                        return Target.NodeId == Edge.To && Target.TypeId == ConsumerType;
                    });
            });
    }

    /** 只按主图的准确业务输出匹配后继，不读取失败/结束边或同名追加分支。 */
    const FAbilityGraphNode* FindConnectedNode(const UAbilityGraphDefinition& Graph, const FAbilityGraphNode& Source,
                                               FName Port, FName Type)
    {
        return Graph.Nodes.FindByPredicate(
            [&Graph, &Source, Port, Type](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == Type &&
                       Graph.Edges.ContainsByPredicate(
                           [&Source, &Node, Port](const FAbilityGraphEdge& Edge)
                           {
                               return Edge.From == Source.NodeId && Edge.To == Node.NodeId && Edge.OutputPort == Port;
                           });
            });
    }

    /** 只沿目标选取、实际消耗和定位端口读取印记后继。 */
    const FAbilityGraphNode* FindConsumedOrigin(const UAbilityGraphDefinition& Graph, const FAbilityGraphNode& Selector)
    {
        const auto* Consume =
            FindConnectedNode(Graph, Selector, TEXT("TargetFound"), TEXT("Status.ConsumeExposure"));
        return Consume ? FindConnectedNode(Graph, *Consume, TEXT("Consumed"), TEXT("Spatial.TargetOrigin")) : nullptr;
    }

    /** 从入口经明确的时序与空间端口找主查询，避免把追加分支识别为主攻击。 */
    const FAbilityGraphNode* FindMainShape(const UAbilityGraphDefinition& Graph)
    {
        const auto* Entry = Graph.Nodes.FindByPredicate(
            [&Graph](const FAbilityGraphNode& Node)
            {
                return Node.NodeId == Graph.EntryNodeId;
            });
        if (!Entry || Entry->TypeId == TEXT("Query.Shape"))
        {
            return Entry;
        }
        if (Entry->TypeId == TEXT("Query.SelectTargets"))
        {
            if (const auto* Consumed = FindConsumedOrigin(Graph, *Entry))
            {
                if (const auto* Burst = FindConnectedNode(Graph, *Consumed, TEXT("Located"), TEXT("Query.Shape")))
                {
                    return Burst;
                }
                const auto* Delay = FindConnectedNode(Graph, *Consumed, TEXT("Located"), TEXT("Control.DelayOrigin"));
                return Delay ? FindConnectedNode(Graph, *Delay, TEXT("Completed"), TEXT("Query.SelectTargets"))
                             : nullptr;
            }
            const auto* Origin = FindConnectedNode(Graph, *Entry, TEXT("TargetFound"), TEXT("Spatial.TargetOrigin"));
            const auto* Delay = Origin ? FindConnectedNode(Graph, *Origin, TEXT("Located"), TEXT("Control.DelayOrigin"))
                                       : nullptr;
            return Delay ? FindConnectedNode(Graph, *Delay, TEXT("Completed"), TEXT("Query.Shape")) : nullptr;
        }
        FName QueryPort;
        if (Entry->TypeId == TEXT("Control.Repeat"))
        {
            QueryPort = TEXT("Pulse");
        }
        else if (Entry->TypeId == TEXT("Control.StaggeredPoints"))
        {
            QueryPort = TEXT("Point");
        }
        else if (Entry->TypeId == TEXT("Control.DelayOrigin") || Entry->TypeId == TEXT("Spatial.Anchor"))
        {
            QueryPort = TEXT("Completed");
        }
        else
        {
            return nullptr;
        }
        if (const auto* Direct = FindConnectedNode(Graph, *Entry, QueryPort, TEXT("Query.Shape")))
        {
            return Direct;
        }
        if (const auto* Surface = FindConnectedNode(Graph, *Entry, QueryPort, TEXT("World.FindOwnedSurface")))
        {
            return FindConnectedNode(Graph, *Surface, TEXT("Found"), TEXT("Query.Shape"));
        }
        const auto* Select = FindConnectedNode(Graph, *Entry, QueryPort, TEXT("Query.SelectTargets"));
        const auto* Anchor = Select
                                 ? FindConnectedNode(Graph, *Select, TEXT("TargetFound"), TEXT("Spatial.TargetOrigin"))
                                 : nullptr;
        return Anchor ? FindConnectedNode(Graph, *Anchor, TEXT("Located"), TEXT("Query.Shape")) : nullptr;
    }

    /** 涟漪地雷的初次接触为零伤害，实际伤害由波前发布。其他技能继续使用主攻击的伤害输出。 */
    const FAbilityGraphNode* FindPrimaryDamage(const UAbilityGraphDefinition& Graph)
    {
        const auto* Producer = FindMainShape(Graph);
        if (!Producer)
        {
            Producer = Graph.Nodes.FindByPredicate(
                [&Graph](const FAbilityGraphNode& Node)
                {
                    return Node.NodeId == Graph.EntryNodeId &&
                           (Node.TypeId == TEXT("Spatial.Wavefront") || Node.TypeId == TEXT("Query.SelectTargets"));
                });
        }
        if (Producer)
        {
            if (const auto* Damage = FindConnectedNode(Graph, *Producer, TEXT("TargetFound"), TEXT("Effect.Damage")))
            {
                return Damage;
            }
            if (const auto* Gate = FindConnectedNode(Graph, *Producer, TEXT("TargetFound"), TEXT("Condition.ElementalStatus")))
            {
                if (const auto* Damage = FindConnectedNode(Graph, *Gate, TEXT("Passed"), TEXT("Effect.Damage")))
                {
                    return Damage;
                }
            }
        }
        const auto* Wave = Graph.Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Spatial.PrototypeWave");
            });
        if (Wave)
        {
            const auto* Damage = Graph.Nodes.FindByPredicate(
                [&Graph, Wave](const FAbilityGraphNode& Node)
                {
                    return Node.TypeId == TEXT("Effect.PrototypeDamageBatch") &&
                           Graph.Edges.ContainsByPredicate(
                               [&Node, Wave](const FAbilityGraphEdge& Edge)
                               {
                                   return Edge.From == Wave->NodeId && Edge.To == Node.NodeId;
                               });
                });
            if (Damage)
            {
                return Damage;
            }
        }
        return Graph.Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Effect.InputDamage") ||
                       Node.TypeId == TEXT("Effect.PrototypeDamageBatch") || Node.TypeId == TEXT("Effect.NovaDamage");
            });
    }

    /** Scope 配置直接声明形态。飞行端必须有精准直属 Scope，避免把其它分支的弹种当作当前弹种。 */
    bool HasPrototypeKind(const UAbilityGraphDefinition& Graph, const FAbilityGraphNode& Node,
                          EAbilityPrototypeKind Kind)
    {
        if (const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Node.Config))
        {
            return Scope->Parameters && Scope->Parameters->Kind == Kind;
        }
        return Graph.Nodes.ContainsByPredicate(
            [&Graph, &Node, Kind](const FAbilityGraphNode& Candidate)
            {
                const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Candidate.Config);
                return Scope && Scope->Parameters && Scope->Parameters->Kind == Kind &&
                       Graph.Edges.ContainsByPredicate(
                           [&Candidate, &Node](const FAbilityGraphEdge& Edge)
                           {
                               return Edge.From == Candidate.NodeId && Edge.To == Node.NodeId;
                           });
            });
    }

    /** 只选经实际策略识别的阶段。同类多阶段技能优先使用基础图声明的主攻击。 */
    const FAbilityGraphNode* FindCardTarget(const UAbilityGraphDefinition& Graph, const FWorkshopCard& Card)
    {
        if (Card.Id == TEXT("WideRelay") || Card.Id == TEXT("MoreRelayTargets"))
        {
            const auto* Query = FindMainShape(Graph);
            return Query && Query->TypeId == TEXT("Query.SelectTargets") && Query->NodeId != Graph.EntryNodeId
                       ? Query
                       : nullptr;
        }
        if (Card.Id == TEXT("PhaseChilled") || Card.Id == TEXT("PhaseArcane") || Card.Id == TEXT("PhaseFrost"))
        {
            const auto* Shape = FindMainShape(Graph);
            const auto* Gate =
                Shape ? FindConnectedNode(Graph, *Shape, TEXT("TargetFound"), TEXT("Condition.ElementalStatus")) : nullptr;
            // 只有真实的双分支技能可用此类卡，精确区分两路而非取第一个伤害节点。
            const auto* Other = Gate ? FindConnectedNode(Graph, *Gate, TEXT("Rejected"), TEXT("Effect.Damage"))
                                     : nullptr;
            if (!Other)
            {
                return nullptr;
            }
            if (Card.Id == TEXT("PhaseChilled"))
            {
                return Gate;
            }
            return Card.Id == TEXT("PhaseFrost") ? Other :
                   FindConnectedNode(Graph, *Gate, TEXT("Passed"), TEXT("Effect.Damage"));
        }
        if (!Card.RequiredGraphType.IsNone() && !Graph.Nodes.ContainsByPredicate(
                                                    [&Card](const FAbilityGraphNode& Node)
                                                    {
                                                        return Node.TypeId == Card.RequiredGraphType;
                                                    }))
        {
            return nullptr;
        }
        if (Card.TargetType.IsNone())
        {
            return FindPrimaryDamage(Graph);
        }
        if (Card.Slot == TEXT("Element") || Card.Slot == TEXT("ReactionControl"))
        {
            const auto* Damage = FindPrimaryDamage(Graph);
            return Damage && (Damage->TypeId == Card.TargetType ||
                              (Card.Slot == TEXT("Element") && Damage->TypeId == TEXT("Effect.Damage")))
                       ? Damage
                       : nullptr;
        }
        if (Card.Id == TEXT("WideArea") || Card.Id == TEXT("WideShatter") || Card.Id == TEXT("WideGale") ||
            Card.Id == TEXT("VolatileReach"))
        {
            const auto* Shape = FindMainShape(Graph);
            const auto* Query = Shape ? Cast<UAbilityQueryNodeConfig>(Shape->Config) : nullptr;
            const auto Geometry = Card.Id == TEXT("WideShatter") ? EAbilityQueryGeometry::Line
                                : Card.Id == TEXT("WideGale")    ? EAbilityQueryGeometry::Cone
                                                                 : EAbilityQueryGeometry::Sphere;
            if (Query && Query->Query.Geometry == Geometry)
            {
                return Shape;
            }
            if (Card.Id != TEXT("WideArea"))
            {
                return nullptr;
            }
        }
        return Graph.Nodes.FindByPredicate(
            [&Card, &Graph](const FAbilityGraphNode& Node)
            {
                if (Card.Slot == TEXT("Propagation"))
                {
                    return Node.TypeId == TEXT("Delivery.ProjectileAwait") ||
                           Node.TypeId == TEXT("Propagation.Sequential") || Node.TypeId == TEXT("Propagation.Forked");
                }
                if (Card.Slot == TEXT("JumpTarget"))
                {
                    return Node.TypeId == TEXT("Propagation.Sequential") || Node.TypeId == TEXT("Propagation.Forked");
                }
                if (Card.Id == TEXT("WideArea"))
                {
                    if (Node.NodeId != Graph.EntryNodeId)
                    {
                        return false;
                    }
                    if (Node.TypeId != TEXT("Scope.Prototype"))
                    {
                        return false;
                    }
                    using K = EAbilityPrototypeKind;
                    return HasPrototypeKind(Graph, Node, K::Burst) || HasPrototypeKind(Graph, Node, K::Cone) ||
                           HasPrototypeKind(Graph, Node, K::Field) || HasPrototypeKind(Graph, Node, K::Trap);
                }
                if (Node.TypeId != Card.TargetType)
                {
                    return false;
                }
                if (Card.Id == TEXT("ThawSurface") || Card.Id == TEXT("WaterSource"))
                {
                    const auto* Conversion = Cast<UAbilityConvertSurfaceNodeConfig>(Node.Config);
                    if (Node.NodeId != Graph.EntryNodeId || !Conversion)
                    {
                        return false;
                    }
                    return (Card.Id == TEXT("ThawSurface") ? Conversion->InputKind : Conversion->ResultKind) !=
                           ETerrainSurfaceType::Water;
                }
                if (Node.TypeId == TEXT("World.DispelSurface"))
                {
                    const bool bHarvest = FindConnectedNode(Graph, Node, TEXT("Removed"), TEXT("Status.Barrier")) ||
                                          FindConnectedNode(Graph, Node, TEXT("Removed"), TEXT("Control.Repeat"));
                    if (bHarvest != (Card.Id == TEXT("WideHarvest") || Card.Id == TEXT("HarvestWater")))
                    {
                        return false;
                    }
                }
                // 三张落地卡作用于真实 Landed 后继，即使同一张图追加了其它地表，也不会抢占。
                if (Card.Slot == TEXT("RushFloorKind") || Card.Slot == TEXT("RushFloorRadius") ||
                    Card.Slot == TEXT("RushFloorDuration"))
                {
                    const auto* Move = Graph.Nodes.FindByPredicate([&Graph](const FAbilityGraphNode& Candidate)
                    {
                        return Candidate.NodeId == Graph.EntryNodeId && Candidate.TypeId == TEXT("Movement.Character");
                    });
                    return Move && FindConnectedNode(Graph, *Move, TEXT("Landed"), TEXT("World.ElementSurface")) == &Node;
                }
                // 只有技能自己的地形、波前或筛选入口接受这些卡。普通追加分支不抢占同类型改装位置。
                if ((Card.TargetType == TEXT("World.ElementSurface") || Card.TargetType == TEXT("World.IgniteSurface") ||
                     Card.TargetType == TEXT("World.DispelSurface") || Card.TargetType == TEXT("World.ConvertSurface") ||
                     Card.TargetType == TEXT("Control.StaggeredPoints") ||
                     Card.TargetType == TEXT("Spatial.Wavefront") || Card.TargetType == TEXT("Query.SelectTargets")) &&
                    Node.NodeId != Graph.EntryNodeId)
                {
                    return false;
                }
                if (!Card.ScopeConsumer.IsNone() && !ConnectsTo(Graph, Node.NodeId, Card.ScopeConsumer))
                {
                    return false;
                }
                if (Card.Id == TEXT("DeepMend") || Card.Id == TEXT("DeepRecharge"))
                {
                    const auto* Restore = Cast<UAbilityRestoreSelfNodeConfig>(Node.Config);
                    return Restore && Restore->bMana == (Card.Id == TEXT("DeepRecharge"));
                }
                if (Card.Id == TEXT("LongStasis"))
                {
                    return Graph.Edges.ContainsByPredicate([&Node, &Graph](const FAbilityGraphEdge& Edge)
                    {
                        return Edge.From == Graph.EntryNodeId && Edge.To == Node.NodeId &&
                               Edge.OutputPort == TEXT("TargetFound");
                    });
                }
                if (Card.Id == TEXT("DeepField"))
                {
                    // 同一个 Scope 类型也服务持续开关。该类根不消费 Duration，不能展示这张卡。
                    const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Node.Config);
                    const auto* Field = Scope ? Cast<UBatch6FieldPrototype>(Scope->Parameters) : nullptr;
                    return !Field || !Field->bFollowCasterToggle || Scope->LogicalDepth != 0;
                }
                if (Card.ExtensionKind.IsSet() || Card.Id == TEXT("SentryStock"))
                {
                    const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Node.Config);
                    if (!Scope || !Scope->Parameters)
                    {
                        return false;
                    }
                    if (Card.Id == TEXT("SentryStock"))
                    {
                        // 奥术哨塔原上限为 5 发，不能让“备用弹”降为 4 发。已安装时仍需显示拆除入口。
                        return Scope->Parameters->Count <= Card.Value;
                    }
                    const auto* Extension = Cast<UExtensionNode>(Scope->Parameters);
                    return Extension && Extension->Behavior == Card.ExtensionKind.GetValue() &&
                           (Card.Id != TEXT("TightPull") || Extension->bPull);
                }
                return !Card.PrototypeKind.IsSet() || HasPrototypeKind(Graph, Node, Card.PrototypeKind.GetValue());
            });
    }

    /** 片段从可信配置创建，原图编译器负责接边、复制、重映射以及预算校验。 */
    UAbilityGraphDefinition* MakeFiniteBranch(UObject* Outer)
    {
        auto* Branch = NewObject<UAbilityGraphDefinition>(Outer);
        Branch->SchemaVersion = 2;
        Branch->CompatibilityProfile = TEXT("UnifiedV2");
        return Branch;
    }

    /** 爆裂从历史位置重新选敌。延时版本在等待后查询，击杀版本先校验真实击杀。 */
    UAbilityGraphDefinition* BuildExplosion(UObject* Outer, bool bOnKill, float DelaySeconds)
    {
        auto* Branch = MakeFiniteBranch(Outer);
        auto* Query = NewObject<UAbilityQueryNodeConfig>(Branch);
        Query->Query.Radius = bOnKill ? 200.f : 250.f;
        Query->Query.MaxTargets = bOnKill ? 6 : 8;
        auto* Damage = NewObject<UAbilityDamageNodeConfig>(Branch);
        Damage->Damage = bOnKill ? 10.f : 12.f;
        Damage->DamagePerLevel = 0.f;
        Damage->DamageType =
            FGameplayTag::RequestGameplayTag(bOnKill ? TEXT("Damage.Fire") : TEXT("Damage.Thunder"), false);
        const FGuid QueryId = FGuid::NewGuid();
        const FGuid DamageId = FGuid::NewGuid();
        Branch->EntryNodeId = QueryId;
        Branch->Nodes = {{QueryId, TEXT("Query.Shape"), Query}, {DamageId, TEXT("Effect.Damage"), Damage}};
        Branch->Edges = {{QueryId, DamageId, TEXT("TargetFound")}};
        if (bOnKill)
        {
            const FGuid GateId = FGuid::NewGuid();
            Branch->EntryNodeId = GateId;
            Branch->Nodes.Add({GateId, TEXT("Condition.Killed"), NewObject<UAbilityKilledGateConfig>(Branch)});
            Branch->Edges.Add({GateId, QueryId, TEXT("Passed")});
        }
        else if (DelaySeconds > 0.f)
        {
            auto* Delay = NewObject<UAbilityDelayNodeConfig>(Branch);
            Delay->DelaySeconds = DelaySeconds;
            const FGuid DelayId = FGuid::NewGuid();
            Branch->EntryNodeId = DelayId;
            Branch->Nodes.Add({DelayId, TEXT("Control.DelayOrigin"), Delay});
            Branch->Edges.Add({DelayId, QueryId, TEXT("Completed")});
        }
        return Branch;
    }

    /** 只计入正伤害。计数与单次资源返还由现有节点绑定本次根施法。 */
    UAbilityGraphDefinition* BuildRhythmMana(UObject* Outer)
    {
        auto* Branch = MakeFiniteBranch(Outer);
        auto* Counter = NewObject<UAbilityCombatRuleConfig>(Branch);
        Counter->Rule = EAbilityCombatRule::EveryNthDamage;
        Counter->Value = 3.f;
        auto* Restore = NewObject<UAbilityCombatRuleConfig>(Branch);
        Restore->Rule = EAbilityCombatRule::RestoreMana;
        Restore->Value = 12.f;
        const FGuid CounterId = FGuid::NewGuid();
        const FGuid RestoreId = FGuid::NewGuid();
        Branch->EntryNodeId = CounterId;
        Branch->Nodes = {{CounterId, TEXT("Control.EveryNthDamage"), Counter},
                         {RestoreId, TEXT("Effect.RestoreMana"), Restore}};
        Branch->Edges = {{CounterId, RestoreId, TEXT("Passed")}};
        return Branch;
    }

    /** 重新验证伤害结果的目标生命，随后读取实际结算后的血量或状态。 */
    FGuid AppendTargetCondition(UAbilityGraphDefinition& Branch, FName Type, EAbilityCombatRule Rule, float Value,
                                bool bRequirePositiveDamage = false)
    {
        auto* Resolve = NewObject<UAbilityResolveImpactNodeConfig>(&Branch);
        Resolve->bRequirePositiveDamage = bRequirePositiveDamage;
        auto* Condition = NewObject<UAbilityCombatRuleConfig>(&Branch);
        Condition->Rule = Rule;
        Condition->Value = Value;
        const FGuid ResolveId = FGuid::NewGuid();
        const FGuid ConditionId = FGuid::NewGuid();
        Branch.EntryNodeId = ResolveId;
        Branch.Nodes = {{ResolveId, TEXT("Target.ResolveDamageTarget"), Resolve}, {ConditionId, Type, Condition}};
        Branch.Edges = {{ResolveId, ConditionId, TEXT("TargetFound")}};
        return ConditionId;
    }

    /** 达到低血门槛后施加原有石化状态。 */
    UAbilityGraphDefinition* BuildLowHealthStone(UObject* Outer)
    {
        auto* Branch = MakeFiniteBranch(Outer);
        const FGuid Condition =
            AppendTargetCondition(*Branch, TEXT("Condition.HealthBelow"), EAbilityCombatRule::HealthBelow, .35f);
        auto* Petrify = NewObject<UAbilityPetrifyNodeConfig>(Branch);
        Petrify->DurationSeconds = 2.f;
        const FGuid PetrifyId = FGuid::NewGuid();
        Branch->Nodes.Add({PetrifyId, TEXT("Status.Petrify"), Petrify});
        Branch->Edges.Add({Condition, PetrifyId, TEXT("Passed")});
        return Branch;
    }

    /** 同一命中只从主攻击进入一次分支。附伤继续走共享抗性与元素反应。 */
    UAbilityGraphDefinition* BuildElementalFollowUp(UObject* Outer, const FWorkshopCard& Card)
    {
        auto* Branch = MakeFiniteBranch(Outer);
        const FGuid Condition =
            AppendTargetCondition(*Branch, TEXT("Condition.ElementalStatus"), EAbilityCombatRule::ElementalStatus,
                                  static_cast<float>(Card.RequiredExposure));
        auto* Damage = NewObject<UAbilityDamageNodeConfig>(Branch);
        Damage->Damage = Card.Value;
        Damage->DamagePerLevel = 0.f;
        Damage->DamageType = AbilityElements::ToDamageTag(Card.BonusElement);
        const FGuid DamageId = FGuid::NewGuid();
        Branch->Nodes.Add({DamageId, TEXT("Effect.Damage"), Damage});
        Branch->Edges.Add({Condition, DamageId, TEXT("Passed")});
        return Branch;
    }

    /** 正伤害才开启后继，随后核对同一存活目标及该次结算后的元素结果。 */
    UAbilityGraphDefinition* BuildElementalControl(UObject* Outer, const FWorkshopCard& Card)
    {
        auto* Branch = MakeFiniteBranch(Outer);
        const FGuid Condition =
            AppendTargetCondition(*Branch, TEXT("Condition.ElementalStatus"), EAbilityCombatRule::ElementalStatus,
                                  static_cast<float>(Card.RequiredExposure), true);

        auto* Slow = NewObject<UAbilitySlowNodeConfig>(Branch);
        Slow->Duration = Card.Value;
        const FGuid SlowId = FGuid::NewGuid();
        Branch->Nodes.Add({SlowId, TEXT("Status.Slow"), Slow});
        Branch->Edges.Add({Condition, SlowId, TEXT("Passed")});
        return Branch;
    }

    /** 面板只提交卡片 ID，所有传播上限、端口和效果参数由服务器填充。 */
    bool BuildCardPatch(UObject* Outer, const UAbilityGraphDefinition& Graph, const FWorkshopCard& Card,
                        FAbilityRuntimeRecipePatch& Patch)
    {
        const auto* Node = FindCardTarget(Graph, Card);
        if (!Node)
        {
            return false;
        }
        const FName DamagePort =
            Node->TypeId == TEXT("Effect.PrototypeDamageBatch") ? TEXT("Contact") : TEXT("DamageResolved");
        if (Card.Modifier.IsNone())
        {
            const bool bExpiry = Card.Slot == TEXT("Expiry");
            const FName Port = bExpiry ? FName(TEXT("Expired")) : DamagePort;
            FAbilityNodeRegistry Registry;
            Registry.RegisterBuiltins();
            FString Error;
            if (!AbilityRuntimeModifierTemplates::CanPublishConfiguredPort(*Node, Port, Registry, Error))
            {
                return false;
            }
            Patch.TargetNodeId = Node->NodeId;
            Patch.Operation = EAbilityRecipePatchOperation::AppendBranch;
            Patch.OutputPort = Port;
            if (Card.Id == TEXT("RhythmMana"))
            {
                Patch.BranchGraph = BuildRhythmMana(Outer);
            }
            else if (Card.Id == TEXT("LowHealthStone"))
            {
                Patch.BranchGraph = BuildLowHealthStone(Outer);
            }
            else if (Card.Id == TEXT("GuardOnHit"))
            {
                auto* Branch = MakeFiniteBranch(Outer);
                auto* Barrier = NewObject<UAbilityBarrierNodeConfig>(Branch);
                Barrier->Capacity = 25.f;
                Barrier->Duration = 3.f;
                Barrier->bOnDamage = true;
                Branch->EntryNodeId = FGuid::NewGuid();
                Branch->Nodes = {{Branch->EntryNodeId, TEXT("Status.BarrierOnDamage"), Barrier}};
                Patch.BranchGraph = Branch;
            }
            else if (Card.RequiredExposure != EAbilityExposure::None)
            {
                Patch.BranchGraph = Card.BonusElement == EAbilityElement::Original
                                       ? BuildElementalControl(Outer, Card)
                                       : BuildElementalFollowUp(Outer, Card);
            }
            else if (bExpiry || Card.Id == TEXT("KillBurst"))
            {
                const float Delay = Card.Id == TEXT("DelayedBurst") ? Card.Value : 0.f;
                Patch.BranchGraph = BuildExplosion(Outer, !bExpiry, Delay);
            }
            return Patch.BranchGraph != nullptr;
        }
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.TargetNodeId = Node->NodeId;
        Edit.ModifierId = Card.Modifier;
        Edit.FragmentId = Card.Fragment;
        Edit.Value = Card.Value;
        Edit.Count = 3;
        Edit.Interval = .15f;
        Edit.Radius = 500.f;
        Edit.MaxDepth = 1;
        Edit.BranchesPerTarget = 2;
        if (Card.Modifier == TEXT("Append.Registered"))
        {
            Edit.OutputPortId = DamagePort;
        }
        FString Error;
        return AbilityRuntimeModifierTemplates::BuildPatch(Outer, *Node, Edit, Patch, Error, &Graph);
    }

    /** 集中石化同时更改查询上限与直属状态时长。两条补丁作为同一来源一起提交和撤销。 */
    bool BuildCardPatches(UObject* Outer, const UAbilityGraphDefinition& Graph, const FWorkshopCard& Card,
                          TArray<FAbilityRuntimeRecipePatch>& Patches)
    {
        FAbilityRuntimeRecipePatch Edit;
        if (!BuildCardPatch(Outer, Graph, Card, Edit))
        {
            return false;
        }
        if (Card.Id == TEXT("FocusedGaze"))
        {
            const auto* Status = Graph.Nodes.FindByPredicate(
                [&Graph, &Edit](const FAbilityGraphNode& Node)
                {
                    return Node.TypeId == TEXT("Status.PetrifyBatch") &&
                           Graph.Edges.ContainsByPredicate(
                               [&Edit, &Node](const FAbilityGraphEdge& Edge)
                               {
                                   return Edge.From == Edit.TargetNodeId && Edge.To == Node.NodeId &&
                                          Edge.OutputPort == TEXT("Completed");
                               });
                });
            if (!Status)
            {
                return false;
            }
            FAbilityRuntimeRecipePatch Duration;
            Duration.TargetNodeId = Status->NodeId;
            Duration.Operation = EAbilityRecipePatchOperation::DurationScale;
            Duration.Scalar = 1.5f;
            Patches.Add(MoveTemp(Duration));
        }
        if (Card.Id == TEXT("ChilledShatter"))
        {
            const auto* Consume = Graph.Nodes.FindByPredicate([](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Status.ConsumeExposure");
            });
            if (!Consume)
            {
                return false;
            }
            auto Removal = Edit;
            Removal.TargetNodeId = Consume->NodeId;
            Patches.Add(MoveTemp(Removal));
        }
        if (Card.Id == TEXT("RuptureSteam") || Card.Id == TEXT("RelayShocked"))
        {
            const auto* Selector = Graph.Nodes.FindByPredicate([&Edit](const FAbilityGraphNode& Node)
            {
                return Node.NodeId == Edit.TargetNodeId;
            });
            const auto* Consume = Selector
                                      ? FindConnectedNode(Graph, *Selector, TEXT("TargetFound"),
                                                          TEXT("Status.ConsumeExposure"))
                                      : nullptr;
            if (!Consume)
            {
                return false;
            }
            // 两个补丁作为同一改装来源接纳；筛选和实际消耗不能因为安装顺序分离。
            auto Removal = Edit;
            Removal.TargetNodeId = Consume->NodeId;
            Removal.Parameter = EAbilityConfigParameter::ElementalStatus;
            Removal.Scalar = static_cast<float>(Card.RequiredExposure);
            Patches.Add(MoveTemp(Removal));
        }
        Patches.Add(MoveTemp(Edit));
        return true;
    }
} // namespace

FText UAbilityRuntimeRecipeDeveloperComponent::GetCardAbilityLabel(FGameplayTag AbilityTag)
{
    // 目录只包含接受过真实图编译检查的技能身份。Actor 与 Mass 变体共享名称。
    struct FSkillLabel
    {
        FName Tag;
        FText Label;
    };
    static const FSkillLabel Labels[] = {
        {TEXT("Abilities.Fire.FireBolt"), NSLOCTEXT("SkillCards", "FireBolt", "火焰箭")},
        {TEXT("Abilities.Composition.FrostFan"), NSLOCTEXT("SkillCards", "FrostFan", "寒霜扇射")},
        {TEXT("Abilities.Composition.FrostFault"), NSLOCTEXT("SkillCards", "FrostFault", "寒霜断层")},
        {TEXT("Abilities.Composition.BrambleMine"), NSLOCTEXT("SkillCards", "BrambleMine", "藤蔓地雷")},
        {TEXT("Abilities.Composition.StormSeal"), NSLOCTEXT("SkillCards", "StormSeal", "雷电印记")},
        {TEXT("Abilities.Art.GlacialLance"), NSLOCTEXT("SkillCards", "GlacialLance", "冰棱裂爆")},
        {TEXT("Abilities.Composition.FireSeed"), NSLOCTEXT("SkillCards", "FireSeed", "种火")},
        {TEXT("Abilities.Composition.HunterSentry"), NSLOCTEXT("SkillCards", "HunterSentry", "猎手哨塔")},
        {TEXT("Abilities.Composition.RippleMine"), NSLOCTEXT("SkillCards", "RippleMine", "涟漪地雷")},
        {TEXT("Abilities.Art.InfernoVortex"), NSLOCTEXT("SkillCards", "InfernoVortex", "烈焰风暴")},
        {TEXT("Abilities.Art.ThunderPrison"), NSLOCTEXT("SkillCards", "ThunderPrison", "雷狱结界")},
        {TEXT("Abilities.Mage.RimeSigil"), NSLOCTEXT("SkillCards", "RimeSigil", "霜冻符域")},
        {TEXT("Abilities.Composition.Aftershock"), NSLOCTEXT("SkillCards", "Aftershock", "环形余震")},
        {TEXT("Abilities.Batch6.EarthShockwave"), NSLOCTEXT("SkillCards", "EarthShockwave", "大地震波")},
        {TEXT("Abilities.Batch6.GlacialStorm"), NSLOCTEXT("SkillCards", "GlacialStorm", "极寒风暴")},
        {TEXT("Abilities.Batch6.SolarFireball"), NSLOCTEXT("SkillCards", "SolarFireball", "熔日火球")},
        {TEXT("Abilities.Mage.MeteorCinders"), NSLOCTEXT("SkillCards", "MeteorCinders", "陨火余烬")},
        {TEXT("Abilities.Mage.PiercingTempest"), NSLOCTEXT("SkillCards", "PiercingTempest", "穿云雷球")},
        {TEXT("Abilities.Arcane.ArcaneNova"), NSLOCTEXT("SkillCards", "ArcaneNova", "奥术新星")},
        {TEXT("Abilities.Mage.ArcaneSentry"), NSLOCTEXT("SkillCards", "ArcaneSentry", "奥术哨塔")},
        {TEXT("Abilities.Extension.FrostParasite"), NSLOCTEXT("SkillCards", "FrostParasite", "寒霜寄生")},
        {TEXT("Abilities.Extension.StormBrand"), NSLOCTEXT("SkillCards", "StormBrand", "雷霆烙印")},
        {TEXT("Abilities.Extension.EmberBrand"), NSLOCTEXT("SkillCards", "EmberBrand", "三重焰印")},
        {TEXT("Abilities.Extension.StormConvergence"), NSLOCTEXT("SkillCards", "StormConvergence", "雷链聚爆")},
        {TEXT("Abilities.Extension.GravityWell"), NSLOCTEXT("SkillCards", "GravityWell", "引力塌缩")},
        {TEXT("Abilities.Extension.BrambleRepulse"), NSLOCTEXT("SkillCards", "BrambleRepulse", "荆棘震退")},
        {TEXT("Abilities.Batch6.PetrifyingGaze"), NSLOCTEXT("SkillCards", "PetrifyingGaze", "石化凝视")},
        {TEXT("Abilities.Batch6.FissionChain"), NSLOCTEXT("SkillCards", "FissionChain", "叉状闪电")},
        {TEXT("Abilities.Batch6.SequentialChain"), NSLOCTEXT("SkillCards", "SequentialChain", "逐跳闪电链")},
        {TEXT("Abilities.Arcane.ArcaneShards"), NSLOCTEXT("SkillCards", "ArcaneShards", "奥术碎片")},
        {TEXT("Abilities.Prototype.Burst"), NSLOCTEXT("SkillCards", "PrototypeBurst", "定点爆裂")},
        {TEXT("Abilities.Prototype.Cone"), NSLOCTEXT("SkillCards", "PrototypeCone", "扇形冲击")},
        {TEXT("Abilities.Prototype.Field"), NSLOCTEXT("SkillCards", "PrototypeField", "脉冲领域")},
        {TEXT("Abilities.Prototype.Trap"), NSLOCTEXT("SkillCards", "PrototypeTrap", "触发地雷")},
        {TEXT("Abilities.Mage.ArcaneBarrier"), NSLOCTEXT("SkillCards", "ArcaneBarrier", "奥术护盾")},
        {TEXT("Abilities.Mage.StasisField"), NSLOCTEXT("SkillCards", "StasisField", "凝滞力场")},
        {TEXT("Abilities.Mage.SoakingRain"), NSLOCTEXT("SkillCards", "SoakingRain", "润泽雨幕")},
        {TEXT("Abilities.Mage.CinderBlast"), NSLOCTEXT("SkillCards", "CinderBlast", "炽焰涌爆")},
        {TEXT("Abilities.Mage.ThunderImpact"), NSLOCTEXT("SkillCards", "ThunderImpact", "雷鸣骤击")},
        {TEXT("Abilities.Mage.CrystalBurst"), NSLOCTEXT("SkillCards", "CrystalBurst", "寒晶爆发")},
        {TEXT("Abilities.Mage.VenomMire"), NSLOCTEXT("SkillCards", "VenomMire", "沼蚀毒床")},
        {TEXT("Abilities.Mage.MireIgnition"), NSLOCTEXT("SkillCards", "MireIgnition", "熔沼引燃")},
        {TEXT("Abilities.Mage.FrostSurge"), NSLOCTEXT("SkillCards", "FrostSurge", "霜锋潮")},
        {TEXT("Abilities.Mage.WetArc"), NSLOCTEXT("SkillCards", "WetArc", "潮印放电")},
        {TEXT("Abilities.Mage.TerrainDispel"), NSLOCTEXT("SkillCards", "TerrainDispel", "涡流涤地")},
        {TEXT("Abilities.Mage.IceShatter"), NSLOCTEXT("SkillCards", "IceShatter", "狭线裂霜")},
        {TEXT("Abilities.Mage.GaleSweep"), NSLOCTEXT("SkillCards", "GaleSweep", "风压散流")},
        {TEXT("Abilities.Mage.EmberDetonation"), NSLOCTEXT("SkillCards", "EmberDetonation", "灼印迸火")},
        {TEXT("Abilities.Mage.TidalMend"), NSLOCTEXT("SkillCards", "TidalMend", "潮息复苏")},
        {TEXT("Abilities.Mage.FrostAegis"), NSLOCTEXT("SkillCards", "FrostAegis", "霜甲净化")},
        {TEXT("Abilities.Mage.LavaFreeze"), NSLOCTEXT("SkillCards", "LavaFreeze", "寒封熔地")},
        {TEXT("Abilities.Mage.FrostCorrode"), NSLOCTEXT("SkillCards", "FrostCorrode", "腐化霜地")},
        {TEXT("Abilities.Mage.SteamCondensation"), NSLOCTEXT("SkillCards", "SteamCondensation", "蒸汽冷凝")},
        {TEXT("Abilities.Mage.ConductiveRecharge"), NSLOCTEXT("SkillCards", "ConductiveRecharge", "导电回充")},
        {TEXT("Abilities.Mage.ThunderMarkBurst"), NSLOCTEXT("SkillCards", "ThunderMarkBurst", "雷印迟爆")},
        {TEXT("Abilities.Mage.FrostShower"), NSLOCTEXT("SkillCards", "FrostShower", "霜晶阵雨")},
        {TEXT("Abilities.Mage.WaterShallows"), NSLOCTEXT("SkillCards", "WaterShallows", "水纹浅滩")},
        {TEXT("Abilities.Mage.WaterFreeze"), NSLOCTEXT("SkillCards", "WaterFreeze", "凝潮冻土")},
        {TEXT("Abilities.Mage.WaterElectrify"), NSLOCTEXT("SkillCards", "WaterElectrify", "雷沼通电")},
        {TEXT("Abilities.Mage.FrostHarvest"), NSLOCTEXT("SkillCards", "FrostHarvest", "霜壳回收")},
        {TEXT("Abilities.Mage.BogHarvest"), NSLOCTEXT("SkillCards", "BogHarvest", "沼息回收")},
        {TEXT("Abilities.Mage.TidalPhases"), NSLOCTEXT("SkillCards", "TidalPhases", "潮痕两相")},
        {TEXT("Abilities.Mage.EmberRush"), NSLOCTEXT("SkillCards", "EmberRush", "焰落突进")},
        {TEXT("Abilities.Mage.MistLift"), NSLOCTEXT("SkillCards", "MistLift", "升潮化雾")},
        {TEXT("Abilities.Mage.MistCondense"), NSLOCTEXT("SkillCards", "MistCondense", "雾凝清池")},
        {TEXT("Abilities.Mage.StormMist"), NSLOCTEXT("SkillCards", "StormMist", "雷雾环生")},
        {TEXT("Abilities.Mage.MistDispel"), NSLOCTEXT("SkillCards", "MistDispel", "旋风驱雾")},
        {TEXT("Abilities.Mage.StormCatalyst"), NSLOCTEXT("SkillCards", "StormCatalyst", "雷地借势")},
        {TEXT("Abilities.Mage.FrostCatalyst"), NSLOCTEXT("SkillCards", "FrostCatalyst", "霜地借势")},
        {TEXT("Abilities.Mage.EmberRupture"), NSLOCTEXT("SkillCards", "EmberRupture", "熔印裂爆")},
        {TEXT("Abilities.Mage.ConductiveRelay"), NSLOCTEXT("SkillCards", "ConductiveRelay", "雷印传递")},
    };
    const FName Tag = AbilityTag.GetTagName();
    const auto* Row = Algo::FindByPredicate(Labels,
                                            [Tag](const FSkillLabel& Item)
                                            {
                                                return Item.Tag == Tag;
                                            });
    return Row ? Row->Label : FText::GetEmpty();
}

void UAbilityRuntimeRecipeDeveloperComponent::RefreshCardView()
{
    const auto* Entry = Entries.Find(DeveloperState.AbilityTag);
    if (!Entry || !Entry->Compiled)
    {
        return;
    }
    for (const auto& Card : Cards)
    {
        if (!CardFitsAbility(Card, DeveloperState.AbilityTag) || !FindCardTarget(*Entry->Compiled, Card))
        {
            continue;
        }
        const FName* Source = InstalledCardsBySource.FindKey(Card.Id);
        auto& View = DeveloperState.Cards.AddDefaulted_GetRef();
        View.Id = Card.Id;
        View.Slot = Card.SlotLabel;
        View.Label = Card.Label;
        View.Description = Card.Description;
        View.bInstalled = Source && OwnedSources.Contains(*Source);
    }
}

bool UAbilityRuntimeRecipeDeveloperComponent::PreviewCard(FName CardId, FGuid Session, int32 ExpectedRevision,
                                                          FString& Error)
{
    if (!CheckAuthority(true, Error))
    {
        return Reject(TEXT("请使用法师角色开启本局试装。"), Error);
    }
    const auto* Entry = Entries.Find(DeveloperState.AbilityTag);
    if (!bCardPanel || Session != GraphPanelSessionId || !DeveloperState.bPreviewActive || !Entry || !Entry->Base ||
        !Entry->Compiled || Entry->Revision != ExpectedRevision ||
        AbilityRuntimeRecipes::Fingerprint(*Entry->Base) != PreviewBaseHash)
    {
        return Reject(TEXT("当前配方已更新，请重新选择卡片。"), Error);
    }
    const auto* Card = Algo::FindByPredicate(Cards,
                                             [CardId](const FWorkshopCard& Item)
                                             {
                                                 return Item.Id == CardId;
                                             });
    if (!Card || !CardFitsAbility(*Card, DeveloperState.AbilityTag))
    {
        return Reject(TEXT("当前技能无法使用这张卡。"), Error);
    }
    FAbilityRuntimeRecipeContribution Contribution;
    Contribution.SourceId =
        FName(*FString::Printf(TEXT("Workshop.%s.Card.%s"), *PreviewId.ToString(), *Card->Slot.ToString()));
    Contribution.bOrderedWorkshopEdit = true;
    const auto* Previous = Entry->Contributions.FindByPredicate(
        [&Contribution](const FAbilityRuntimeRecipeContribution& Item)
        {
            return Item.SourceId == Contribution.SourceId;
        });
    const UAbilityGraphDefinition* BuildGraph = Entry->Compiled;
    if (Previous)
    {
        // 保持已有卡槽的应用顺序。只向上游取参数，避免把下游选敌复制到替换卡里。
        Contribution.Priority = Previous->Priority;
        const auto Upstream = Entry->Contributions.FilterByPredicate(
            [Previous](const FAbilityRuntimeRecipeContribution& Item)
            {
                return Item.Priority < Previous->Priority ||
                       (Item.Priority == Previous->Priority && Item.SourceId.LexicalLess(Previous->SourceId));
            });
        UAbilityGraphDefinition* Replayed = nullptr;
        if (!AbilityRuntimeRecipes::Compile(this, Entry->Base, Upstream, Replayed, Error))
        {
            return Reject(TEXT("当前卡片无法替换。可以先拆除与它关联的卡片。"), Error);
        }
        BuildGraph = Replayed;
    }
    else
    {
        for (const auto& Existing : Entry->Contributions)
        {
            if (Existing.Priority == MAX_int32)
            {
                return Reject(TEXT("当前改装无法添加。请先重置试装。"), Error);
            }
            Contribution.Priority = FMath::Max(Contribution.Priority, Existing.Priority + 1);
        }
    }
    if (!BuildGraph || !BuildCardPatches(this, *BuildGraph, *Card, Contribution.Patches))
    {
        return Reject(TEXT("当前技能无法使用这张卡。"), Error);
    }
    return StageGraphContribution(Contribution, CardId, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerBeginCardPreview_Implementation(FName DefinitionId, FGuid Session)
{
    FString Error;
    if (!CheckAuthority(true, Error) || !Session.IsValid() ||
        (Definitions.IsEmpty() && !InitializeCatalog(nullptr, Error)))
    {
        Reject(TEXT("此技能暂时无法试装。请确认当前使用法师角色。"), Error);
        return;
    }
    const auto* Definition = Definitions.FindByPredicate(
        [DefinitionId](const FAbilityRuntimeDeveloperDefinition& Row)
        {
            return Row.DefinitionId == DefinitionId;
        });
    if (!Definition || GetCardAbilityLabel(Definition->Base.AbilityTag).IsEmpty())
    {
        Reject(TEXT("此技能暂时无法试装。请确认当前使用法师角色。"), Error);
        return;
    }
    if (DeveloperState.bPreviewActive)
    {
        if (bCardPanel && Session == GraphPanelSessionId && DefinitionId == DeveloperState.SelectedId)
        {
            PublishState();
            return;
        }
        Reject(TEXT("请先结束当前试装，再选择其他技能。"), Error);
        return;
    }
    bCardPanel = true;
    GraphPanelSessionId = Session;
    Observations.Reset();
    if (!BeginPreview(DefinitionId, Error))
    {
        bCardPanel = false;
        GraphPanelSessionId.Invalidate();
        Reject(TEXT("暂时无法开始试装，请确认角色已进入房间。"), Error);
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerPreviewCard_Implementation(FName CardId, FGuid Session,
                                                                               int32 ExpectedRevision)
{
    FString Error;
    PreviewCard(CardId, Session, ExpectedRevision, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerRemoveCard_Implementation(FName CardId, FGuid Session,
                                                                              int32 ExpectedRevision)
{
    FString Error;
    const FName* Source = InstalledCardsBySource.FindKey(CardId);
    if (bCardPanel && Session == GraphPanelSessionId && Source)
    {
        const bool bHasTargetCard = DeveloperState.Cards.ContainsByPredicate(
            [](const FAbilityRuntimeWorkshopCardView& Card)
            {
                return Card.bInstalled && (Card.Id == TEXT("WeakestJump") || Card.Id == TEXT("FarthestJump"));
            });
        if ((CardId == TEXT("Chain") || CardId == TEXT("Fork")) && bHasTargetCard)
        {
            Reject(TEXT("请先拆除传播目标卡，再拆除传播方式。"), Error);
            return;
        }
        RemoveGraphSource(*Source, ExpectedRevision, Error);
        return;
    }
    Reject(TEXT("这张卡已拆除或属于另一次试装。"), Error);
}
