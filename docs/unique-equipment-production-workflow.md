# 暗金与套装开发流程

本文件是后续制作入口，适用于固定暗金、装备对既有技能的局部修正，以及由不同部件组成的技能联动套装。首批实例及研究见[暗金装备指南](2026-11-16-unique-equipment-guide.md)。普通加法装备和随机词缀仍使用[装备制作流程](equipment-production-workflow.md)。

## 先确定装备合同

每批装备先填写下表，再配置资产。明确范围后可以直接按用户授权实现；流程不额外要求审批、提交或推送。

| 必填项 | 要明确的内容 |
|---|---|
| 身份与槽位 | 稳定定义ID、中文名称、允许槽位、实际查看过的图标；改显示名不改旧ID |
| 收益与代价 | 属性名、加法或乘法、倍率、曲线等级；解释0基础值及现有抗性/暴击算法的影响 |
| 技能入口 | 真实GA和节点路径、精确技能或系列身份、原费用和冷却、动作读取装备的时点 |
| 套装 | 每个不同部件的独立GrantedTag、所需标签集合、奖励由哪个规则消费、拆套后的行为 |
| 组合 | 双戒指是否允许同名、与已有暗金是否互斥、多个伤害类型/特效转换是否冲突 |
| 发行与验证 | 开发拾取入口或正式掉落接入的本次范围；资产、逻辑、画面、联机分别怎样验收 |

优先用现有规则表达玩法；新费用、充能、连锁触发、召唤、套装件数减一或额外常驻套装GE，需要单独扩展与验证。不得将这些未实现的能力描述成仅改表即可得到。

## 配置层与运行层

| 位置 | 职责与约束 |
|---|---|
| `DT_Equipment` | `FBackpackItemDefEntry`；名称/图标/允许槽位/稳定ID/GE类/效果等级。固定暗金不可堆叠、不走药水UseEffect、不随机重抽 |
| `CT_UniqueEquipmentStats` | 位于 `/Game/Equipment/Unique`，保存属性倍率与技能数值；`FScalableFloat.Value=1` 引用对应行，横轴为装备GE等级 |
| `GE_<ID>` | 位于 `/Game/Equipment/Unique/Effects`，父类 `UUniqueEquipmentGameplayEffect`；Infinite、Period=0、Stacking=None，只允许安全属性与TargetTags组件 |
| 属性Modifier | Channel1加法或暗金专用Channel2 `MultiplyCompound`；乘法因子范围[0.05,10]。2表示+100%，0.7表示−30%，不填100或−30 |
| 技能规则 | 选择精确技能或已接入系列，配置曲线、转换类型、特效与套装Tag。数值读生效Spec等级 |
| `Config/Tags/UniqueEquipmentTags.ini` | 套装部件身份；至少两种不同标签，不能用同件数量冒充两件套 |
| `Scripts/Equipment/unique_equipment_catalog.json` 与作者脚本 | 声明新装备的初始值并只补缺失资产/行；现有表格才是调参入口，重跑初始化不能覆盖设计师值 |

普通 `UEquipmentGameplayEffect` 与随机 `URandomEquipmentGameplayEffect` 仍只走原Channel1加法权限。当前Health/Mana和IncomingDamage/XP等结算属性不能成为常驻装备Modifier。属性最终计算为 `Channel1结果 × 各暗金因子`；护甲×2不等于伤害减半，0穿透×2仍为0。

服务器沿用 `Soul → E拾取 → 背包实例 → EquipmentComponent → 独立Spec → PlayerState ASC`。卸下按该实例GE句柄回收，保存GUID和装备快照，不保存Handle或把最终属性写回基础值。穿戴、卸下和替换成功后，装备组件按最终上限维持操作前的生命与法力比例。

属性曲线在装备/重建Spec时求值；技能曲线在每次动作时按生效Spec等级求值。调参后用新PIE或重新穿戴统一验证。已经发射的弹体保存本次快照；拆套和卸装影响后续动作。

## 技能与套装接入

1. 查 `MyFireBolt::SpawnProjectiles`、`StanderWeaponActionAbility` 以及对应共享动作，确认前端实际传入 `EquipmentAbilityTag`。图节点需在其配置显式选择身份，默认为空不受装备影响。
2. 投射物规则由 `EquipmentAbilityModifiers::ApplyProjectileRules` 消费：旧等级弹数先算，再加额外数量；额外数量向下取整、每条0–16、修改后总数上限32；倍率同时作用于基础伤害和DebuffDamage，散射取最大，禁追踪覆盖默认追踪。
3. 近战规则由 `EquipmentMeleeModifiers::Resolve` 消费，在 `ExecuteStrike` 接触时读取一次并冻结数值/伤害类型/命中特效；保留原动画、范围、命中次数、来源生命与取消校验。当前近战也支持 `RequiredSetTags` 和 `SetDamageMultiplier`，旧规则默认为空集合/倍率1。其他新增能力先核对实际结构体与校验，不能把投射物额外弹数等字段直接当成近战已支持字段。
4. 套装由参与部件的GE授予不同Tag，技能规则要求全部精确标签存在时用套装倍率**替代**普通倍率，而不是再乘一次。一个奖励只有一个消费方，避免每件重复奖励。
5. 同一技能的不同伤害类型或命中特效转换不能依赖Active GE遍历顺序决定胜者。当前近战冲突会拒绝本次接触动作；新装备应通过槽位互斥、不同技能身份或明确的组合设计避免玩家正常配装触发此失败。

不用装备再次激活完整GA，也不复制伤害执行器。需要扩展规则时遵循[统一GA指南](ga-unified-authoring-guide.md)：配置校验、共享动作、前端身份接线、实际行为及生命周期测试一起完成。

### 法术系列与周身特效

| 已接入系列或动作 | 实际成员和接线 | 能修改的值 |
|---|---|---|
| `Equipment.AbilityFamily.ArcaneArea` | 奥术新星 `Shape.ArcaneNova`、奥术碎片 `Shape.ArcaneShards` | 本次基础伤害、持续伤害和目标查询半径 |
| `Equipment.AbilityFamily.LightningChain` | 闪电引导 `Channel.Electrocute` | 伤害、二次索敌半径和额外连锁目标数 |
| 传统投射物 | FireBolt 的 `Delivery.ActorProjectiles` | 原 `ProjectileRule`，选择精确技能 |
| 标准近战接触 | 真实 Stander ActionTag 进入 `Combat.CharacterStrike` | `MeleeRule` 的伤害倍率、元素、命中特效和周身特效 |

- 法术 `SpellRule` 选择 `AbilityTag` 或 `FamilyTag`。系列成员在 `EquipmentAbilityModifiers::GetSpellFamily` 精确登记。新成员先确认原生前端确实传入身份、共享节点消费参数、已有图的空身份兼容，再同步作者白名单和真实技能用例。
- 精确规则和系列规则各按自己的生效装备 Spec 计算。相同字段的倍率逐件相乘，整次半径倍率限制在 0.5～2。齐套用套装伤害倍率替代该件普通倍率；同一次施法的范围、雷链和伤害在节点启动时冻结。雷链先算 GA 原有目标数再加装备额外数量，本批额外总量上限 8，整体目标预算上限 64。
- 近战可独立配置 `BodyEffect` 和所引用的 `BodyEffectDuration` 曲线，与 `HitEffect` 分开。接触动作开始时在目标循环前多播，每次接触播放一次，挥空也能看到。组件挂到施法者根组件，在 0.1～3 秒内到期销毁。相同技能出现不同周身系统时也视为冲突，制作时检查所有能同穿的组合。
- 新星现有 Cue 随实际半径标记冲击边界，碎片使用原命中 Cue。确定新装备的资产后分别检查实际伤害范围、粒子大小及其是否跟随角色。无渲染测试用于数值与状态，画面使用 PIE 验收。

本批五件可用作选型范例。 [第三批暗金与构筑](2026-11-16-unique-equipment-spell-builds.md)

## 制作与更新资产

1. 读取当前代码、旧表行和曲线键，记录工作区已有修改。并行任务存在时只改分配给本任务的文件，构建/资产保存串行执行。
2. 检查图标的实际纹理，不靠数字文件名猜用途。确认所有GameplayTag、伤害类型映射、技能路径和Niagara资产真实存在。
3. 新反射字段先完整构建。编辑器正在占用DLL或待保存资产时，请用户保存并关闭；不能用旧DLL写新字段。
4. 更新作者清单后执行 `Scripts/Equipment/author_unique_equipment.py`。作者先补曲线，再创建缺失GE，最后合并缺失物品行；修改旧包前备份，逐行/逐键证明原数据保留。
5. 同名资产已存在时读取并校验，保留策划值。结构不符合需求则失败并报告；确需修改已有结构，编写明确白名单的迁移，不把“幂等初始化”变成无条件重建。
6. 再运行一次作者，验证没有新增/变更、曲线与表行没有重复。另起Unreal进程设置 `UNIQUE_EQUIPMENT_VERIFY_ONLY=1`，核对磁盘SHA256、实际值与作者报告。

```powershell
$EquipmentProject = (Get-Location).Path
$EquipmentEditor = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$EquipmentScript = (Join-Path $EquipmentProject 'Scripts/Equipment/author_unique_equipment.py').Replace('\', '/')
& $EquipmentEditor (Join-Path $EquipmentProject 'test_25d.uproject') -unattended -NullRHI -nosplash -nop4 -run=pythonscript ("-script=" + $EquipmentScript)
# 同一命令再跑一次检查幂等；随后独立进程只读验证。
$env:UNIQUE_EQUIPMENT_VERIFY_ONLY = '1'
try {
    & $EquipmentEditor (Join-Path $EquipmentProject 'test_25d.uproject') -unattended -NullRHI -nosplash -nop4 -run=pythonscript ("-script=" + $EquipmentScript)
} finally {
    Remove-Item Env:UNIQUE_EQUIPMENT_VERIFY_ONLY
}
```

每次运行保存独立 `Saved/Equipment/UniqueEquipment/<RunId>/report.json`。检查 `success`、真实新增/保留数量、日志和独立读回结果；不能仅凭进程退出码或文件存在认定成功。

### 已有GE结构迁移

只有本次确需调整已有结构时执行。先读取并记录原GE完整字段、曲线逐行逐键、物品行和包SHA256，备份包；为获授权的字段建立白名单。例如把已有近战规则接入套装，可以只改 `MeleeRule.RequiredSetTags` 和 `SetDamageMultiplier` 的曲线引用，保留设计师已调的普通倍率、其它Modifier、显示数据及稳定ID。

迁移要求原结构匹配预期；已经等于目标结构则不操作，遇到不同结构则报告并停下该资产写入。保存前比较所有白名单外字段，保存后独立进程读回并核对。不要删除GE重建，不因修改清单后作者拒绝旧结构就把旧调参覆盖回默认值。迁移脚本是按需编写的专用工具，当前缺失初始化器不承担任意旧结构同步。

### 作者清单格式

根字段为 `schema_version: 1` 和 `items`。每件的 `stats`、`projectile`、`melee`、`spell` 数值字典生成 `<ID>_<字段>` 曲线行。同一件使用一种技能规则。已有行从引擎曲线表调参。

结构字段包括 `id/label/icon/slots/description`、独立部件 `tag`、`set_description` 和 `required_set_tags`。投射物使用 `projectile_ability_tag/disable_homing`，近战使用 `melee_ability_tag/damage_type/hit_effect/body_effect`，法术使用 `spell_ability_tag` 或 `spell_family_tag`。具体字段与安全边界由 `validate_catalog` 在写入资产前校验。新增条目参考清单里的已验证实例。

清单未声明的技能规则也必须是空默认结构；已有GE还残留旧规则时，作者会拒绝并要求定向迁移。`Scripts/Equipment/verify_unique_author_contract.py` 可在独立Unreal Python进程只读核对：所有正常条目通过、移除清单规则的反例被拒绝、资产包哈希不变。它不执行作者主函数，也不保存GE。显示说明属于可编辑策划文本，省略清单初始文案不会删除既有描述。

## 验证与交付

| 层级 | 最低覆盖 |
|---|---|
| 配置/校验 | 非法倍率、错误通道、未注册Tag、缺失曲线、空规则、不同套装部件、冲突规则；普通/随机模板不获得暗金权限 |
| 实际属性 | 真实初始化GE＋暗金，正负属性、派生属性、双戒指独立回收、换装资源比例、保存与重复恢复 |
| 实际技能 | 激活真实GA或真实共享动作的输出/扣血；至少一条真实GA身份接线验证。只手工填写Tag或检查Active GE不足以证明技能入口正确 |
| 套装与生命周期 | 单件/完整套/拆任一件/重新穿戴/读档，其他技能隔离、已发射弹体快照、取消后不补伤害 |
| 回归 | 装备/背包/对应动作专项及全项目 `test_25d`；检查新鲜报告、非零测试数、失败和未运行数 |
| 用户试玩 | Soul名称/E拾取/详情、角色动作和特效、服务器与远程客户端观察；NullRHI不能替代画面验收 |

开发入口 `Equipment.SpawnUniques` 在服务器/Standalone生成样例Soul；当前命令的具体批次以实现与当批交付文档为准。正式掉落规则、关卡布置和存档迁移按用户明确范围接入。

交付说明新增装备表、调参入口、实际构建与报告、待验收项。遇到既有测试失败，用历史报告/源码和资产指纹区分；并行任务造成的构建阻塞如实记录，不修改他人代码来伪造全绿。可复用skill维护源为 `skills/build-unique-equipment/SKILL.md`，安装副本同步后校验内容一致。
