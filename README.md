# Roguelike 联机 RPG：GAS、随机地牢与装备构筑

基于 **Unreal Engine 5.8 / C++ / Blueprint** 的俯视角联机 RPG，从 Aura / GAS 课程工程扩展。当前开发重点是随机地牢闯关、Actor/GAS 敌人、可组合技能、表格驱动装备与局域网协作；同时保留纯 Mass 怪物与混合战斗实验场。

更新于 **2026-10-31**。下表按本地源码、资产制作记录及已有测试整理；“已实现”不代表所有地图均启用，也不代表打包版本已验收。GitHub 最近一次同步仅包含代码、配置、脚本和文档，新增地图、蓝图、纹理等资产仍需本地资源配套，单独克隆代码不能保证体验最新全部玩法。

[详细功能清单](docs/project-feature-catalog.md) · [GA 全流程与实际示例](docs/ga-composition-authoring-runtime-walkthrough.md) · [GA 制作流程](docs/ga-production-and-runtime-guide.md) · [随机装备指南](docs/2026-10-31-random-equipment-guide.md)

## 当前玩法

| 玩法环节 | 玩家可以做什么 | 当前规则与边界 |
|---|---|---|
| 创建游戏 | 选择存档、单人或 LAN 房间，与队友进入地图 | OnlineSubsystemNull；尚无公网匹配、账号或中继服务 |
| 随机探索 | 探索主线与支路，查看已访问房间、相邻门和房型 | 14～18 房；服务器按 Seed 生成清单，各端装配；整层创建和销毁，非逐房流送 |
| 房间战斗 | 全体存活队员集结后关门开战，击败必杀目标与后续波次 | 当前 L_RoguelikeRun 使用 Actor/GAS 敌人；已清房间可回访，不重复发奖 |
| 多类型敌人 | 对抗长矛哥布林、弹弓哥布林和食尸鬼 | 种子驱动洗牌袋保证类型覆盖；普通怪使用有限对象池 |
| 精英战 | 躲避石像精英的重锤、盾击和地震 | 蒙太奇命中时序、感叹号/红区预警、墙体与地板约束；多人及最终手感仍待专门验收 |
| Boss 战 | 对抗 Boss、处理半血增援和范围脉冲，清完必杀目标后开启出口 | 已接入房间阶段规则；不是完整的随机 Boss 技能池 |
| 技能构筑 | 学习、升级、装备主动/被动技能，搭配投射、范围、持续、连锁等效果 | 18 种基础原型 + 3 种扩展机制、14 个组合 GA；技能目录和可用项依地图配置 |
| 局内收益 | 清房获得个人金币，领取宝箱、购物、休整，Boss 后退出本层 | 宝箱 F；商店点击商品；休整 F；目前未实现自动连续爬层或局外成长闭环 |
| 药水与背包 | 拾取、堆叠、交换、使用药水和整组丢弃 | 消耗品走既有 GE；服务端校验与背包持久化 |
| 装备拾取与穿戴 | 靠近 Soul 查看名称，E 拾取，B 同时打开背包与装备栏，选择穿脱 | 左右戒指槽与胸甲槽；出生房间原 5 件固定样例 + 3 件随机样例 |
| 随机装备 | 获得不同品质、基础数值、前后缀及词缀等级的装备 | 服务器生成一次；穿脱、丢弃、复制、读档保留实际结果；当前支持 20 项持久属性加法，含主属性、次级属性和六种抗性 |
| 共享掉落与等级 | 普通怪概率掉药水，精英/Boss 保底药水与词缀装备，清战斗房额外 2 件装备 | 全模式共用规则；Mass 无掉落；房间按入口深度定级，装备当前上限 20；[配置与验收](docs/2026-11-09-shared-loot-and-room-levels.md) |
| 队友倒地 | 存活队友继续清房，倒地者清房后复活；全灭则失败 | 保留原 Pawn/ASC；异常移除必杀目标会明确失败，不算击杀 |
| 地形战斗 | 利用或躲避岩浆、冰霜、毒性与藤蔓区域 | 分别提供灼烧、减速、中毒和缠绕；泥潭目前仅视觉 |
| Mass 实验玩法 | 在独立测试地图对抗 200 Goblin，测试 Actor/Mass 混合技能 | 保留独立 ECS 实现；不代表当前随机地牢默认启用 Mass 或已有大规模性能保证 |

## 功能模块与实现

| 模块 | 已开发功能 | 实现 / 文档入口 |
|---|---|---|
| 基础角色与成长 | 鼠标寻路、WASD、属性、抗性、护甲、格挡、暴击、经验、升级与加点 | [Player](Source/test_25d/Private/Player)、[AbilitySystem](Source/test_25d/Private/AbilitySystem)；基础来自 Aura 并持续适配 |
| 技能制作与运行 | 数据资产行为节点、命中/周期/完成子节点、预算与取消清理、Actor/Mass 目标适配 | [制作与调用流程](docs/ga-production-and-runtime-guide.md) |
| 关卡生成 | 任务图、主支路配额、网格/端口装配、有界搜索、确定性备用布局、Ready 检查 | [生成规则](docs/2026-10-28-roguelike-generation-increment2.md) |
| 地牢运行 | 集结、锁门、波次、击杀账本、一次性奖励、宝箱/商店/休整、胜负、探索地图 | [房间玩法](docs/2026-10-28-roguelike-generation-increment3.md)、[生命周期验收](docs/2026-10-30-dungeon-lifecycle-validation.md)；增量文档前段保留历史配置，当前敌人以本表为准 |
| 敌人生命周期 | 异步预载、分帧生成、类型配额、死亡布娃娃、休眠回收、生命租约与旧回调隔离 | [对象池](docs/2026-10-29-dungeon-enemy-pool.md)、[怪物组成](docs/2026-10-30-dungeon-enemy-variety.md) |
| 石像精英 | 独立攻击 GA、数据配置、动画与插槽特效、地面预警、血条与移动修复 | [实现与验收](docs/stone-golem-implementation-and-acceptance.md) |
| 装备系统 | 表格定义、槽位校验、唯一实例 ID、穿脱事务、独立 GE Handle、资源上限处理 | [装备规范](docs/equipment-development-standard.md)、[表格说明](docs/2026-10-30-equipment-tables.md) |
| 随机词缀 | 基础范围、品质权重、词缀互斥组、等级档、精度、RollData 保存、实例详情显示 | [随机装备指南](docs/2026-10-31-random-equipment-guide.md) |
| Mass 系统 | Fragment/Processor、空间索引、导航缓存、数据胶囊、共享骨骼实例、差量快照 | [PureMass 架构](docs/2026-10-23-pure-mass-guide.md)、[混合目标](docs/2026-10-23-mixed-mass-actor-skills.md) |
| 联机与存档 | LAN 会话、密码匹配、成员状态、组队传送、玩家身份与技能/背包恢复 | [LAN](docs/2026-10-22-lan-rooms-guide.md)、[传送](docs/2026-10-22-party-portal-guide.md) |
| 开发工具 | Unreal Python 资产生成/读回、PowerShell 构建测试、原生 Automation、PIE 探针、制作技能 | [build-ga](skills/build-ga/SKILL.md)、[build-equipment](skills/build-equipment/SKILL.md)、[Scripts](Scripts) |

## 底层技术

| 技术 | 项目中的用途 | 关键实现方式 |
|---|---|---|
| UE 5.8、C++、Blueprint | 核心规则与引擎集成、资产配置和表现 | Runtime 模块 test_25d；UCLASS/USTRUCT 反射；UBT/UHT 构建 |
| Gameplay Ability System（GAS） | 玩家/Actor 敌人技能、属性、伤害、消耗、冷却和装备 | ASC、AttributeSet、GA、GE、GameplayTag、GameplayCue、伤害执行计算 |
| Infinite GE + SetByCaller | 按随机装备实例施加属性 | 共用 GE 类，每件独立 Spec/Handle；基础属性 Channel0，装备 Channel1 加法 |
| 数据驱动 | 技能、物品、装备数值、随机词缀与地牢规则 | DataAsset、DataTable、CurveTable、FScalableFloat、软引用 |
| 确定性随机 | 地牢布局、怪物组成、装备品质与数值 | FRandomStream + Seed；装备额外保存实际结果与版本，读档不重抽 |
| UE 网络复制 | 权威规则、交互请求、属性与实例同步 | Server RPC、属性复制/OnRep、FastArray；服务器验证目标、实例和状态版本 |
| OnlineSubsystemNull | 局域网房间与会话旅行 | LAN Session 创建/搜索/加入；未接入公网服务 |
| Mass Entity / ECS | 独立普通怪实验系统 | Fragment 保存数据，Processor 批处理；共享 Crowd Actor 渲染与复制 |
| AI、Recast/NavMesh 与碰撞查询 | Actor 敌人追逐、Mass 寻路、门口连通和命中边界 | AIController/BehaviorTree/Blackboard、导航路径、Sweep、空间索引 |
| 对象池与分帧调度 | 控制常见 Actor 怪的重复生成成本 | 有限容量、按类型配额、跨遭遇复用、生命租约；不复用活动对象 |
| UMG、Slate、MVVM | HUD、技能菜单、背包、装备栏、商店和小地图 | Widget/ViewModel、FieldNotify、事件通知、输入路由与相机方向投影 |
| Niagara、蒙太奇、骨骼动画、ProceduralMesh | 技能/拾取特效、石像攻击时序、范围预警 | 插槽定位、动画状态机、程序化地面范围；Mass 使用 InstancedSkinnedMesh |
| SaveGame 与快照 | 玩家进度、背包和装备持久化 | 稳定 ID/GUID、嵌套 SaveGame 字段、快照验证及 GE 重建 |
| Automation、Unreal Python、MCP、PowerShell | 资产制作与自动化验证 | 原生测试、资产读回、真实 GE 测试、单机/双端 PIE；报告保存在 Saved |

## 试玩入口

使用 UE 5.8 和 Windows C++ 工具链编译 `test_25dEditor / Development Editor / Win64`。最新玩法需配套本地资产。当前本地构建采用禁用 test_25d 模块 Unity 的配置，说明见[详细功能清单](docs/project-feature-catalog.md#本地构建说明)。

| 地图路径 | 用途 | 操作 / 配置 |
|---|---|---|
| `/Game/RoguelikeGeneration/Maps/L_RoguelikeRun` | 当前地牢闯关、敌人、经济与装备入口 | Seed / Expected Players 在宿主配置；双人用 2 Players + Listen Server，Expected Players=2 |
| `/Game/RoguelikeGeneration/Maps/L_RoguelikeFloor` | 随机关卡布局与装配回归 | 不把此地图的房型标记等同完整战斗玩法 |
| `/Game/PrototypeExtensions/Batch5/L_PrototypeLab` | 组合技能实验场 | 35 项测试技能目录；Mass 新会话 999 技能点是测试规则 |
| `/Game/levels/L_DungeonLabyrinth` | 200 Goblin 功能测试 | 混合技能、移动、动画与实体死亡 |
| `/Game/PureMass/Maps/L_PureMassDungeon` | 小规模 Mass 战斗 | 基础实体通路验证 |
| `/Game/levels/L_TerrainEffectsTest` | 地形状态效果 | 灼烧、冰霜、毒性与藤蔓 |

## 已有验证与待完成项

以下是历史执行结果，本次文档整理没有重新运行游戏测试，也不是远端 CI 状态。

| 范围 | 已有证据 | 尚未据此验证 |
|---|---|---|
| 项目自动化 / 随机装备 | 2026-10-31 全属性扩展：220 项通过（120 无警告、100 带警告），0 失败；旧三属性样例另有单机及双端 PIE 记录 | 新增 20 属性的双端 PIE、全部手动 UI 体验、独立 Dedicated Server、延迟/丢包 |
| 地牢玩法 | 单人及双人整层流程、真实 GE 战斗、倒地复活/全灭、服务点交互与重复奖励防护记录 | 所有种子稳定性、完整键鼠整层战斗、长期性能；装备 PIE 曾出现导航 Ready 超时，仍需跟踪 |
| 对象池 | 原生生命周期测试、真实蓝图布娃娃与双端复用验证 | 同条件性能基准；单次采样不能代表帧时间上限 |
| 石像精英 | 三个攻击 GA、特效清理、中断/死亡取消、移动骨骼变化和血条验证 | 多人复制、低帧率时序、门口拥堵与最终手感 |
| 技能 / Mass | 第五批组合技能 Actor/Mass 双端战斗、菜单和多次换图记录 | 万级怪物保证、全部传统技能无修改适配 Mass |
| 产品化 | Windows 编辑器和 LAN PIE 为主要开发环境 | Shipping 打包、公网服务、连续爬层、装备百分比/触发词缀/洗练、完整怪物随机词缀体系 |

## 开发规范与来源

| 文档 | 用途 |
|---|---|
| [详细功能清单](docs/project-feature-catalog.md) | 更完整的技能、功能与源码索引 |
| [怪物开发规范](docs/monster-development-standard.md) | Actor/GAS 复用、攻击预警、死亡与回收边界、验收路径 |
| [装备制作流程](docs/equipment-production-workflow.md) | 固定/随机装备、后缀、GE 迁移和验证步骤 |
| [装备开发规范](docs/equipment-development-standard.md) | 服务端装备事务、GE 约束、复制及持久化 |
| [随机装备接入指南](docs/2026-10-31-random-equipment-guide.md) | 表格编辑、实例生命周期、样例与测试 |
| [GA 制作与调用全流程](docs/ga-production-and-runtime-guide.md) | 从技能定义、制作资产到输入、结算与清理 |
| [现有 GA 组合与实际示例](docs/ga-composition-authoring-runtime-walkthrough.md) | 迁移后的资产组合、真实调用顺序、动画/特效、网络与生命周期；含火种、石化、Stander、StoneGolem等案例 |
| [课程学习记录](docs/aura-learning-notes.md) | 原首页保留的学习笔记 |

基础工程参考 Stephen Ulibarri 的 [GAS Top Down RPG 课程](https://www.udemy.com/course/unreal-engine-5-gas-top-down-rpg/) 与 [Aura 原仓库](https://github.com/DruidMech/GameplayAbilitySystem_Aura)，学习亦参考 CSDN 暮志未晚NAN、GitHub ShinyRuo 的笔记。课程基础代码与第三方美术素材不作为本项目原创内容。
