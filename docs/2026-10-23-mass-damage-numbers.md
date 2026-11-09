# Mass 伤害数字补齐

原因：原浮字依赖 `UMyAttributeSet::ShowFloatingText → AMyPlayerController::ShowDamageNumber(ACharacter*)`。Mass 敌人没有 Character/ASC，扣血经过 `UPureMassSubsystem::DamageFragment`，此前该入口没有反馈通知。此前混合目标测试检查了 HP 和消耗，没有检查浮字，因此漏掉此问题。

当前行为（2026-11-09 更新）：

- 统一扣血入口通知伤害来源的 `APureMassPlayerController`，包含世界位置、实体 ID、结算伤害、格挡和暴击标记。直接攻击的结算伤害已经过抗性、格挡、暴击和护甲计算；飘字包含超过目标剩余生命的部分，与 Actor/GAS 的 `IncomingDamage` 显示语义一致。致死后不依赖实体继续存在。
- 实际扣血和 `UPureMassSubsystem::ApplyDamage` 返回值仍受剩余生命限制。例如结算伤害 4000、目标剩余生命 10 时，飘字显示 4000，实际移除生命及返回值为 10；该区别不改变伤害公式、击杀判断或经验奖励。
- 控制器只向攻击者所属客户端发送浮字；Listen Server 房主也支持。沿用原有攻击者可见的显示规则。
- 每 0.1 秒合并相同实体、相同格挡/暴击类型的数字，通过不可靠的表现层 RPC 发送；丢包不影响服务器扣血。单批及本地活跃浮字上限 256，限制密集战斗的表现开销。
- 客户端复用 `/Game/BluePrints/UI/FloatingText/BP_DamageTextComponent`，保留数值格式、配色、动画和自动销毁。每个本地控制器创建一个可见、不复制、无碰撞的共享浮字 Actor，组件挂在这个 Actor 上，并显式绑定该控制器的 LocalPlayer。没有新增逐怪 Actor 或怪物组件。
- 技能、来源明确的持续伤害和技能地表统一覆盖。没有攻击者的自然环境伤害不广播给所有玩家。原 Actor/GAS 浮字链路未改动。

## 2026-11-09 过量伤害显示修正

旧实现把实际移除的 HP 同时用于 Mass 飘字，导致高伤害命中剩余 10 HP 的怪物时仅显示 10，而 Actor 显示完整结算伤害。`DamageFragment` 现在分别保存 `EvaluatedDamage` 与 `Removed`：前者供数字队列使用，后者供扣血和返回结果使用。来源验证、数字批处理、死亡清理、击杀与经验去重保持原有流程。

新增原生回归 `test_25d.PureMass.Combat.DamageNumbersUseEvaluatedDamage`，覆盖上述 4000/10 场景、非致死扣血、抗性/格挡/暴击结算以及重复致死命中不再发送数字或奖励。修复前报告 `Saved/Automation/UnifiedGA/20261109T054913-0d31bb9c/index.json` 已复现两条“预期 4000、实际 10”的飘字断言失败；修复后的构建与回归结果另行记录。

## 后续可见性修复

上一轮测试只检查浮字对象、文本和位置，没有验证它真正显示到屏幕。原实现把组件直接挂在 PlayerController 下；PlayerController 默认隐藏，而 UE 的 `UWidgetComponent::UpdateWidgetOnScreen` 要求 `!GetOwner()->IsHidden()`，因此对象存在但没有加入屏幕层。此次改用上面的共享可见 Actor，并在控制器 EndPlay 时销毁。它不依赖怪物存活，也不受玩家死亡隐藏或重生影响。

`Saved/PureMass/DamageNumbers/hidden-owner-before-fix.json` 记录新增可见归属断言的修复前失败。带 UI 截图 `Saved/PureMass/DamageNumbers/visibility-shot.png` 已实看，房主视角的 Goblin 上方显示 `123`。原来的 HighResShot 不包含 Slate UI，不能作为浮字显示证据。

首轮验证记录（只证明数据链路，不证明屏幕可见）：

- `Saved/PureMass/DamageNumbers/regression-before-fix.json`：双客户端真实施法使 HP 下降，但浮字数量为 0，复现失败。
- `Saved/PureMass/DamageNumbers/build.log`：Editor 编译成功。沿用临时模块独立编译避开旧 Unity 测试命名冲突，完成后恢复本地配置。
- `Saved/PureMass/DamageNumbers/tests/index.json`：119 项原生测试全部成功，0 失败、0 未运行。
- `Scripts/PureMass/verify_damage_numbers.py` / `Saved/PureMass/DamageNumbers/pie.json`：双客户端验证实际技能浮字、非致死场景中文本与实际扣血的一致性（沿用原 UI 的整数显示）、所属客户端路由、致死一击存在浮字、持续伤害合并、房主显示、组件清理和无敌人代理 Actor。该历史测试没有验证过量伤害的数字语义，不能替代 2026-11-09 新增回归。

原生测试中的资源警告仍保留。浮字脚本会临时修改 PIE 测试对象，停止 PIE 即清理，不保存角色或关卡资产。

后续验证补充：`Saved/PureMass/FeedbackUI/mass-remote-hit.json` 直接从 C++ 读取指定 PIE 客户端的 `bAddedToScreen`，配套 PNG 捕获该客户端 Slate 视口；脚本检查文件新鲜度与屏幕层状态。`host-visible.png` 的房主伤害数字 `10` 已在图像中确认。完整原火球适配和 Actor/Mass 验证见 [原 FireBolt 修复记录](2026-10-23-legacy-firebolt-mass-fix.md)。
