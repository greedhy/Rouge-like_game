# 完美闪避与世界缓流 GA 原型设计

日期：2026-11-15。状态：用户已批准，已进入实现与验证阶段；翻滚为默认拥有的 Startup Ability，无可替换槽位，世界减速同时提供独立可复用服务与 GA。本文保留设计目标，当前实现及明确延期项见第11节和[实现/复用指南](../../perfect-dodge-and-world-time-guide.md)；构建、资产、自动化及 PIE 的最终状态以[本轮实施记录](../../../Saved/GAAuthoring/PerfectDodge/20261115-implementation/progress.md)为准。按本地 main 开发，不自动提交、推送；保留当前未提交代码与 Content 调参。

## 1. 目标与参考

为魔法师与标准近战 Stander 提供同一项默认授予的 Startup 翻滚能力，内含完美闪避判定。玩家无需学习、装备或选择替换槽位。以快速地面位移和周身特效占位，后续可替换翻滚动画。成功时世界、怪物与敌方弹道慢速，触发者获得正常速度的操作机会。先做世界时间服务与独立测试入口，再接闪避；两个入口共用实际动作，闪避不嵌套激活世界缓流 GA。

参考的可核查规则：

| 参考 | 可借鉴的行为 | 本项目采用方式 |
|---|---|---|
| [Bayonetta 官方介绍](https://www.nintendo.com/en-gb/Games/Nintendo-Switch-download-software/Bayonetta-1313466.html) | 精确时机躲避触发 Witch Time，敌人变慢并留下攻击机会 | 采用可自由行动的慢速奖励 |
| [《旷野之息》官方探索指南](https://assets.nintendo.com/image/upload/v1675114089/Microsites/zelda-breath-of-the-wild/pdf/ExplorersGuide.pdf) | 在敌人攻击即将命中时躲开，短暂慢速并提供 Flurry Rush | 采用“临近真实攻击 + 清晰成功反馈”，首版不做强制反击连段 |

两者支持的是“精确时机 + 真实威胁 + 奖励机会”这一设计归纳，不是统一行业标准。下文窗口、倍率、距离均为本项目建议值，未将任何游戏的帧数当成事实。

不在首版加入耐力属性、职业专属反击、自动锁敌追击、自动处决、残影模型系统或旧翻滚迁移。按用户补充要求，不新增可替换技能槽，不制作装备物品，不因武器变化移除这项基础动作。

## 2. 当前项目依据

遵循 [统一制作指南](../../ga-unified-authoring-guide.md)、[框架/调用链](../../ga-framework-reference.md)、[制作流程](../../ga-production-and-runtime-guide.md)、[怪物规范](../../monster-development-standard.md)、[C++ 规范](../../cpp-readability-standard.md) 与 `skills/build-ga/SKILL.md`。

| 已查入口 | 现状与设计影响 |
|---|---|
| `MyGameplayAbility` / `AbilityExecutionSession` | 已有每次激活会话、动作去重、一次 Commit 与关闭入口；新 GA 继承它 |
| `AbilityMovementNodeExecutions` | `Movement.Character`、`AbilityMovementActions::GroundDash`、`UAbilityMovementOwnershipComponent` 已存在；共用分段胶囊扫掠、地面和房间约束 |
| `AbilityGraphWorldSubsystem::Tick` | 当前按传入 DeltaSeconds 推进，不能假设有不受时间膨胀影响的时钟 |
| `StanderWeaponActionAbility` / 武器 DA | 目录确有 `GA_DodgeRoll/GA_Dodge`。旧武器动作已有位移、格挡等事件，但未发现完美闪避/时间服务；名字不能证明无敌或完美判定 |
| `MyAbilitySystemLibrary::ApplyDamageEffect` | Actor 的通用直接伤害入口，进入 `GE_Damage → ExecCalc_Damage → MyAttributeSet` |
| `MyAttributeSet::HandleIncomingDamage` | 扣血后接受击、击退、飘字及 Debuff；不能靠扣血后补血模拟闪避 |
| `PureMassSubsystem` / `PureMassIncomingDamageEffect` | Mass 对玩家伤害会直接施加 IncomingDamage GE，不能只修改 Actor 通用伤害入口就宣称覆盖所有怪物 |
| `MyGameModeBase::GetAbilityInfoForActor` | 依据 Profession 选择职业预设；本次 Startup 接入绕开可学习目录，检查各预设真实 Pawn 的 Startup 配置 |
| `MyAbilitySystemComponent::ServerEquipAbility` | 已有服务器换槽；本次基础能力应明确拒绝被换槽/替换，不改旧主动技能流程 |
| `AbilityInputTagHeld` | 会反复尝试激活普通技能；完美闪避要单独支持按下触发，不能直接继承长按连放 |
| PlayerState ASC / Stander 武器装备 | ASC 跨 Avatar 生命周期；武器槽保护仍保留，共通技能不能冒充武器授予技能 |

工程声明 UE 5.8，本机引擎为 5.8.1。当前主工作区有大量既有修改，不将它们纳入本任务或做全量格式化。只读资产检查与其限制记录于 `Saved/GAAuthoring/PerfectDodge/20261115-design/`。

本轮独立 Unreal Python 进程已从磁盘读回：Stander GameMode 指向 `/Game/BluePrints/Characters/Stander/DA_SwordAbilityInfo`，Mage 的 DungeonRunGameMode 指向 `/Game/PureMass/DA_MassAbilityInfo`。Sword_Shield `DA_DodgeRoll` 只有一项 Move 事件（0.05 秒开始、250 cm、持续 0.30 秒），没有 Guard 或完美事件。两种候选 Aura 均能加载为 NiagaraSystem。进程退出 0，但启动记录有 329 条警告，包括旧资产的 GameplayTag 警告；这不是资产全面校验通过或新功能测试通过，实施前应在完整构建并正确初始化标签的进程中重新读回。

## 3. 方案取舍

以下选择已随本设计获用户批准；实际首版覆盖范围及延期项见第11节。

| 方案 | 优点 | 限制 |
|---|---|---|
| 仅受击前拦截 | 最少接入点，容易验证 | 位移已经离开攻击体积后没有伤害事件，容易漏判漂亮闪避 |
| 预警/距离直接奖励 | 简单直观 | 打空、隔墙、取消攻击也可能奖励；攻击预测误差大 |
| **实际攻击生效 + 短时起点见证体 + 受击拦截（推荐）** | 已移开身体仍可证明刚躲开攻击，不对空放奖励 | 需为真实近战/范围/弹道接入只读威胁查询，并提供攻击身份 |

世界慢速推荐全局 TimeDilation 配合受控玩家补偿。仅对怪物逐个设置 CustomTimeDilation 无法覆盖场景、物理和 Mass；只降低动画速度不改变真实攻击和移动。底层使用每 World 一个 Subsystem 和租约，不创建进程全局静态单例。

首个可玩验收范围为单机/Standalone。服务器权威与身份结构保留；联网模式在首版明确拒绝这两个新入口并显示不支持原因，不静默施加不同步的全局时间。合作模式的全员补偿、复制时钟和晚加入属于后续独立验收，未承诺首版完成。

## 4. 可执行的完美闪避定义

**玩家主动发起翻滚后，在其早期精确窗口内，服务器确认同一生命的敌方直接攻击已经实际生效，攻击几何命中玩家当前胶囊或翻滚起点见证胶囊；该次攻击具备可闪避资格且本次翻滚尚未奖励，则认定一次完美闪避。**

见证胶囊是查询数据，不是可受伤 Actor：保存翻滚开始时的胶囊位置、半径与半高，只存在于完美窗口，不扩大碰撞半径、不沿整条路径留下拖尾。攻击仍使用当前真实几何、墙体遮挡、高度、阵营与房间检查。命中起点只授予奖励，不在起点结算伤害、消耗弹道或触发受击。

规则明细：

1. `t0` 为权威端接受本次激活并成功 Commit 的时间；输入排队时间不算窗口，不接受客户端自报的回溯命中。
2. 只在攻击实际接触/伤害窗口检查；感叹号、红色预警、敌人距离、按键本身都不能触发。
3. 当前胶囊在无敌期内遭遇合格直接命中时，伤害、命中附带 Debuff、受击与击退一并拦截。见证体只用于奖励；不修改当前身体的碰撞。
4. 一次翻滚只发出一次 `PerfectDodged`。先原子标记已奖励和攻击去重，再通知图/表现，避免同步回调重入。
5. 每个攻击段有 `(SourceLifeToken, AttackInstanceId, HitSequence)`；同一段重复 Trace、多个骨骼、多个伤害类型只算一次。多段攻击可在后续新翻滚中再次判定，但同段不能重复。
6. 默认仅直接近战、直接投射物和明确瞬时爆发可完美闪避。已挂载 DOT、持续地面伤害、环境伤害、友军和显式不可闪避攻击不奖励；原型无敌也不免疫这些持续/环境伤害。
7. 已失效攻击、取消的预警、死亡/池复用后的旧来源、跨房间或隔墙见证、过期见证均拒绝。低帧率时用攻击实际采样区间和扫掠几何，不把任意迟到回调追认到窗口内。
8. 早按：可能普通闪避成功但不奖励。晚按：已经结算的伤害保留，不回血补判。空翻滚：只有位移和普通占位表现。
9. 慢速进行中关闭新的完美奖励，普通闪避照常可用；避免连续慢速锁死世界。不可用“世界变慢导致窗口延长”间接变宽判定。

### 建议初值

所有“动作秒”均为不受慢速缩放、但暂停时停止的 Gameplay Real 时钟；GAS GE 的世界时间另行标注。

| 参数 | 初值 | 语义 |
|---|---:|---|
| 位移距离 | 420 cm | 地面 2D 方向，阻挡可缩短，不能穿墙/穿房间/跨悬崖 |
| 位移时长 | 0.30 动作秒 | 均匀位移为首版，曲线以后可调 |
| 完美窗口 | `[0, 0.12)` 动作秒 | 不按固定帧数实现 |
| 普通无敌 | `[0, 0.24)` 动作秒 | 只拦截可闪避直接攻击 |
| 后摇结束 | 0.42 动作秒 | 0.30 起恢复普通移动，0.42 起允许攻击/施法 |
| 翻滚冷却 | 0.80 世界秒 | 专用 Cooldown GE，沿用当前 GAS 计时；受世界慢速影响，文案明确 |
| Mana / 伤害 | 0 / 0 | 不引入耐力；完整 Commit 一次，固定值不随技能等级增长 |
| 慢速倍率 | 0.20 | 相对世界原有基准倍率 |
| 进入/保持/退出 | 0.05 / 1.20 / 0.15 动作秒 | 总长 1.40 秒，平滑恢复 |
| 奖励锁定期 | 成功后 3.0 动作秒 | 与正在慢速的禁奖条件同时满足才可再奖；当前生命持有，重复基础授予校验不刷新 |

```text
动作秒    0        .12       .24   .30        .42
完美窗口  [=========)
无敌      [==================)
快速位移  [========================)
后摇                                [==========)
成功事件        └→ 进入 .05 → 保持 1.20 → 退出 .15 → 释放时间租约
```

移动方向优先使用非零移动输入，其次使用当前水平移动方向，最后使用角色朝向；激活时锁定，不边滚边追踪鼠标。服务器验证方向为有限、单位化的水平向量，不接收客户端目标位置。地面/存活/控制状态/冷却/位移占用在 Commit 前验证；位移租约预约成功再 Commit，失败归还预约。撞墙仍消耗冷却，关闭位移与窗口并进入剩余后摇，防止贴墙重复开无敌。首版禁止空中激活，不做任意取消攻击的特权；已有攻击/强控/位移占用时拒绝。

## 5. 世界缓流服务与时钟合同

新增拟名 `UAbilityWorldTimeSubsystem`，公开 `Acquire(Request) -> LeaseHandle`、`Release(Handle)`。请求包含来源生命、会话/请求 GUID、倍率、进入/保持/退出时长、补偿对象和结束原因。服务只在所属 Game/PIE World 生效。

并发规则：每个有效请求生成自己的包络倍率 `s_i(t)`，世界采用 `G = Baseline × min(s_i(t))`，不连乘。各自到期，不重复刷新旧请求；用请求 GUID 去重。释放某一请求只重算其余请求，最后释放才恢复保存的基准。期间其他需要改全局时间的系统也必须接服务；检测到外部写入时安全终止本服务并记录冲突，不以旧值覆盖新控制者。允许区间 `0.05 <= SlowScale <= 1`，拒绝 0、NaN、负数和无界时长。

租约每帧复核 World、Avatar、ASC、RunId 与生命。正常翻滚结束不移除已经赢得的慢速；死亡、失去 Avatar、撤销基础能力 Spec、换图、Run 结束、来源失效则立即释放该来源请求。自然结束可渐出，世界退出/死亡等异常结束立即清理。释放幂等，旧租约不修改新生命的 CustomTimeDilation。

| 系统 | 首版时钟/处理 |
|---|---|
| 怪物移动、动画、AI 与攻击计时 | 世界时间，自然变慢；不再额外给怪物乘 0.2 |
| 敌方投射物、环境动态、物理 | 世界时间，按实际引擎表现验证；静态场景没有“变慢”的动画 |
| PureMass 模拟 | 使用世界 Delta 的处理链必须实测；不能给无 Actor 的实体设置 CustomTimeDilation |
| 触发者 Character 移动与动画 | 保存原 CustomTimeDilation，补偿本服务因子：`C = C_baseline / S_effective`，保留原世界/角色其他速度效果 |
| 闪避位移、窗口、奖励锁、慢速寿命 | Gameplay Real（不受 Dilation、暂停时冻结），任务自己从服务取 Delta；不改旧图默认时钟 |
| UI、镜头、输入 | 正常交互；不得停掉输入或让镜头缓动被补偿两次，分别实测 |
| 已有技能 GE 冷却、DOT、延迟图 | 保留世界时间，首版不迁移全套 GAS 计时；“玩家正常动作”不承诺所有旧技能计时正常 |
| 法师/玩家新发射投射物 | 用显式参与组件登记 Owner 来源与原速度倍率，在慢速期间补偿；包括存活的自有弹道，生成后也可加入；退出恢复 |
| 玩家图驱动的周期场/延迟连锁 | 首版保持世界时间，表现及说明明确；后续可加独立时间域配置 |
| 周身成功特效 | 随角色动作时钟，退出及时回收；不能因世界慢速拖成长期循环 |

UE 本机 `World.h` 明确：`GetUnpausedTimeSeconds` 仍受膨胀影响；`GetRealTimeSeconds` 不受膨胀但暂停仍流逝。实现用 RealTime 差量并在暂停期间更新采样基点而不累计；恢复的首帧不得补跑暂停时长。Actor CustomTimeDilation 仅影响相应 Tick，不能解决 TimerManager、PlayerState ASC 或世界图时间。对 Niagara、独立投射物、相机使用明确适配，禁止将“Actor 已补偿”当成全链正确的证据。

Standalone 首版世界缓流 GA 是开发测试入口，不登记为普通可学习技能；零 Mana、独立冷却，明确与完美奖励共用参数和服务。以后若作为可装备法术，另设费用/冷却/技能目录，不更改底层服务。

## 6. 统一 GA 框架扩展

拟建 `UPerfectDodgeAbility : UMyGameplayAbility`，只负责输入/Commit/方向/前端状态与结束；无武器、骨骼、职业硬编码。核心运行在注册策略和共享动作内。

| TypeId（拟新增） | 配置 / 执行职责 | 类型化接口与寿命 |
|---|---|---|
| `Defense.PerfectDodge` | 校验时序关系；原子打开窗口、取得位移所有权；调用现有 GroundDash；维护查询见证与去重 | Origin 输入；`PerfectDodged: DodgeResult` 非终止、`Blocked: Origin` 非终止、`Completed: Origin` 终止；绑定 GA |
| `Time.WorldSlow` | 独立测试 GA 的请求包装，Acquire/等待结束/Release | Origin 输入，`Started: Origin`、`Completed: Origin`；默认绑定测试 GA |
| `Time.WorldSlowOnDodge` | 校验 DodgeResult 来源，再调用同一 Acquire；无第二套时间实现 | DodgeResult 输入，`Started: Origin`、`Completed: Origin`；独立施法效果 |

`DodgeResult` 是拟新增端口类型，包含防御者 SourceToken、DodgeExecutionId、攻击身份、实际判定时刻、见证/身体命中位置及方向、攻击者原生命快照。它不授予攻击者活目标权限，未来反击需要专门重新解析原生命。需同步更新 payload 枚举、输入结构、验证、边连接校验和注册测试，不能只新增 enum 名字。

专用前端在真实成功事件用当前会话去重后启动独立奖励图（只运行注册动作，不激活完整 GA），由 `UAbilityGraphWorldSubsystem` 持有。慢速节点从 Gameplay Real 时钟查询自身进度，不用世界 Delta 累加寿命；图调度仍沿用现有框架。取消不发 `Completed`。

奖励在成功瞬间立即启动并 Adopt 到 World，不等待翻滚结束。Adopt 后不再注册到原翻滚会话的“正常结束即取消”清理中，自己持有冻结配置和原 SourceToken；Avatar 上的防御组件按 AbilitySpecHandle + Avatar 生命记录其 ExecutionId/时间 Lease。该组件监听死亡、EndPlay、ASC Avatar 变更以及基础能力 Spec 撤销，精确取消自己的奖励。正常翻滚结束、撞墙进入后摇保留已赢得的奖励；非致死 DOT 只有实际触发动作打断时才取消奖励；强控/显式取消翻滚立即撤销，死亡始终撤销。Startup 技能不存在玩家卸下操作。

现有 SourceToken 没有玩家生命代数，不能仅靠其存活检查防止同一 Pawn 复活后的旧回调。首版防御组件新增单调 LifeGeneration；死亡同步使旧代失效并清理资源，重新初始化/复活开启新代。窗口、攻击见证、时间租约、奖励图均携带该代并复核；若未来需要跨能力共用，再提取到统一生命合同。测试必须包含同 Pawn 死亡后复活及 ASC 更换 Avatar，不能只测销毁重生。

服务在每帧只更新一次 Gameplay Real 时间，各消费者读取统一时间戳，避免多任务重复累计；低帧率只允许误差在一个实际模拟帧内，不能把长卡顿后的旧攻击补成新奖励。

为防前摇移动竞态，`Defense.PerfectDodge` 内直接调用已有 GroundDash 共享动作，而不并列启动第二个会争抢租约的 `Movement.Character` 节点。后续真实蒙太奇只替换表现前端；默认时序保持 DA 驱动，不同时用 Notify 与计时器开两次窗口。

身份/状态拟定：`Abilities.Common.PerfectDodge`、`Cooldown.Common.PerfectDodge`、`State.Dodge.Active`、`State.Dodge.Invulnerable`、`State.Dodge.PerfectWindow`、`GameplayCue.Common.Dodge`、`GameplayCue.Common.PerfectDodge`。状态 Tag 仅用于可见性和互斥，不是客户端自报成功的依据。无敌/窗口由带所有权的组件和对应 GE 句柄管理，不能全局 RemoveLooseGameplayTag 误删其他来源。

## 7. 真实攻击与伤害接入

建议新增 `FDodgeAttackContext` 和 `AbilityDodgeActions::EvaluateIncomingHit/ObserveThreat`，仍使用原 ASC/GE 伤害通路。攻击资格由服务器的攻击配置决定；缺少可信上下文时不给完美奖励，不能根据收到任意 IncomingDamage 猜造攻击身份。

接入原则：

1. 在真实攻击命中几何产生处捕获攻击身份，并在同一批查询中检查短时见证胶囊。只复用几何，不对见证体调用伤害函数。近战/范围跟实际预警定义一致，投射物按上一位置到当前位置 Sweep，不能每帧球形近敌扫描。
2. 在 `ApplyDamageEffect` 通过现有合法性检查后、创建与施加 GE 前调用共享拦截。`ExecCalc_Damage` 的直接 GE 路径需同一防重复拦截/结果标记，防止绕过该 helper；同一上下文只作一次防御裁决。
3. PureMass 对玩家的直接 GE 入口调用同一防御服务，用 EntityHandle/Run 生命作为攻击源，不强行假造敌人 ASC。Actor 与 Mass 独立列覆盖清单。
4. 不拦截没有声明可闪避的持续 DOT/地形 GE。若某旧路径缺少分类，保持旧行为并在覆盖报告列未支持，不默认吞伤害。
5. 拦截结果显式区分 Applied/Dodged/Rejected，避免调用方把有 Context 或应用尝试当真实伤害；投射物身体命中后的消耗沿原规则，见证体命中则继续飞行。闪避成功不产生伤害飘字或受击事件。
6. 威胁提供者失效/死亡/回收时取消本生命订阅；只读查询中禁止刷新旧 SourceToken 成为新生命。

首个纵向切片必须覆盖 Actor 单体近战、范围爆发、敌方投射物三条实际通路。Mass 在同轮扩展其直接打玩家入口与威胁几何并测试后才列支持。未接入的怪物仍受世界慢速，但不得声称其攻击能触发完美闪避。

## 8. 双职业、输入与资产

拟新增资产集中于 `/Game/UnifiedAbilities/Defense/PerfectDodge/` 和 `/Game/UnifiedAbilities/Time/WorldSlow/`：

- `GA_PerfectDodge`、`DA_PerfectDodge`、`GE_CD_PerfectDodge`、`GC_Dodge`、`GC_PerfectDodge`。
- `GA_WorldSlow_Debug`、`DA_WorldSlow`、奖励图 `DA_PerfectDodgeReward`。图使用 Schema 2 / UnifiedV2，独立稳定 NodeId。
- 双职业共用同一 GA 类、Tag 和核心 DA；可选表现配置分别指定颜色/挂点。后续动画 Profile 可按 Avatar 提供，核心不 Cast Stander。

**Startup 授予是用户已确定要求。** 将同一 `GA_PerfectDodge` 追加到 Mage 的实际 Pawn（含 BP_Player / 地牢 BP_DungeonPlayer 的有效继承配置）与 BP_StanderPlayer 的 `startup_ability`。以按类和 AbilityTag 幂等的基础能力协调函数保障每个 ASC 恰好一个 Spec；不能同时在父类与子类重复授予。固定等级 1、0技能点，不登记为可学习项，不加技能树按钮，不提供装备/替换入口。允许 HUD 显示只读的固定按键/冷却提示，但不做技能槽。

新增 `InputTag.Dodge` 与 `IA_Dodge` 仅用于固定输入路由，`StartupInputTag=InputTag.Dodge`；建议 Space（实施前读真实 IMC，若冲突保留原键并使用已核实的空闲映射）。通过现有 Enhanced Input → ASC 输入链激活。添加基础能力不可重绑到技能槽的元数据/接口，ServerEquipAbility 拒绝移动该基础 Spec，也拒绝其他 Spec 占用 InputTag.Dodge；旧武器六槽和菜单保持原合同。用户可以更改物理按键绑定，但不能用别的 GA 替换基础翻滚。

输入增加通用 `PressOnly` 策略/虚函数，旧 GA 默认保持原 Held 行为；本技能只在 Pressed 发起，按住不连滚，Released 不取消正在执行的翻滚。不要将它伪装成 Toggle。服务器验证当前 Avatar 的基础 Spec、固定输入路由、存活/强控/移动占用；不依赖用户装备状态。

`APlayerCharacter::LoadProgress` 的旧档分支只恢复 SavedAbilities 并 UpdateAbilityStatuses，不能靠追加 Startup 数组补齐旧档。新增幂等 `EnsureIntrinsicStartupAbilities`，在新档授予后、旧档恢复后、无存档的新建入口及新 Avatar 初始化完成时运行。该列表仅含明确标记的固定基础能力，不能重放全部旧 Startup 数组覆盖玩家选择。普通技能保存遍历跳过基础能力，恢复也跳过已识别的基础能力历史项，再由 Ensure 统一授予固定身份/输入；旧档没有该项也自动拥有。重复 Load/旅行仅校验，不重复 GiveAbility，不重置已有冷却。正在翻滚/慢速/见证/租约均不存档。武器换装只回收自己的句柄，不移除基础翻滚 Spec。

表现首版：快速移动使用现有 locomotion，不需要匹配骨骼的翻滚蒙太奇；普通闪避用低亮度周身风环，完美成功用一次高亮脉冲、短音效和轻微屏幕色调，再进入慢速。候选资源为 `/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Air` 和 `NS_Free_Spells_Aura_Mana`，当前仅列候选，必须读回并检验循环/空间/缩放/寿命后使用。特效无伤害碰撞、无玩法计时权；缺资源时降级为可见的简单材质环，并记录降级。首版不添加强制镜头旋转或重度震屏。

## 9. 生命周期、扩展点与交付顺序

所有取消路径先关闭事件入口，再清监听/见证、结束动作任务、释放位移租约、移除自己拥有的 GE/Cue，最后按第6节规则处理已在成功瞬间移交的时间租约。自然完成不再次移交；死亡、基础 Spec 撤销、旅行与强控取消撤销现有奖励。奖励锁不因本次 GA 正常结束被清除，随当前 ASC/Avatar 生命保存到到期，重复基础能力校验不能刷奖励。

扩展点：窗口/距离/时序来自私有配置；成功后可把奖励图换为回蓝、护盾、增伤或反击准备；可根据职业选择不同表现；世界时间服务将来可服务击杀慢镜/剧情/主动时间法术。不要为各职业复制完整 GA，也不要为每个奖励建立第二套执行器。

实现按以下可独立验收的顺序推进：

1. 时间服务、租约、Gameplay Real 时钟、世界节点、独立测试 GA；验证慢速恢复与玩家/法师弹道补偿。
2. 攻击上下文、共享防御裁决和三类攻击几何接入，先验证有真实威胁才奖励。
3. 闪避节点/前端/按下输入，复用位移所有权；先逻辑后接占位表现。
4. 双职业 Startup/固定输入/旧档补齐/保存排除接入，制作可重入白名单资产脚本，完整 Editor 构建后创建引用新反射类型的资产。
5. 独立进程读回；跑行为回归与项目要求的全量 test_25d；实际 PIE 观测两角色与慢速世界。

## 10. 验收矩阵

| 类别 | 必须观察到的结果 |
|---|---|
| 判定边界 | 0、0.119、0.120 秒边界；早/晚/空翻滚；身体已移开但起点受真实攻击；攻击打空/隔墙/不同层不奖励 |
| 实际防御 | 合格攻击在无敌窗内 Health 不变，无附带 Debuff/HitReact/Launch；窗外正常；原有 DOT 照常 |
| 去重 | 一击多骨骼、多伤害类型、重复回调、多怪同帧均每翻滚一次奖励；同攻击段不重复 |
| 时钟 | 30/60/120 fps，已有 0.5 世界基准；世界达到相对 0.2，1.40 动作秒自然退出；暂停恢复不跳时 |
| 并发 | 0.5 与 0.2 请求取最慢；结束一个不恢复另一个；重复 Release；第三方写入冲突不覆盖其值 |
| 生命 | 死亡、同Pawn复活、失去 Avatar、基础Spec撤销、Run/World退出、敌人回收后旧攻击均无残留；验证两次敌人生命 |
| 位移 | 墙、坡、台阶、房间边界、悬崖、已有位移占用；取消后移动/转向状态归还且不恢复新生命旧状态 |
| 跨职业 | Mage 与 Stander 出生即拥有同一基础 GA；不同武器/无武器可激活；无学习/装备操作；旧武器槽保持原行为；替换翻滚或把它拖入其他槽被服务器拒绝 |
| 玩家补偿 | 跑步/转向/攻击蒙太奇/相机、法师已存在和新发射弹体分别测；旧 GE 冷却仍走世界时间且 UI 一致 |
| 输入/存档 | 按住一次只翻滚一次；新档、旧档、重复加载、重进/旅行都默认拥有且只有一份 Spec；不花技能点、不重置现有冷却、临时无敌不保存 |
| 世界表现 | 怪物动画/移动/攻击预警/敌方弹道慢速同步；Mass 单独测试；普通与完美特效可区分并清理 |
| 失败关闭 | 非法参数、未知节点、错误端口、无有效 Avatar、联网首版拒绝激活；不扣费、不留慢速 |

自动化必须读取真实 Health、GE/Tag/租约/移动状态、世界倍率、事件计数和实际 Actor/弹道，不只测参数。配置脚本成功、编译成功、资产独立读回、行为自动化、用户 PIE 与联网分别记录，不能相互代替。

## 11. 当前实现与验证边界

用户已批准本设计及“独立世界减速可复用”的补充要求。单机首版、0.12秒完美窗口、0.2倍世界倍率、保持1.2秒及原GAS冷却继续使用世界时间，均为本轮实施参数，不再是待确认建议。

当前源码已经加入每World时间租约服务、`Time.WorldSlow`节点、独立`UWorldSlowAbility`、共享`UPerfectDodgeComponent`、`Defense.PerfectDodge`节点及固有翻滚GA。Startup补齐、PressOnly输入、禁止换槽和存档排除沿现有ASC/角色通路实现。资产脚本配置`GA_PerfectDodge`、`GA_WorldSlow`和`IA_Dodge`，以Space接入Mage/Stander，并保留其他Startup与输入内容；具体已保存和读回状态以本轮报告为准。

首版实际边界与原设计目标需区分：

- 完美奖励由共享防御组件直接取得时间服务租约，不嵌套激活世界减速GA；防御节点目前输出`Origin`，尚未实现攻击者结果载荷或可替换奖励图。独立世界减速GA有3个世界秒原生冷却，但不自动授予、不绑定按键、不登记学习目录；直接服务请求不自动附带该冷却。
- 近战、预警攻击共享生效动作及带有效来源身份的`AMyProjectile`已接适配器；PureMass仅接当前身体直接命中，离开范围会取消原蓄力，尚无起点见证奖励。未接入的直接GE、DOT和地面伤害没有统一闪避免疫。
- 自动时间补偿覆盖来源角色，以及Owner或Instigator为来源的`UProjectileMovementComponent`弹体和Stander手写Tick的`AStanderWeaponProjectile`；已有与新生成弹体均接入发现流程，最终回归结果待本轮报告。首版拒绝联网启动，不宣称多人同步已完成。
- 表现当前为Air/Mana两种Niagara占位引用；未制作翻滚蒙太奇、成功音效或屏幕后处理，也未生成第8节设想的独立奖励DA/GameplayCue资产。

Editor构建和资产独立读回通过；25项新增行为测试通过。全项目424项中423项通过，1项既有时间轴制作引用检查失败并已单独复现，详情见[实施记录](../../../Saved/GAAuthoring/PerfectDodge/20261115-implementation/progress.md)。未完成用户PIE视觉验收；双职业真实Space输入、动画/特效外观、世界与各类弹道的视觉同步仍需分别验收。调用方式、默认参数与生命周期清理见[实现/复用指南](../../perfect-dodge-and-world-time-guide.md)。
