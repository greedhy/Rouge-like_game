# 暗金装备设计、调参与实现

日期：2026-11-16。首批范围：五件固定暗金、法师/近战各一个技能变体、一个两件套联动。数值用于验证系统和构筑方向，需要后续实战平衡。源码设计见 [设计合同](superpowers/specs/2026-11-16-unique-equipment-design.md)。

后续制作统一使用[暗金与套装流程](unique-equipment-production-workflow.md)和 `$build-unique-equipment`；[第二批](2026-11-16-unique-equipment-batch2.md)扩展近战两件套与四件新装备。本篇数值和报告保留首批语义，最新制作/回归结果见对应批次。

## 同类游戏的可借鉴机制

以下选择有代表性的构筑核心，不是 2026 年当前赛季强度排行榜。历史补丁的机制按该版本引用，避免把旧数值当成最新平衡。

| 官方案例 | 已核验机制 | 本项目借鉴 |
|---|---|---|
| Diablo III 斯奎特项链 | 未受伤时伤害最高增加100%，承受伤害最高增加50% | 收益与风险绑定同一战斗条件，促使玩家重视走位与防御。见[官方物品页](https://us.diablo3.blizzard.com/en-us/item/squirts-necklace-P66_Unique_Amulet_010)与[2.6.6补丁](https://news.blizzard.com/en-gb/article/23093788/patch-2-6-6-is-now-live) |
| Diablo III 无尽之途 | 移动时最多减伤50%，站定时最多增伤100% | 两件套改变移动/输出节奏；套装应有玩法身份。见[罗盘玫瑰官方页](https://us.diablo3.blizzard.com/en-us/item/the-compass-rose-Unique_Ring_013_x1) |
| Diablo III 皇家华戒 | 所需套装件数减少1，最低仍为2件 | 修改配装约束本身；组合空间大，适合有完整套装事务后再做。见[官方页](https://us.diablo3.blizzard.com/en-us/item/ring-of-royal-grandeur-Unique_Ring_107_x1) |
| Diablo IV 星陨头冠 | 1.3.0（2024-01-23）：陨石由耗蓝改成2充能及11–6秒充能冷却，并额外落下3颗陨石 | 同时改变技能形态与资源循环，使玩家重新选择属性。见[官方1.3–1.5补丁归档](https://news.blizzard.com/en-us/article/24140806/diablo-iv-patch-notes-1-3-1-5) |
| Diablo IV 亵渎之魂戒指 | 1.2.2（2023-11-07）：按各自间隔自动触发已装备的尸体相关技能 | 装备可改变操作循环；实现需限制频率并防触发递归。见[官方1.0–1.2补丁归档](https://news.blizzard.com/en-us/article/24092662/diablo-iv-patch-notes-1-0-1-2) |
| Last Epoch Mourningfrost 早期设计 | 2018-01-28开发博文：敏捷转为移速与冰伤收益，同时失去攻/施速收益并降低最大生命 | 重新分配属性价值，而非一律提高所有收益。该描述只代表早期设计。见[官方Unique Item Design](https://forum.lastepoch.com/t/unique-item-design/1264) |

设计归纳：暗金应有稳定的名称、机制和代价；随机稀有装备负责广泛补属性，暗金负责改变属性需求、目标选择或操作循环。大额“提高”必须说明作用对象：护甲属性翻倍不等于受到伤害减半；暴击伤害属性翻倍不等于所有最终伤害翻倍。本项目没有通用技能增伤属性，因此技能伤害倍率直接进入对应共享动作的本次参数。

## 首批装备与构筑

| 装备 / 定义ID | 槽位 | 收益 | 代价 | 构筑意义 |
|---|---|---|---|---|
| 血玻戒指 / `UniqueBloodglassRing` | 左/右戒指 | 护甲穿透属性提高120%（×2.2） | 最大生命降低30%（×0.70） | 物理爆发，需要用防御或走位补偿生命风险 |
| 磐石契甲 / `UniqueStonePactArmor` | 护甲 | 护甲提高150%（×2.5） | 最大法力降低40%（×0.60） | 防御近战，技能续航成为配装约束 |
| 灰烬冠 / `UniqueAshenCrown` | 头盔 | 最大法力、法力恢复各提高100%（×2） | 最大生命降低35%（×0.65） | 高施法容量，灰烬誓约第一件 |
| 灰烬棱晶 / `UniqueAshenPrism` | 魔法石 | FireBolt额外2弹，至少40°散射 | 每弹基础伤害与燃烧伤害降至55%，取消追踪；最大法力降低25% | 扇面清场；原等级弹数先算再加2，不增加费用或动画次数 |
| 熔誓战靴 / `UniqueCinderOathBoots` | 鞋子 | 剑盾Attack1接触伤害转火焰、基础伤害提高80%（×1.8） | 护甲降低30%（×0.70） | 从物理抗性/穿透思路转向火焰目标选择；默认和装备剑盾共用技能入口 |

**灰烬誓约（冠＋棱晶）**：FireBolt每弹由55%提高到70%，仍保留失去追踪、生命降低、魔法石法力上限代价。两种不同装备标签都必须存在。此首批是技能联动套装，没有额外常驻套装GE或通用套装计数器。

等级1 FireBolt由1弹变3弹；若全中，基础伤害总量由100%变165%，两件套210%。这是命中前的理论总量，实际受散射、敌人抗性、暴击和命中情况影响。等级越高原弹数越多，固定+2弹的边际收益越低；不能宣称所有等级均为65%总增伤。燃烧触发概率/持续时间保留，多弹产生更多独立命中机会，后续需据实战平衡。

后续候选：以护甲代价换范围的“回响战靴”、站定增伤移动减伤的两件套、将施法耗蓝转为充能的魔法石、套装需求减一的戒指。它们分别需要范围表现同步、状态计时、费用预测/回滚、通用套装事务，本批不混入。

## 运行链路

```text
DT_Equipment定义＋专属Unique GE＋数值CurveTable
  → Soul拾取保存同一个ItemInstanceId
  → 背包装备请求与版本检查
  → 服务器EquipmentComponent创建独立Spec
  → Infinite GE施加到现有PlayerState ASC
  → Modifier进入Channel2；装备标签由该GE持有
  → FireBolt/剑盾动作按精确AbilityTag读取当前生效规则
  → 本次局部参数快照 → 原共享发射/接触动作 → 原Actor/Mass伤害
  → 卸装只回收对应句柄；下次动作恢复，已发射弹体不追溯修改
```

没有第二套ASC、没有运行时修改GE/GA默认对象，也没有由装备再次激活一个完整GA。技能的输入、动画、Commit、冷却和网络执行模式保持原入口职责。近战特效复用角色现有多播 `ShowSwordImpact`；火球复用原复制弹体。

## 百分比、表格与保存语义

| 配置位置 | 修改内容 |
|---|---|
| `/Game/Equipment/DT_Equipment` | 名称、图标、槽位、稳定定义ID、GE引用、EquipEffectLevel |
| `/Game/Equipment/Unique/CT_UniqueEquipmentStats` | 五件装备全部属性倍率、额外火球数、伤害倍率、最小散射角、套装伤害倍率 |
| `/Game/Equipment/Unique/Effects/GE_<定义ID>` | Modifier对应哪个属性、技能规则的精确AbilityTag、禁追踪、套装所需Tag、近战转换伤害类型与命中特效 |
| `Config/DefaultGame.ini` | Channel0基础、Channel1加法装备、Channel2暗金最终复合乘法 |

属性公式为 `Channel1结果 × m1 × m2 …`。例如基础护甲100、普通装备加50、两件分别×2和×0.7，结果210。倍率2填写为2，不是100；0.7表示降低30%，不填写-30。百分比因子允许[0.05,10]，负数/0/非有限值拒绝。普通和随机GE继续只能使用其原有加法通路。

这批是**固定暗金**：每件有独立GUID，但不抽随机词缀、不新建RollData版本。属性Modifier在装备/重建Spec时求值，改表后用新PIE或重新穿戴验证；技能规则则在每次动作时按活动Spec等级读曲线，下次动作可读到新值，在途弹体不追溯更新。为使整件装备的属性和技能参数一致，调参后建议重新穿戴。随机装备仍使用生成时保存的数值，旧实例不会因为此批改动重抽。穿戴、替换或卸下时，服务器按最终上限维持原来的生命和法力百分比，见[装备接入与验收](2026-10-30-equipment-guide.md#生命周期和兼容规则)。

倍率放大的是已有属性，血玻戒指对0护甲穿透仍得到0，因此它需要搭配已有穿透基础/词缀。恢复属性同理：0乘2仍为0。

两枚同类型血玻戒指允许按现有双戒指规则穿戴：收益与代价都逐件相乘（穿透×4.84，最大生命×0.49），不算作灰烬套装。以后若要限制同名暗金只能一件，应明确变更装备事务校验与旧存档策略。

技能参数的浮点曲线也在GE等级读取；额外弹数向下取整，单条规则0–16，修改后的总弹数最多32。无技能身份的旧动作不进入装备规则。近战规则仅精确匹配剑盾Attack1，其它剑盾技能、其它武器和法师技能不会被战靴改伤害。原技能窗口仍显示基础配置，暗金修正由装备详情说明；本批未改技能菜单的动态预览。

非法GE/规则会被装备校验拒绝。如果在运行中直接把已生效GE引用的曲线改坏，投射物忽略非法规则；近战遇非法或互相冲突的伤害类型/命中特效转换则终止本次接触动作，避免按容器顺序决定元素。首批没有冲突规则，后续新增装备应覆盖组合冲突测试。

## 制作与测试入口

作者脚本：`Scripts/Equipment/author_unique_equipment.py`。只创建缺失资产及表行，保留旧表和已有调参；生成前备份被修改的旧装备表。设置 `UNIQUE_EQUIPMENT_VERIFY_ONLY=1` 时只读验证，必须另起进程读回磁盘资产。

开发控制台命令 `Equipment.SpawnUniques`：仅在服务器/Standalone游戏世界中，向第一个玩家附近生成可拾取Soul，不直接发到背包；Shipping构建不注册此命令。首批实现固定生成五件，第二批改为按当前目录枚举全部有效固定暗金。按E拾取，再在现有装备菜单穿戴。正式掉落池仍只选择已有随机装备，因此这些固定暗金不会改变精英/Boss/清房保底。

## 验证记录

| 验证阶段 | 实际结果 | 证据 |
|---|---|---|
| 初始RED | 6项按预期失败，证实旧实现没有暗金乘法、百分比展示和投射物变体 | `Saved/Automation/UniqueEquipment/Red/index.json` |
| 近战RED | 1项按预期失败，证实旧接触动作没有装备元素转换及倍率 | `Saved/Automation/UniqueEquipment/MeleeRed/index.json` |
| 新代码构建 | GreenBuild与AssetTestBuild均成功 | `Saved/Logs/UniqueEquipment-GreenBuild.log`、`Saved/Logs/UniqueEquipment-AssetTestBuild.log` |
| 原生近战集成测试构建 | NativeBuild成功，包含真实GA、Timeline与装备事务测试 | `Saved/Logs/UniqueEquipment-NativeBuild.log` |
| 真实资产制作 | 新建5个暗金GE和1张数值CurveTable；原装备表27行保留，合并新增5行 | `Saved/Equipment/UniqueEquipment/20261116T104115157787Z_56452/report.json` |
| 作者脚本幂等 | 再次运行成功，已有资产与策划值保留，表行不重复追加 | `Saved/Equipment/UniqueEquipment/20261116T104223240030Z_85656/report.json` |
| 独立进程读回 | 只读验证成功，核对磁盘资产、目录、曲线、技能规则、图标及表行 | `Saved/Equipment/UniqueEquipment/20261116T104239732598Z_9268/report.json` |
| 相关回归 | 68/68通过；涵盖装备、技能规则及相关共享动作 | `Saved/Automation/UniqueEquipment/Focused/index.json` |
| 全项目回归（追加真实近战GA用例前） | 433项中432项通过，1项既有目录校验失败；不能标为全量通过 | `Saved/Automation/UniqueEquipment/Full/index.json` |
| 独立进程复核 | 新增真实剑盾Attack1集成测试通过；目录校验再次复现相同9项错误 | `Saved/Automation/UniqueEquipment/NativeAndCatalog/index.json` |
| 最终相关回归 | 69/69通过，包括10项暗金专项和新增真实近战GA用例；0失败、0未运行 | `Saved/Automation/UniqueEquipment/FinalFocused/index.json` |

全项目唯一失败为 `test_25d.UnifiedGA.Catalog.AllConfigurationAssets`。其报告包含9个Timeline缺少“校验器可识别的蒙太奇引用”，**不等于9个运行时技能损坏**。本次结果与11月14日已有全项目及独立进程报告一致；9个资产的磁盘MD5及错误文本逐项相同，证据保存在 [历史基线对比](../Saved/Equipment/UniqueEquipment/timeline-baseline-comparison.json)。

- 根目录的 `DA_Magic_Attack1`、`DA_Sword_Shield_Attack1`、`DA_Sword_Shield_Block` 是旧目录布局保留路径；按武器分类的新路径 `Magic/DA_Attack1`、`Sword_Shield/DA_Attack1`、`Sword_Shield/DA_Block` 均有真实WeaponAction引用，并通过本次校验。尚未进行完整引用清理，因此本次不删除这三份资产。
- `Sword_Shield/DA_Stander_Combo`、`Dash`、`Guard`、`Lift`、`ShieldBash`、`Spin` 六份时间轴由旧 `GA_Stander_*` CDO的 `Timeline` 字段引用，历史独立读回记录包含其真实蒙太奇。当前目录测试仅扫描 `UStanderWeaponActionConfig::Timeline`，未扫描这类GA CDO，形成校验覆盖缺口；不能将六份资产直接认定为孤立或无效。

本次未通过修改旧Timeline、删除资产或放宽断言掩盖该失败。后续应单独补齐目录校验对GA CDO及蒙太奇变体的引用识别，再决定旧路径清理方案。原生近战集成测试使用真实背包装备事务、生产GA_Attack1及其蒙太奇/Timeline，核对火焰伤害倍率、正常结束和卸装后的物理抗性恢复。

最后拟做一次“移除近战身份传递应失败”的变异验证时，工作区并行新增的 `GuardParryComponent.cpp` 调用受保护的 `AEnemyCharacter::InvalidateEnemyAttack` 导致构建错误。已取消该轮构建、逐字恢复暗金接线并核对SHA256一致；变异测试未执行，不记为已验证。用户确认另一个任务正在开发格挡/弹反，要求保留其修改，因此本任务未改动该模块。以上成功测试基于已成功构建的暗金DLL，不能据此宣称包含并行新增源码的当前工作区已完整构建通过。被测DLL哈希见 `Saved/Equipment/UniqueEquipment/validated-native-dll.json`；中止构建日志为 `Saved/Logs/UniqueEquipment-MutationBuild.log`。

用户手动PIE画面、双客户端视觉同步及实战平衡尚未验收。已有NullRHI自动化和资产读回验证逻辑与配置，不替代这些验收。
