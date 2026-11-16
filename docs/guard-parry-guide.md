# 标准玩家右键格挡与弹反

本文记录 Stander 标准玩家现有右键格挡的弹反扩展。开发入口仍是[统一制作指南](ga-unified-authoring-guide.md)和[框架/调用链](ga-framework-reference.md)；复用已注册的 `Status.GuardWindow` 和已有 GA 动画前端，不新增替换技能槽或第二套格挡执行器。

## 行为定义

实际格挡窗口打开后的前 **0.15 动作秒**为弹反窗口，范围为 `[0, 0.15)`。动作秒读取 `UAbilityWorldTimeSubsystem::GetGameplayRealTime()`：不随世界减速拉长，游戏暂停时停止推进。窗口必须同时保有本次 Guard GE 和有效的防守者生命；提前结束格挡、取消、死亡或失效会提前失去弹反资格。

合格敌方攻击在此窗口内真实接触玩家时，先执行弹反裁决，再进入原伤害通路。可反射的远程弹道改变归属并反向继续飞行；接入的直接近战攻击被拦截，原攻击者获得默认 **1 世界秒**眩晕。世界秒随世界时间流速推进，因此弹反奖励或已有世界减速会延长眩晕的现实持续时间。窗外继续采用原有普通格挡规则。

按住右键、重复打开同一 Guard GE 或命中不会刷新这 0.15 秒。组件按攻击 ID 去重，同次攻击重复回调不重复施加眩晕或发布成功事件；不同攻击可在剩余有效窗口内分别触发。原 GA 或图若在首次成功后关闭 Guard，余下窗口随之结束。窗口不额外增加朝向限制，仍依赖原攻击接触、敌我和房间筛选。

弹反成功同时尝试获得与完美翻滚相同参数的世界减速奖励：**世界速度 0.2 倍，0.05 秒渐入、1.2 秒保持、0.15 秒渐出，补偿防守玩家的动作速度**。奖励直接调用已存在的独立世界时间服务，不嵌套激活 `GA_WorldSlow`；世界、怪物和未补偿 Actor 使用慢速世界时间。时间服务与补偿范围见[完美闪避与世界时间服务](perfect-dodge-and-world-time-guide.md)。反射弹体也会更新该服务的补偿受益者登记。

每个 Guard 只在首次成功时尝试一次奖励。同一窗口后续攻击不重试、不刷新；若任意世界减速请求仍活跃，或该弹反组件的 **3 动作秒奖励锁**未结束，跳过奖励，反射/眩晕仍成功。跳过不会新增奖励锁；真正调用 `Acquire` 时开始计锁，即使服务拒绝请求也不在该窗口内重试。这样连续弹反、完美翻滚与独立减速调用不会互相叠加或延长已存在的慢速。

## 真实输入与配置

输入保持 `RightMouseButton → /Game/BluePrints/Input/InputActions/IA_RMB → InputTag.RMB`。标准玩家存在两条实际格挡入口：

| 入口 | 实际配置 | 弹反字段 |
|---|---|---|
| 初始授予的 `GA_Stander_Guard` | `BP_StanderPlayer.SwordConfig → /Game/BluePrints/Characters/Stander/DA_Stander` | `UStanderCombatConfig.GuardParry` |
| 武器 `DA_WeaponSet.Abilities` 中 `InputTag.RMB → GA_Block` | `GA_Block.ActionConfig → 本武器目录的 DA_Block` | `FStanderWeaponActionEvent.Parry`，仅 `Kind=Guard` 事件 |
| 以后组合图复用 | 已注册 `Status.GuardWindow` 的 `UAbilityGuardWindowNodeConfig` | `Parry`，默认关闭，由该图明确选择开启 |

三处都使用 `FGuardParrySettings`。原生默认 `bEnabled=false` 保持旧配置兼容；本次目标资产显式开启。

| C++ 字段 | Python 属性 | 本次值 | 约束 |
|---|---|---|---|
| `bEnabled` | `enabled` | `true` | 只对已确认的右键 Guard 配置开启 |
| `Window` | `window` | `0.15` | 有限，`(0, 1]` 动作秒 |
| `MeleeStunDuration` | `melee_stun_duration` | `1.0` | 有限，`(0, 10]` 世界秒 |
| `bRewardWorldSlow` | `reward_world_slow` | `true` | 本次弹反成功启用独立减速奖励；仍受 `bEnabled` 总开关控制 |
| `RewardLock` | `reward_lock` | `3.0` | 有限，`[0, 60]` 动作秒 |
| `Reward.Scale` | `reward.scale` | `0.2` | 世界速度倍率，复用时间服务校验 |
| `Reward.BlendIn` | `reward.blend_in` | `0.05` | 奖励动作秒 |
| `Reward.Hold` | `reward.hold` | `1.2` | 奖励动作秒 |
| `Reward.BlendOut` | `reward.blend_out` | `0.15` | 奖励动作秒 |
| `Reward.bCompensateSource` | `reward.compensate_source` | `true` | 补偿原防守者及时间服务支持的归属 Actor |

只读资产检查确认以下 **11** 个武器族的实际 RMB 动作都为 Guard，包括 Bow。每组均是 `/Game/BluePrints/Characters/Stander/Weapons/<族名>/DA_WeaponSet → GA_Block → DA_Block`，且 `DA_Block.Events` 恰有一个 Guard 事件。

| 武器族 | 原 Guard 事件起点 | 原持续时间 | 原耗蓝 / 冷却 |
|---|---:|---:|---:|
| Bow | 0.2 | 1.6 | 15 / 3 |
| DoubleBlades | 0.2 | 1.6 | 15 / 3 |
| DualBlades | 0.2 | 1.6 | 15 / 3 |
| Fighter | 0.2 | 1.6 | 15 / 3 |
| Hammer | 0.2 | 1.6 | 15 / 3 |
| Magic | 0.2 | 1.6 | 15 / 3 |
| Pistol | 0 | 1.6 | 15 / 3 |
| Rapier | 0.2 | 1.6 | 15 / 3 |
| Spear | 0.2 | 1.6 | 15 / 3 |
| Staff | 0.2 | 1.6 | 15 / 3 |
| Sword_Shield | 0 | 1.6 | 15 / 3 |

表中时间来自原事件配置，保留原蒙太奇及 Timeline 阶段；弹反始于该 Guard 事件实际执行，不统一提前到鼠标按下时。旧 `DA_Stander` 的 `GuardDuration=0.8`、`GuardCooldown=3`、反击 `Counter` 与原 Timeline 引用也保留。`GA_Stander_Guard` 接收同一 Guard 句柄的弹反成功通知，在回调退栈后沿用原反击转换流程；武器 `GA_Block` 继续原本的格挡动画流程。

本次资产写入白名单只有 `DA_Stander` 和上述 11 个 `DA_Block`，共 12 个现有 DataAsset。没有修改 WeaponSet 的授予列表、GA 类、初始技能数组、输入映射、蒙太奇、Timeline、费用或冷却。未来武器如果 RMB 是移动或其他行为，不能因为资产名叫 `GA_Block` 就打开弹反，必须检查真实事件类型。

## 共用逻辑与扩展

调用链为：原格挡 GA / Timeline Guard 事件 / `Status.GuardWindow` 节点 → `AbilityCharacterActions::OpenGuardWindow` → 本次 Guard GE → `UGuardParryComponent` 记录窗口 → 实际投射物或近战接触适配器 → 反射或眩晕、首次成功尝试独立时间奖励 → 原前端清理。

组件在权威端按需要创建，窗口绑定具体 Guard GE 和 `FAbilitySourceToken`。节点的 `Parried` 端口携带防守者的 `Origin`，延迟到下一次图推进发布；它不携带攻击者活目标权限，不能直接用于给攻击者追加效果。`bCloseOnBlock` 同样控制成功弹反后的完成行为。节点结束撤销自身 Guard 句柄并解除监听，不影响其他来源的格挡。

已经获得的减速租约由弹反组件独立持有，与 Guard 句柄分开。**格挡正常完成、取消、进入原反击或因换装关闭格挡，都不会撤销已赢得的减速**，奖励继续到自然结束。防守者死亡、倒地时显式失效、Avatar/运行/生命令牌变化、`Invalidate` 或 `EndPlay` 才清理本组件的奖励与窗口。清理只释放本组件的租约，不撤销完美翻滚或其他调用者的时间请求；该规则与翻滚动作本身取消时的奖励清理规则分别管理。

新增敌方近战通路应在伤害和附加 Debuff 前接入同一个 `TryParryMelee`，保留真实几何、房间、原生命令牌和稳定攻击 ID。不能在通用受伤 GE 后反推“这一定是可弹反近战”，否则持续伤害、范围场或非攻击效果会误触发。

Actor 近战使用独立 `UGuardParryStunEffect`，授予 `Debuff.Stun`、阻止能力激活，结合已有眩晕响应停止移动，并作废/取消当前攻击；不借石化效果制造眩晕。直接近战的已接入共享通路为 `Melee.Contact` 和角色/怪物接触动作。PureMass 的直接攻击走 `TryParryMass → ApplyStun`，验证实体句柄并清除待发生攻击和预警，暂停其 AI 推进；该状态独立于石化计时。攻击者死亡、池回收或实体复用后，旧命中不能控制新生命。

## 反射弹道的归属与寿命

普通 `AMyProjectile` 与敌人弹弓 `AEnemySlingProjectile` 在伤害、终止爆点和销毁前判断弹反。成功时复用当前弹体，速度反向、大小保持，关闭旧追踪目标；不重生一个额外弹体，也不重新增加寿命或伤害倍率。

- 每个弹体最多反射一次。改变 `Owner`、`Instigator`、来源 ASC、世界上下文和来源生命令牌，清空旧目标及旧施法的伤害回调，避免原技能收到返程命中。
- 冻结的伤害参数及剩余寿命继续使用原值。归属变更后，实际伤害仍走原 GAS 伤害流程，攻击源属性按该流程结算；不承诺最终掉血数值与反射前必然相同。
- 原追踪组件被解除，沿当前入射速度的反方向飞行；没有自动追踪原射手的额外保证。原射手已走开或受到掩体遮挡时，返程可能打空或撞墙。
- 原射手的目标生命令牌用于阻止返程命中同一 Actor 复用后的新生命。弹弓弹体移除旧射手的专用死亡委托，转由防守者的有效生命约束；防守者死亡或来源失效会让弹体终止。
- 真实接触点和返程碰撞继续生效，原方向预扫到的墙不会在同一回调中错误终结已反向的弹体。没有穿墙奖励。

当前不覆盖所有视觉上像“弹道”的技能。`AMyOrbitingFireBolt` 保有独立环绕/施法上下文，明确拒绝通用反射；`AStanderWeaponProjectile` 是另一条玩家武器弹体通路，本次没有给它增加通用入射反射。光束、范围场、持续伤害、未接入伤害前适配器的自定义投射物也不应声称支持。以后新增带独立飞行目标、源死亡委托或施法持有者的子类，应显式实现归属转移合同，或覆写 `SupportsParryReflection()` 拒绝通用反射。

结算使用权威端和现有移动复制；窗口没有新增客户端预测或成功特效 RPC。独立世界时间服务目前限定 **Standalone**，联机环境中的减速请求会被拒绝，弹反拦截/反射/眩晕仍可独立结算。联机时延、客户端观感和未来世界时间同步需要独立验收，不能仅凭服务器单元测试宣称联机已完成。

## 资产制作与验证记录

脚本和准确命令见 [Scripts/GA/GuardParry/README.md](../Scripts/GA/GuardParry/README.md)。先完整构建 Editor，再执行只读检查和预检；明确应用开关后才备份、写入白名单资产；最后另起 PID 从磁盘读回。脚本不编译或修改 Blueprint。

| 阶段 | 本次证据 | 状态 |
|---|---|---|
| 原资产与输入只读检查 | `Saved/GAAuthoring/GuardParry/inspect-20261116T104617-961f4c2b/inspection.json`；11 武器族、真实 Guard、fallback 和右键路由；dirty `[] → []` | 已检查 |
| Editor 完整构建 | `Saved/GAAuthoring/GuardParry/20261116-implementation/build-04.log` | 完整构建通过，包含当前并行装备源码；弹反生产逻辑自 build-02 后未改变 |
| 资产预检、备份与写入 | `Saved/GAAuthoring/GuardParry/author-20261116T110439-79dd4d51/preflight-result.json`；`author-20261116T110522-b28c055e/authoring-result.json` | 12 个白名单资产备份、保存成功；原字段及保护包保持一致 |
| 独立 PID 资产读回 | `Saved/GAAuthoring/GuardParry/verify-20261116T110602-50cafea5/verification-result.json` | PID 112564 独立读回通过；写入 PID 34268 |
| 弹反专项自动化 | `Saved/GAAuthoring/UnifiedGA/20261116T111313-caa217ba/validation.json`；对应 `Saved/Automation/UnifiedGA/20261116T111313-caa217ba/index.json` | 17/17 通过，包含真实默认 GA、11 武器装备/RMB、反射伤害、Actor/Mass 眩晕及慢速生命周期；输入哈希前后一致 |
| 全量回归自动化 | `Saved/Automation/UnifiedGA/20261116T111359-4ba7ff3a/index.json`；对应 `Saved/GAAuthoring/UnifiedGA/20261116T111359-4ba7ff3a/validation.json` | 452/455 通过，输入哈希前后一致；弹反、翻滚、世界减速合计 42 项全部通过；全量门禁仍因下列 3 项失败而未通过 |
| 玩家 PIE / 视觉 / 联机 | 下列清单 | 待用户验收 |

本次全量失败项：`Equipment.Unique.AuthoredAssets` 和 `Equipment.Unique.StormSetTimelineIntegration` 缺少并行装备开发尚未生成的 `UniqueStormforgedHelm`；`UnifiedGA.Catalog.AllConfigurationAssets` 仍报告此前已有的 9 个时间轴缺少作者蒙太奇引用（包括旧 Stander / Sword_Shield 时间轴）。它们没有被屏蔽或标成通过，本次也没有改写这些资源。真实格挡前端使用其现有蒙太奇执行的行为测试已通过。

初轮两项失败已根据实际机制修复测试夹具：Mass 单帧推进有 0.25 秒上限，测试改用正常短帧累计；弹体夹具补齐 `InitializeComponents`，保留真实 `ProjectileMovement` Tick 与旧扫掠接触检查。第二轮 17 项行为通过，但被并行装备源码修改触发的输入哈希检查拒绝；重新完整构建后的上述专项记录才是最终通过证据。测试有共享无网格夹具的 Socket/清理告警，不宣称零告警。

## PIE 验收

1. 标准玩家确认右键能走装备武器的 `GA_Block`；切换剑盾、双刀和至少一种远程外观武器，确认格挡动作、耗蓝、冷却与原来一致。单独验证 fallback `GA_Stander_Guard` 的初始授予和原反击衔接。
2. 在近战怪物预警后恰好打开格挡，让伤害接触落在最初 0.15 动作秒：玩家不承受本次攻击伤害，敌人中止当前攻击并眩晕约 1 世界秒；到期恢复移动和攻击。稍晚接触应仍走普通格挡，没有额外眩晕。
3. 在弹弓石块或普通可反射弹道接触前及时格挡：弹体从真实接触处反向、玩家不被该次弹体击中，返程能够命中仍在原生命的敌人；旧追踪不把弹体折回玩家。墙体仍能阻挡返程。
4. 按住右键、快速重复输入、同一攻击多组件重叠，确认不会刷新同一个窗口或重复眩晕。连续两个不同攻击仅在尚未关闭的有效窗口内分别弹反。
5. 分别在格挡中取消、换装、死亡/复活、离开房间；确认旧窗口和委托不残留。弹体反射后再让原射手死亡/池回收，以及让防守者死亡，检查各自归属和生命处理。
6. 对 PureMass 敌人验证预警、攻击终止、眩晕到期和实体回收后恢复；确认没有石化材质或永久停滞。
7. Standalone 弹反成功后确认世界和怪物渐入 0.2 倍慢速、玩家动作被补偿，保持 1.2 动作秒后渐出；弹反窗口仍为 0.15 动作秒，暂停时不流逝，眩晕按世界秒推进。连续弹反不得刷新奖励；先触发完美翻滚或独立世界减速，再弹反，原奖励不得叠加或延长。
8. 奖励已获得后正常结束格挡、切换原反击、取消或换装，减速应继续自然结束；3 动作秒奖励锁内再次弹反仍有拦截/反射/眩晕，但无新慢速。防守者死亡、倒地、换生命或离场时清理自身奖励，不能清理其他来源的减速。
9. 检查普通格挡动画、原反击表现和弹体飞行的可读性。本次复用原表现，未新增独立弹反闪光/音效资产；如需更明显反馈，后续沿成功事件配置表现。
10. 若测试联机，分别记录主机、客户端按键延迟、服务器命中归属、返程轨迹同步和眩晕恢复；世界慢速当前不支持联机，单独标注该限制及其他场景通过情况。
