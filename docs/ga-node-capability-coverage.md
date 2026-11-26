# GA 节点能力覆盖表

本表由真实注册表和实际法师目录只读审计生成。完整节点、具体配置、逐节点参数及端口见[机器表](ga-node-capability-coverage.json)。

审计时间（UTC）：`2026-11-21T19:26:46.895Z`。共 175 个注册类型、86 个目录条目，其中 82 个可选；实际默认图观察到 61 种节点。

登记了逻辑不等于允许任意连接。运行时仍需配置校验、真实端口载荷、目标原生命、有限/持续合同及共享预算。表中0表示本次目录未观察到，不能据此断言策略不可用。

“实际可改参数”是实际默认配置经 GraphView/模板校验得到的并集；同类型的其他配置可能不同。新加入的传播、查询和条件节点通常不在原默认图，因此其0/空值需结合下方工作台合同阅读。

## 当前工作台合同

- `Propagation.Sequential`：原位设置总投递数、逐跳等待、搜索半径、衰减和选敌策略。
- `Propagation.Forked`：上述参数加层数、每次命中分支数和并发开关。旧配置默认串行，工作台新建默认并发；并发位不足排队，累计额度耗尽终结。
- `Query.SelectTargets`：Origin → TargetFound/NoTarget/Completed；半径(0,3000]、数量1..16，最近/最远/最低生命比例。
- `Condition.Killed`：真实DamageResult → Passed或Rejected；0表示未击杀，1表示击杀。输出仅位置，不恢复目标权限。
- `Condition.Chance`：Origin → Passed或Rejected；概率[0,1]由服务器每次调用决定。
- 有限时序、目标筛选、元素状态条件、命中计数、限频与资源反馈见[元素组装指南](ga-elemental-composition.md)。
- 新规则以 Append.Registered 追加后可以继续选节点调参、选输出口追加。新查询/条件不提供 Insert.Registered，防止分支拒绝时仍无条件续接原线。

可信追加片段：`Target.Petrify`、`Area.Petrify`、`Target.Slow`、`Query.SelectTargets`、`Condition.Killed`、`Condition.Chance`、`Control.DelayOrigin`、`Control.Repeat`、`Query.WaitForTarget`、`Spatial.Wavefront`、`Condition.HealthBelow`、`Condition.ElementalStatus`、`Condition.DamageApplied`、`Control.EveryNthDamage`、`Control.DamageTriggerLimit`、`Effect.RestoreMana`、`Effect.RestoreHealth`。

实际操作、生命周期和本轮验证见[扩展指南](ga-graph-gameplay-expansion.md)。全表的“有限/纯”是注册声明，实际配置及入边仍需二次检查；Actor/Mass列也不代表逐类型联机视觉验收。

## 全部注册类型

| 类型 | 目录实例 | 输入 → 声明输出 | Actor/Mass | 有限/纯 | 实际默认图可改参数 |
|---|---:|---|---|---|---|
| `Channel.Electrocute` | 0 | Origin → DamageResolved:DamageResult, PeriodElapsed:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Channel.ElectrocuteHost` | 1 | Origin → Period:TargetSet, Final:TargetSet, DamageResolved:DamageResult, PeriodElapsed:Origin, Completed:Origin* | 1/0 | 0/0 | Append.Status.Petrify |
| `Channel.OrbitingFireBolts` | 0 | Origin → Replenished:Origin, Launched:LiveTarget, OnHit:ConfirmedImpact, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Combat.CharacterStrike` | 0 | Origin → DamageResolved:DamageResult, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Combat.TelegraphedContact` | 0 | Origin → DamageResolved:DamageResult, Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Condition.Chance` | 0 | Origin → Passed:Origin*, Rejected:Origin* | 1/1 | 1/1 | 未观察到 |
| `Condition.DamageApplied` | 0 | DamageResult → Passed:DamageResult*, Rejected:DamageResult* | 1/1 | 1/1 | 未观察到 |
| `Condition.ElementalStatus` | 0 | LiveTarget → Passed:LiveTarget*, Rejected:LiveTarget* | 1/1 | 1/1 | 未观察到 |
| `Condition.HealthBelow` | 0 | LiveTarget → Passed:LiveTarget*, Rejected:LiveTarget* | 1/1 | 1/1 | 未观察到 |
| `Condition.Killed` | 0 | DamageResult → Passed:Origin*, Rejected:Origin* | 1/1 | 1/1 | 未观察到 |
| `Control.DamageTriggerLimit` | 0 | DamageResult → Passed:DamageResult*, Rejected:DamageResult* | 1/1 | 1/0 | 未观察到 |
| `Control.Delay` | 0 | LiveTarget → Completed:LiveTarget*, Cancelled:LiveTarget*, Failed:LiveTarget* | 1/1 | 1/0 | 未观察到 |
| `Control.DelayOrigin` | 0 | Origin → Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Control.Emitter` | 0 | Origin → Launch:LiveTarget, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Control.EveryNthDamage` | 0 | DamageResult → Passed:DamageResult*, Rejected:DamageResult* | 1/1 | 1/0 | 未观察到 |
| `Control.ExtensionBurst` | 7 | Origin → Action:Origin, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.ExtensionContactEnd` | 4 | Origin → Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.ExtensionDelay` | 1 | Origin → Action:Origin, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.ExtensionSequence` | 2 | Origin → Contact:TargetSet, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.FieldCadence` | 11 | Origin → Action:Origin, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.LightningSteps` | 2 | LiveTarget → Step:Origin, DamageResolved:DamageResult, Completed:Origin* | 1/1 | 0/0 | Append.Status.Petrify, Replace.Status.Petrify |
| `Control.PrototypeDelay` | 8 | Origin → Action:Origin, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.PrototypeEmitter` | 5 | Origin → Launch:LiveTarget, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.PrototypeEnd` | 7 | Origin → Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.PrototypeFollowField` | 1 | Origin → Action:Origin, Period:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.PrototypeLifetime` | 30 | Origin → Action:Origin, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.PrototypeOrbitInventory` | 2 | Origin → Launch:PreparedInventory, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.PrototypeSurfaceClock` | 8 | Origin → Action:Origin, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.PrototypeTrap` | 6 | Origin → Action:Origin, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.Repeat` | 0 | Origin → Pulse:Origin, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Control.ShardPoints` | 1 | Origin → Point:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Control.TargetChannel` | 2 | Origin → Action:TargetSet, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Control.TargetSequence` | 2 | Origin → Action:TargetSet, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Defense.PerfectDodge` | 0 | Origin → PerfectDodged:Origin, Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Delivery.ActorProjectiles` | 0 | Origin → Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Delivery.EnemySling` | 0 | Origin → Spawned:Origin, Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Delivery.ExtensionProjectile` | 2 | Origin → Contact:TargetSet, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Delivery.ForkedChain` | 0 | LiveTarget → DamageResolved:DamageResult, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Delivery.ForkedVolley` | 0 | LiveTarget → DamageResolved:DamageResult, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Delivery.NativeOrbitBolt` | 1 | PreparedInventory → Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Delivery.ProjectileAwait` | 1 | Origin → Hit:ConfirmedImpact, DamageResolved:DamageResult, Blocked:Origin, TargetLost:Origin, Expired:Origin, Returned:Origin, Cancelled:Origin*, Completed:Origin* | 1/1 | 1/0 | Append.Status.Petrify, Propagation.Forked, Propagation.Sequential, Set.CollisionRadius |
| `Delivery.PrototypeFlight` | 21 | Origin → Contact:TargetSet, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 1/0 | Set.CollisionRadius, Set.DamageFalloff, Set.HomingTurnRate, Set.ImpactLimit, Set.ImpactPolicy, Set.Motion, Set.Range, Set.Speed |
| `Delivery.PrototypeOrbit` | 2 | PreparedInventory → Contact:LiveTarget, OnHit:ConfirmedImpact, Completed:Origin* | 1/1 | 0/0 | Append.Status.Petrify |
| `Delivery.SequentialChain` | 0 | LiveTarget → DamageResolved:DamageResult, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Delivery.WeaponProjectile` | 0 | Origin → Spawned:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Distribution.PrototypeMultishot` | 4 | Origin → Pellet:Origin, Completed:Origin* | 1/1 | 0/0 | Set.PelletTargetPolicy |
| `Distribution.Scatter` | 1 | Origin → Launch:Origin, Completed:Origin* | 1/1 | 0/0 | Set.Count, Set.Spread |
| `Effect.Damage` | 0 | LiveTarget → DamageResolved:DamageResult, Completed:DamageResult* | 1/1 | 1/0 | 未观察到 |
| `Effect.DamageBatch` | 0 | TargetSet → DamageResolved:DamageResult, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Effect.ElectrocuteFinal` | 1 | TargetSet → Completed:TargetSet* | 1/0 | 0/0 | Replace.Status.Petrify, Scale.Damage |
| `Effect.ElectrocutePeriod` | 1 | TargetSet → Completed:TargetSet* | 1/0 | 0/0 | Replace.Status.Petrify, Scale.Damage |
| `Effect.GameplayEffect` | 0 | Origin → Applied:Origin* | 1/0 | 0/0 | 未观察到 |
| `Effect.GameplayEffectTarget` | 0 | LiveTarget → Applied:Origin* | 1/0 | 0/0 | 未观察到 |
| `Effect.InputDamage` | 4 | LiveTarget → DamageResolved:DamageResult, Completed:DamageResult* | 1/1 | 1/0 | Append.Status.Petrify, Replace.Status.Petrify, Scale.Damage, Set.ElementConversion |
| `Effect.LightningDamageBatch` | 2 | TargetSet → Completed:Origin* | 1/1 | 0/0 | Replace.Status.Petrify, Scale.Damage |
| `Effect.NovaDamage` | 1 | TargetSet → DamageResolved:DamageResult, Completed:Origin* | 1/1 | 0/0 | Append.Status.Petrify, Replace.Status.Petrify, Scale.Damage |
| `Effect.PrototypeDamageBatch` | 89 | TargetSet → Contact:DamageResult, DamageResolved:DamageResult, Completed:Origin* | 1/1 | 1/0 | Append.Status.Petrify, Replace.Status.Petrify |
| `Effect.RestoreHealth` | 0 | DamageResult → Passed:DamageResult*, Rejected:DamageResult* | 1/1 | 1/0 | 未观察到 |
| `Effect.RestoreMana` | 0 | DamageResult → Passed:DamageResult*, Rejected:DamageResult* | 1/1 | 1/0 | 未观察到 |
| `Effect.ShardDamageBatch` | 1 | TargetSet → DamageResolved:DamageResult, Completed:TargetSet* | 1/1 | 0/0 | Append.Status.Petrify, Replace.Status.Petrify, Scale.Damage |
| `Event.ImpactFact` | 0 | DamageResult → Completed:ConfirmedImpact* | 1/1 | 1/0 | 未观察到 |
| `Event.OrderedCompletion` | 0 | Origin → Expired:Origin, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Event.OriginalDamage` | 0 | DamageResult → Forward:DamageResult* | 1/1 | 1/0 | 未观察到 |
| `Event.OriginalImpact` | 0 | ConfirmedImpact → Forward:ConfirmedImpact* | 1/1 | 1/0 | 未观察到 |
| `Event.OriginalLive` | 0 | LiveTarget → Forward:LiveTarget* | 1/1 | 1/0 | 未观察到 |
| `Event.OriginalOrigin` | 8 | Origin → Forward:Origin* | 1/1 | 1/0 | 未观察到 |
| `Event.OriginalTargets` | 0 | TargetSet → Forward:TargetSet* | 1/1 | 1/0 | 未观察到 |
| `Event.PrototypeCompletedInput` | 15 | Origin → Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Field.ShroomSpore` | 0 | Origin → DamageResolved:DamageResult, Applied:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Inventory.NativeOrbit` | 1 | Origin → Launch:PreparedInventory, Contact:ConfirmedImpact, OnHit:ConfirmedImpact, Replenished:Origin, Launched:LiveTarget, Completed:Origin* | 1/0 | 0/0 | Append.Status.Petrify |
| `Legacy.Actor.Beam` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Burst` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Chain` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Cone` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Delay` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Emitter` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Field` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Homing` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Line` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Multishot` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Orbit` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Piercing` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Projectile` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Returning` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Ring` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Surface` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Trap` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Actor.Wave` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Legacy.Extension.Attach` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Extension.Burst` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Extension.Chain` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Extension.Delay` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Extension.Displace` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Extension.Projectile` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Extension.Stack` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Extension.Surface` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Beam` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Burst` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Chain` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Cone` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Delay` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Emitter` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Field` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Homing` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Line` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Multishot` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Orbit` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Piercing` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Projectile` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Returning` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Ring` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Surface` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Trap` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Legacy.Mixed.Wave` | 0 | Origin → OnHit:ConfirmedImpact, OnPeriod:Origin, OnExpire:Origin, Multishot:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Melee.Contact` | 0 | Origin → DamageResolved:DamageResult, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Move.GruxCharge` | 0 | Origin → DamageResolved:DamageResult, Applied:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Movement.Character` | 0 | Origin → Blocked:Origin, Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Movement.PrototypeAreaDisplace` | 2 | Origin → Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Movement.PrototypeTargetDisplace` | 1 | LiveTarget → Completed:Origin*, Replaced:Origin*, TargetLost:Origin*, Rejected:Origin* | 1/1 | 0/0 | Replace.Status.Petrify |
| `Movement.TargetDisplace` | 0 | LiveTarget → Completed:Origin*, Replaced:Origin*, TargetLost:Origin* | 1/1 | 1/0 | 未观察到 |
| `PassThrough.ConfirmedImpact` | 0 | ConfirmedImpact → Apply:ConfirmedImpact, Continue:ConfirmedImpact* | 1/1 | 1/0 | 未观察到 |
| `PassThrough.DamageResult` | 0 | DamageResult → Apply:DamageResult, Continue:DamageResult* | 1/1 | 1/0 | 未观察到 |
| `PassThrough.LiveTarget` | 0 | LiveTarget → Apply:LiveTarget, Continue:LiveTarget* | 1/1 | 1/0 | 未观察到 |
| `PassThrough.Origin` | 0 | Origin → Apply:Origin, Continue:Origin* | 1/1 | 1/0 | 未观察到 |
| `PassThrough.TargetSet` | 0 | TargetSet → Apply:TargetSet, Continue:TargetSet* | 1/1 | 1/0 | 未观察到 |
| `Prepare.NativeFireBolt` | 1 | Origin → Completed:Origin* | 1/1 | 0/0 | Scale.Damage |
| `Presentation.LightningBeams` | 2 | Origin → Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Presentation.NovaCue` | 1 | Origin → Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Presentation.PrototypeImpact` | 42 | DamageResult → Completed:DamageResult* | 1/1 | 1/0 | Append.Status.Petrify |
| `Presentation.PrototypeOrbitImpact` | 0 | DamageResult → Completed:DamageResult* | 1/1 | 0/0 | 未观察到 |
| `Presentation.PrototypePulse` | 3 | Origin → Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Presentation.ShardCue` | 1 | Origin → Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Propagation.Forked` | 0 | Origin → Hit:ConfirmedImpact, DamageResolved:DamageResult, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Propagation.Sequential` | 0 | Origin → Hit:ConfirmedImpact, DamageResolved:DamageResult, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Query.LightningTargets` | 2 | Origin → Completed:TargetSet* | 1/1 | 0/0 | Scale.Radius |
| `Query.Nearest` | 0 | Origin → TargetFound:LiveTarget, NoTarget:Origin, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Query.NovaTargets` | 1 | Origin → Completed:TargetSet* | 1/1 | 0/0 | 未观察到 |
| `Query.PetrifyArea` | 1 | Origin → Completed:TargetSet* | 1/1 | 1/1 | Scale.Radius |
| `Query.PrototypeShape` | 55 | Origin → Completed:TargetSet* | 1/1 | 1/0 | 未观察到 |
| `Query.SelectTargets` | 0 | Origin → TargetFound:LiveTarget, NoTarget:Origin, Completed:Origin* | 1/1 | 1/1 | 未观察到 |
| `Query.Shape` | 0 | Origin → TargetFound:LiveTarget, NoTarget:Origin, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Query.ShapeBatch` | 0 | Origin → Completed:TargetSet* | 1/1 | 1/0 | 未观察到 |
| `Query.ShardTargets` | 1 | Origin → Targets:TargetSet* | 1/1 | 0/0 | 未观察到 |
| `Query.WaitForTarget` | 0 | Origin → Triggered:Origin, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Scope.Prototype` | 128 | Origin → Ready:Origin* | 1/1 | 1/0 | Scale.Damage, Scale.Duration, Scale.Radius, Set.ArmDelay, Set.CollisionRadius, Set.Count, Set.DamageFalloff, Set.DetonationDelay, Set.HalfHeight, Set.HomingTurnRate, Set.InnerRadius, Set.MaxTransfers, Set.MoveDistance, Set.Period, Set.Radius, Set.Range, Set.SafeDistance, Set.Speed, Set.Spread, Set.StackThreshold, Set.TransferRadius, Set.Width |
| `Shape.ArcaneNova` | 0 | Origin → DamageResolved:DamageResult, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Shape.ArcaneShards` | 0 | Origin → DamageResolved:DamageResult, PeriodElapsed:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Spatial.Anchor` | 0 | Origin → Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Spatial.PrototypeWave` | 5 | Origin → Contact:TargetSet, TargetFound:LiveTarget, Period:Origin, Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Spatial.Wavefront` | 0 | Origin → TargetFound:LiveTarget, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `Spawn.ShamanMinions` | 0 | Origin → Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Spawn.Summon` | 0 | Origin → Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `State.Attach` | 0 | LiveTarget → Period:LiveTarget, Transferred:LiveTarget, TargetLost:Origin*, Completed:Origin* | 1/1 | 1/0 | 未观察到 |
| `State.ExtensionSurface` | 2 | Origin → Expired:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `State.PrototypeAttach` | 3 | LiveTarget → Period:LiveTarget, Transferred:LiveTarget, TargetLost:Origin*, Completed:Origin* | 1/1 | 0/0 | Replace.Status.Petrify |
| `State.PrototypeStack` | 3 | LiveTarget → Threshold:Origin, Released:Origin* | 1/1 | 0/0 | Replace.Status.Petrify |
| `State.PrototypeStackImpact` | 3 | ConfirmedImpact → Threshold:Origin, Released:Origin* | 1/1 | 0/0 | 未观察到 |
| `State.Stack` | 0 | LiveTarget → Threshold:Origin, Released:Origin* | 1/1 | 1/0 | 未观察到 |
| `State.StackDamage` | 0 | DamageResult → Threshold:Origin, Released:Origin* | 1/1 | 1/0 | 未观察到 |
| `State.StackImpact` | 0 | ConfirmedImpact → Threshold:Origin, Released:Origin* | 1/1 | 1/0 | 未观察到 |
| `Status.GruxRoarArmor` | 0 | Origin → DamageResolved:DamageResult, Applied:Origin, Completed:Origin*, Cancelled:Origin*, Failed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Status.GuardWindow` | 0 | Origin → Opened:Origin, Blocked:Origin, Parried:Origin, Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Status.OwnedEffect` | 2 | Origin → Applied:Origin, Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Status.OwnedEffectTarget` | 0 | LiveTarget → Applied:Origin, Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `Status.Petrify` | 0 | LiveTarget → Completed:LiveTarget*, Failed:LiveTarget* | 1/1 | 1/1 | 未观察到 |
| `Status.PetrifyArea` | 0 | Origin → Completed:Origin* | 1/1 | 0/0 | 未观察到 |
| `Status.PetrifyBatch` | 1 | TargetSet → Completed:Origin* | 1/1 | 1/1 | Replace.Status.Petrify, Scale.Duration |
| `Status.PrototypeSlow` | 4 | DamageResult → Completed:DamageResult* | 1/1 | 0/0 | Append.Status.Petrify |
| `Status.Slow` | 0 | LiveTarget → Completed:LiveTarget* | 1/1 | 1/0 | 未观察到 |
| `Target.PrototypeLive` | 7 | Origin → TargetFound:LiveTarget*, Confirmed:ConfirmedImpact* | 1/1 | 0/0 | Append.Status.Petrify |
| `Target.ResolveDamageTarget` | 0 | DamageResult → TargetFound:LiveTarget, NoTarget:Origin, Completed:Origin* | 1/1 | 1/1 | 未观察到 |
| `Target.ResolveImpact` | 2 | ConfirmedImpact → TargetFound:LiveTarget, NoTarget:Origin, Completed:Origin* | 1/1 | 1/1 | 未观察到 |
| `Time.WorldSlow` | 0 | Origin → Completed:Origin* | 1/0 | 0/0 | 未观察到 |
| `World.PrototypeSurface` | 8 | Origin → Acquired:Origin | 1/1 | 0/0 | 未观察到 |
| `World.ReleasePrototypeSurface` | 8 | Origin → Released:Origin* | 1/1 | 0/0 | 未观察到 |

输出端口后的 `*` 表示该策略声明的终态口，不代表所有调用都会发出它。

复现：完整构建后运行 `test_25d.GraphExpansion.Capabilities.ActualRegistryAndCatalogAudit`，再执行本脚本；对应验证报告和DLL指纹保存在本轮验证记录。
