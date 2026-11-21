# 法师 GA 动态组合拆分审计

日期：2026-11-19。状态：只读审计已完成，拆分设计待确认；没有实施运行时代码修改，也没有执行本轮行为测试。

本次目标是复用行为算法：例如将闪电链的逐跳选敌与延迟机制用于火焰箭投递，每次等待真实火焰箭命中再传播。不能通过激活另一个完整 GA、隐藏闪电结算或提前保存组合 GA 蓝图代替。

## 1. 清单依据与覆盖范围

独立 Unreal Python commandlet 成功读取当前目录、角色启动授予、GA CDO 和配置引用。报告：

- `Saved/GAAuthoring/DynamicMage/20261119/scope-readback.json`：290 个候选/目录 GA 类、476 个 DataAsset 配置；读取涉及的 773 个包前后 SHA256 相同。这个数字包含排除的 Stander 候选，不是 773 个法师技能。
- `Saved/GAAuthoring/DynamicMage/20261119/frontend-readback.json`：三个旧被动与 FireBolt、Electrocute、ArcaneShards 的真实蓝图节点/连线；六个包读取前后不变。
- 当前源码用于解释行为，commandlet 使用当前已编译 DLL 读取资产。没有将本次只读结果冒充准确源码树的构建或游戏行为通过；启动仍有旧标签等警告。
- [逐 GA 清单](ga-dynamic-mage-inventory.json)：60 个法师目录身份、86 个类版本，另列 4 个相关保护入口，共 90 行；所有实现/运行验收状态仍为 Planned/Pending。

三个主要目录为默认 `DA_AbilityInfo`、PureMass `DA_MassAbilityInfo` 和 Batch5 `DA_AbilityInfo`。按真实 `RequiredProfession=Mage` 取并集，不能以目录名称或技能外观猜职业。Stander 目录的 200 个不同类明确排除，其中包括 ArtBatch 的三个近战技能。

Actor/Mixed 共用 AbilityTag 的不同类分别建行。当前迁移矩阵遗漏 `GA_SequentialChain`，本轮已经在新清单中登记；不得因旧矩阵缺项而漏做。现有迁移矩阵的 Verified 只表示原迁移合同，不证明内部算法已经能替换重组。

### 目录中的全部身份（60）

| 类别 | 身份短名 | 身份数 / 类数 |
|---|---|---|
| 传统主动 | FireBolt、Electrocute、ArcaneShards、ArcaneNova | 4 / 4 |
| 传统环绕被动 | OrbitingFireBolt | 1 / 1 |
| 旧被动占位 | HaloOfProtection、LifeSiphon、ManaSiphon | 3 / 3 |
| 内部属性监听 | ListenForEvent | 1 / 1 |
| 基础原型 | Projectile、Multishot、Burst、Beam、Surface、Orbit、Field、Trap、Homing、Piercing、Chain、Cone、Wave、Returning、Emitter、Delay、Ring、Line | 18 / 36 |
| 前四批组合 | FireSeed、FrostFan、BrambleMine、StormSeal、Aftershock、FrostFault、RippleMine、HunterSentry | 8 / 16 |
| 第五批 | Attach、Stack、Displace、StormBrand、FrostParasite、EmberBrand、StormConvergence、GravityWell、BrambleRepulse | 9 / 9 |
| 第六批 | StarFlameHalo、PetrifyingGaze、SolarFireball、GlacialStorm、FissionChain、EarthShockwave、SequentialChain | 7 / 7 |
| MageBatch | MeteorCinders、RimeSigil、PiercingTempest、ArcaneSentry、ArcaneMeditation、ElementalWard | 6 / 6 |
| ArtBatch 法师 | GlacialLance、InfernoVortex、ThunderPrison | 3 / 3 |

目录外保护：`GA_Listen_New`、`GA_PerfectDodge`、`GA_WorldSlow`、`GA_DelayedPetrify`。闪避属于当前法师出生基础能力；世界减速保留现有 Standalone 限定；内部监听不开放为任意战斗片段。

三个旧被动的当前蓝图：Halo/Mana 仅激活与结束事件；LifeSiphon 有 PrintString，未发现吸血/回蓝/护盾结算图。对应原生 `UMyPassiveAbility` 负责停用订阅，角色有被动表现组件。它们需要兼容保护，不按名称补造未实现玩法。实际可重构玩法身份为其余 56 个（53 主动、环绕被动和两个 OwnedGE 被动）。

## 2. 统一图现在做到的程度

`GraphInstance::StartWithInput` 可以接收内存图，校验后冻结快照，并运行注册策略。旧原型与第五批通过 `AbilityLegacyGraphCompiler` 接入；每个 `Legacy.*` 节点内部依旧调用整个 Driver。

公共 `OnHit` 发生在旧 Driver 已经完成伤害之后。因此它适合追加后继，不能用来替换原命中的伤害或投递。`OnPeriod/OnExpire` 也由 Driver 内部节拍与寿命决定。

传统 `Delivery.ActorProjectiles` 生成弹体即发布终结 `Completed`；`Delivery.SequentialChain` 则内置直接伤害和光束，均没有“发起可替换投递并等待这一投递真实完成”的合同。旧 Chain 驱动同样不是纯传播调度器。

## 3. 需要拆分的行为轴

以下是共享逻辑的拆分单位，不要求把每一条辅助函数都注册成节点。一个策略可以拥有有界状态机，但其子投递、查询和效果必须能被独立选择。

| 行为族与覆盖技能 | 必要拆分 | 需要保留的合同 |
|---|---|---|
| FireBolt / Projectile | 发射分布、弹体飞行/碰撞、真实命中/阻挡/超时回执、命中效果 | 原 Notify、等级弹数、Mass 桥、装备规则、GA 正常结束后的在途弹体；一处伤害 |
| Homing / Piercing / Returning | 追踪运动、穿透计数与去重、往返阶段与命中集合 | 墙优先、目标失效、每阶段去重、折返剩余帧时间 |
| Multishot | 方向分布与生成预算；单次投递为可替换片段 | 不因每支分裂重置根预算；独立子命中身份 |
| Chain / SequentialChain / FissionChain | 最近目标/可见性、逐跳/BFS传播、延迟、访问历史、衰减；投递与效果分离 | 不重复命中原生命；首目标死亡不阻止按历史位置继续；不放开循环图 |
| Burst / Cone / Ring / Line / Nova | 保留几何筛选的查询、逐目标效果、表现 | 高度、内圈、3D/2D距离、原阵营规则、批量先捕获再伤害 |
| Wave / EarthShockwave | 波前推进、扫过集合、范围查询、效果 | 仅波前或整圆的原语义、尾帧边界、一生命一次 |
| Field / GlacialStorm 等区域 | 周期调度、锚点、查询、伤害和 Slow 等状态 payload | 各兼容 profile 的尾帧、追赶和结束事件 |
| Trap / Delay | 布防、接触触发、一次延迟、正常到期与取消、效果片段 | 原 ArmDelay/Duration 语义；取消不追加爆炸 |
| Emitter / HunterSentry / ArcaneSentry | 周期、索敌、发射请求与成功次数 | 无目标或拒绝投递不消耗成功次数，不积攒空场连发 |
| Surface | 地面投影、表面所有权/寿命、接触效果 | 沿用 Terrain 服务；不虚构旧 Surface.OnHit |
| Orbit / OrbitingFireBolt / StarFlameHalo | 库存补充、环绕位置、攻击信号订阅、目标管理、发射与命中 | Actor 实弹与 Mixed 虚拟弹差异、耗蓝节拍、撤回/停用和所属资源清理 |
| Beam / Electrocute | 锁定与次目标、维持条件、周期效果、结束效果、束表现 | 原输入/最短持续时间/费用桥/移动锁；原 Actor-only 支持不被默默放宽 |
| ArcaneShards | 点阵和地面投影、顺序调度、单点查询/效果、每片表现 | 首片立即、首次之后费用、尾等待后冷却、历史友方 Actor 分支 |
| Attach / FrostParasite | 附着锚点、周期、宿主失效转移、查询与效果 | 旧生命身份、转移次数、归属和取消 |
| Stack / EmberBrand / StormBrand | 跨施法状态键、计数、阈值、延迟引爆、效果片段 | 原施法者/EffectId/目标生命隔离；转移所有权，达到阈值的致死快照 |
| Displace / GravityWell / BrambleRepulse | 目标查询、受控位移、免疫、完成后继 | 复用移动租约/RootMotion/Mass/碰撞导航，不另造移动器 |
| PetrifyingGaze / Slow | 范围查询与单体状态 payload | 伤害后复核原生命；复用现有 Petrify/Slow 组件与子系统 |
| MageBatch / ArtBatch / 所有已组装 GA | 将旧宏内部逻辑按上述轴拆分，再复原原配方 | GA 身份、数值、图标、目标模式、消耗与表现时点 |
| ArcaneMeditation / ElementalWard | `Status.OwnedEffect` 已适合复用，不重复拆成第二套 GE 系统 | Infinite GE 独立句柄、移槽/取消只回收自己 |

## 4. 兼容要求中不能被“统一”抹掉的差异

- Actor Field 包含最后边界；普通 Mixed 多数提前结束；Batch6 某些场显式包含尾边界。
- 周期追赶：Actor 某些循环消费；Mixed 最多 8 次；Extension 最多 16 次；SequentialChain 每帧最多一跳并重新等待完整间隔。
- Actor Beam/Orbit 整数秒耗蓝，Mixed 按 Delta；旧 Shards 首片之后费用与尾等待冷却不能改成统一起手 Commit。
- Electrocute 周期效果由目标 ASC 自建 Spec，结束最终伤害另走源伤害服务。现有异常/历史分支单列观察，不能借重构顺手修正。
- 无修正的原 GA 不改变装备规则的实际读取时点。新动态模式的冻结时点另有明确合同；同一装备倍率不得应用两次。
- 取消不等于正常到期。已经存在的独立弹体正常生存和显式撤销的效果必须按原家族规则区分。
- 同步伤害/GE/死亡/销毁回调可能取消或重激活 GA；每个回执携带原执行/步骤身份，清理只操作原资源。

## 5. 两个框架层面的必要处理

1. `GraphInstance::Advance` 每帧重新验证原调用输入；当前长驻传播若持有首个 `LiveTarget`，首击致死后可能失去后续推进资格。新的传播持有历史命中与起点，后续目标逐个捕获，不能靠重新认领已复用目标绕过校验。
2. PureMass/Extension 宿主分开保存执行参数快照与稳定表现资产。临时图/临时 DA 不会自动在客户端成为合法资产引用。动态数值应使用稳定资源引用加本次有限表现参数同步，确保范围/方向/缩放反映实际逻辑。

## 6. 完成标准

“已拆分”需同时具备原入口与新节点的真实共用实现、可替换端口、目标/寿命合同和行为测试。只增加 TypeId、包装整个 Driver、创建静态组合 DA 或运行一条共享策略测试都不足以完成。

每个清单条目最终记录：原功能兼容、动态复原、是否有新跨族组合、Actor/Mass 范围、自动化报告、PIE/网络/视觉状态。实施范围及分层验收以 [设计合同](superpowers/specs/2026-11-19-dynamic-mage-composition-design.md) 为准。
