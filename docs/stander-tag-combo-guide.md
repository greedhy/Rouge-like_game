# 标准近战普攻连招与 GAS 预测对照

当前 `BP_StanderPlayer.DefaultWeaponSet` 已指向剑盾，所以出生玩家也会走武器组的三段。默认剑盾武器组与旧 `GA_Stander_Combo` 启动能力共用 Tag 接续。左键按下启动第一段，预输入最多保存一个。正式连招的伤害、蒙太奇启动和阶段裁决都由服务器执行。

## 正式连招

- `UStanderComboComponent` 保存于当前 Avatar。蓝图可调用 `RequestComboInput(InputTag)`，输入经已生效的 ASC 槽位路由。武器动作按当前装备实际授予的 Spec 和 `ActionTag` 匹配；旧启动能力按 `StageTag` 从原三段配置取动作。
- 默认转移为 `Combo.Stage.Light1 + InputTag.LMB → Light2`、`Light2 + LMB → Light3`。可在组件默认值配置精确的后续输入。额外输入若已有独立技能绑定，保留原槽位的处理权。
- 接触后且本段剩余不超过 0.15 动作秒时接收一次预输入。时间轴正常结束后才允许接续，预输入在结束回调退出后消费。未预输入可在 0.9 动作秒内再按。挥空仍可接续。
- 真实结束发放下一段的 `State.Combo.Ready.*`，同时约束当前连招、已授予 Spec、装备授予代次和化身。消耗、失效、死亡/倒地或中断后移除。第三段完成回到入口；换装或切换到另一项主动动作使旧接续资格失效。
- 装备的三段仍读取各自的 Timeline 和原动作事件，旧启动能力读取原 `MontageVariants`。攻击命中仍执行共享 `Combat.CharacterStrike`。已调短的第一段 0.10 秒 Recovery 和第三段约 0.089 秒命中后区间均直接使用原资产，第二段按现有配置保留。没有为此提交资源调参。

## 实验设计

`Scripts/GA/StanderCombo/test_prediction.ps1` 在回环地址启动两个独立 UE 进程。开发用 GameMode 只在明确启动参数下授予探针 GA，两个对照采用相同的 `LocalPredicted` 网络策略和三个独立 Spec。拥有者在同一预测作用域依次激活三个 GA，服务器逐条处理 GAS 原生激活 RPC。区别是后续 GA 是否要求前段授予的 `ActivationRequiredTags`。

| 故障注入 | 前置 Tag | 权威端接受的段 | 客户端结束的段 |
| --- | --- | --- | --- |
| 无 | 有 | 1、2、3 | 无 |
| 拒绝第一段 | 无 | 2、3 | 1 |
| 拒绝第一段 | 有 | 无 | 1、2、3 |
| 拒绝第二段 | 有 | 1 | 2、3 |
| 拒绝第二段 | 无 | 1、3 | 2 |

两组都产生独立的 `Current` 键并共享首段的 `Base`，父键被拒绝时两组均能观察到子键拒绝回调。无前置 Tag 时服务器仍可接受后续段，子键回调和 GA 结束是两个不同的观测量。用前置 Tag 让服务器逐段给出拒绝后，对应的客户端 GA 实例也结束。

实验覆盖 GAS 的同步激活预测和拒绝。正式普攻的异步缓存和蒙太奇推进使用服务器裁决，拥有者播放来自现有蒙太奇同步。将来若让拥有者先播放正式普攻，需要为异步接续补独立预测作用域和应用层清理，并单独复测蒙太奇实例、费用和其他本地表现。

## 复验

```powershell
& Scripts/GA/Unified/test.ps1 -SkipBuild -Filter 'test_25d.StanderCombo+test_25d.AbilityTimeline+test_25d.Stander+test_25d.Player.BasicInput+test_25d.UnifiedGA.GuardParry'
& Scripts/GA/StanderCombo/test_prediction.ps1 -IncludeProduction
```

第一个命令验证真实资源的三段选择、短时间轴、按住输入门禁、缓存、中断重置、到期和旧启动 GA。第二个命令包含五项预测对照，并另外从远端拥有者的标准 ASC 输入入口按下左键，读回正式人物在双端的真实蒙太奇顺序。报告保存在 `Saved/GAAuthoring/StanderCombo/` 及 `Saved/Automation/UnifiedGA/`。

- 最终构建与 33 项相关回归通过。报告 `Saved/Automation/UnifiedGA/20261116T151738-9af3e855/index.json`。
- 双进程回环验证通过。五种预测场景和正式角色双端 `1→2→3→1` 见 `Saved/GAAuthoring/StanderCombo/net-00a91039357742c78173162b0c59f1ba/result.json`。验证入口是正式 ASC 按下输入，过程无渲染，尚无实机网络延迟和画面验收。
- 独立 UE 实例读回与设计基线一致。记录 `Saved/GAAuthoring/StanderCombo/implementation/asset-verification.json`。
- 全量回归 458/462。三项暗金测试缺少当前资源，一项旧时间轴目录校验报告没有作者蒙太奇引用。报告 `Saved/Automation/UnifiedGA/20261116T151517-b246dd5d/index.json`。
