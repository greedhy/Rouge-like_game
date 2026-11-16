# 第二批暗金：雷铸誓约、暴击与续航

本批按[暗金与套装制作流程](unique-equipment-production-workflow.md)及 `skills/build-unique-equipment/SKILL.md` 制作。用户已授权整理流程并继续其他装备开发；以下四件是本批有限实现，数值作为试玩基线。

## 装备合同

| ID / 名称 | 槽位 / 已查看图标 | 属性收益与代价 | 技能 / 套装 |
|---|---|---|---|
| `UniqueStormforgedHelm` 雷铸战盔 | Helmet / `41139_1` 蓝金头盔 | 最大法力×0.70 | 剑盾 `Skill7_Around` 接触伤害转雷，基础×1.6；雷铸誓约第一件 |
| `UniqueStormboundMail` 风暴锁甲 | Chest / `64755_1` 银色锁甲 | 最大生命×1.6、护甲×0.65 | 雷铸誓约第二件，仅提供属性和部件标签 |
| `UniqueGamblersRing` 孤注赌戒 | 左/右Ring / `25186_1` 绿宝石戒 | 暴击率×2、暴击伤害属性×1.8、护甲×0.60 | 赌暴击爆发，牺牲物理防御 |
| `UniqueStillwaterStone` 静海魔石 | MagicStone / `44769_1` 蓝绿球 | 法力恢复×3、最大法力×1.5、暴击伤害属性×0.50 | 以暴击爆发换持续施法续航 |

雷铸誓约：`Equipment.Unique.StormforgedHelm` 与 `Equipment.Unique.StormboundMail` 两个精确部件标签都生效时，由战盔规则将旋转攻击倍率从1.6替换成2.3。不是1.6再乘2.3；锁甲不重复执行技能规则。拆任意件后下次接触按剩余规则计算。

目标真实技能为 `/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/GA_Skill7_Around`，精确身份 `Abilities.StanderWeapons.Sword_Shield.Skill7_Around`，当前默认键1。旧熔誓战靴只影响Attack1，因此可同时穿戴，两种转换不会竞争同一个接触动作。保持原费用、动画、接触次数、360°范围与半径。雷伤转换不额外附加眩晕或燃烧。

雷击表现采用已有Niagara候选 `/Game/Art/CompositionEffects/NS_StormSeal_Pulse`，制作时校验真实类型，沿原 `ShowSwordImpact` 多播显示；画面适配须单独PIE验收。

暴击“属性×1.8”不是总伤害×1.8；0暴击属性仍为0，实际效果遵循原暴击公式。两枚同名赌戒沿既有双戒指规则各自相乘，暴击属性与护甲代价都叠加。静海魔石与灰烬棱晶争用魔法石槽，构成弹幕与续航之间的选择。主属性/派生值不通过装备手工反算。

## 最小实现

- `FEquipmentMeleeRule` 增加 `RequiredSetTags` 和 `SetDamageMultiplier=1`；空规则和原五件GE默认兼容。
- 校验倍率范围和至少两种部件Tag；`EquipmentMeleeModifiers::Resolve` 按本次ASC标签快照与独立Spec等级选择倍率，保留原冲突拒绝及失败不修改输入的行为。
- 装备详情增加近战套装实际倍率说明。
- `author_unique_equipment.py` 从 `unique_equipment_catalog.json` 读取全部九件初始配置；生成与校验都使用逐件技能身份、元素、套装和特效，不再硬编码首批规则。旧GE/表行/曲线值保留。
- 新装备复用 `DT_Equipment` 与 `CT_UniqueEquipmentStats`；清单只提供缺失项默认值，后续调参在引擎表格进行。
- 开发命令按实际目录枚举固定暗金并分圈生成Soul，新增件自动可试玩；不额外改地图或正式随机掉落池。

## 验证合同

1. 近战套装逻辑先观察RED，再实现选择逻辑，覆盖单件/齐套/拆套/同件重复/抑制/等级/冲突及当前挥击快照。
2. 真实新资产从目录加载并穿脱，验证实际初始化属性、资源夹取、重复保存恢复及原九件各自独立。
3. 激活生产 `GA_Skill7_Around`、推进真实蒙太奇和Timeline，读取当前DA事件伤害D；中立无暴击目标物抗50%、雷抗0时，原技能0.5D、单盔1.6D、齐套2.3D，拆套和卸装恢复。与旧战靴共存时仍分离技能身份。
4. 作者初始化、幂等、独立进程读回；保留旧表行与曲线键，记录包哈希与备份。
5. 新鲜专项及全项目回归；skill结构校验、独立使用演练、安装副本一致性。PIE画面、双端同步和实战平衡单列。

## 实际结果

| 阶段 | 实际结果 | 报告 |
|---|---|---|
| Skill | 维护源和个人安装副本校验通过、SHA256一致；完成独立使用演练，补充作者清单与定向迁移入口 | `Saved/Skills/UniqueEquipment/skill-validation.json` |
| 近战规则RED | 新增3项均按预期失败，暴露旧逻辑缺少套装选择/校验 | `Saved/Automation/UniqueEquipment/Batch2Red/index.json` |
| 规则GREEN与旧入口 | 4/4通过，包括近战套装3项和原Attack1真实GA测试 | `Saved/Automation/UniqueEquipment/Batch2CoreGreen/index.json` |
| 最终源码构建 | Editor构建成功 | `Saved/Logs/UniqueEquipment-Batch2-FinalBuild.log` |
| 资产制作 | 新建4个GE、追加11条曲线和4个物品行；原32行保留，现为36行 | `Saved/Equipment/UniqueEquipment/20261116T111606529071Z_53340/report.json` |
| 幂等 | 重跑无资产修改、无新增行，原36行保留 | `Saved/Equipment/UniqueEquipment/20261116T111709741508Z_52864/report.json` |
| 独立读回 | 新进程核对9件暗金、清单和所有包SHA256，成功 | `Saved/Equipment/UniqueEquipment/20261116T111727251665Z_185824/report.json` |
| 作者缺陷RED/GREEN | 审查发现“清单撤销规则却漏检旧GE残留”问题；旧函数2例均漏检，新函数2例均拒绝，11个包哈希全程不变 | `Saved/Equipment/UniqueEquipment/AuthorContract/20261116T111817712549Z_158132/report.json`、`20261116T111835243483Z_41744/report.json` |
| 本批相关回归 | 73/73通过（含14项暗金专项），0失败、0未运行 | `Saved/Automation/UniqueEquipment/Batch2Focused/index.json` |
| 全项目回归 | 455项中454通过、1项既有目录校验失败，0未运行 | `Saved/Automation/UniqueEquipment/Batch2Full/index.json` |

真实 `StormSetTimelineIntegration` 已验证齐套/拆套/重穿/重复读档、法力费用、范围和正常结束。目标物抗50%、雷抗0%、火抗25%，新Skill7与旧Attack1的实际扣血分别按雷/火抗计算，证明身份隔离和真实元素转换。近战逻辑测试还覆盖同次挥击中途拆套不改变已冻结参数、下次挥击恢复。

当前共9件固定暗金，开发控制台 `Equipment.SpawnUniques` 按目录枚举后在服务器玩家附近分圈生成Soul；按E拾取并穿戴。元数据仍改 `DT_Equipment`，数值仍改 `/Game/Equipment/Unique/CT_UniqueEquipmentStats`。作者JSON是新增默认值，不是覆盖同步既有策划值的工具。

全项目唯一失败仍为 `test_25d.UnifiedGA.Catalog.AllConfigurationAssets` 的相同9个既有Timeline引用校验问题；详见[首批验证记录](2026-11-16-unique-equipment-guide.md#验证记录)及历史基线对比，本批未修改这些资产或放宽断言。此前并行格挡模块的编译阻塞已经解除，本批最终构建成功，未改动该模块。

用户PIE画面、双客户端特效和最终平衡仍待验收，NullRHI与资产读回不替代这些项目。修改保留本地main，未提交或推送。
