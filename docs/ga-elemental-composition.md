# 技能图有限片段与元素反应

属性面板第四页继续使用现有的“技能组装”工作台。选中输出口可追加片段，选中新增节点可修改实际使用的参数。预览通过后应用草稿，下一次 `T` 键测试施法读取新图。根 GA 仍负责输入、动画和费用。

## 工作台操作

| 可选片段或参数 | 接入点 | 实际效果 |
| --- | --- | --- |
| `Control.DelayOrigin` | `Origin` | 等待后从 `Completed` 发出原位置 |
| `Control.Repeat` | `Origin` | 按周期发出有限次数 `Pulse`，次数、周期和最长持续时间均可设置 |
| `Query.WaitForTarget` | `Origin` | 在布防等待后检查区域，首次发现敌人时发出位置型 `Triggered` |
| `Spatial.Wavefront` | `Origin` | 推进波前，从 `TargetFound` 逐个发出当前仍有效的目标 |
| `Set.TargetFilter` | `Query.SelectTargets` 节点 | 排序和数量限制之前按 Actor、Mass、低血或指定元素状态筛选 |
| `Condition.HealthBelow`、`Condition.ElementalStatus` | `LiveTarget` | 判断当前血量比例或元素状态，发布保持 `LiveTarget` 的 `Passed/Rejected` |
| `Condition.DamageApplied` | `DamageResult` | 只在观测到正实际扣血时进入 `Passed` |
| `Control.EveryNthDamage` | `DamageResult` | 每根按节点计数，第 N、2N、3N 次有效扣血通过 |
| `Control.DamageTriggerLimit` | `DamageResult` | 限制每根施法通过次数，`Set.TriggerCooldown` 可进一步设置两次通过的最短间隔 |
| `Set.ElementConversion` | `Effect.InputDamage` 节点 | 0 保留原伤害元素，1 火、2 水、3 雷、4 奥术、5 冰。实际抗性、扣血和新状态使用转换后的标签 |
| `Effect.RestoreMana`、`Effect.RestoreHealth` | `DamageResult` | 每根施法每个恢复节点获得一次机会，恢复施法者 1–20 点并受资源上限约束 |

`Set.TargetFilter` 的 0 为全部、1 为 Actor、2 为 Mass、3 为生命低于 35%。4–11 依次对应灼热、潮湿、感电、奥术印记、蒸汽、导电、冻僵、冻结。`Condition.ElementalStatus` 使用对应状态编号 1–8，其中 7 为冻僵、8 为冻结。

体验示例可从火球开始。选中 `Effect.InputDamage`，用 `Set.ElementConversion = 2` 得到水元素版本。从它的 `DamageResolved` 追加 `Effect.RestoreMana`，设为 8，然后在未满蓝时施法。想让一次施法命中多名敌人后才回蓝，可在 `DamageResolved` 与恢复节点之间通过两次追加建立 `Control.EveryNthDamage → Passed → Effect.RestoreMana` 分支。传播仍经原来的投递节点，同一根技能只在第二、第四等有效受击时通过计数分支。

图中的计数从实际正伤害回执累积，归属一次施法和具体规则节点，整根结束即清除。被拦截、未结算、零伤害或无法观测实际扣血的结果不会贡献计数或资源。恢复发生在伤害之后，费用仍在原 GA 激活过程结算。

这些片段通过 `Append.Registered` 从兼容端口追加。原有分支继续执行。要仅在条件通过时追加某个效果，连接新条件的 `Passed`，并把效果放在这条新分支上。无限引导输出仍只允许经审核的同步纯载荷，长链和高频执行受图原有预算约束。`DamageResult` 是历史事实，需要操作活目标时仍先复核生命。

## 元素规则

首版借鉴《神界：原罪》系列的火水相克、潮湿导电思路。项目中的规则按实时战斗简化为每个目标生命一个元素状态槽。状态在后续命中时改变反应或供筛选使用。参考 [Larian 的环境交互介绍](https://larian.com/support/faqs/what-is-environment-interaction_35) 和[《神界：原罪 2》开发者更新](https://forums.larian.com/ubbthreads.php?Number=593775&ubb=showthreaded)。

| 现有伤害元素 | 一级状态 | 默认时长 |
| --- | --- | --- |
| 火 | 灼热 | 4 秒 |
| 水 | 潮湿 | 4 秒 |
| 雷 | 感电 | 4 秒 |
| 奥术（`Damage.Magic`） | 奥术印记 | 4 秒 |
| 冰（`Damage.Ice`） | 冻僵，移动和攻击速率 ×0.65 | 4 秒 |

| 目标已有状态 | 本次元素 | 结果 |
| --- | --- | --- |
| 灼热 | 水 | 转为蒸汽，2 秒 |
| 潮湿 | 火 | 转为蒸汽，2 秒 |
| 潮湿或蒸汽 | 雷 | 本次实际承伤 ×1.25，转为导电，2 秒 |
| 感电 | 水 | 本次实际承伤 ×1.25，转为导电，2 秒 |
| 导电 | 雷 | 本次实际承伤 ×1.25，导电转为感电 |
| 奥术印记 | 火、水或雷 | 本次实际承伤 ×1.20，换成来袭元素的一级状态 |
| 灼热 | 奥术 | 本次实际承伤 ×1.20，转为奥术印记 |
| 冻僵 | 冰或水 | 消耗前置状态，转为冻结，1.25 秒 |
| 潮湿 | 冰 | 消耗前置状态，转为冻结，1.25 秒 |
| 冻结 | 任意元素 | 照常扣血，沿用本次冻结的到期时间 |

其它直接元素伤害应用或刷新本元素状态。物理、纯粹伤害和既有持续伤害的周期扣血不改变状态槽。反应倍率在本次扣血中乘一次，致死结算不会给已死亡生命留下新状态。原来技能配置的燃烧、眩晕和减速仍按原能力处理。蒸汽和导电目前是目标身上的状态，地面传播、客户端状态图标和专属特效留给后续表现接入。`Set.ElementConversion` 改的是结算元素，火球模型和发射特效仍取原资产。

这套规则也影响已经存在的元素组合。比如猎手哨塔的三颗火弹各带一次奥术爆点，首颗火弹先留下灼热，后续交替命中依次触发增幅，零抗性夹具中的总伤害由 84 变为 97.2。每次施法的投递次数和费用仍由原配置决定。

## 代码维护

Actor 的即时伤害经 `ApplyDamageEffect` 或 `UExecCalc_Damage` 记录本次伤害类型，由 `UMyAttributeSet::HandleIncomingDamage` 在扣血前处理反应。状态随 ASC 属性集保存，读取核对当前 Avatar、怪物池租约及到期时间。Mass 在 `UPureMassSubsystem::ApplyDamage` 调用同一 `AbilityElements::Evaluate`，状态保存在当前实体片段，随实体移除消失。权威服务器更新状态和实际资源，生命、法力继续用原复制链路，客户端无需计算反应。

`AbilityRuntimeModifierTemplates::BuildPatch` 创建可信配置，`AbilityRuntimeRecipes::Compile` 验证源端口、配置类和预算。使用 `AbilityGameplayRuleNodes` 添加新规则时保持 `TypeId`、参数轴和端口匹配。元素表的时长和倍率集中在 `AbilityElementalExposure.cpp`，首版可在一处调整。需要从编辑器调平衡时可将表迁移到受校验的 Data Asset，保持同一共享结算入口。

### 零伤害状态和反应分支

雨幕使用 `Control.Repeat.Pulse → Query.Shape.TargetFound → Status.ApplyExposure`。最后一个节点只消费 `LiveTarget`，交给 Actor 属性集或 Mass 实体提交反应结果，忽略反应表中的伤害倍率。它有 `Applied` 可供继续连边，失效或跳过的旧生命只会完成调用。已有冻结保持原截止时间，后续脉冲不能不断续冻。图退出会停止后续脉冲，已落地状态仍由目标自身管理。

开发图可在 `LiveTarget` 业务输出端口追加可信片段 `Status.ApplyExposure`。值使用同一元素表的整数 1–5。历史命中端先接 `Target.ResolveDamageTarget` 或 `Target.ResolveImpact`，再接 `TargetFound`。这个节点用 `Set.ExposureElement` 和 `Scale.Duration` 编辑。伤害节点 `Effect.Damage` 分别通过 `Set.ElementConversion` 和 `Scale.Damage` 改元素和伤害值。伤害后控制先检查实际扣血和同一活目标，再走 `Condition.ElementalStatus → Status.Slow`。正伤害校验可以单独用 `Condition.DamageApplied`，深层反应卡则用明确启用正伤害校验的 `Target.ResolveDamageTarget` 缩短一层图深度。查询的 `Completed` 支路发送同一权威落点与半径给短时 Niagara 表现。

这一批用定点限时脉冲配合目标状态，复用了相克关系。地形仍由原接触系统管理。参考 [Larian 的地表转换规则](https://docs.larian.game/Osiris/API/TransformSurfaceAtPosition)。

后续新增的临时地表、毒地引燃和按状态放电见[地表候选与节点设计](ga-elemental-terrain-wave.md)。这些地块继续使用同一套接触系统。

### 冰系技能与行动控制

| 技能 | 冰伤片段 |
| --- | --- |
| Frost Fan（`FrostFan`） | 扇形弹体命中 |
| Frost Fault（`FrostFault`） | 直线命中 |
| 寒霜寄生（`FrostParasite`） | 附着后的伤害脉冲 |
| 极寒风暴（`GlacialStorm`） | 延时展开后的区域伤害 |
| 冰棱裂爆（`GlacialLance`） | 穿透命中和末端爆裂 |
| 霜冻符域（`RimeSigil`） | 霜域伤害和结束爆裂 |

节点伤害独立结算的持续技能可以多次施加冻僵。仅生成霜地表、已有周期 GE 的扣血和 Mass 状态型 DOT 沿用自身机制。冰伤使用独立标签，Actor 复用现有水抗属性，Mass 可在怪物类型资产中配置独立冰抗，未配置时读取水抗。

Actor 路径从 `HandleIncomingDamage` 提交实际扣血后走 `AEnemyCharacter::SetIceCombatState`。预警、锁定和蒙太奇出手使用同一个冻僵倍率，冻僵时开始的攻击也按该倍率延长冷却。冻结作废攻击代次及预警、取消活跃 GA、停止 AI 和位移。复制的冰状态带对象池租约。服务器控制到期和恢复，客户端收到状态后更新动画速度与冰霜 Niagara。死亡和回收立即清理该生命的定时器和表现。

Mass 的攻击前摇、攻击间隔和后摇使用按冻僵倍率缩放的步长。冻结清掉待命中目标，暂停攻击时钟及移动，解冻后重新判定目标。群体 FastArray 同步冰状态，渲染侧保持当前动画采样姿势、显示冰霜 Niagara，并随实体移除清理。共用状态的时间使用权威世界时钟。

独立资产检查及迁移入口为 `Scripts/GA/Ice/configure_ice_spells.py`。用项目 Unreal Python commandlet 运行，`GA_ICE_MODE=audit` 输出实际绑定，`apply` 备份并保存明确的冰伤节点，`verify` 从冷启动编辑器读回。`DefaultGameplayTags.ini` 在技能目录预加载前登记 `Damage.Ice`，伤害快照构建时将其关联到抗性。

PIE 可从角色属性面板第四页选择「霜冻符域」，点击「开始测试」，隐藏面板后把鼠标对准能承受多段伤害的怪物，按 `T` 施法。霜域第一次伤害施加冻僵，后续伤害触发冻结。要验证潮湿配合，可以先用结算元素转换为水的技能命中同一目标，再命中冰伤。

## 验证

Development Editor 构建成功。全量自动化 1052 项执行，1051 项通过。唯一失败是既有 `AllConfigurationAssets` 用例中的 9 个 Stander 时间轴缺少蒙太奇引用。新增 3 项测试验证真实 Actor/Mass 伤害与元素转换、四种有限片段的实际执行、连续命中计数、限频和资源恢复。报告位于 `Saved/Automation/UnifiedGA/20261121T192558-253d0215/index.json`。

同一模块在空 QA 地图通过单机 104 项、Listen Server 远端 113 项检查。两次都用真实第四页编辑火球、预览和应用配方，通过 `T` 键与原施法事件命中三个 Actor，逐项核对湿润条件、定时脉冲、回蓝、原始费用与关闭后的授予回收。远端额外按实际位置核对三个目标的复制生命值。报告分别在 `Saved/GAAuthoring/ElementalComposition/RealCasting/aae085249c5d47c28cc1467991adba88/report.json` 和 `Saved/GAAuthoring/ElementalComposition/RealCasting/b18830209f5c43e1876a51f79ebf593f/report.json`。

冰系扩展完成 Development Editor 构建和独立资产读回，覆盖 6 个技能身份、8 个 GA 入口、10 个配置包以及 12 处伤害配置。全量自动化执行 1052 项，通过 1051 项，失败仍为上述 Stander 配置检查。报告为 `Saved/Automation/UnifiedGA/20261122T062812-507421a7/index.json`。单机与双人监听服务器都从第四页使用真实 `T` 键施放霜冻符域，记录 Actor 和 Mass 的扣血、冻僵、原地冻结和恢复。远端也验证了冻结复制、姿势保持与解冻。报告为 `Saved/GAAuthoring/Ice/latest-pie.json` 和 `Saved/GAAuthoring/Ice/latest-pie-remote.json`。
