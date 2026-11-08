# GA 动画时间轴首版

用户已确认：保留现有 Notify → GameplayEvent 通路；一次连续播放蒙太奇，按阶段改变倍率，不用逐帧跳位置控制动作。

## 实施范围与顺序

1. 新增时间轴 DataAsset、可选 DataTable 时长行和每次施法的解析快照；测试映射、非法配置、事件去重、取消后的事件拒绝。
2. 新增 AbilityTask，负责播放、分段倍率、事件校验、完成/中断和监听清理。保留引擎 PlayMontageAndWait / WaitGameplayEvent，不修改引擎源码；一个蒙太奇只能由一个播放任务拥有。
3. Stander 武器动作配置可选择时间轴，接入原有动画位置事件和 Actor/Mass 伤害通路。未配置时保持现状。创建独立样例时间轴并挂接少量实际动作，进行自动化验证。
4. 文档记录蓝图迁移方法、同步和生命周期、测试结果及 PIE 验收。StoneGolem、FireBolt 等后续逐项迁移，不能把框架可用等同所有 GA 已迁移。

## 规则

- 动画片段以蒙太奇源时间表示，各阶段连续覆盖完整蒙太奇；实际时长来自阶段默认值或数据表行。倍率 = 源片段长度 / 实际时长。激活时解析，进行中的施法不读取热改表。
- 动画位置是动画绑定事件的唯一时钟；实际时长为分段速率的目标，边界切换有一帧量化误差。首版不支持跳段、循环、负速率和逐帧 Seek。
- 时间轴只编排，不产生伤害。现有配置事件与 Notify 事件不得同时驱动同一命中。每个事件 ID 一次施法仅执行一次，多段命中配置多个 ID。
- 事件必须核对本次播放实例和窗口。旧的无来源 GameplayEvent 不自动变成可信事件；蓝图可继续使用原监听，但迁移到校验入口时须补充实例来源。专用 Notify 适配器随事件携带来源实例。
- 阶段配置打断许可及可取消到的技能标签；首版提供策略查询，不自动实现输入缓存/技能替换事务。死亡、对象失效和强制结束始终清理，不受普通打断许可限制。
- 首版任务仅在服务器运行；ASC 的蒙太奇播放和倍率复制承担客户端动画同步。GameplayEvent、任务状态、CastId、数据表本身不自动复制。客户端预测与独立阶段 UI 快照不在本次范围。
- 任务由 GA 持有并随 GA 销毁。结束先禁用事件入口，再解绑 ASC/动画委托；只停止本次播放，不停止后续技能的蒙太奇。伤害窗口、格挡、移动占用和预警仍由 GA 的幂等 EndAbility 清理。
- 变速不改变投射物寿命、冷却或其他现实秒计时。Stander 格挡窗口改按动画结束位置移除，避免 GE 的固定时长先到期。

## 本轮记录

- 基线：工作区已有大批格式/注释与资产修改，本轮不回退或提交它们。
- 不创建独立 checkout：现有未提交资产是当前项目真实基线，切换 checkout 会丢失所需素材。
- 2026-11-06：完整Editor构建通过，最终12项原生测试全部通过。报告：`Saved/Automation/AbilityTimeline/20261106-165229/index.json`；构建日志：`Saved/Logs/AbilityTimeline-build-final.log`。
- 首次占位实现的两项测试均失败，补齐实现后通过；实际播放测试进一步复现并修复了自然混出误判、动画结束但GA继续时ASC动画占用残留、最后一帧Queued Notify来源实例已被销毁导致漏事件三个边界。
- 只读代码审查发现自然混出及结束帧推进问题，已修复并加入非零混出/终点断言。所有本轮C++文件通过clang-format检查。
- 本轮未进行渲染画面/用户PIE/双端或专服验收，不能把NullRHI真实动画运行测试等同视觉、联网验收。

## 配置与调用方法

在 UE 新建 `AbilityTimelineDefinition` DataAsset，填写 `Phases`：

| 字段 | 含义 |
|---|---|
| Name | 唯一阶段名，例如 Windup / Release / Recovery |
| SourceEnd | 该阶段在蒙太奇中的结束位置，单位秒；起点由前一阶段终点确定 |
| Duration | 希望该段实际播放多少秒 |
| Timing | 可选 DataTable 行句柄；表行结构选择 AbilityTimelineTimingRow，行内 Duration 覆盖默认值 |
| bInterruptible | 普通受击打断的策略查询值，不屏蔽强制死亡/换装取消 |
| CancelInto | 允许主动取消到的能力标签；首版只提供查询，不自动尝试激活其他GA |

例如源区间 0–0.6、0.6–0.8、0.8–1.2，目标时长分别 .3、.2、.8，则倍率为 2、1、.5。源时间 .6 的命中标记不移动，实际约 .3 秒触发。

**Stander 表事件路径**：在 `StanderWeaponActionConfig.Timeline` 中引用 DA。原 `Events.Time/Duration` 均仍是源动画秒数；时间轴生效后 `PlayRate` 不再控制播放。此路径不允许同时配置时间轴 `Events`（Notify窗口），以避免并行伤害。未引用时间轴的武器不改变执行方式。

**蓝图 Notify 路径**：

1. 保留目标数据、Commit和原伤害/投射物逻辑。调用DA的 `Validate For Montage` 检查配置合法，再扣费；失败可读取Error说明。
2. 用 `Run Ability Timeline` 替代这一次播放的 `PlayMontageAndWait`，不能两个节点各播放一次。绑定 `OnEvent` 到原业务，`OnFinished` 到统一 `EndAbility`。
3. 在蒙太奇上用 `Ability Timeline Event` 通知保留原 GameplayTag，并在 DA 的 `Events` 配置同 Tag 的源时间接收窗口。通知直接放在 Montage 上，首版不支持放在 Sequence 内的局部时间。
4. 专用 Notify 仍向角色 ASC 发送 GameplayEvent；原 `WaitGameplayEvent` 机制没有被引擎修改。迁移某个伤害事件时，将原伤害监听执行线移到任务 `OnEvent`，不要两边都执行。
  5. `OnPosition` 提供源时间，可用于窗口表现；任务本身不产生伤害。相同 Tag 每次施法只接受一次，多段攻击需多个事件 ID/Tag。

旧自定义 Notify 只发 Tag、没有播放实例来源时，不会通过新任务的严格校验。可继续走原监听，或迁移到专用适配器；不要在接收端把“当前实例ID”伪装成来源ID，这无法拒绝旧动画通知。

## 网络与生命周期

- 首版为服务器任务。`ASC.PlayMontage` 与 `CurrentMontageSetPlayRate` 更新 GAS 蒙太奇复制；没有每帧同步时间轴游标，也没有自定义 CastId RPC。客户端没有收到一次 GameplayEvent，不代表服务器漏算；客户端表现应依赖蒙太奇/Cue/既有角色表现复制。
- Dedicated Server 必须使相应骨骼动画及通知继续更新。首版没有“没有动画就用计时器补伤害”的兜底，否则会出现跳过动作仍伤害。双端、专服及丢包延迟尚需专门验收。
- `UAbilityTask_RunTimeline` 每次激活创建，由GA和GameplayTasks持有；UPROPERTY保存配置/蒙太奇，弱引用AnimInstance。快照清除DataTable引用，只保留已解析值。Notify对象属于资产且共享，不保存玩家/施法状态；每次通知创建的来源上下文在同步派发后由GC回收。
- 结束先关闭事件门，再解绑ASC事件与GA取消委托，仅解除自身蒙太奇实例的回调。自然混出仍按实例推进，不用 `Montage_IsActive` 判断已经结束。正常结束前交付终点源位置，超过 .15 秒未处理内容则失败关闭，防止卡顿后补发一整段旧攻击。
- Stander的死亡、眩晕、换装和取消仍走现有GA收尾；取消销毁本次记录的投射物，正常结束让弹体按既有生命周期继续。格挡通过动画窗口或EndAbility移除；移动总距离仍由源动画进度决定。

## 首版边界

只支持单段连续、无根运动、无循环的蒙太奇，Montage自身RateScale为1，默认自动混出。配置需保证自动混出只在最终恒定倍率阶段开始；否则校验拒绝，需缩短Montage混出或将最终阶段起点提前。首版不支持自定义提前BlendOutTriggerTime、多Section跳转、Seek和客户端预测。未来可扩展专门的位移/根运动策略，不能直接放开校验。

时间片段实际时长是目标值，分段倍率在游戏帧边界调整，允许一帧量化误差。暂停动画不是延长前摇的实现方式；不要在任务运行中再由其他蓝图修改其播放速度或位置。

这里的秒数指正常动画全局倍率下的游戏时间；世界时间膨胀会另行作用。首版不要再叠加Mesh的GlobalAnimRateScale来实现同一项攻速调整。

## 首批样例与复验

样例位置：`/Game/AbilityTimelines/Stander`，分别绑定剑盾 `DA_Attack1`、剑盾 `DA_Block`、魔法 `DA_Attack1`。样例默认保持旧时长，改阶段Duration即可调试，不改源命中位置。

**注意：出生自带剑盾不等同于已经装备剑盾物品。** 出生时左键是 `GA_Stander_Combo`，使用 `DA_Stander.Combo` 的三段动作和固定PlayRate，尚未接入新时间轴。只有在背包装备剑盾武器物品后，左键才替换为 `Weapons/Sword_Shield/GA_Attack1`，读取 `DA_Sword_Shield_Attack1`。修改这份时间轴不会改变出生默认连击，也不会把全部三个连段一起变速。

- 创建：`Scripts/GA/AbilityTimeline/author_samples.py`，完整构建后由UE执行；保存前备份被修改的三个动作DA。重跑保留已存在时间轴的调参值。
- 原生回归：`Scripts/GA/AbilityTimeline/test.ps1`，每次写入独立报告目录。
- PIE：装备剑盾，分别测试普通攻击和格挡；装备魔法武器测试发射。比较默认配置与缩短/延长Windup、Recovery后的命中姿态和时长；格挡中取消/死亡应立即清理状态，弹体正常结束后继续飞行。
- 这三项是首批接入，不代表FireBolt、StoneGolem和全部已有GA已经迁移；这些应逐个检查蓝图通知来源和业务执行线后接入。

最终测试包括：阶段源时间/实际时间映射、数据表快照及无效配置、事件身份/去重/取消、真实蒙太奇前摇Notify和尾帧Queued Notify、非零自然混出、末帧位置交付、任务结束而GA继续时动画占用释放、三个样例独立读回、实际装备授予后的慢放格挡及取消清理，以及现有Stander资产、动作和换装回归。Actor/Mass伤害结算继续复用原通路，本轮没有额外做Actor/Mass伤害数值或双端复制验收。

### 2026-11-06：Recovery 0.1 秒运行复验

用户报告出生默认左键未随 `DA_Sword_Shield_Attack1.Recovery.Duration` 变化。资产读回和用户确认表明，测试的是出生自带连击，而非装备物品授予的GA。未更改生产能力或强制给玩家换装。

在隔离临时PIE中按已解析的左键GA类通过ASC直接激活，使用正常引擎帧推进任务（没有手动调用Task.TickTask）：

| 实际GA | Recovery设置 | 实测移动锁定 | 最大播放倍率 |
|---|---:|---:|---:|
| 出生 GA_Stander_Combo 首段 | 不读取该时间轴 | 0.947秒 | 1 |
| 装备剑盾 GA_Attack1 | 0.833333秒 | 1.008秒 | 1 |
| 装备剑盾 GA_Attack1 | 0.1秒 | 0.277秒 | 8.333 |

压缩后理论总长为前摇0.116667 + 释放0.05 + 后摇0.1 = 0.266667秒；实测含帧量化误差。报告：`Saved/GAAuthoring/AbilityTimeline/recovery-investigation/pie-recovery.json`。本次不是物理鼠标输入测试；自动注入输入未成功触发后，改为按已核实的槽位对应GA直接激活。

探针位于 `Scripts/GA/AbilityTimeline/` 的 `setup_recovery_probe.py`、`run_recovery_probe.py`、`restore_recovery_probe.py`。只临时替换运行内存里的时间轴引用，测试后恢复，不保存用户调参资产；原未保存蒙太奇保持未保存状态。新增C++测试曾因调试器/编辑器DLL锁未链接，已撤去该临时测试文件；本次结论以当前加载版本的PIE证据为准。
