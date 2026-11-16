---
name: build-equipment
description: 在 test_25d UE5 GAS 项目中制作、修改和验证装备资产，复用装备图标、DT_Equipment 元数据表及 CT_EquipmentStats 数值曲线表。用于新增装备、修改装备数值或装备资产测试。
---

# 制作表格驱动的装备

固定暗金、百分比取舍和装备技能/套装联动先读 `skills/build-unique-equipment/SKILL.md` 与 `docs/unique-equipment-production-workflow.md`；下文Channel1限制属于普通/随机装备，专用暗金子类可按其校验使用Channel2复合乘法。

在当前 test_25d 项目根目录工作。先阅读 `docs/equipment-production-workflow.md`、`docs/equipment-development-standard.md`、`docs/2026-10-30-equipment-tables.md`，并核对实际代码与资产；文档不授权扩展主动能力、随机词条或 UI 改版。

## 数据与职责

- `/Game/Equipment/DT_Equipment`：`FBackpackItemDefEntry` 行；修改名称、图标、槽位、定义 ID、GE 类和效果等级。
- `/Game/Equipment/CT_EquipmentStats`：GE 的 ScalableFloat 引用；行建议 `<ItemName>_<Attribute>`，横轴为 GE 等级。普通装备使用等级 1，ScalableFloat Value=1。
- `/Game/BluePrints/DA/DA_BackpackItemDefinitions` 的 EquipmentTable 指向装备表；Items 中仍保留原药水，不能迁移或覆盖它们。
- GE 派生自 `UEquipmentGameplayEffect`：Infinite、Period=0、Stacking=None，只支持当前校验允许的加法属性与 TargetTags。数值改表，不改共享 GE CDO 的运行时数据。
- 装备 Modifier 必须使用 Channel1（EquipmentBonuses）和 AddBase。Channel0 保留给 GE_SecondaryAttributes 的基础属性 Override；不能把装备放回 Channel0。UE 5.8 的 GameplayAbilitiesDeveloperSettings 通过 OverrideConfigSection 读取 DefaultGame.ini 的 [/Script/GameplayAbilities.AbilitySystemGlobals] 段；开关和通道别名必须写在该段，不能写到新设置类同名段。新装备参考 author_examples.py，已有三件样例可用 migrate_equipment_channels.py 保留数值迁移。
- 装备不可堆叠、MaxStack=1、不走药剂 UseEffect。已有定义 ID 被实例/存档引用，不为改显示名而重命名 ID。

## 工作流程

1. 读现有表行与 GE 引用，确定新增或编辑的准确范围。选择 `/Game/Assets/EquipmentIcon` 图标前查看实际纹理；数字文件名不能用于猜测内容。
2. 新装备先添加所需曲线行，再创建 GE 并引用该行，最后添加 DataTable 行。保持 ID 唯一，AllowedEquipmentSlots 与玩家 EquipmentComponent.AvailableSlots 匹配。
3. UE Python 通过 CSVImportFactory 的 AutomatedImportSettings 导入 CurveTable；DataTable 用 DataTableFactory（struct=`/Script/test_25d.BackpackItemDefEntry`）和 DataTableFunctionLibrary。Modifier 序列化方式参考 `Scripts/Equipment/author_examples.py`。
4. 编辑已有表时先读全表，合并目标行并保留其他行；不能用示例 JSON 整表覆盖设计师数据。`author_examples.py` 是三个样例的幂等初始化器：只创建缺失表/GE，重复执行不会重置已有表值；不是任意新增装备的同步器。
5. 保存后重新加载核对表行、图标、GE 类、曲线引用和槽位。检查 UE 日志及脚本成功标记，不以文件存在或进程启动作为成功证据。
6. 执行 `Scripts/test_equipment.ps1`，确认最新代码编译成功、报告时间更新、测试确实运行。真实资产测试 `test_25d.Equipment.AuthoredTableAssets` 覆盖样例图标、穿脱、改表后数值生效和药水目录保留；修改样例策划值时同步调整其明确预期，不删除断言。
7. 必须将真实玩家 GE_SecondaryAttributes 与装备同时加载，验证实际属性增量、卸下还原及主属性变化后的加成。仅检查 Active GE 或空 ASC 上的属性不够；真实 PIE 脚本 Scripts/Equipment/pie_attribute_probe.py 可逐件核对样例。
8. 新增装备需追加针对该资产的运行验证：装备数值、独立卸除、重复恢复。运行中已存在的 GE 不承诺随曲线表编辑即时刷新；在新 PIE 或卸装后重新装备验证。
9. 交付区分资产检查、自动化、双客户端 PIE。自动化通过不能宣称联机 UI 已验收，也不能自动授予玩家测试装备。

## 编辑器与构建

原物品目录或 PlayerState 被编辑器锁定时，请用户保存并关闭项目；不能覆盖未保存的编辑器数据。需要新反射字段时先确认构建成功，再执行资产脚本。独立后缀 DLL 可用于自动化验证，但当前编辑器仍使用旧模块，必须说明重启要求。

技能本身不要求提交、推送或更改无关物品。仓库中的此文件为维护源；若安装个人技能副本，修改后同步并验证副本。

## 随机装备（2026-10-31）

随机装备新增流程参考 `docs/2026-10-31-random-equipment-guide.md` 与 `Scripts/Equipment/author_random_equipment.py`。配置 DT_Equipment 的 bRandomizedEquipment/GenerationProfile，再配置 DT_EquipmentGenerationProfiles、DT_EquipmentRarities、DT_EquipmentAffixes；MinValue/MaxValue 可引用 CT_RandomEquipmentLevels。不要用重跑初始化脚本覆盖设计师已编辑的行。

随机装备使用 GE_Equipment_Random（RandomEquipmentGameplayEffect），EquipmentStats 注册表中的 20 项持久属性 以 Equipment.Stat.* SetByCaller 写入独立 Spec，Channel1 AddBase。固定装备仍保留原静态 GE 校验。新增其他属性必须同步扩展白名单、GE 模板、校验和测试，不能只在表里新增 Tag。

仅服务器生成一次 RollData：保存版本、ProfileId、品质、物品等级、seed、实际基础数值、词缀 ID/等级/互斥组/实际数值。拾取、换装、卸下、丢弃、复制和存档必须完整移动同一实例及 RollData；不得根据 seed 在读档/显示时重抽。旧固定装备使用空 GenerationVersion=0；禁止把已存在固定物品的定义直接改成随机类型，新增定义 ID 或显式迁移旧存档。

数值范围和权重改表只影响新生成物品，旧物品采用保存的结果。稳定的 ProfileId/定义 ID 不应随调参重命名。运行 `test_25d.Equipment.Random.*`、真实初始化 GE 组合测试以及 `pie_random_equipment_probe.py` / `pie_random_equipment_network_probe.py`；必须核对实例实际数值与玩家属性增量，不能仅凭 Active GE 存在判定成功。

## 全属性扩展

20 项持久属性的 Tag、Modifier 和显示名称由 EquipmentStats 注册表统一管理，见 docs/2026-10-31-equipment-all-attributes.md。共用随机 GE 用 author_all_equipment_suffixes.py 迁移，新增后缀只合并缺失行。随机 GE 允许非空属性子集，禁止重复属性、错误 Tag 映射及实例属性未覆盖；当前资源与结算元属性仍禁止。扩展是否验证完成以该文档最新记录为准。旧三属性 PIE 脚本不能直接验证全属性池：主属性可能产生派生加成，应更新断言或使用明确的测试实例。
