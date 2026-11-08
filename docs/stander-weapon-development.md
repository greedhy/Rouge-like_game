# Stander 武器装备与技能组

## 范围与入口

沿用现有 PlayerState ASC、背包实例和装备事务。源素材保留；新资产位于 `/Game/BluePrints/Characters/Stander/Weapons`。装备某组武器后切换手持模型、移动 AnimBP 与全部武器 GA；卸下后恢复原 Stander 演示基线和被占用的非武器技能槽。

| 动作组 | 装备定义ID后缀 | 独立GA数 |
|---|---|---:|
| Sword_Shield 剑盾 | StanderWeaponSwordShield | 17 |
| Hammer 战锤 | StanderWeaponHammer | 17 |
| DualBlades 双持刀刃 | StanderWeaponDualBlades | 17 |
| Bow 弓 | StanderWeaponBow | 17 |
| Pistol 手枪 | StanderWeaponPistol | 19 |
| Magic 施法动作组 | StanderWeaponMagic | 17 |
| Spear 长矛 | StanderWeaponSpear | 17 |
| Staff 法杖 | StanderWeaponStaff | 17 |
| Fighter 徒手格斗 | StanderWeaponFighter | 17 |
| Rapier 刺剑 | StanderWeaponRapier | 18 |
| DoubleBlades 双刃动作组 | StanderWeaponDoubleBlades | 18 |

完整ID前缀为 `BackpackItem:`。Magic/Fighter没有对应实体手持模型，切换时隐藏旧武器；没有虚构新的模型资产。428个源动作的用途与辅助阶段见 `stander-weapon-animation-inventory.md`、`Scripts/GA/StanderWeapons/clip-design-mapping.json`。191个独立GA不代表428个文件都应成为独立伤害技能。

展示地图：`/Game/BluePrints/Characters/Stander/Weapons/L_StanderWeapons`，摆放11个实际装备拾取物。已有生成房间入口的 EquipmentEntranceSamples 也会在 Stander 试炼中追加这11种武器，其他角色不追加。通过既有 E 拾取、背包和武器槽完成换装。

## 技能选择

每组默认使用 LMB、RMB、1、2、3、4。全部额外动作一并授予为 Unlocked，打开技能菜单的 **Weapon Skills** 页，选择动作后使用现有装备技能按钮选快捷槽；每组默认六槽使用不同动作图标。武器技能不消耗永久技能点，不通过普通技能学习/存档列表授予。

旧武器能力按实际授予句柄取消和回收。换武器使旧投射物失效；换戒指不影响武器投射物。正常完成施法后的弹体继续飞行至命中或寿命结束。冷却GE在同一ASC的换装期间保留，不通过重新装备重置。跨新ASC的旅行冷却快照不是本次扩展内容。

## UE中调整参数

- 每组 `DA_WeaponSet`：Family、Abilities（全部GA及默认InputTag）、AnimClass、主副手模型、挂点和变换。
- 每个动作 `DA_<动作>`：显示名、耗蓝/冷却、Montage/PlayRate和按时间排序的Events；`GA_<动作>`引用该配置。
- Melee事件：Damage、Radius、Angle、DeathImpulse。冲量使用角色局部前/右/上方向，环向攻击按受害者方向向外施力。
- Projectile事件：Radius是弹体碰撞半径，射程由ProjectileSpeed×ProjectileLifetime决定，LaunchYaw支持环向发射。以锁定朝向附近15度锥体内最近的可见敌人校准手部/腾空发射偏移，仍受墙体和房间约束。
- Move事件：Time/Duration、MoveDistance、MoveHeight；分步扫掠，不越过墙、房间边界或不可行走地面。跳跃采用胶囊弧线，取消时恢复下落。
- Guard事件：仅指定动画窗口增加格挡能力；结束、打断或换装都清除对应效果。
- 装备行在 `/Game/Equipment/DT_Equipment`；共享 `GE_WeaponTraining` 首版提供固定+1护甲，攻击伤害由动作配置决定，未做最终平衡。

源动作文件中的 Slow/Stun/Pull 不表示已实现对应控制效果，游戏中以“招式N”显示；当前没有凭名称追加减速、眩晕或牵引。击杀布娃娃冲量不等同于活体击退。

## 验证记录

- 原生构建已通过；`Saved/GAAuthoring/StanderWeapons/NativeFinal`：35项通过，其中13项带警告，0失败。
- `readback.json`：11组191技能、图标、装备行、配置和动画引用读回通过。
- `showroom-readback.json`：11拾取物位置、真实物品身份和场景照明读回。
- `runtime-26353958b9524f7c8ebbbfb6db4661e6.json`：191个GA全部运行通过，包含真实装备授予、蒙太奇、耗蓝、重复激活拒绝、Actor/Mass攻击、友军/范围外保护及状态清理。
- `locomotion-ui.json`：11组全部通过，验证实际移动速度与脚骨运动、对应AnimBP、完整武器技能列表、描述选择及服务器快捷槽换绑。探针使用GameplayTag序列化值比较，避免Python结构体对象比较误判。
- `runtime-lifecycle.json`：3项通过，验证腾空取消并落地、已发射箭矢在换武器后清理，以及Mass致死后Hips实际物理速度；无清理错误。
- `trial-entrance.json`：实际进入L_RoguelikeRun，确认Stander玩家及11个武器物品ID全部存在；使用隔离自动化存档。
- 最终描述与只读槽查询接口改动后构建通过，菜单/移动测试在该版本执行。原生35项及191技能测试在此前同一战斗实现上通过。
- 自动化已结束并恢复展示地图。最终视觉、逐帧接触表现、操作手感和数值平衡待用户PIE验收；未宣称完成多人联机验收。

## 以后扩展

先更新源动作用途清单和设计manifest，再配置独立GA与WeaponSet/DT行。资产脚本按AuthoringOwner保护已有对象并备份共享目录；独立 `Config/Tags/*.ini` 使用 `GameplayTagList=`，不要套用Default层级文件的 `+` 前缀。多段蒙太奇必须调用原生长度刷新并读回，BlendSpace需要刷新缓存。最后完成原生与真实装备运行测试，再交用户PIE验收。
