# 动态法师技能：交付证据与测试边界

日期：2026-11-20。本文记录实际测试边界，不代替全目录验收。

当前全量自动化基准为 `Saved/Automation/UnifiedGA/20261120T100009-47fec444/index.json`：1015项执行、1014项成功。本轮新增与修改的功能均成功，唯一失败为任务前已有的 Stander 时间轴9项蒙太奇引用校验；原资产未为此改写。全部82个玩法默认图已导出为细节点，68个实际 Prototype 派生前端、9个实际 Mage/Art 前端、原生五类前端专项及65个原型动作对照成功。325次持续纯载荷、Follow伤害后650次实际状态追加与原费用对照、显式分布目标策略、同步原生命转换、编译快照复制与只读校验均已取得专项证据。最终真实RHI报告 `20261120T100137-3bd3175f` 的5项渲染专项全部通过；全量与渲染的输入清单均保持不变。真实面板完整组合与最终视觉状态仍须按下文及逐项报告记录，不能由全量自动化推定全部验收完成。

## 本轮真实游戏与网络验证

以下报告位于 `Saved/GAAuthoring/DynamicMage/RealCasting/<报告ID>/report.json`。测试在已有专用空 QA 地图运行，通过真实鼠标目标、T 输入、原 TargetData、动画及 Notify 路径施放；没有手工注入命中、伤害或动画事件。双人用例按真实 PlayerId 匹配远端客户端、服务器 Avatar 和主机玩家，不把同世界读回作为网络证据。每次报告与 `pie-loaded-build.json` 记录对应二进制，早期报告不冒充后续构建的复测结果。

| 场景 | 报告ID | 已观察结果 |
|---|---|---|
| Standalone 逐跳火球 | `acd1871511b54c96bba09cf1b50f2f55` | 三次真实飞行与接触、同一冻结版本、衰减、费用和石化；只证明单机。 |
| Listen Server 远端逐跳火球 | `da6291c65e48495181bc9d70381e0563` | 真实远端编辑和施法，三次服务器结算；客户端投射物、HP及石化收敛，另一玩家配方隔离。 |
| Listen Server 固定持续区域，半径改为450 | `d6537e36961d4e15946add1b3a0a41e6` | 编辑值明确不同于原资产；双端半径快照均为450，范围内真实伤害/状态、范围外不伤，原费用保持。 |
| Listen Server 跟随持续区域，半径改为450 | `4d6fab746abb427487062a4727778b5c` | 双端冻结几何一致；原开关持续超过资产有限时长并继续正常扣除维持费用，关闭后回收。 |
| Listen Server 被动＋范围石化 | `1ed8df2ea72c48f8b110f2d151acd36c` | 原属性修改持续15秒以上，新增状态同步且无隐藏伤害，主机玩家隔离。 |
| Listen Server 在途修改、撤回及恢复默认 | `a4ca7c8d18bf4d94861c7d966ff7b426` | 旧伤害链完成旧版本；新状态链不夹带原火焰/DOT伤害；撤回不取消在途链，下一次恢复原单发技能。 |
| Listen Server 原生火球数量3、半径64 | `eee37be308be4101b7345118ee56a0e4` | 三次真实接触共享同一目标原生命和Root，仅一次费用；服务器与客户端的世界碰撞半径均为64。旧失败报告曾明确观测64/32，修复仅同步被选中纯值。 |
| Standalone 基础真实 UMG 编辑与施法 | `858e54ba995647ccb847ef20ab6790f6` | 真实选择器/按钮、Apply后1.5仍保留、真实Slate焦点/隐藏施法/状态与费用、Remove、准确Close回执、原Spec全集和场景归还。 |

以上报告均有精确关闭请求回执、原基础属性与职业恢复、仅本测试拥有的 Actor/投射物/AI 清理记录。QA 中的职业与确定性属性准备仅用于隔离既有角色状态，未修改原资产、存档或生产资格检查。

82个实际类另有默认原输入/动作检查汇总 `Saved/GAAuthoring/DynamicMage/CatalogCasting/resolved-064613-82-classes.json`：77类首次通过，5类因高频事件覆盖有界日志而漏记初次费用，改为逐帧纯值采样后定向重测通过，真实费用和Mana差值吻合，未修改GA费用。它证明逐类真实输入与动作，不代替完整数值时序、全部动态面板组合或用户视觉验收。

早期 UMG 批次发现测试桥在数字框获得焦点后设值，旧文本在失焦时把值覆盖回1；其低门槛“至少命中一个目标”结果不能作为请求数量/范围/时序的证据。修正桥接顺序后，完整14场景已在 `UMGScenes/c657575f02e94822aeeaf697718c4f9a/report.json` 重新执行并全部成功，包含Apply后的实际数值检查。Standalone 面板在途版本 `d8d94c29dacc48f39c3aae6affc74370`、远端基础面板 `751745c94dc440d4a317438c29f0b410`、远端命令版本 `f5b05015c69b47bda06d2511c946477d` 和远端真实面板版本 `5d53a85ac22b4675a885948ea4f74b11` 均成功并完成清理。这些报告绑定090852构建；后续源码修订需要单列新鲜证据。详细面板检查见[面板验证记录](ga-dynamic-mage-umg-validation.md)，粒子美术可读性及最终手感留给[用户PIE验收](ga-dynamic-mage-acceptance.md)。

独立资产检查 `Saved/GAAuthoring/DynamicMage/20261119-implementation/protected-package-final-check.json` 对773个原始包逐个重新计算SHA256，结果为0变化、0缺失。后文保存各轮接口诊断和早期测试边界，历史报告中的“待验证”不覆盖本节后续取得的新报告；也不能把早期报告当成后续二进制的复测。

最终DLL `3F800D638548AFCC8DC49A0DCE929D81DCC67A49901BD3223D95A4577287F048` 的远端总报告为 `FinalNetwork/f73c4611075c4d5db7a779f706264dde/report.json`：7/7成功并清理，包含本次修正影响的Follow伤害后状态追加；上表双端行已更新为该批报告。另有远端真实基础面板 `01ea123988f241968b8ab698c234a535` 的43项检查及远端UMG在途版本 `2e12c7e91e5d41cf98acd145d3bf2ecc` 成功并清理。早期Standalone14场景仍注明其090852二进制，不冒充最终DLL下的重复执行。

最终单机补测Follow `faeab0153e2440e7846bceb6c9b01770` 的32项、基础真实UMG `858e54ba995647ccb847ef20ab6790f6` 的38项检查均通过并完成清理。最终汇总为 `Saved/GAAuthoring/DynamicMage/20261119-implementation/final-validation-summary.json`：PIE后1520个验证输入文件重新比对为0变化，原 `L_RoguelikeRun` 与节流设置已恢复，`dirty=[]`、`pie=[]`；最终编辑器保留原地图供用户验收。**本轮实施和自动验证完成，用户表现／手感验收待进行。**

## 构建环境与保留事项

验证使用UE5.8 Editor Development。工作区新引入的UnLua及其LuaProtobuf、LuaRapidjson、LuaSocket、UnLuaTestSuite与当前工具链存在独立兼容问题，本轮构建、自动化及PIE命令临时禁用这些插件；未改工程的插件启用配置，不能据此宣称完整UnLua环境已验证。其Lua构建规则仅有旧编译器枚举的兼容修订，未移植整个插件。

全量测试保留任务前已有的 `test_25d.UnifiedGA.Catalog.AllConfigurationAssets` 失败：9个Stander时间轴缺少Montage引用。该问题单列为原有资产基线，未为了获取全绿而改变本轮范围外资产。用户最终动画与粒子验收仍为Pending。开发在本地main完成，未自动暂存、提交或推送。

## 证据分层

1. **目录定义**：加载实际类、导出并验证图。只证明配置可生成，不能证明该类在游戏中实际释放过。
2. **共享行为**：真实 Actor GE、Mass HP、物理飞行、原计时器、状态和 Cue。用于验证模块动作；测试类或配置不能冒充实际目录前端。
3. **实际目录前端**：原类授予原 ASC，经原输入/TargetData/Notify/费用接口启动。另注明是否使用传输、伤害依赖或动画依赖夹具。
4. **游戏验收**：Standalone/Listen Server 的真实本地输入、鼠标、面板、视觉和多人隔离。以下服务器自动化不能替代这一层。

## 已有路径与未覆盖项

| 实际类路径 | 已有证据 | 明确边界 |
|---|---|---|
| `/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C` | `NativePanel.ServerTransport.FireBoltPropagationAwaitAppendAndFrozenWithdrawal` 与 `FireBoltPropagationPostDamageAppendControl`。实际目录 BeginPreview/ApplyEdit；原蓝图 TargetData、原 Aura 蒙太奇事件；真实 BP_FireBolt 飞行，致死 Mass→Actor→Mass，逐次等待、衰减、旧快照、新修正和关闭预览恢复默认。 | T 输入先执行生产路由，再由显式客户端预测包夹具进入真实 ServerTryActivate。不是实际网络；本地鼠标/屏幕投射待游戏验收。测试实例固定伤害 GE/数值以观测真实 HP，不修改 CDO/资产。 |
| `/Game/BluePrints/AbilitySystem/Player/Ability/Thunder/GA_Electrocute.GA_Electrocute_C` | `NativePreview.BeamUnmodifiedPreviewUsesOriginalTargetNotifyAndRelease`。真实目录选择器、同一 PlayerState ASC、原 TargetData/Notify/松键与费用，正式零修正和无贡献预览对照。`NativeElectrocute` 行为测试另覆盖325次实际周期费用、65目标批次和死亡回调最终伤害先于冷却。 | 前端测试为兼容播放绑定了测试蒙太奇/模型和确定性 SBC 伤害依赖，不能算原动画视觉验收。周期/最终伤害作者 ASC 差异由共享行为测试验证。选择器的精确路径已在当前相关报告中断言并记录。 |
| `/Game/BluePrints/AbilitySystem/Player/Ability/PassiveSpell/OrbitingFireBolt/GA_OrbitingFireBolt.GA_OrbitingFireBolt_C` | `NativePreview.OrbitDefaultThenPanelContactScalarUsesOneASC`。实际目录选择器、原 ASC 开关输入、原库存补充/耗蓝、原攻击 Notify、真实库存弹体接触；面板改变实际接触载荷倍率，旧激活快照和精确租约回收。 | 使用确定性伤害依赖。真实本地面板/鼠标和网络待验收。选择器精确路径已在当前相关报告中断言并记录。 |
| `/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneNova/GA_ArcaneNova.GA_ArcaneNova_C` | `NativePreview.ActualNovaSameASCDefaultAndZeroEditPreview`：同一 PlayerState ASC 正式输入→自然冷却→实际目录 BeginPreview 零修正→原 T 输入。原 GA/GE 均未替换；真实 Actor 扣血、范围外不伤、单次费用与冷却先于伤害、原攻击报告、冻结摘要与关闭保持一致。 | 原 Nova 立即施法，没有 TargetData 或动画 Notify 等待。该测试为权威输入整合，不验证本地鼠标、实际网络或粒子视觉。之前 `NativeFrontends.NovaAndOrbitUseFrozenRecipe` 的手工 RegisterBase 仍仅算桥接证据。 |
| `/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneChard/GA_ArcaneShards.GA_ArcaneShards_C` | `NativePreview.ServerTransportActualShardDirectoryDefaultAndZeroEditPreview`：同一 ASC 正式施法→原冷却标签自然到期→实际目录零修正预览，原 TargetData、原 PointCollection、原蒙太奇、真实二次 ASC 按键和 Notify；验证首片先于费用、单次费用、尾段冷却、实际扣血和清理。`NativeShards.*` 另验证共享点时钟、替换状态、结果消费者和独立倍率。 | LP 激活通过明确的服务器预测包传输夹具；原动画播放/事件被实际使用，但不替代游戏视觉与网络验收。前端用例固定当前测试实例的伤害依赖。原生资产默认 Actor 后端；共享动作显式开启 Mass 的用例不代表该资产默认支持 Mass。 |
| `/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C` | `SpecialPetrify.DefaultSeparatesQueryAndBatchStatus`、`RealFrontendConsumesPanelRadiusSnapshot`。实际目录、原立即施法入口、独立查询/状态、面板半径、Actor/Mass 状态及原费用。 | 仅服务器自动化范围；状态视觉与网络待游戏验收。 |
| `/Game/CompositionBatch6/GA_FissionChain.GA_FissionChain_C` | `SpecialLightning.ServerTransportActualFrontendConsumesPanelSnapshot`。实际目录、原 ASC Pressed/Held、显式预测包、原 TargetData、原伤害/费用、追加真实 Mass 石化、结束与旧回调去重。 | 当前真实行为是 **Volley：首目标邻域同时受击，所有光束从施法者出发**。没有启用闲置 BFS、MaxDepth 或 BranchesPerTarget 语义。冷却标签断言与精确类日志在当前相关报告中成功。 |
| `/Game/CompositionBatch6/GA_SequentialChain.GA_SequentialChain_C` | 同一真实前端测试。`SpecialLightning.MixedDefaultVolleyAndSequential` 另验证首击致死后 Actor/Mass 传导、每帧最多一跳、历史落点、衰减与取消；`All128TargetsAndMinimumHopDelay` 验证128目标及0.01秒间隔。 | 原策略首目标死亡会被图的 LiveTarget 复核提前结束，已单独修复为该策略持有目标跟踪责任；当前报告旧控制与细图均成功。冷却标签断言与精确类日志在当前相关报告中成功。 |

## 闪电模块的实际职责与生命周期

`Control.LightningSteps → Query.LightningTargets → Effect.LightningDamageBatch → Presentation.LightningBeams` 是四个独立配置/策略。控制拥有次数、等待和访问历史；查询拥有半径；载荷拥有原衰减与主伤害倍率；表现拥有资源和寿命。原完整兼容策略与细节点调用相同的 `AbilityLightningActions::FindTargets / ApplyHit / DamagePayload / Present`。

一次步骤的只读帧绑定原 RootCast 和来源生命，只能借用该图中当前宿主的精确资源索引。结束/取消会解绑该索引并释放当前帧；队列/执行对象保活参数，目标只保留弱生命身份。真实 GE 回调周围持有必要强引用，避免同步取消和 GC 留下悬空访问。

先捕获 Volley 整批原生命，再结算；Sequence 每次等待后重新选择一个目标。原伤害配置关闭 Debuff 和径向伤害，并保留已配置的力，不调用投射物 force 准备、不额外掷击退 RNG。只有原前端 Commit 扣费/冷却；细节点不再 Commit。

本体 Cue 完成后，宿主发布独立 DamageResolved。附加延迟 OnHit 不属于本体 continuation，不拖延下一跳。客户端仍通过原 `ABatch6ChainPresentation` 的端点、资源复制与到期寿命表现；服务器瞬态图和帧不复制给客户端。

有限128目标宿主使用明确登记的 RegisteredSustained 合同，复用现有256 starts/s 的 token bucket、活动对象和帧额度。没有提高普通有界图的64次上限，也没有提升全局速率。128条观测结果可以跨帧排空；实际整批伤害与 Cue 不能因此只执行前64个目标。

## 接口问题的验证进度

只读审核发现 `Effect.LightningDamageBatch` 和替换为状态后的 Cue 仍读取帧中的完整原集合，没有消费公开 TargetSet 的过滤结果。合法子集可能造成额外伤害/额外光束。下一批新增两个 RED 用例：仅保留原 secondary 时保留其原 falloff；替换 Petrify 时伤害不发生且光束仅对应实际目标。另验证同源但未被本步捕获的新生命不能借 TargetSet 注入权限，拒绝必须发生在任何 GE 之前。

`20261120T004924-44f5921d` 已实际观察两个 RED：合法子集仍有额外伤害/光束；注入场景未在任何 GE 之前拒绝。实现改为完整集合预校验、原生命匹配、保留原 primary 身份和按实际接收者绘制 Cue；`20261120T010627-169c3db2` 两项均成功。公开 TargetSet 是实际效果/Cue 接收集合，帧中的完整原集合只授予既有原生命权限，不能注入新目标。

真实 `GA_ArcaneShards` 用例先前的第二次施法被尚未移除的冷却标签拒绝。夹具改为等待原冷却标签真正移除后，`20261120T013244-58d8bbdf` 整项成功，当前相关报告继续成功；未移除 GE、修改冷却或改动 MyASC 输入逻辑。Spec key 日志本身没有证明输入产品行为错误。

`ContactProfiles.ConfiguredForcesMatchesOriginalMixedActor` 在同一报告中成功：旧 Mixed Orbit 的实际 `Driver.Hit` 与独立 InputDamage 节点均使用真实 GE/HP/上下文力，验证主伤/DOT 独立累计倍率、原已配置 force 原样传递且不额外掷击退 RNG。此为载荷合同证据，物理 Orbit 库存/飞行由其他专用用例验证。

六个 MageBatch 与三个法师 Art 的实际目录前端对照已在 `20261120T030029-0fc49685` 执行。每例使用正式 ASC 输入和原 TargetData，再通过实际零编辑预览检查真正被消费的快照；不手工注册规范化图。四个 Mixed 主动技能的实际 Mass 血量预算、三个 Art 主动技能的原 Actor GE，以及全部原费用/冷却/正常结束对照成功。两个被动完整成功，包括30秒维持、属性数值和精确归还 GE；七个主动例仍只在“真实预览快照不含旧宏”断言失败，故尚不能宣称这七个类的默认出口已切到细节点。LP 病例使用明确的服务器预测包传输夹具，不替代实际网络与鼠标验收。

后续共享动作/框架完善继续按 RED→实现→新鲜运行记录推进。

## Scope 参数轴的真实消费合同

“支持参数轴”表示本 Scope 的实际动作消费该值。Scope 包含一个字段、字段可以改写、图中存在伤害孩子，都不能证明该父 Scope 支持伤害倍率。子 Scope 在进入时重新冻结自己的 Damage/DamagePerLevel，并重置累计倍率；父容器不隐式修改孩子。

只读审核发现 `UAbilityPrototypeScopeNodeConfig::TryScaleRecipeParameter` 原先对所有 Kind/Behavior 接受 Damage、Radius、Duration，部分轴只改变未消费字段。实际目录 `BeginPreview → ApplyEdit` 原子性测试要求不支持的编辑被拒绝，保持版本、摘要、来源和原参数。在 `20261120T030029-0fc49685` 中，14个普通无效轴和14个 Extension 无效轴均实际出现 RED。实现按消费表拒绝后，`20261120T032521-a73a9785` 与 `20261120T034249-a049dd8a` 的14个普通拒绝项、27个 Extension 组合和 Burst Damage、Field Radius、Projectile Duration、single Multishot Damage 四个真实原 GE/HP/运动寿命正例全部通过。此结论只覆盖这45个用例中的倍率合同，不代表新增加的类型化参数、完整面板或真实联机已验收。

普通原型的首批明确不支持项：Emitter/Delay/Surface 的父 Damage；非 single Multishot 容器的三个轴；Projectile、Beam、Orbit、Line 的通用 Radius；Delay 的通用 Duration；根 Orbit 和 Mixed 根随动 Field 的 Duration。Emitter 的 Radius 实际用于选敌，Surface 的 Radius 实际用于地表尺寸，不能将它们与无效轴一并禁掉。投射物几何读取 CollisionRadius，Beam 读取 Range，Orbit 读取专用 OrbitSettings；不能把通用 Radius 偷换成这些字段以制造“支持”。Delay 的真实时间字段为 ArmDelay。

Extension 必须按 Behavior 判断，不能按其父类 Kind。下表描述本片段的 Damage/Radius/Duration；“支持”仍需实际当前配置通过完整合法性校验。

| Extension Behavior | Damage | Radius | Duration | 实际消费路径 |
|---|---|---|---|---|
| Burst | 支持 | 支持 | 不支持 | 独立球形查询/批伤害立即完成；不以 Duration 保持本体。 |
| Projectile | 支持 | 不支持 | 支持 | 飞行读取 CollisionRadius/Range/Duration，接触进入独立批伤害。 |
| Chain | 支持 | 支持 | 支持 | 逐跳选择读取 Radius，时钟读取 Duration，批伤害读取本帧 Damage。 |
| Delay | 不支持 | 不支持 | 不支持 | 等待 ArmDelay，然后进入独立孩子 Scope。 |
| Attach | 不支持 | 不支持 | 支持 | 锚点时钟读取 Duration；转移读取 TransferRadius；周期孩子独立结算。 |
| Stack | 不支持 | 不支持 | 不支持 | 状态读取 StackLifetime/DetonationDelay，阈值孩子有自己的 Scope。 |
| Displace 单目标 | 不支持 | 不支持 | 支持 | 原位移/RootMotion 读取 Duration；通用 Radius 只可能缩放表现，不改变实际作用范围。 |
| Displace 区域 | 不支持 | 支持 | 支持 | 区域候选读取 Radius，原位移和本体时钟读取 Duration。 |
| Surface | 不支持 | 支持 | 支持 | 工厂仅消费类/尺寸；本体时钟销毁地表。地表类自己的伤害不是此 Scope.Damage。 |

`ScopeScalars.ExtensionBehaviorMatrix` 从真实目录导出图扫描对应 root/child Scope，分别检查上表27个组合；不会为测试伪造一个父类 Kind 相同的 Extension DA。该矩阵是参数事务/能力合同证据，不能替代前端输入、状态作用或游戏视觉测试。
