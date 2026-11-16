# 装备模块接入与验收

后续扩展：[2026-11-16 暗金装备](2026-11-16-unique-equipment-guide.md)在专用固定暗金GE中增加Channel2复合百分比和受限技能规则。原普通/随机装备权限、服务器事务与按实例清理保持不变。下文首期限制不代表该专用暗金路径的当前边界。

## 已实现的接口

PlayerState 默认拥有 `UEquipmentComponent`，复用原 ASC 和 `UBackpackComponent`。每件装备在背包、装备槽、世界掉落和持久化快照间保留 `ItemInstanceId`。装备不消耗物品；换装把旧装备放回来源格，满背包可以换装但不能卸装。

权威流程：客户端提交槽位、实例与两侧版本 → 服务器校验 → 新 GE 成功应用 → 移除旧句柄 → 提交两侧状态 → 通知 UI 与既有存档监听器。装备和背包快照一并恢复。外部移除装备效果后标记 `bNeedsReconcile`，下一次合法装备操作先尝试对账，角色初始化也会对账；失败时保留记录并拒绝变更。

本期效果支持 **固定加法 Modifier 和 TargetTags GE Component**。效果必须派生自 `UEquipmentGameplayEffect`，Infinite、非周期、不堆叠；不允许执行计算、动态数值、当前生命/魔法或伤害/经验元属性。乘法、随机词条、主动/触发能力留待后续扩展，不能绕过资产校验使用。

## 配置一件装备

1. 在项目 Gameplay Tags 中定义所需装备槽 Tag，例如 `Equipment.Slot.Ring.Left`。在玩家 PlayerState 蓝图的 EquipmentComponent 默认值中，将其加入 `AvailableSlots`。
2. 新建派生自 `EquipmentGameplayEffect` 的 GE 蓝图，添加一个 Armor 加法 Modifier，例如 +5。保持默认 Infinite、Period=0、Stacking=None。
3. 在玩家现有 `BackpackItemDefinitions` 目录中新增物品项：稳定且唯一的 `ItemDefinitionId`、名称、图标、世界模型和 `PickupActorClass`；设置 `bEquippable=true`、`bStackable=false`、`MaxStack=1`、`bUsable=false`，填写 `AllowedEquipmentSlots`、`EquipEffect` 和 `EquipEffectLevel`。
4. 复用现有带 `UWorldPickupComponent` 的拾取 Actor。单件装备 Quantity 必须为 1。服务器生成多件时使用多个拾取实体，或通过背包批量添加创建独立实例。
5. 在服务器和客户端打包中包含目录、GE 和拾取类的引用。不要改原 Potion 的 UseEffect 或持续策略。

后续已添加三件表格驱动样例并接入物品目录与玩家槽位，见 [装备表格与样例](2026-10-30-equipment-tables.md)。仍未修改现有蓝图 UI 布局，也不会自动给玩家发放测试装备。

## UI 接线

- 从玩家 PlayerState 调用 `GetEquipmentComponent`。
- 装备按钮调用 `RequestEquip(BackpackSlotIndex, SlotTag)`；卸装按钮调用 `RequestUnequip(SlotTag)`。这两个入口读取本地实例 ID/版本，提交对应服务器 RPC。
- 绑定 `OnEquipmentChanged` 刷新 `Slots`，绑定 `OnRequestResult` 显示操作成功/失败；失败后按最新复制状态刷新。两侧复制到达时间可能不同，短暂旧版本请求会被服务器拒绝，不能在 UI 本地直接扣物品或施加 GE。
- `Slots` 保存已占用槽位。空槽由配置的 `AvailableSlots` 渲染，物品名称与图标仍从物品目录读取。
- `bNeedsReconcile=true` 时显示效果待恢复。不要由客户端对 ASC 补效果。

## 生命周期和兼容规则

- 普通角色在加载基础属性后恢复装备，在恢复旅行 Health/Mana 前恢复装备上限；PureMass 角色在默认属性初始化后恢复装备。
- 常规死亡和倒地保留装备记录，死亡/倒地期间拒绝换装；PureMass 原有清除全部 GE 的重生流程在新 Avatar 初始化后补建装备效果。
- 快照只保存物品与槽位身份，不保存 GE Handle。重复恢复会替换本模块拥有的效果，避免叠加。
- 旧背包快照缺少实例 ID 时生成 ID，缺少装备数组时按空装备处理。未知的已装备定义/非法槽位拒绝整个导入；旧背包未知定义沿用既有保留行为，不可被装备，待定义恢复后处理。不得把目录暂时缺失当作删除物品的依据。
- 保存 Strength/Intelligence/Resilience/Vigor 使用基础值，防止把装备加成永久写入角色基础属性。
- 穿戴、卸下和替换由服务器按操作前比例与最终上限结算生命、法力。两项独立结算，上限相同的资源无需写入。装备 GE 只配置上限或影响上限的主属性，当前资源由装备事务提交。

## 验证记录

- 初版 Editor Development 编译成功；初版装备/背包自动化执行 36 项，无失败。
- 独立审查发现并修复：基础属性存档重复加成、旅行资源恢复顺序、快照半更新通知、外部 GE 移除后的恢复路径。
- 最新修复已用 `-ModuleWithSuffix=test_25d,7301` 编译成功，独立测试进程使用新模块。项目自动化 191 项完成：100 项无警告成功、91 项带警告成功、0 失败、0 未运行；装备 6 项全部无警告成功。报告：`Saved/Automation/EquipmentFinal/index.json`，日志：`Saved/Logs/EquipmentFinal.log`。
- 用户关闭编辑器后，常规默认 DLL 构建及 `Scripts/test_equipment.ps1` 也已通过：装备/背包共 37 项，36 项无警告成功、1 项带警告成功、0 失败、0 未运行。模块清单已恢复指向 `UnrealEditor-test_25d.dll`。加 `-AllProjectTests` 可执行项目测试。
- 用户 PIE 与具体蓝图资产配置验收尚未完成。
- 2026-11-16 资源比例改动通过默认 DLL 的装备与背包回归（61/61）。用例覆盖不同与相同上限替换、失败回滚、反复换装、主属性派生上限和真实暗金资产。全项目回归 457/458 通过，唯一失败为既有 Timeline 目录校验。报告在 `Saved/Automation/EquipmentResourceRatio`。
- 同日监听服务器 PIE 中，远程客户端穿脱红宝石戒指、蓝宝石戒指和铁卫胸甲后，生命、法力与上限都与服务器一致，满值比例保持 100%。报告在 `Saved/Automation/EquipmentResourceRatio/PIENetworkFixed/Saved/EquipmentResourceRatio/report.json`。血条、蓝条画面待手动验收。

## PIE 验收清单

1. Listen Server + 远程客户端分别拾取、装备和卸装，确认操作只影响本人；若发布专服，再运行 Dedicated Server。
2. 基础护甲 10，两件分别 +5/+7：依次验证 15、22、17、10；两件同类装备也应独立移除。
3. 保持药剂效果时卸装，确认药剂不受影响。
4. 填满背包换装，确认旧装备回到来源格；直接卸装应失败且状态不变。
5. 快速重复点击、模拟延迟、倒地期间点击，不应产生复制物品、重复效果或跨玩家操作。
6. 生命为 60/100 时穿戴 +50 上限装备，验证 90/150。按同一槽替换为 +100 上限装备，验证 120/200，卸下后回到 60/100。法力也按相同步骤验证，旅行后检查保存的当前值。
7. 保存、退出重连、跨地图和死亡重生后确认实例 ID 与属性一致，主属性不能每次恢复都增加。
