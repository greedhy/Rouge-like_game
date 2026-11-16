# 完美闪避与世界时间服务使用指南

日期：2026-11-15。Editor 构建及资产独立读回通过，新增25项玩法测试全部通过。完整项目424项中423项通过；唯一失败的配置检查发现9个既有Stander时间轴缺少制作蒙太奇引用，单独运行也复现。用户PIE视觉验收尚未进行。具体证据见[本轮实施记录](../Saved/GAAuthoring/PerfectDodge/20261115-implementation/progress.md)，设计目标见[已批准设计](superpowers/specs/2026-11-15-perfect-dodge-design.md)。

## 玩家入口与配置

Mage 与 Stander 共用 `/Game/UnifiedAbilities/Defense/PerfectDodge/GA_PerfectDodge`，固定 **Space**，通过 `IA_Dodge → InputTag.Dodge → ASC` 路由。资产脚本将其加入 `BP_Player`、`BP_DungeonPlayer`、`BP_StanderPlayer` 各自的有效 `startup_ability`，保留原技能列表。无需学习、装备或选择槽位；按下触发，长按不连续翻滚，松开不取消动作。方向依次取最后移动输入、当前速度、角色朝向的水平分量。

`UPerfectDodgeAbility` 是原生固有能力；服务器启动校验在新档、旧档恢复和 Avatar 初始化后补齐固定 Spec，不重置已有冷却。普通技能存档跳过它，换槽入口拒绝移动它，也拒绝其他技能占用 `InputTag.Dodge`。武器技能仍沿用原有槽位。

| 参数 | 当前配置 | 时钟 |
|---|---:|---|
| 地面位移 | 420 cm / 0.30 秒 | 动作秒 |
| 完美判定窗口 | `[0, 0.12)` 秒 | 动作秒 |
| 合格直接攻击的无敌窗口 | `[0, 0.24)` 秒 | 动作秒 |
| 动作结束 | 0.42 秒；位移结束后恢复移动 | 动作秒 |
| 翻滚冷却 | 0.80 秒，`Cooldown.Common.PerfectDodge` | GAS 世界秒 |
| 完美奖励间隔锁 | 3.00 秒 | 动作秒 |
| 世界倍率 | 相对请求开始前基准的 0.20 倍 | — |
| 减速进入 / 保持 / 退出 | 0.05 / 1.20 / 0.15 秒 | 动作秒 |
| Mana 消耗 | 0 | — |

完美闪避要求：早期窗口内，**已接入的敌方攻击实际生效**，命中当前身体，或其真实几何覆盖翻滚起点的短时见证胶囊。预警、距离和空翻滚本身不授予奖励。见证体只作查询，不受伤、不消费弹体；当前身体被弹体接触后仍按原规则结束弹体。墙体、房间、来源生命及攻击几何继续检查；迟到的弹道采样不能追认到翻滚开始之前。

同一次翻滚至多奖励一次；组件在请求时间服务前记录攻击 ID 和本次奖励状态。世界已有慢速请求或奖励间隔未到时，普通闪避仍可执行，但不再叠加完美奖励。翻滚要求角色存活、处于地面、没有位移占用及眩晕/受击阻挡；不授予主动中断当前攻击的特权。移动阻挡可提前缩短位移并关闭判定窗口。

## 独立复用世界减速

**GAS 入口：** `/Game/UnifiedAbilities/Time/WorldSlow/GA_WorldSlow`，继承 `UWorldSlowAbility`。它没有 Startup 授予、默认按键或技能目录登记；调用方显式授予后通过现有 ASC 激活。默认零 Mana，独立原生冷却 **3 个世界秒**，标签 `Cooldown.Time.WorldSlow`。`Settings` 使用上表的减速参数。该 GA 一次 Commit 后运行 `Time.WorldSlow`，结束或取消时释放自身请求。服务直接调用不会自动附带这个 GA 冷却。

**蓝图入口：** 在来源所在 World 通过 **Get World Subsystem → AbilityWorldTimeSubsystem** 取得服务，然后调用：

1. `RequestWorldSlow(Source, LifetimeOwner, Settings)` 返回 `Guid` 句柄；无效句柄表示失败。
2. 保存句柄，必要时用 `IsActive(Handle)` 查询；取消、离场等业务清理调用 `Release(Handle)`。重复释放安全。
3. `LifetimeOwner` 应是覆盖整个效果期间的有效 UObject。服务只弱引用它；对象失效、来源生命失效、自然到期或 World 退出都会撤销对应请求。不要用马上结束的临时对象持有需要延续的奖励。

`Source` 必须是存活、权威且与当前 ASC/Avatar/World 绑定一致的 Actor。`CancelSource(Source)` 用于同 Pawn 倒地、复活等需要撤销该来源全部旧请求的生命边界；普通调用方优先释放自己的句柄。

**C++ 入口：** 包含 `AbilitySystem/Time/AbilityWorldTimeSubsystem.h`；先用 `AbilityExecutionIdentity::CaptureSource` 捕获当前身份，提交费用前可调用只读 `CanAcquire`，再调用会重新校验的 `Acquire`：

```cpp
FAbilitySourceToken Token;
FString Error;
FAbilityWorldSlowSettings Settings;
FGuid Handle;
if (AbilityExecutionIdentity::CaptureSource(Source, Token, Error))
{
    auto* Time = Source->GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>();
    if (Time && Time->CanAcquire(Token, Settings, LifetimeOwner, Error))
    {
        Handle = Time->Acquire(Token, Settings, LifetimeOwner, Error);
    }
}
// 调用方保存Handle与服务的弱引用；清理时在服务仍有效的条件下调用Time->Release(Handle)。
```

不能把 `CanAcquire` 成功当作已取得请求；检查 `Acquire` 返回值。配置要求有限数值、倍率 `[0.05, 1]`、保持时间大于零、过渡时间非负、总时长不超过 30 秒；每 World 最多 64 个有效请求。相同来源身份、持有者和完整参数的重复请求复用句柄，不刷新计时；需要独立寿命时使用独立持有者。

**统一图入口：** Schema 2 / `UnifiedV2` 中使用 `TypeId="Time.WorldSlow"`，配置类 `UAbilityWorldSlowNodeConfig`，实际执行类 `UAbilityWorldSlowNodeExecution`。输入载荷为 `Origin`，同时必须携带有效 `SourceToken`；自然到期发布终结端口 `Completed: Origin`，外部撤销走取消。节点 `Stop` 只释放自己的租约。

翻滚 GA 与 `Defense.PerfectDodge` 节点都调用 `UPerfectDodgeComponent` 的共享 `Reserve → Begin → Finish` 动作。成功奖励由组件直接调用同一时间服务 `Acquire`，**不嵌套激活 `GA_WorldSlow`**。组件持有奖励，因此翻滚正常结束后慢速可继续；动作取消、死亡、Spec 撤销、换生命或离场会清理相应资源。防御节点当前输出 `PerfectDodged: Origin` 与终结端口 `Completed: Origin`；尚无攻击者结果载荷，也没有可替换的奖励图配置，不能从该端口取得攻击者目标权限。

## 时钟、补偿与攻击覆盖

动作秒来自 `GetGameplayRealTime()`：以不受时间膨胀影响的 World Real Time 差值推进，暂停期间停止。窗口、翻滚位移、奖励锁与减速包络使用此时钟；现有 GAS GE、TimerManager 等仍使用其原有世界时钟，世界变慢时冷却的现实耗时会变长。

并发请求取各自包络中的最慢倍率，作用于本轮开始前的世界基准。例如原基准为 0.5，请求 0.2 时得到 0.1；角色补偿只抵消本服务新增的慢速，保留原基准。最后一个请求结束后恢复基准。若外部系统改写世界倍率，服务撤销自己的请求及补偿，不用旧基准覆盖对方；对外部改写的角色 `CustomTimeDilation` 也不强制覆盖。

默认补偿来源角色，以及 Owner 或 Instigator 为来源的两类已有/新生成弹体：带 `UProjectileMovementComponent` 的弹体，以及 Stander 手写 Tick 移动的 `AStanderWeaponProjectile`。新弹体在生成完成后的后续服务 Tick 发现。其他 Actor 可显式 `RegisterCompensatedActor(Actor, Beneficiary)`，但需有该受益者的有效补偿请求。既有/新弹体倍率与恢复、实际Mass处理器在引擎缩放Delta下的位移均已通过自动化；独立Niagara、相机和所有组件时钟不能因此统称为已验证正常速度。

| 已接入路径 | 当前边界 |
|---|---|
| 统一近战 `AbilityMeleeNode` | 真实近战查询检查起点见证，当前身体命中在施加伤害前拦截 |
| 预警攻击共享动作 `ObserveTelegraphedAttack / ApplyTelegraphedTargets` | 在实际攻击阶段复用形状、地面、高度和遮挡；包括调用此通路的单体/范围攻击 |
| `AMyProjectile` | 携带有效 `ExecutionSource` 的弹体使用实际移动段检查见证，并在身体碰撞时拦截；无来源身份的旧弹体不能据此宣称覆盖 |
| PureMass `AttackPlayer` | 仅当前身体直接命中的生效入口；翻滚离开范围后原 AI 会取消蓄力，不对起点补做见证奖励 |

拦截发生在这些适配器的伤害调用之前，避免该次命中的扣血及伤害附带效果。它不是覆盖所有 `GameplayEffect`、`ExecCalc_Damage` 或旧伤害入口的全局免疫层。持续 DOT、地面/环境伤害及未接入路径保持原行为；新增敌人必须逐条确认实际攻击调用链。

两个入口目前都限定 **Standalone**，联网模式拒绝启动；服务器身份校验不等于已经支持复制或多人时间同步。首版无翻滚蒙太奇、音效或屏幕后处理：资产脚本仅配置 `NS_Free_Spells_Aura_Air` 与 `NS_Free_Spells_Aura_Mana` 两个 Niagara 占位引用，由组件清理，其外观和寿命仍需 PIE 观察。

## 资产与验证续跑

[资产脚本说明](../Scripts/GA/PerfectDodge/README.md)提供只读预检、白名单备份/写入和独立进程读回命令。写入只涉及两个 GA、三个输入资产及三个 Pawn；不添加学习目录、可替换槽位、奖励 DA 或 GameplayCue 资产。失败后根据本次 `failure.json` / `journal.json` 与磁盘实际状态续跑，不覆盖恢复用户后续修改。

本轮验证状态以[实施记录](../Saved/GAAuthoring/PerfectDodge/20261115-implementation/progress.md)及其链接的新鲜报告为准。实际三种Pawn的Startup和ASC按下激活、Mass真实命中伤害及位移、Actor近战/范围/弹道与时间生命周期已有自动化证据；物理Space输入、双职业动作手感、普通/完美特效、镜头及慢速视觉同步仍需用户PIE验收。
