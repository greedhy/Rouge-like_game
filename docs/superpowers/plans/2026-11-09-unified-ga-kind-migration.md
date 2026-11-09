# 统一 GA P2：18 种 Kind 兼容迁移 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在已有 GA 身份、Actor/Mass 伤害与生命周期通路上，为前四批 18 种 Kind 建立可注册的执行策略、逐族兼容配置和金样迁移证据。

**Architecture:** 先把 P1 的“两种 TypeId 硬编码分派”改成真实执行策略注册，再建立多目标/位置载荷、伤害服务和明确的执行寿命域。按行为族提取旧 Runtime 的状态推进规则，迁移为新策略；旧 GA 只保留输入、校验、Commit 和持有/移交执行的入口职责。Actor 与混合实现分别固定兼容语义，不用一种时钟、目标规则或结束原因覆盖两者。

**Tech Stack:** UE 5.8、项目现有 C++/GAS、Actor/Mass、DataAsset、原生 Automation、UE Python 资产审计、PowerShell 新鲜报告检查。

**Spec:** `docs/superpowers/specs/2026-11-08-unified-ga-environment.md` 第 4–6、8–9 节；前阶段计划 `docs/superpowers/plans/2026-11-08-unified-ga-environment.md`。

**历史设计记录。** 本文保留原 P2 拆分与金样要求，不是当前待办入口，下面拟议文件名与分阶段开关不应直接当成现有 API。用户随后授权一次推进全部 GA，实际实现采用共用 Driver、兼容编译器与 Schema 2 策略，预算裁定及收口以 [全量计划](2026-11-09-all-ga-migration.md)、[框架说明](../../ga-framework-reference.md) 和 [验证记录](../../2026-11-09-all-ga-migration-verification.md) 为准。未逐条照搬的拟议步骤保持未勾选，不伪装成逐项实施记录；PIE/联机/视觉独立验收。

## Global Constraints

- 使用项目当前 UE 5.8、C++、GAS 和 Actor/Mass 通路，不引入第二套 ASC、背包或伤害结算系统。
- 保留已有 GA 路径、技能 Tag、输入槽语义、装备实例身份与存档身份；迁移只改变经过验证的内部执行实现。
- 共享 DataAsset 是只读配置；运行状态放入每次执行实例，不在施法时修改共享 DA/CDO。
- 服务器决定目标、费用、概率、控制和伤害；首实现包不增加客户端预测图。
- 必须遵守 `docs/cpp-readability-standard.md`、`.clang-format`；涉及敌人状态或池复用须遵守 `docs/monster-development-standard.md`。
- 图校验默认拒绝任意连接环。后续重复只能由有界 Period/Repeat 节点表达。
- 新 V1 图默认上限：64 次节点调用、256 个接受事件、32 个同时活动节点、深度 8；每帧最多处理 64 个队列事件，其余下一帧继续，整体预算耗尽则取消整图。
- 旧执行器原 3 层/64 次等限制继续由兼容配置保留，不直接替换。
- `LegacyEntry` 是能力声明，不是完成状态。注册旧 GA、调用旧 GA 或在新节点中生成整个旧 Runtime，均不能记为完成公共行为提取。
- P2 不迁移第五批 Extension、Batch6 专属 Field/Chain 扩展、时间轴/Notify、Stander、装备派生入口；保留这些调用者并跑受影响回归。
- 不删除旧 Runtime，不批量覆盖 Content，不以新原生测试通过替代用户双端 PIE 验收。

## Review Focus

- 长帧恰好跨 Duration/ArmDelay/Period：不得多扣蓝、漏尾帧或改变旧到期子节点顺序；Task 1、5、6、7 固定分族轨迹。
- 致死命中后子节点：位置快照可用于明确允许的查询，旧目标令牌不能对下一池生命施加效果；Task 2、3、5 验证。
- GA 同步结束但效果未结束：有限弹体/Field/Delay 保持独立，Beam/Orbit 仍受取消管辖；Task 4、8 验证。
- 多目标、一帧多次命中与重入取消：每次调用/事件独立去重，观察者取消后不补发子效果；Task 2、3、6 验证。
- 无限期根 Orbit 与有限总事件预算、旧子生成拒绝与新整图取消存在真实冲突：Task 1 必须审核可执行的预算政策；未解决的行保留 AdapterRequired，不能写成 Verified。

---

## 源码依据与当前差异

以下锚点均来自本次实际读取的生产实现和测试；行号易随 P1 格式调整变化，实施时按符号重新定位。

| 简写 | 文件及主要符号 |
|---|---|
| A | `Source/test_25d/Private/AbilitySystem/Prototypes/PrototypeRuntime.cpp`：`Spawn`、`BeginPlay`、`Tick`、`Hit`、`Emit`、`Finish`、`TickProjectile`、`Pulse`、`ConePulse`、`ShapePulse`、`TickWave`、`TickEmitter`、`TickChain`、`TickOrbit`、`CleanupOwnedActors`、`OrbitHit` |
| M | `Source/test_25d/Private/PureMass/PureMassRuntime.cpp`：`Spawn`、`BeginPlay`、`Tick`、`Hit`、`Emit`、`Finish`、`Projectile`、`Pulse`、`Nearest`、`Chain`、`Orbit`、`NotifyCommittedAttack`、`EndPlay` |
| GA-A / GA-M | `Private/AbilitySystem/Prototypes/PrototypeAbility.cpp`：`CanActivateAbility`、`CheckCost`、`ApplyCost`、`ApplyCooldown`、`OnTargetData`、`Execute`、`EndAbility`；`Private/PureMass/PureMassAbility.cpp`：`ValidateCombatTarget`、`ExecuteMass`、`SpendMana`、`EndAbility`（路径均以 `Source/test_25d/` 为前缀） |
| 配置/预算 | `Public/AbilitySystem/Prototypes/AbilityPrototype.h`：`EAbilityPrototypeKind`；对应 cpp：`UAbilityPrototype::IsValidConfiguration`、`UPrototypeCastContext::ReserveSpawn` |
| 目标/伤害 | `Private/PureMass/PureMassCombatTarget.cpp`：`Resolve/Gather/Sweep/Visible/GroundVisible`；`Private/PureMass/PureMassSubsystem.cpp`：`ApplyDamage/ApplyPetrify/IsRoomDamageSourceAllowed`；`Private/AbilitySystem/MyAbilitySystemLibrary.cpp`：`ApplyDamageEffect` |
| P1 实际执行 | `Public/AbilitySystem/Unified/AbilityNodeRegistry.h`、`Private/AbilitySystem/Unified/AbilityGraphInstance.cpp`：`Register` 目前只有描述；`InvokeNode` 比较 Delay/Petrify 字符串；`EnqueueEvent` 只接受序号 1 的 Completed；`ResolveCurrent` 当前要求全图共享一个活目标 |

必须固定的金样差异：

1. **扣蓝与时序。** A 的 Beam/Orbit 累积 `ManaClock`，整秒 `FloorToInt` 后一次性扣 `ManaPerSecond * Seconds`；不足则整笔不扣并取消。M 每个 Tick 扣 `ManaPerSecond * Delta`。双方都在部分寿命/目标判断之前收费，不能以“本帧后来失败”为由自动退款。根费用都只在 GA Commit 一次，子节点不再次 Commit。
2. **Beam 丢目标。** A 对无效/超距/遮挡目标 `Finish(true)`，标记共享 Context 取消，不发 OnExpire，已生成子效果后续停止。M 对同类分支 `Finish(false)`，允许 OnExpire，已有子效果不因这个节点正常结束被撤销。两者都不能在真实 Cancelled 之后发 Completed；M 的兼容到期要显式带 TargetLost/Blocked 原因和位置输入，不能假装目标仍活着。
3. **周期不是统一上限。** M `Tick` 的普通周期和 Beam/Field/Chain/Emitter 循环每帧最多 **8** 次，扣除已处理周期后保留其余 backlog。A Beam/Surface 等普通周期每帧最多一次且 `Fmod` 丢掉整周期积压；A Field 使用容差 while，覆盖 `(0, Duration]`，没有 8/16 次截断；A Emitter while 按成功发射数/Context 预算收敛，空闲或失败时保留模余数。`AExtensionRuntime::Tick` 的 **16** 次并将 Clock 限到一个 Period，是第五批 P3 的规则，不能误写成前四批默认。
4. **尾帧。** A Wave/Returning/Emitter 消费 `min(Delta, Duration - oldAge)`，Field 单独消费最后部分并补最后边界脉冲。M 普通 UAbilityPrototype 多数先 `Age >= Duration` 结束，跳过尾帧行为；仅 `UBatch6FieldPrototype` 的特定 Wave/Field 走 `bFinalBoundary`。P2 不把 Batch6 子类当普通 Kind 转换。
5. **子事件。** A `Hit` 在伤害后读取 Actor 位置再 `Emit(OnHit)`，然后可能广播 OnKill；M 在伤害前保存位置，伤害后 `Emit(OnHit)`，不提供相同的 BehaviorEvent 广播。A `Emit` 即使 Child 为空也可广播本地事件；M 没有对应广播。OnHit 不等于“实际扣血大于零”，不能据此过滤格挡/免疫/权限拒绝后的既有分支；新增 DamageResolved 必须如实区分尝试、结果与位置采样时机。
6. **终止与资源。** 双方 `Finish(true)` 会取消共享 Context，`Finish(false)` 可以生成独立子节点；Finish 后保留约 0.35 秒复制窗口。A Multishot 容器例外为 0.15 秒，M 为 0.35 秒；容器均不发自身命中/到期钩子。A Surface/Orbit 子 Actor、ASC 委托由 `CleanupOwnedActors/EndPlay` 清理；M 清 Surface 和 OrbitBolts，不能把清理延后到普通 GC。
7. **入口与几何。** GA-A 直线光标只选方向；GA-M `ValidateCombatTarget` 仍校验请求位置/射程。GA-A 普通弹体初始点有方向前移 60，M 从 Avatar+Z30 起；Returning、Chain 起点也不同。A Burst/Field/Trap 的 `Pulse` 没有 M `Pulse` 的逐目标 LOS；A Trap 触发候选与 M `Nearest` 可见目标也不同。必须按 profile 保留，不能借公共查询服务悄悄统一。
8. **旧预算与环。** `ReserveSpawn` 接受深度 0..3、总生成 64，拒绝新子节点不等于取消已有节点；Multishot 子弹同深度，每颗也消耗生成名额。旧 DA 自引用可被深度截断，现有 `PureMass.Mixed.ChildrenAndBudgets` 验证二叉自引用共 15 次。新图仍拒绝环：转换器按 0..3 的有限层数展开旧引用，不保留执行环。

## 文件与接口设计边界

下面均为**待实现的新接口**，不是对 P1 已有 API 的描述。头文件放 `Source/test_25d/Public/AbilitySystem/Unified/`，对应 cpp 放 `Source/test_25d/Private/AbilitySystem/Unified/`；后文省略模块前缀的 `Private/Tests/` 均指 `Source/test_25d/Private/Tests/`。配置用反射类型，执行状态只由每次调用持有；禁止把状态塞入描述符或共享配置。

### 执行策略注册（先于任一 Kind 迁移）

- 新建 `AbilityNodeExecution.h/.cpp`，声明 `UAbilityNodeExecution : UObject`（Transient/Abstract），每次调用创建独立对象：
  - `virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& InvocationId, const UAbilityNodeConfig& Config, const FAbilityNodeInput& Input, FString& Error)`。
  - `virtual void Advance(float DeltaSeconds)`。
  - `virtual void Stop(EAbilityGraphEndReason Reason)`；幂等，先关闭回调再解绑/释放。
- `FAbilityNodeDescriptor` 增加 `TSubclassOf<UAbilityNodeExecution> ExecutionClass`、`TArray<FAbilityPortDescriptor> Ports`、`EAbilityExecutionLifetime Lifetime`；`Register` 同时验证配置和执行类，拒绝“有描述、无实现”。保留 V1 `OutputPorts` 读兼容，由明确转换函数生成 Ports，不让两个列表独立可编辑。
- `UAbilityGraphInstance::InvokeNode` 仅通过注册表构造 ExecutionClass，`Advance` 调策略；新增 `bool Publish(const FGuid& InvocationId, FName Port, const FAbilityNodeInput& Payload, EAbilityGraphEndReason Reason)`、`void FinishInvocation(const FGuid& InvocationId, EAbilityGraphEndReason Reason)`。P1 Delay/Petrify 迁入 `Nodes/AbilityBasicNodeExecutions.h/.cpp`，禁止继续增加 TypeId 字符串 if 链。
- 执行实例由 UPROPERTY 数组/映射持有，Start 与回调期间不得保留可能被 TMap 扩容作废的引用；策略不能持有越过 Stop 的裸 Owner 指针。

### 类型化输入、目标查询和事件

- 新建 `AbilityNodeInput.h`：`FAbilityNodeInput` 包含 SourceToken、可选 LiveTargetToken、Origin、Direction、可选 `FAbilityImpactSnapshot`。快照记录位置、方向、已确认命中身份和序号；它不是活目标能力凭证。
- `EAbilityPortPayload {Origin, LiveTarget, ConfirmedImpact, DamageResult}`；`FAbilityPortDescriptor {Name, Payload, bTerminal}`。端口名称至少覆盖 `TargetFound/NoTarget/HitConfirmed/DamageResolved/PeriodElapsed/Completed/Cancelled/Failed`，每个策略只声明实际支持项。bTerminal 表示结束本调用，Completed仍可建立后继；Cancelled/Failed额外禁止业务执行连接。
- P1 `Start(Graph, Source, Target, Error)` 保留作为 V1 委托入口；新增 `StartWithInput(Graph, const FAbilityNodeInput&, const FAbilityExecutionOptions&, FString&)`。SchemaVersion=1 完全保持；SchemaVersion=2 才允许多事件、位置输入、寿命域与兼容策略。
- 图不再以单个根目标失效取消所有无关分支。Source 每次回调必验；要求活目标的调用按各自令牌验；查询/区域/旧固定原点 Delay 可以没有根目标。V1 单目标行为不变。
- 新建 `AbilityTargetQueryService.h/.cpp`：`bool Gather(const FAbilitySourceToken&, const FAbilityQueryRequest&, TArray<FAbilityTargetToken>& Out, FString& Error)`、`bool Sweep(const FAbilitySourceToken&, const FAbilitySweepRequest&, FAbilityQueryHit& Out, FString& Error)`。Request 携带 Geometry/Origin/Direction/尺寸/上限/目标排除集和 CompatibilityProfile；服务端捕获每个结果的真实生命令牌。
- `FAbilityQueryRequest` 的 `Geometry` 为 Sphere/Cone/Ring/Line/WaveFront，尺寸字段包含 Radius/InnerRadius/Range/Width/HalfHeight/PreviousRadius；`FAbilitySweepRequest` 包含 Start/End/Radius。`FAbilityQueryHit` 区分目标命中与墙命中，保留 Time/ImpactPoint。A 使用原筛选/碰撞与 LOS 策略，M 使用 PureMassCombat；拒绝跨族不支持的目标形态。
- 同调用允许多个唯一 EventSequence；去重键为 ExecutionId/InvocationId/Sequence。终态最多一次，不因每个子目标命中结束父调用；事件排队上限与每帧处理上限分开。

### 伤害、费用和持有者

- 新建 `AbilityDamageService.h/.cpp`：`FAbilityDamageResult Apply(const FAbilitySourceToken&, const FAbilityTargetToken&, const FDamageEffectParams&, float Scale, FName CompatibilityProfile)`。结果含 bAttempted、bKilled、可选 Applied/ActualDamage、ImpactSnapshot；Actor 继续 `UMyAbilitySystemLibrary::ApplyDamageEffect`，Mass 继续 `UPureMassSubsystem::ApplyDamage`，不复制护甲、抗性、暴击或奖励公式。
- Actor 现有 API 返回 EffectContext 而非精确扣血量/可靠的应用成功标志，未有可靠观测时 Applied/ActualDamage 必须为空，不能把有效 Context 当成功施加或把 BaseDamage 当实际伤害；Mass 可采用原伤害返回值，并保留零伤害不等于失败的区分。Debuff 概率沿原路线，金样用禁用/100% 确定性数据，随机行为另测统计/边界，不更换 RNG 顺序。
- 新建 `AbilityUpkeepService.h/.cpp`：`bool Advance(const FAbilitySourceToken&, float Delta, float ManaPerSecond, FName CompatibilityProfile, FAbilityUpkeepState&, FString&)`，Actor 整秒累计和 M Delta 明确分支；底层复用原 SpendMana 路线，根 Commit 不迁到服务内。
- 新建 `AbilityGraphWorldSubsystem.h/.cpp`：`EAbilityExecutionLifetime {BoundToAbility, IndependentCastEffect, TargetPersistentState}`；`bool Adopt(UAbilityGraphInstance*, EAbilityExecutionLifetime, FString&)`、`void CancelExecution(FGuid, EAbilityGraphEndReason)`。只接管服务器执行，UPROPERTY 强引用持有活实例，World teardown/Run/source 失效先关闭所有事件再 Stop 资源；不让独立效果成为失去持有者的 UObject。
- 同施法有共享取消组；BoundToAbility 的 Beam/Orbit 取消会关闭其子效果；有限根 GA **正常结束**只移交 IndependentCastEffect，不取消它。目标持久状态继续由现有组件/子系统拥有，图完成不撤销其他来源状态。
- 新建 `AbilityGraphPresentationActor.h/.cpp`：仅持复制表现快照（ExecutionId/NodeInvocationId/位置/方向/BeamEnd/状态/配置引用），不求值业务图；保留各族结束表现窗口、晚加入可恢复状态和世界退出清理。已有可直接复用的 Surface/OrbitBolt 资产继续使用，不新增第二套特效素材。

## 18-kind 迁移矩阵

所有行当前为 **AdapterRequired**；每行必须分 Actor 与 Mixed 两条证据记录。下列 TypeId 是拟注册的新行为策略，不是旧 GA 包装。普通新 Schema2 图可以组合 Query 与 Damage 节点；兼容 Kind 策略可内部调用同一查询/伤害服务，以保留旧“一次行为生成”计数，不能再次生成 A/M 整个 Runtime。

| Kind（枚举顺序仅用于核对，不作持久协议） | 拟 TypeId / 寿命 | 必须保留的族差异与源锚点 | 金样与最低通过标准 |
|---|---|---|---|
| Projectile | `Delivery.Projectile` / IndependentCastEffect | A TickProjectile 多对象物理扫掠；M Projectile 混合扫掠/墙平局优先，单帧最多 64 重查；首碰/Range/Duration 到期 | 原 ProjectileSweepAndMultishot、NoDoubleHit、WallBeforeEntity；长帧、友军胶囊/Trigger 不挡、墙先目标、每目标一次、准确 OnHit→OnExpire |
| Multishot | `Delivery.Multishot` / IndependentCastEffect | A/M BeginPlay，Count 扇形；容器无钩子，弹丸同深度但消耗名额，A/M 容器复制留存不同 | Count=1、5、16；产生 1+Count 次行为名额，无递归扇形；每颗子弹单独命中/到期，容器零额外事件 |
| Burst | `Shape.Burst` / IndependentCastEffect | A BeginPlay/Pulse 不作 M Pulse 的逐目标 LOS；初次脉冲一次，宿主到 Duration 才 OnExpire | BurstDamageTeamsAndComposition、BurstAndTeams；Actor-only 与 Mixed 结果分别对照，敌友/高度/墙策略不被统一，OnHit 子效果不再 Commit |
| Beam | `Channel.Beam` / BoundToAbility | A Tick 整秒收费、单次周期、目标失效 Cast 取消；M Tick Delta 收费、8 次、正常 Finish/OnExpire | BeamUpkeepAndLostTarget、HomingBeamAndLifetime；0.25/0.75/2.4 秒步进、余额不足、LOS/超距/死亡；分别核对子效果存亡与到期次数 |
| Surface | `Area.Surface` / IndependentCastEffect | A/M BeginPlay 复用 ATerrainSurface；M 显式 bAffectPlayers=false/bAffectEnemies=true；根/子都投影，先失败投影再根 Commit | ChildSurfaceProjectsToFloor、SkillTerrainFiltersCasterAndFriends、Mixed.SkillSurfaceDamageAndControl；无地面不扣根费、大小/来源/房间保持、结束清 Surface 和地表状态 |
| Orbit | `Channel.Orbit` / BoundToAbility（根） | A TickOrbit 真 AMyOrbitingFireBolt + OffensiveAttackTargeted；M OrbitBolts 状态+NotifyCommittedAttack；根无限开关、子受 Duration；整秒/Delta | OrbitCompositionAndTerrainAttribution、OrbitActorTarget、OrbitImmediateLaunchStartsAtCaster；首次同帧发射不从世界原点、上限/发射/返轨/委托清理；预算政策未获审核前不切换此行 |
| Field | `Area.Field` / IndependentCastEffect | A Tick 全部追赶含最后边界；M 普通 Field 每帧 8 且通常先到期；Batch6 子类排除 | CompositionFieldFinalPulseAndCatchup、FieldTrapExpiryAndCancellation；Duration=1、Period=.1、Delta=1.1：A 10 次，M 普通 0 次；多帧积压与最后 OnExpire 顺序固定 |
| Trap | `Area.Trap` / IndependentCastEffect | A Tick 只按候选触发；M Nearest 要求可见；M 一般周期可在布防中产 OnPeriod，A Trap 提前 return 不产 | FieldTrapExpiryAndCancellation、ShapesAndTimedKinds；ArmDelay 前无伤害、首次有效触发恰一次、被墙遮住候选按 profile、取消无到期效果 |
| Homing | `Delivery.Homing` / IndependentCastEffect | A TickProjectile 与 M Projectile 都有转速限制；目标失效正常结束；不同起点/扫掠与 M 通用 OnPeriod | HomingTracksAndLosesTarget、HomingBeamAndLifetime；移动/死亡/池换 Lease 后不追新生命，转角上限、墙和正常到期位置 |
| Piercing | `Delivery.Piercing` / IndependentCastEffect | A/M 按首次命中顺序 `pow(Falloff, hitCount)`，Count 上限，历史目标排除；混合命中与墙规则 | PiercingUniqueTargetsAndWall、PiercingOrderAndWalls；Actor→Mass→Actor 排序、0/1 Falloff、Count 达限、墙前后、同生命不重复 |
| Chain | `Delivery.Chain` / IndependentCastEffect | A TickChain 首跳即时/随后每帧最多一跳清 Clock；M Chain 首跳后可 8 次追赶，末跳可当帧 Finish；A OnExpire LastChainTarget，M 使用当前 Target | ChainFalloffNoRevisitAndWall、ChainExpiresAtLastHitTarget、AlternatingChain；首跳 Range/后跳 Radius、不可回访、末跳位置和目标载荷分别记录，不套用 Batch6 BFS |
| Cone | `Shape.Cone` / IndependentCastEffect | A Spawn 平面化方向、角边界有 KINDA_SMALL_NUMBER；M Pulse 平面筛选但保留原方向载荷；一次命中、到期另发 | ConeAngleRangeTeamsAndWall、ConePresentationAndChildrenSharePlanarDirection；边角/身后/同原点/墙，子节点方向与本地表现一致 |
| Wave | `Shape.Wave` / IndependentCastEffect | A TickWave 只扫新波前，忽略后来进入已过内圈目标；M 普通 Wave 扫当前全盘但目标去重；Batch6 wave-front 扩展排除 | WaveSweepsOnceAndSkipsPassedInterior、ShapesAndTimedKinds；晚进内圈 Actor/Mass 分别命中或不命中，最大半径/尾帧/每生命一次 |
| Returning | `Delivery.Returning` / IndependentCastEffect | A/M 均消费掉头剩余时间、返程清 HitTargets、返程固定 Falloff；A 消费 Duration 尾帧，M 普通先到期 | ReturningTwoPassesAndMovedCaster、ReturningConsumesTurnaroundAndFinalFrameTime；同目标两程各一次、每程 Count、移动施法者、同帧去返、末帧差异 |
| Emitter | `Control.Emitter` / IndependentCastEffect | A TickEmitter while，成功子 Spawn 才计 Count，失败/空闲留 Fmod；M Tick 最多8，空闲清0，失败处理保留剩余循环 | EmitterSelectsEnemyAndRespectsCountAndCancel、EmitterCatchupAndSuccessfulEmissionCount、ChildrenAndBudgets；仅成功计数、空闲后不突然补射、预算拒绝、目标与方向位置正确 |
| Delay | `Control.LegacyDelay` / IndependentCastEffect | 不可直接复用 P1 Control.Delay：旧行为固定原点、可无目标，触发 OnExpire；A ArmDelay 分支先于 Duration，M 普通 Duration 先判且可有通用 OnPeriod | DelayFixedOriginOnceAndCancel、BasicKindsAndComposition；移动源/目标不移动原点、长帧只一次、取消/源失效零效果、死亡目标仅允许位置型子行为 |
| Ring | `Shape.Ring` / IndependentCastEffect | A ShapePulse/M Pulse：水平内外边界、HalfHeight、LOS；一次命中，持续期只为到期钩子 | RingHoleBoundaryHeightAndWall；内洞/恰边界/楼层/墙、混合对象同几何，取消后无到期效果 |
| Line | `Shape.Line` / IndependentCastEffect | A ShapePulse/M Pulse 有向矩形；A 光标只选方向而 M 入口还做射程校验；一次命中 | LineOrientedRectangleOnceAndWall、LineCursorOnlySelectsDirection、TargetValidation；长度/半宽/高度/端点/身后、远光标按入口 profile、无重复命中 |

每个行为族配置写 `LegacyActor.<Family>.v1` 或 `LegacyMixed.<Family>.v1`（Family 为 Shape/Delivery/Timed/Channel/Surface），并保存原 Kind 名称和原 DA 路径用于审计；拒绝任意 FName 拼写的 profile。不同族仍可有明确的 Kind 子政策，例如 Field/Emitter 的追赶差异，不使用全局 `bLegacy` 布尔开关。

## 实施任务

### Task 1：P1 门槛、双后端金样与预算兼容审核

**Files:** 新建 `Source/test_25d/Private/Tests/AbilityKindGoldenFixture.h`、`AbilityKindGoldenTests.cpp`；新建 `Scripts/GA/Unified/compare_kind_traces.py`、`kind-migration-manifest.json`；维护 `docs/ga-unified-migration-matrix.json`。

**Interfaces:** 测试夹具 `RunLegacy(FName Backend, EAbilityPrototypeKind Kind, TConstArrayView<float> Deltas, const FAbilityGoldenScenario&, FAbilityGoldenTrace&)`；trace 记录 Tick序号/伤害前后生命/法力/命中身份/生成计数/位置/结束/子事件。测试专用观察器不进入生产类；无公开观测的 M 子事件通过实际子 Runtime/结果确认。

- [ ] 核对 P1 新鲜完整构建、全部 UnifiedGA 及旧 Batch6 石化回归、保存后样例与 Actor/Mass 实际效果、取消/池复用/旅行证据。未通过时只记录计划，不开始 P2 产品代码。
- [ ] 在同一场景分别驱动 A/M 原实现，固定上述差异轨迹；输出 JSON 到 `Saved/GAAuthoring/UnifiedGA/P2/<run>/golden/`，记录准确 Git SHA、资产路径/包 hash、后端、Delta 序列和随机条件。
- [ ] 跨进程比较用夹具目标逻辑名+生命代次映射随机GUID/实体ID，保留同次调用关系与序号，不能直接比较新建World/ExecutionId字面值。等距目标独立测试真实遍历顺序；普通金样避免不受保证的碰撞查询排序，不能先按名字排序而改变原选择行为。
- [ ] 写 `test_25d.UnifiedGA.KindGolden.Actor.*` / `.Mixed.*`，覆盖 18 名称；每个用例必须有行为断言，不以“Spawn 非空”表示支持。原测试仍执行；显式注册或 ExpectedTests 列出动态测试，禁止零发现通过。
- [ ] 写失败的比较器测试：同名测试缺失、后端互换、空轨迹、同伤害但费用/事件时序不同、旧报告、Actor/Mixed 被合并均失败。比较允许浮点误差但不允许丢事件，默认位置 .01、数值 1e-4，金额与离散次数严格。
- [ ] **预算审核必须形成具体记录：** P1 UnifiedV1 的硬上限及超限取消保持不变。P2 兼容转换的“行为生成额度”沿旧 0..3/64，抽出的服务调用不额外吃旧额度；新框架物理队列/活动资源另有硬限制。仅对能在 V1 安全资源包络内证明等价的配置开放自动转换。
- [ ] 对不在包络内的配置，转换器返回 `NeedsBudgetReview`，明确记录所需 calls/events/active/stream lifetime。尤其无限期根 Orbit 以及旧达到64后仍继续主效果的图：**不得静默重置全局计数，不得后台换 ExecutionId 续命，不得把事件降成未计费诊断来绕过预算。** 本计划的默认可执行政策是继续旧入口，保留 AdapterRequired；必须另有经审核的 Streaming 预算协议或用户明确接受有限寿命/超限取消的语义变更，才能放行这些行。
- [ ] 金样只比较启动时有效、固定配置的正常行为；原 `EmitterCatchupAndSuccessfulEmissionCount` 在运行中修复无效子配置的测试继续留给旧Runtime。新图必须启动前拒绝无效子配置并冻结施法快照；这项规范要求的差异单列 `InvalidAtStart/LiveConfigEdit`，不能通过修改快照迁就旧测试或把失败输入标为已等价迁移。
- [ ] 运行 `Scripts/GA/Unified/test.ps1 -Filter 'test_25d.UnifiedGA.KindGolden+test_25d.Prototypes+test_25d.Compositions+test_25d.PureMass'`，保存实际用例和 fresh index；提交金样与预算决策文档。此任务审核是后续提取前置条件。

### Task 2：真实策略注册与 Schema2 事件协议

**Files:** 新建 `AbilityNodeExecution.h/.cpp`、`AbilityNodeInput.h`、`Nodes/AbilityBasicNodeExecutions.h/.cpp`；修改 P1 `AbilityNodeRegistry.h/.cpp`、`AbilityGraphTypes.h`、`AbilityGraphDefinition.h/.cpp`、`AbilityGraphInstance.h/.cpp`；新建 `Private/Tests/AbilityNodeExecutionTests.cpp`、`AbilityGraphEventContractTests.cpp`。

**Interfaces:** 采用上文执行策略/输入签名；增加 `FAbilityExecutionOptions {CompatibilityProfile, Lifetime}`。`UAbilityNodeExecution` 输出只能经 Owner.Publish，终止经 Owner.FinishInvocation，不能直接递归调用后继。

- [ ] 先写并运行失败用例 `RegisteredStrategyActuallyRuns`、`MissingExecutorRejected`、`InvocationStateIsIsolated`、`MultipleTargetsAndSequences`、`CancelDuringObserver`、`SnapshotCannotFeedPetrify`、`Schema1Unaffected`。
- [ ] 按接口替换 P1硬编码分派；新增 V2端口输入类型校验、每调用终态一次、目标级生命周期验证。Failed/Cancelled 仍只走终止诊断，不能连接到玩法效果。
- [ ] 用一个测试专属策略从注册表执行，证明以后增加节点无需改 GraphInstance；保留全部 P1 Delay→Petrify/预算/重复事件/配置快照结果。
- [ ] 验证 `test_25d.UnifiedGA` 和 Task1金样全部通过后独立提交 `feat: register unified node execution strategies`，未迁移 Kind 不提前声明可执行。

### Task 3：查询/伤害/持续费用公共服务

**Files:** 新建 `AbilityTargetQueryService.h/.cpp`、`AbilityDamageService.h/.cpp`、`AbilityUpkeepService.h/.cpp`、`AbilityKindCompatibility.h/.cpp`；修改 A/M Runtime 的局部调用点（只在服务逐项对照通过后）；新建 `Private/Tests/AbilityKindServiceTests.cpp`。

**Interfaces:** 采用上文 Gather/Sweep/Apply/Advance；`AbilityKindCompatibility::Find(FName Profile, FName KindName)` 返回不可修改的策略记录，未知组合拒绝。策略记录明确 TargetPolicy、VisibilityPolicy、UpkeepMode、PeriodMode、EndMapping、LifetimeBoundary 和 SpawnAdmission。

- [ ] 先写失败测试：Actor 与 Mass 实际伤害/免疫/致死、敌友/房间/跨世界、客户端拒绝、同对象下一 Lease、墙平局/Trigger、确认命中后死亡只允许位置型子行为、整秒与 Delta 费用轨迹。
- [ ] 最小提取既有服务调用；Actor 保留 GE/EffectContext，Mass 保留 ApplyDamage/房间检查。无实际伤害量时不伪造 DamageResult；记录 LegacyOnHit 的采样前后差异。
- [ ] 使旧 A/M Runtime 也调用服务，先证明旧后端金样不变，再供新节点使用；Kind分派、计时和结束流程此步保持原状，便于独立回滚。
- [ ] 运行新服务测试 + 原 Prototypes/Compositions/PureMass/Batch6 回归。分别提交查询、伤害、费用三个可审查小改动，失败时回退调用点而非改金样迎合新输出。

### Task 4：独立效果持有者、资源清理与复制表现

**Files:** 新建 `AbilityGraphWorldSubsystem.h/.cpp`、`AbilityGraphPresentationActor.h/.cpp`；修改 `AbilityTask_RunGraph.h/.cpp`、GraphInstance；新建 `Private/Tests/AbilityGraphLifetimeDomainTests.cpp`。

**Interfaces:** 采用 Adopt/CancelExecution；`FAbilityPresentationSnapshot` 只含可重建表现数据，无 SourceASC/权威目标令牌；策略 Stop 必须撤销每个注册的 Delegate/Timer/Actor/GE handle 的本次所有权。

- [ ] 先写失败测试 `NormalGaEndRetainsProjectile`、`BoundCancelClosesChildren`、`SourceLeaseClosesIndependent`、`WorldTeardownReentrantCancel`、`NoResidualWorldContext`、`StatusOutlivesGraphWithoutClearingOtherSources`。
- [ ] 让任务显式移交有限独立执行；Beam/Orbit 继续绑定任务，共享施法取消组不因单个节点正常结束关闭。保留目标持久状态原持有者。
- [ ] 复制表现用状态而非只发一次 Multicast；覆盖初帧命中、复制留存、晚加入、世界退出；不把业务图复制到客户端。
- [ ] 跑自动化并记录双端 PIE 的 Actor/Mass/Surface/弹体视觉待验项。无残余句柄/Actor/Subsystem 持有实例后提交。

### Task 5：瞬发形状与固定原点 Delay

**Files:** 新建 `Nodes/AbilityShapeNodes.h/.cpp`、`Nodes/AbilityLegacyDelayNode.h/.cpp`；在 Registry 注册 Burst/Cone/Ring/Line/LegacyDelay；新建 `Private/Tests/AbilityKindShapeMigrationTests.cpp`。

**Interfaces:** 每个节点有专用 UAbilityNodeConfig 派生类，构造时固定 profile 后复制数值；Shape 策略内部用 Query/Damage 服务，发布确认/结果/完成端口；LegacyDelay 使用 Origin 输入，P1 Control.Delay 保持原捕获目标契约。

- [ ] 写 `KindMigration.Actor/Mixed.<Kind>` 五组失败对照；场景包含无目标、空放费用、边界高度、阻挡、方向、固定原点、致死子 Burst、无地面子 Surface 与取消。
- [ ] 抽出执行规则后替换对应策略，保留 Burst 等“初始脉冲后仍等 Duration 才到期”的寿命，不把即时伤害误当即时完成。
- [ ] Schema2 转换仅连接声明的端口；旧 OnHit 对应服务完成后的兼容事件，不能直接连到命中前阶段。位置快照不得流入要求活体的状态节点。
- [ ] 跑金样/旧测试/新策略互相组合测试（例如 Delay→Ring、Cone命中→位置Burst），每类都能不依赖旧 Runtime 完成。分族提交。

### Task 6：弹道族及普通 Chain

**Files:** 新建 `Nodes/AbilityProjectileNodes.h/.cpp`、`Nodes/AbilityChainNode.h/.cpp`；Registry 增加 Projectile/Multishot/Homing/Piercing/Returning/Chain；新建 `Private/Tests/AbilityKindDeliveryMigrationTests.cpp`。

**Interfaces:** 每次调用独占 HitTargets、Travel、Direction、ReturningPhase、LastImpact；多弹丸有独立 InvocationId 和共享取消组，父扇形不是递归 GA；Chain 使用事件端口串联的单链策略，不调用 Batch6 BFS。

- [ ] 先写六类 Actor/Mixed 失败对照，断言命中顺序、每程计数/衰减、长帧转弯余时、末帧寿命、末跳载荷、起点偏移、墙同时间优先和友军穿过。
- [ ] 提取并接上 Query/Damage/Presentation/Lifetime 服务；限制单帧碰撞重查并保留 M 的 64 上限，预算超限与旧子拒绝按审核结果执行。
- [ ] 加入投射物延迟后 Actor池复用/Mass句柄替换、世界旅行、观察者取消；重解令牌，不将“相同裸Actor/EntityId”当相同生命。
- [ ] 跑对应原 `Prototypes2/3`、`PureMass.Mixed`、`PureMass.LegacyFireBolt` 回归，后者仅证明没有破坏传统火球通路，不能算本次 Kind 节点迁移证据。分弹道与 Chain 两次提交。

### Task 7：有限时间与地表族

**Files:** 新建 `Nodes/AbilityTimedNodes.h/.cpp`、`Nodes/AbilitySurfaceNode.h/.cpp`；Registry 增加 Field/Trap/Wave/Emitter/Surface；新建 `Private/Tests/AbilityKindTimedMigrationTests.cpp`。

**Interfaces:** `FAbilityPeriodState` 保留 Age/Clock/SuccessfulEmissions/PreviousRadius；`FAbilityPeriodBatch {DueCount, ActiveDelta, bExpired}`；`FAbilityPeriodBatch AdvancePeriods(FName Profile, FName KindName, float Delta, FAbilityPeriodState&)` 返回本帧实际可处理批次并更新保留时钟，不能自改调用者终止原因。Surface 继续持有已有 ATerrainSurface 的本次实例。

- [ ] 先写五类失败对照：第8/9/16/17个周期、整 Duration 长帧、尾帧、波前晚进、Trap遮挡、Emitter无目标与子拒绝、地表根/子投影和房间。
- [ ] 测试必须证明 A Field 无人为8/16截断，M8保留积压，以及 P3 Extension16规则未被本次公共 helper 误覆盖。大量同步伤害如超出新预算包络，不延后一帧冒充等价；转换阶段明确 NeedsBudgetReview。
- [ ] 保留 Emitter 成功次数与事件发布/子接收结果的因果：不能仅“事件入队成功”就当旧 Spawn 成功。用已验配置、准入预留或回执处理，回执前不多加成功Count；取消/预算拒绝不能补射。
- [ ] 跑原 Prototypes3/Compositions/Mixed.Surface/Batch6 回归；Batch6子类保持旧入口，不因继承 UAbilityPrototype 自动使用普通 Kind策略。逐族提交。

### Task 8：Beam、Orbit 及持续输入

**Files:** 新建 `Nodes/AbilityChannelNodes.h/.cpp`；修改 GA-A/GA-M 输入与 EndAbility 的可选路由；仅在已审查接口处复用 `AMyOrbitingFireBolt`；新建 `Private/Tests/AbilityKindChannelMigrationTests.cpp`。

**Interfaces:** Channel策略接收服务端目标请求与 Released/ToggleOff，事件来自已提交攻击通知；`FAbilityUpkeepState` 只属于本次调用；BoundToAbility 引用由原 GA/Task 持有。

- [ ] 先写 Beam 双后端金样：0.25+0.75+2.4秒、余额不足、释放、超距、墙阻挡、死亡、Run变更；精确比较扣费发生顺序与子状态存亡。
- [ ] 提取 Beam，把 M旧正常到期与 A整组取消显式映射；NoTarget位置分支可延续，失效LiveTarget不得施加效果。未审核行为不得以统一 Cancelled 实现掩盖差异。
- [ ] 先写 Orbit根/子、有/无目标、最大弹数、同帧发射、返轨、AttackDelegate解绑、ASC换Avatar和反复开关测试；**Task1预算冲突未关闭时只完成测试/策略候选，不启用迁移，也不标 Verified。**
- [ ] 预算协议获审核且测试覆盖后，保留 A真弹体和 M轻量表示的外观/目标选择差异，复用已抽伤害服务，不激活额外 GA。双端持续输入和取消效果由用户 PIE确认后提交/登记。

### Task 9：转换器、逐资产路由、回滚与 P2 退出审核

**Files:** 新建 `AbilityKindGraphCompiler.h/.cpp`、`AbilityKindMigrationSettings.h/.cpp`、`Scripts/GA/Unified/plan_kind_migration.py`、`apply_kind_migration.py`、`verify_kind_migration.py`、`verify_kind_migration_pie.py`；修改 GA-A/GA-M 可选执行路由、`Scripts/GA/Unified/audit_catalog.py`、`docs/ga-unified-migration-matrix.json`、`docs/ga-unified-authoring-guide.md`；新增 `Private/Tests/AbilityKindAssetMigrationTests.cpp`。

**Interfaces:** `bool AbilityKindGraphCompiler::Compile(const UAbilityPrototype&, FName Backend, UObject* Outer, FAbilityKindCompileResult&)`，仅Status=Ready返回true；Result含 Status(Ready/NeedsBudgetReview/Unsupported)、Graph、Issues、SourcePaths、Fingerprint。`UAbilityKindMigrationSettings` 使用按原GA软路径的映射 `Graph/CompatibilityProfile/bEnabled/SourceFingerprint`；禁止按整个父类一次性切换。

- [ ] 先写失败测试：旧OnHit/OnPeriod/OnExpire自引用有限展开、共享DA多父引用、未知子类拒绝、源调参后Fingerprint失配、图配置复制、不改变原输入/技能Tag/目录/存档引用、单次费用、空放、投影失败不扣费、禁止运行失败后回退重复Commit。
- [ ] 制作脚本默认 dry-run，列出全部包和新资产候选到 Saved；只处理明确白名单且已通过本族金样的资产，输出到 `/Game/UnifiedAbilities/Migrated/`，保持原GA和DA路径。GA只在路由明确Enabled且编译/验证成功时选新执行；无映射仍旧入口。已选新图失败直接结束，不同一次施法里再走旧路。
- [ ] 旧循环按根深度0到3展开为有限无环图；不能拿Schema2拒环去静默丢掉旧子链。超资源包络返回明确问题，不改预算后强行导入。
- [ ] 保存后退出UE，以第二个进程重载资产、读取真实CDO/引用并跑 `test_25d.UnifiedGA.KindMigration`、所有P1、所有旧Prototypes/Compositions/PureMass及受影响Batch6回归。新旧输出分别与冻结金样比较，不用“新旧当前相同”替代原基线。
- [ ] 每行保存 Backend、Kind、原GA/DA、目标Graph、Profile、源/目标Fingerprint、源码SHA、实际执行测试名/报告、PIE状态、回滚设置、遗留问题；Inventory未知类不可漏行。仅登记或脚本能加载不能改成Verified。
- [ ] **回滚单位为GA映射行：** 新施法前将该行bEnabled置false；活动新图按原快照完成或显式取消并记原因，不在运行中改执行器。保留新资产用于问题复现，恢复路由不会改Tag/存档类身份；旧Runtime仍可供未迁移/P3依赖使用。
- [ ] P2退出审核要求18个Kind的Actor/Mixed行为证据以及全部发现的前四批资产行清零；任何Orbit预算/特有子类/视觉未验项仍存在时，报告“部分迁移”，不得写“P2完成”或“全部GA统一”。

## 统一验证命令与交付约定

常规每个实现任务：先写真实失败用例并保存失败报告，再最小实现、clang-format、完整 Editor 构建，最后运行精确测试集。不得以测试过滤器空选、旧DLL、旧JSON或仅检查源码字符串代替执行。

```powershell
& ./Scripts/GA/Unified/test.ps1 -Filter 'test_25d.UnifiedGA.KindGolden+test_25d.UnifiedGA.KindMigration+test_25d.UnifiedGA+test_25d.Prototypes+test_25d.Compositions+test_25d.PureMass+test_25d.Batch6'
```

实施时按真实新增声明生成 ExpectedTests；动态测试需要显式清单，不从成功报告反推预期。脚本保留现有新鲜时间戳、零测试/缺项/失败状态/重复结果拒绝规则。回归发现旧失败必须逐条归因，不能删断言或跳过难例来获得绿色。

既有 PIE脚本为场景参考：`Scripts/GA/Prototypes/verify_network_pie.py`、`verify_batch2_pie.py`、`verify_batch3_pie.py`、`verify_batch4_pie.py`、`verify_compositions_pie.py` 及对应 travel脚本；新脚本复用真实输入和目标，不直接调用私有命中函数替代联机路径。自动化不替代用户的预警/动画/VFX/晚加入验收，涉及敌人攻击的既有预警义务保持。

本计划自检：18个枚举名称全部列出；前四批A/M时钟、子事件、目标形态、旧预算与寿命均有任务和断言；16次周期准确归入P3；P1API现状与P2拟扩展区分；预算语义冲突明确阻止错误迁移；未承诺P2已经实现或全部18行必可直接放行。
