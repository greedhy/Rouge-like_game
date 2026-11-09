# GA 统一框架：运行、组合与扩展

本文件说明 2026-11-09 迁移实现的代码合同。制作流程以 [统一制作指南](ga-unified-authoring-guide.md) 为入口，逐资产状态及验证证据以 [迁移矩阵](ga-unified-migration-matrix.json) 为准；代码结构存在不等于该资产已完成视觉或联机验收。

完整实例如火种、三重焰印、两种石化、Stander 和 StoneGolem 的资产及调用顺序，见 [GA 全流程与实际示例](ga-composition-authoring-runtime-walkthrough.md)。

## 单一开发路线

```text
输入 / AI / 装备授予（原有 ASC 与稳定 AbilityTag）
  -> UMyGameplayAbility::PreActivate：本次 FAbilityExecutionSession
  -> GA 入口：目标确认、费用、动画/Timeline、结束规则
  -> 图入口：UAbilityGraphInstance -> UAbilityNodeExecution -> 同一共享动作
     或专用动画入口：会话去重 -> 同一 Ability*Actions 共享动作
  -> 原 Actor GAS / PureMass / 状态组件 / 复制表现
```

新行为写成“配置 + 注册策略 + 测试”，由现有 GA 前端或 `UUnifiedGraphAbility` 调用。专属动画、角色状态和 AI 选择可以保留在前端；不要再复制伤害执行器，也不要在一个节点中激活完整旧 GA 来冒充行为组合。

| 层 | 主要类型 | 管理内容 |
|---|---|---|
| 激活会话 | `FAbilityExecutionSession` | 每次激活独立 GUID，动作序号去重，完整 Commit 一次，输入/Notify 信号，清理回调 |
| 资产 | `UAbilityGraphDefinition`、`UAbilityNodeConfig` | SchemaVersion、稳定 NodeId/TypeId、私有参数、边、预算和兼容配置 |
| 执行 | `UAbilityGraphInstance` | 冻结配置、生成 InvocationId、校验来源/载荷、排队、推进和终止 |
| 工厂 | `FAbilityNodeRegistry` | TypeId、配置类、真实执行类、输入/端口类型、寿命和目标能力 |
| 具体行为 | `Nodes/Ability*Node*`、`AbilityCharacterActions`、`AbilityTraditionalActions` | 实际查询、投射物、伤害、状态、召唤、格挡和表现 |
| 旧资产编译 | `UAbilityLegacyGraphCompiler` | 将旧 DA 引用图编译为同一 Schema 2 结构，保留原参数和明确兼容语义 |
| 生命周期 | `UAbilityTask_RunGraph`、`UAbilityGraphWorldSubsystem` | GA 绑定图、独立施法图、世界退出清理；持久目标状态仍由原组件/子系统管理 |

目录分别记录 `entryDispatch=GraphInstance`、`SharedRegisteredAction` 和 `HostSessionSharedAction`。共享动作入口表示原动画宿主和图节点调用同一个实际动作函数，不表示原宿主已经改为图时钟。AI 选择器和 GAS 基类是入口/管理职责；被动监听器若实际施加 GE，也必须复用已注册的 GE 动作，不能因名称为监听器而漏迁移。

## 注册动作与资产兼容

注册表源码在 `Private/AbilitySystem/Unified/AbilityNodeRegistry.cpp`。新增节点必须同时登记执行类和端口；只增加名字或目录记录不能通过迁移验收。

| 能力族 | 当前组合接口 | 原资产处理 |
|---|---|---|
| 前四批 Actor / Mixed 18 种 Kind | `Legacy.Actor.*` / `Legacy.Mixed.*` | 原 GA/DA 路径保留，运行时编译后使用提取的 Driver |
| 第五批 | `Legacy.Extension.*` | 原 Behavior 和扩展子连接编译为图边；共享 CastContext 支持跨族组合 |
| 第六批连锁/石化 | `Delivery.ForkedChain`、`Status.PetrifyArea`、`Status.Petrify` | 保留分叉 BFS、状态服务及真实 Actor/Mass 目标生命 |
| 通用查询/伤害 | `Query.Shape`、`Effect.Damage` | 查询输出 LiveTarget，伤害输出历史 DamageResult |
| 传统投射物/召唤 | `Delivery.ActorProjectiles`、`Spawn.Summon` | 原投射物类、散射/追踪和召唤点/间隔共用实现 |
| 传统近战/Nova/碎片 | `Melee.Contact`、`Shape.ArcaneNova`、`Shape.ArcaneShards` | 近战/碎片保留动画前端；Nova 是服务端即时 Commit 后执行，不等待蒙太奇 Notify；各自保留原费用与结束时点 |
| 闪电/环绕 | `Channel.Electrocute`、`Channel.OrbitingFireBolts` | 策略管理目标、计时、费用和句柄，GA 管理输入与激活 |
| 角色/武器/敌人 | `Combat.CharacterStrike`、`Delivery.WeaponProjectile`、`Combat.TelegraphedContact` | 显式保留剑盾、Stander、武器、怪物的几何/伤害/房间差异 |
| 格挡 | `Status.GuardWindow` | 每次执行持有自己的 GE 句柄，结束只撤销该句柄 |
| 武器位移 | `Movement.Character` | GroundDash/WeaponArc 共用原地面、坡度、房间和碰撞扫掠；动画宿主仍提供动作进度 |
| 弹弓/萨满召唤 | `Delivery.EnemySling`、`Spawn.ShamanMinions` | 共用原投射物发射、导航落点、召唤归属和召唤物死亡回收 |
| Grux/蘑菇专属 | `Move.GruxCharge`、`Status.GruxRoarArmor`、`Field.ShroomSpore` | 共用冲锋扫掠去重、护甲与孢子周期，独立图入口管理预警/作用阶段 |
| 通用 GE 与属性监听 | `Effect.GameplayEffect`、`Status.OwnedEffect` | 保留 Spec 等级和 SetByCaller；持久状态只撤销自己的非合并句柄 |
| 活 Actor 的 GE | `Effect.GameplayEffectTarget`、`Status.OwnedEffectTarget` | 输入必须是 LiveTarget，查询后每次施加重验原生命；不把 Mass 实体视为 ASC |
| 快照转活目标 | `Target.ResolveImpact`、`Target.ResolveDamageTarget` | 重新验证原生命令牌，禁止重新捕获已复用 Actor 的新生命 |

旧 DA 仍可调参，原引用关系仍有其校验规则。跨族扩展写入 Schema 2 图，不把第五批指针硬填进旧 `OnHit`。`UAbilityLegacyNodeConfig.Parameters` 是行为参数快照，`PresentationParameters` 是稳定原资产引用；后者供复制表现使用，禁止复制临时参数 UObject。

## 一次施法的调用顺序

以新组合“范围伤害后石化”说明真实函数顺序：

1. ASC 根据 Spec 激活 GA，`PreActivate` 捕获原 Avatar/ASC/世界/生命，建立会话。
2. `UUnifiedGraphAbility` 验证配置和目标模式；目标数据返回后复核来源与目标，再通过 `CommitAbility` 扣除一次费用/冷却。
3. `UAbilityTask_RunGraph::Activate` 调用 `UAbilityGraphInstance::StartWithInput`；图校验、配置快照及输入校验成功后才创建执行对象。
4. `Shape.ArcaneNova` 查询目标、预先捕获生命，调用共享伤害服务；每个结果排入 `DamageResolved`。
5. `Target.ResolveDamageTarget` 解析该结果保存的原目标令牌，仍存活且属于同一租约才输出 `TargetFound`。
6. `Status.Petrify` 调用 `AbilityStatusService::ApplyPetrify`，交给 Actor 石化组件或 Mass 状态通路；图完成不提前撤销独立石化状态。
7. 图完成通知任务，任务通知 GA；GA 先关闭会话，再释放动画、委托和占用。石化由原状态持有者到期清理。

这条组合必须连接 `DamageResolved -> ResolveDamageTarget -> TargetFound -> Petrify`。不能直接把 DamageResult 当 LiveTarget。致死结果可以继续在命中位置触发爆炸；不能继续石化死亡或重新生成后的实体。

## 动画与时间轴

动画仍由 `UAbilityTask_RunTimeline` 或原蒙太奇任务拥有。Timeline 以源动画位置推进事件，修改前摇/后摇实际秒数不会把 Notify 从蒙太奇中删除；事件仍需校验播放实例、会话和单调序号。配置事件与 Notify 只能有一个实际伤害入口。

Stander/武器宿主在事件到达时通过 `DispatchExecutionAction` 调用共享动作。图不是另一个蒙太奇播放器；不要让图和 GA 各播放同一动画。时间轴支持范围和取消能力仍以 [时间轴文档](ability-timeline-framework.md) 为准，不能因接入图就假定已支持所有根运动或预测场景。

格挡后的反击仍由原前端播放反击蒙太奇，在真实命中时间调用 `Combat.CharacterStrike`。把 `Status.GuardWindow` 的格挡事件直接接到伤害节点会立即结算，不会自动得到该反击动画和接触窗口；新图若需要相同表现，必须由动画前端在对应事件恢复后续动作。

## 来源、目标和网络

`FAbilitySourceToken` 保存弱 World、Avatar、ASC、可选 RunId 与敌人 Lease。`FAbilityTargetToken` 保存 Actor 弱身份/Lease，或 Mass 实际 EntityHandle/查询 ID/World。服务器捕获，异步恢复前重新解析；客户端不能提交自报的租约或实体身份作为可信凭据。

图和会话只在本进程执行，不复制 UObject 图、队列或委托。权威端决定目标、费用、随机、伤害和控制；原 ASC GE/属性复制、角色蒙太奇、Cue、Actor 和 Mass 表现通道负责客户端可见结果。旧 LocalPredicted 前端仍可做输入/动画等待，不在客户端运行伤害图。NullRHI 自动化不证明远端画面、晚加入表现或网络延迟行为。

迁移的受击前端保留原 `ApplyGameplayEffectToOwner` 预测键路径。属性监听前端保留客户端 Wait 和原 ASC `MakeOutgoingSpec -> BP_ApplyGameplayEffectSpecToSelf` 调用；该旧 API 不传预测键，非权威施加仍由引擎拒绝，不能擅自补键把旧失败变成预测成功。这两项兼容前端都不放宽服务器图策略的权限。

常驻 `UMyAttributeEventAbility` 的订阅会话绑定当前 World/Avatar/ASC/存活状态/池租约，不绑定出生时尚未完成的 RunId。每条新事件在服务器现场捕获当前 Run 的严格来源，再执行同一 GE 动作；普通 GA、图和延迟动作仍维持 Run 隔离。不得把订阅寿命的例外当成旧伤害令牌可以刷新重用。原因和回归见 [单人经验与伤害反馈修复](2026-11-09-solo-xp-and-mass-damage.md)。

Actor 伤害继续走既有 `GE_Damage` / ExecCalc / AttributeSet；Mass 走 `UPureMassSubsystem::ApplyDamage`。只有实际可观测时才填写 `ActualDamage` / `Applied`；Actor 的施加尝试不能虚构为最终净伤害。Mass 击杀根据本次 Killed 通知确认，不能把查询失败当击杀。

Mass 的 `ApplyDamage` 返回实际移除生命；飘字显示包含过量部分的完整结算伤害，与 Actor `IncomingDamage` 的显示口径一致。图中的净伤害结果不能从飘字反推。

## 生命周期与重入

- 共享 DA/CDO 只读。图启动时复制节点配置，旧编译器冻结所有可达参数（含循环引用）；施法中修改编辑器资产只影响下次激活。
- 图通过 UPROPERTY 保活执行对象、参数和队列资源；世界、ASC、角色和目标使用弱身份。回调期间使用强局部保护，避免同步取消或 GC 使正在执行的对象失效。
- 绑定 GA 的图在 GA 结束时取消。独立效果由 `UAbilityGraphWorldSubsystem` 持有并推进，GA 正常结束可保留；来源失效、换图或世界退出仍关闭。
- 取消先封闭事件入口，再 Stop 策略、清计时器、解绑委托、撤销所属 GE、销毁所属表现/Actor。重复停止无副作用，取消不补发业务 OnExpire/Completed。
- 独立位移和怪物图节点通过角色拥有的 `UAbilityMovementOwnershipComponent` 获取弱执行对象/唯一租约，拒绝同生命并发占用。释放必须匹配该租约；眩晕使预警序号失效不影响移动归还，旧生命不能恢复新生命的移动模式。组件随角色销毁，不是全局单例。
- 石化、叠层、位移仍遵循原持有者的生命周期。旧 Actor 租约、Mass 句柄和 RootMotionSource 对象必须核实，不能按重用的数字 ID 清理别人的状态。
- 伤害、Cue、Spawn、费用、OnDestroyed 都可能同步回调。每一步之后若继续访问来源/目标或安排计时器，必须再次检查来源生命和当前执行是否仍活跃。

## 兼容差异与预算

旧兼容 profile 保留 Actor/Mixed 持续费用、周期追赶、尾帧、目标失效、第五批叠层和位移规则。原递归子调用改为 FIFO 队列是明确的调度差异；子调用额度预约成功不保证之后表现 Actor 生成成功。不要声称所有同步回调顺序完全不变。

新图默认最多 64 次调用、256 个累计事件、32 个活动节点、深度 8，每帧排空最多 64 个事件。显式 `LegacyStreaming` 允许长期渠道累计事件增长，但物理队列、活动数、深度与总生成调用预算仍有界；它不表示可以无限产生子节点。

旧 ArcaneShards 首片之后尝试费用、尾延迟后提交冷却；Electrocute 按周期费用并保留结束命中；旧 Summon/Melee 不额外增加 Commit；Melee 击退概率分支的额外伤害以兼容配置明确保留。新技能应选择清晰的费用合同，不能照搬这些历史例外作为默认。

## 扩展检查表

新增 `TypeId` 时完成参数校验、真实执行类、输入/输出类型、目标支持、寿命与清理，注册到同一工厂。复用现有 Actor/Mass/GE 服务，不建第二套 ASC、伤害系统或全局管理单例。

测试必须观察实际属性、目标状态、生成对象或资源释放，并覆盖取消、重复事件、来源/目标换生命。资产变更先备份和记录受保护配置，保存后另开进程独立读回；同进程 UObject Reload 不能代替磁盘验证。详细步骤见 [统一制作指南](ga-unified-authoring-guide.md)。
