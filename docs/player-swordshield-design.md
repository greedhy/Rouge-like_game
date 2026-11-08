# TinyHero 剑盾玩家首版设计

依据：2026-10-31玩家素材可行性检查，用户授权制定计划并按计划开发。

## 首版边界

TinyHero Polyart作为近战玩家，沿用DungeonPlayerCharacter、PlayerState ASC、现有伤害/装备数值/房间倒地复活。保留原法师入口与资产，新增明确的单人剑盾试炼入口进入生成房间。首版不改旧存档职业，不制作完整职业存档迁移或多人角色选择；这两项需要独立设计。新角色本身继续通过服务器GAS结算。

初始技能为三段普攻、防御、旋转斩。普通攻击按连续施放衔接三段，超时重置；每次只执行当前一段，松手不自动执行剩余连段。沿用输入：LMB普攻、RMB防御、1旋转斩。LMB现有点击移动/Shift空放语义保留。默认不制作翻滚。

## 首版玩法与参数

| 技能 | 行为 | 首版候选参数 |
|---|---|---|
| 三段普攻 | 面向鼠标锁定方向，近身扇形，每目标每段一次 | 物理伤害25/30/40，0耗蓝，范围160cm，角度100/110/120，连段间隔0.9秒 |
| 防御 | 定时盾防，不能同时攻击，结束/取消移除效果 | 1秒，冷却3秒，0耗蓝；复用既有格挡机制使期间必定格挡并减半伤害，全向，不声称方向弹反 |
| 旋转斩 | 原地旋转，周围敌人各一次 | 物理伤害55，耗蓝15，冷却5秒，半径200cm |

技能数值与蒙太奇/命中位置保存在专属UE数据资产 `DA_SwordShield`。三段普攻命中位置分别为0.15、0.20、0.216667秒，旋转斩为0.20秒，均为蒙太奇源时间；PlayRate改变实际等待时间。首版不赋予位移或无敌。角色基础移动速度450cm/s，当前由SwordShieldPlayer构造函数设置。

## 架构与状态合同

- 新建SwordShieldPlayer派生DungeonPlayerCharacter，仅配置模型、静态剑盾附着及近战相关状态；不新建ASC。
- 独立AnimBP/BlendSpace/蒙太奇，使用TinyHero_Skeleton与InPlace移动。复用经过验证的通用速度/状态/Slot图结构。
- 三个专属GAS身份与数据配置。近战期间阻止移动和其他攻击，服务器复核目标、阵营、房间、距离/高度、墙体。
- 命中以蒙太奇位置为准，一段最多一次；动画打断、死亡、倒地、换图后清理计时器、临时GE和移动限制。
- 防御采用按句柄独立移除的临时GE，不清除装备或其他技能效果。
- 防御GE授予 `State.SwordShield.Guarding`，伤害执行器据此复用原有格挡减半分支；不覆盖BlockChance，避免与既有Secondary属性GE冲突。守卫蒙太奇在末帧才触发淡出，GA按配置持续时间结束。
- 原素材不修改。新资产集中在 `/Game/BluePrints/Characters/SwordShield`。

## 验收

原生测试覆盖参数校验、扇形/全周范围、连段重置、死亡/取消清理和临时防御；运行验证真实模型、移动、三段命中、旋转斩、友军/墙后安全、消耗冷却和倒地复活。单人入口实际生成角色并清场。用户最后在PIE验收手感；网络远端、打包及复杂重定向不冒充已通过。

## 试玩与调参

1. 打开 `/Game/levels/LoadMenu` 并PIE，选择有效存档后进入模式选择页，点击“单人剑盾试炼”。原“单人游戏”仍走原入口。
2. WASD移动；鼠标左键对敌人普攻，Shift+左键可空放；连续施放衔接三段。右键定时盾防，数字1旋转斩。
3. 在 `/Game/BluePrints/Characters/SwordShield/DA_SwordShield` 调整三段伤害、半径、角度、Contact、PlayRate、连段窗口、盾防持续/冷却、旋转斩耗蓝/冷却及命中特效。调试时先结束PIE，避免改到临时实例。

角色、AnimBP、蒙太奇、三种GA和专属技能目录均在同一SwordShield目录。图标复用现有DeepCut、盾图标23与StormAttack。剑挂点复用源蓝图Weapon_R，旋转为UE Roll -90°（源四元数+X 90°）；盾为Weapon_L/Yaw -90°。移动BlendSpace须在改采样后重建插值数据，制作脚本已包含该步骤。

本次不把剑盾职业写入旧法师存档：继承PureMassPlayerCharacter的房间初始化与空SaveProgress实现，ASC仍来自PlayerState。职业选择持久化、多人成员各选职业、打包验证属于后续范围。

试炼跳转明确传入 `ArrivalGroup=RG_Entrance`。新存档菜单也会初始化 `Dungeon1PlayerStart1`，不能假定新存档的GameInstance出生标记为空。该参数仅覆盖本次跳转，不改存档或普通单人模式的出生位置。

## 验证记录

证据位于 `Saved/GAAuthoring/SwordShield/`：

- `Final/index.json`：完整Editor构建后15项原生自动化全部成功，含SwordShield、ArcaneNova、Rooms和Roguelike.Combat；其中8项带测试靶无模型/socket等警告，0失败。

- `combat-runtime.json`：三段分别播放Combo1/2/3，四种攻击各一次扣血，友军与范围外不受伤；盾防覆盖0.98秒后并按时移除；旋转斩消耗15。
- `lifecycle-runtime.json`：背后/墙后安全、命中前打断、法力不足、防御打断、倒地移除防御并拒绝技能、复活保留ASC并可重新攻击。
- `cooldown-runtime.json`：动作结束后冷却仍阻止施放，3秒/5秒到期后恢复。
- `movement-runtime.json`：实际位移和脚骨摆动；修复了初建BlendSpace缺失插值数据导致的滑行。
- `room-runtime.json`、`room-combat-runtime.json`：真实RoomSubsystem试炼入口进入生成地图，使用新角色及GameMode；降低靶怪血量并暂停其AI后，新GA击杀10只Actor怪物并完成房间，验证生成/伤害/死亡/清场链路，不代表正常难度平衡通过。
- `poses.json`、`pose-1.png`至`pose-4.png`：带剑盾的实际动作命中时刻截图。修正剑刃反向，并据采样将首段命中提前至0.15秒。
- `final-assets.json`：三种初始能力、三个不同图标、技能目录身份与挂点读回。

实际伤害经过项目现有护甲、抗性、暴击和格挡计算，运行测试验证单次结算而不把裸伤25/30/40/55误当成最终扣血。数值平衡、连段手感、摄像机观感及实际鼠标键盘连续操作仍由用户PIE验收。
