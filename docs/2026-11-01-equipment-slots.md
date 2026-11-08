# 十栏装备与独立元素区域（2026-11-01）

| 顺序 | 名称 | Tag | 分区 |
|---|---|---|---|
| 1 | 武器 | Equipment.Slot.Weapon | 常规 |
| 2 | 左戒指 | Equipment.Slot.Ring.Left | 常规 |
| 3 | 右戒指 | Equipment.Slot.Ring.Right | 常规 |
| 4 | 护甲 | Equipment.Slot.Chest | 常规 |
| 5 | 鞋子 | Equipment.Slot.Boots | 常规 |
| 6 | 头盔 | Equipment.Slot.Helmet | 常规 |
| 7 | 魔法石 | Equipment.Slot.MagicStone | 常规 |
| 8 | 元素1 | Equipment.Slot.Element.1 | 元素 |
| 9 | 元素2 | Equipment.Slot.Element.2 | 元素 |
| 10 | 元素3 | Equipment.Slot.Element.3 | 元素 |

标签及顺序统一定义于 EquipmentSlots；组件注册时在客户端与服务器补齐标准槽位，兼容旧 PlayerState 蓝图仅保存三个槽位的默认配置。护甲沿用 Chest，不迁移已有装备实例和物品表。自定义额外槽位仍保留。

装备面板上部为常规装备滚动列表，下部为带“元素槽”标题的独立滚动区域，不再将元素混在常规列表末尾。按 B 与背包一同打开，操作仍使用原服务器 RPC、实例 GUID、GE Handle 和快照。

制作新物品时，在 DT_Equipment 的 AllowedEquipmentSlots 中填入对应精确 Tag。通用元素物品可允许三个 Element Tag，限定位置物品只允许一个。不能只填父 Tag Equipment.Slot.Element。槽位扩展不自动生成新物品，不更改现有戒指和胸甲的允许位置。

后续元素合成可按三个槽位 Tag 读取 EquipmentComponent.Slots，并监听 OnEquipmentChanged，形成有位置顺序的组合输入。当前没有合成规则、技能授予或触发代码；元素作为装备仍需满足现有 GE 和物品校验。实现合成时需要另行管理授予/移除 GA、服务器权威和恢复幂等性。

## 验证

新增 test_25d.Equipment.TenSlots 检查标准槽位、元素分组、旧 Chest 身份、逐槽穿脱与快照恢复。运行结果在 Saved/Logs/EquipmentSlotsRegression.log；最终手动布局与双端交互仍需 PIE 验收。

本次构建成功，TenSlots 以及装备测试通过。全项目回归存在 test_25d.Stander.DashPhase 失败（其他正在开发的角色功能），不宣称全项目全绿；未修改该角色实现。界面实际显示与双端交互待 PIE 验收。
