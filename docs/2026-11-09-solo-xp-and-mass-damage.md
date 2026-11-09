# 单人模式经验与 Mass 伤害数字修复

本次针对两个实际问题：普通单人模式击杀后没有经验和升级；同一左键攻击对 Actor 怪显示高伤害，对房间 Mass 哥布林仅显示 10。保留现有技能、怪物数值和用户资产调参。

## 实际入口与配置

2026-11-09 独立 UE 资产读取及最近游戏日志确认：

- `BP_LoadScreenGameMode.DefaultMap` 是 `/Game/RoguelikeGeneration/Maps/L_RoguelikeRun`，日志加载 `BP_DungeonRunGameMode`，默认角色是 `BP_DungeonPlayer`。
- 该角色的左键启动技能是原 `GA_FireBolt`，标签 `InputTag.LMB`。其伤害是 `FScalableFloat`，`Value=10`，曲线为 `CT_Damage.Ability_FireBolt`；当前用户曲线的 1 级值为 1000。引擎计算为基础系数乘曲线值；实际命中还经过等级、抗性、格挡、暴击和护甲结算。
- 房间的 `DA_DungeonGameplay.MonsterType` 引用 `DA_DungeonMassMelee`，其 `Health=10`、`XPReward=10`，护甲、抗性、格挡均为零。`DA_MassGoblin.Health=150` 是另一个资产，修改它不影响该房间当前类型。
- 出生被动是 `GA_ListenForEvent`，父类为 `UMyAttributeEventAbility`，监听 `Attributes` 及子标签，使用 `GE_EventBasedEffect` 消费 `Attributes.Meta.IncomingXP`。

这些数值是本次读取快照，不是永久平衡约束，也未被本次修复覆盖。用户看到的 Actor “4000”本身不足以反推出当时目标的全部属性；修复不会强行让不同防御属性的怪物显示相同数字。

## 经验为什么没有到账

```text
玩家 PossessedBy / LoadProgress
  -> 授予并激活 GA_ListenForEvent
  -> 原公共会话及监听器保存此时的 Dungeon RunId（尚未初始化）
地图生成完成
  -> DungeonRunDirector::InitializeRun 更新 RunId
玩家击杀
  -> Actor: AttributeSet::HandleIncomingDamage -> Die -> SendXPEvent
     Mass: DamageFragment -> RemoveMonsterInternal -> SendGameplayEventToActor
  -> XP GameplayEvent 到达玩家 ASC
  -> 旧监听器发现 RunId 改变，结束能力并丢弃事件
  -> IncomingXP GE 未施加，XP、等级和升级通知都不变
```

常驻属性订阅的寿命应属于当前 Avatar，而不是一次施法或地图生成编号。修复只为 `UMyAttributeEventAbility` 选择该寿命：同一 World、Avatar、ASC、存活状态和池租约仍须有效；切换 Avatar、死亡、休眠或销毁后旧订阅不能继续工作。取消仍由 GAS 销毁 WaitGameplayEvent 任务并解绑。

服务器收到每条合法新事件时，再局部捕获当前 Run 的严格来源令牌，调用原共享 GE 动作。该令牌不会覆盖订阅身份。普通攻击、延迟节点、投射物和目标令牌仍保持原 Run 隔离。事件接收时捕获令牌不等于追溯证明事件生产时的 Run；既有击杀发奖时序和归属规则没有在本次重写。

恢复后的后半段调用链：

```text
ReceiveAttributeEvent
  -> BuildEventMagnitudes（保留 Tag、SetByCaller、GE 等级）
  -> AbilityGameplayEffectActions::Apply
  -> GE_EventBasedEffect -> IncomingXP
  -> MyAttributeSet::HandleIncomingXP
  -> 玩家接口 -> MyPlayerState 的 XP / Level / AttributePoints / SpellPoints
  -> OnXPChangedDelegate / OnLevelChangedDelegate -> UI
```

服务器负责经验与等级写入，沿用 PlayerState 属性复制及客户端 OnRep/委托。兼容客户端监听仍不添加预测键，不新增客户端权威修改经验的权限。

## Mass “10 点伤害”的实际含义

原 Mass 路径同时把 `min(剩余生命, 结算伤害)` 用作扣血返回值和飘字，而 Actor 路径显示完整 `IncomingDamage`。因此 4000 点结算伤害击中 10 血的 Mass 怪会立即击杀，但只显示 10。

此处原火球的 Mass 路径来自 `UPureMassSubsystem::AdaptLegacyProjectile` 为 `BP_FireBolt` 安装的 `UPureMassLegacyProjectile`：移动扫掠命中实体后，读取与 Actor 路径相同的投射物 `DamageEffectParams.BaseDamage`，送入 `ApplyDamage`。没有发现该桥把曲线伤害改写成常量 10 的逻辑；10 来自后续血量截断后的显示。

统一后的含义是：

| 数据 | 示例：结算伤害 4000，剩余生命 10 | 用途 |
|---|---:|---|
| 结算伤害 | 4000 | 攻击者可见的伤害数字；已考虑防御与暴击 |
| 实际移除生命 | 10 | 扣血及 `ApplyDamage` 返回值 |
| 死亡次数 | 1 | 移除、经验、遭遇计数等既有死亡流程 |

伤害数字继续由服务器排队，经所属控制器每 0.1 秒合并发送的不可靠 Client RPC 表现。丢失数字不影响服务器扣血。没有新增怪物 Actor，也没有把显示值写回伤害逻辑。

## 验证与试玩

修复前回归报告：`Saved/Automation/UnifiedGA/20261109T054913-0d31bb9c/index.json`。三个经验用例均验证了“Run 初始化前 5 XP 正常到账、初始化后应到 18 XP 却仍为 5”；等级仍为 1、升级点数和通知均缺失。伤害测试验证了应显示 4000 却实际显示 10。替换 Avatar 拒绝旧监听的保护用例通过。

验证用例：`test_25d.Progression.*` 和 `test_25d.PureMass.Combat.DamageNumbersUseEvaluatedDamage`。经验测试使用真实监听 GA/GE 资产、PlayerState、AttributeSet，以及 Actor/Mass 真实死亡入口，验证经验、等级、奖励点、UI 委托和重复击杀去重。伤害测试同时覆盖非致死、抗性、格挡、暴击和重复命中。

最终构建及测试报告在验证完成后补充。用户 PIE 验收应重新进入普通单人模式：击杀后经验条增加，累计达到 `LevelUpInfo` 阈值后升级；左键击杀房间 Mass 哥布林时显示完整结算伤害。不同防御、格挡、暴击和技能等级仍可造成正常数值差异。
