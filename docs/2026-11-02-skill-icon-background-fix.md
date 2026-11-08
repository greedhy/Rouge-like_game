# 技能图标白色方框修复

## 原因与修改

检查技能目录引用的 21 张纹理及共享 Widget 后，确认截图中第六批圆形技能图标的角落 Alpha 为 0；不需要重新抠图。旧 FireBolt 有 BackgroundMaterial，而第六批图标自带圆形图案，目录有意不配置背景材质。

`WBP_SpellGlobe.SetIconAndBrush` 将空 ResourceObject 的普通 Image Brush 赋给 Image_Background，背景层仍以不透明状态参与绘制，因此在透明图标四角露出白色矩形。调整覆盖顺序不能消除这一背景。

修改两个共享资产：

- `/Game/BluePrints/UI/SpellGlobe/WBP_SpellGlobe`：赋值后检查 BackgroundBrush 的资源，有效时背景 RenderOpacity=1，无效时为 0。每次赋值都更新，避免重新装备旧技能后背景无法恢复。
- `/Game/BluePrints/UI/SpellMenu/WBP_My_SpellGLobeButton`：解锁/装备路径同样检查背景材质；可学习与锁定路径恢复其原有 MI_LockedBG 的可见状态。

保留图标、玻璃、边框、冷却文字的层级和纹理引用。没有修改角色、GA 逻辑、存档或技能目录。

## 验证

通过当前 UE 编辑器 MCP 修改、编译并保存资产。PIE 中实例化真实共享 Widget，验证旧 FireBolt 和六个第六批技能，反复切换有效/空背景，调用真实 ClearGlobe 后重新填充，并覆盖菜单 Equipped、Eligible、Unlocked、Locked 状态的 15 次切换。报告全部通过；实际 Slate 截图确认七个技能球均无白色矩形。

- 诊断：`Scripts/UI/inspect_skill_icons.py`
- 修复工具：`Scripts/UI/fix_skill_icon_background.py`（已执行；检测到现有修复会停止，避免重复加节点）
- PIE 回归：`Scripts/UI/skill_icon_probe.py`
- 本次报告：`Saved/GAAuthoring/SkillIconBackground/probe-latest.json`
- 截图：`Saved/PureMass/FeedbackUI/SkillIconProbe.png`

验证针对共用 UI 实例；未逐个角色进入战斗，也未执行打包版本验证。测试未选择或写入任何存档，结束后停止 PIE。
