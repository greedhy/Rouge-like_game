# 基础装备与专精后缀（2026-11-02）

按 build-equipment 制作，图标均从现有 EquipmentIcon 导出目视确认。新增随机装备定义，不覆盖已有固定/随机装备、药水或存档。

| ID | 名称 | 槽位 | 等级1基础范围 | 图标 |
|---|---|---|---|---|
| NoviceIronSword | 铁卫长剑 | Weapon | Strength +2～4；ArmorPene +1～3 | 33616_1 |
| NoviceBattleAxe | 裂甲战斧 | Weapon | Strength +3～5；CriticalHitDamage +2～4 | 61669_1 |
| NoviceArcaneStaff | 学徒法杖 | Weapon | Intelligence +2～4；MaxMana +10～20 | 20152_1 |
| NoviceIronMail | 铁卫锁甲 | Chest | Armor +5～8；MaxHealth +15～25 | 64755_1 |
| NoviceScholarRobe | 学者法袍 | Chest | Intelligence +1～3；MagicResistance +2～4 | 60684_1 |
| NoviceIronBoots | 铁卫长靴 | Boots | Armor +2～4；Resilience +1～2 | 51613_1 |
| NoviceGuardHelm | 守卫头盔 | Helmet | BlockChance +1～3；Armor +2～4 | 54873_1 |
| NoviceMageHat | 学徒尖帽 | Helmet | Intelligence +1～3；ManaRegeneration +0.2～0.4 | 24393_1 |
| NovicePrismStone | 棱光魔法石 | MagicStone | MaxMana +10～20；PureResistance +1～3 | 86021_1 |
| NoviceJadeRing | 翡翠戒指 | Ring.Left | Vigor +1～3；HealthRegeneration +0.2～0.4 | 25186_1 |

戒指允许左右两槽。武器仅通过属性加成生效，没有新增武器模型挂载或改变角色攻击动作。元素物品与技能合成未制作。

## 后缀

新增 20 条 Specialist<属性名> 后缀，增强对应单一属性。Weight=3，互斥组沿用属性名，避免同属性重复词缀。等级 1～9 为第一档，10～20 为第二档（数值两倍）。生命/法力上限为 15～30，恢复属性为 0.3～0.6，其余为 2～4；是初始平衡值，概率属性沿用现有属性单位。

| 属性 | 后缀 |
|---|---|
| Strength | 之蛮力 |
| Intelligence | 之睿智 |
| Resilience | 之坚韧 |
| Vigor | 之生机 |
| Armor | 之铁壁 |
| ArmorPene | 之破甲 |
| BlockChance | 之格挡 |
| CriticalHitChance | 之精准 |
| CriticalHitDamage | 之毁伤 |
| CriticalHitResistence | 之镇定 |
| HealthRegeneration | 之复苏 |
| ManaRegeneration | 之冥想 |
| MaxHealth | 之长生 |
| MaxMana | 之奥能 |
| FireResistance | 之避火 |
| ThunderResistance | 之绝雷 |
| WaterResistance | 之御水 |
| MagicResistance | 之禁魔 |
| PhysicsResistance | 之坚壳 |
| PureResistance | 之棱光 |

专精词缀优先适用自带该属性的装备；戒指和魔法石允许全部专精，防具额外允许六种抗性，武器额外允许暴击相关词缀。具体 Profile 列表写入 DT_EquipmentAffixes，可直接改表。原有通用后缀仍可被抽到，因此这不是排他的职业词缀池。

品质复用 Common/Magic/Rare；基础范围当前为常数，不自动随等级增长，只有后缀分档。旧物品保持实际 RollData。需要扩展20级以上时补充词缀档。

## 制作与试玩

- 脚本：Scripts/Equipment/author_novice_equipment.py；只添加缺失表行，不覆盖设计师同名行。
- 数据：DT_Equipment、DT_EquipmentGenerationProfiles、DT_EquipmentAffixes，均在 /Game/Equipment。
- 共用 GE_Equipment_Random，不为每件生成重复 GE。
- 新物品已通过现有 EquipmentTable 接入目录。可将 EquipmentSoulPickup 的 ItemDefinitionId 设为 BackpackItem:<上表ID>，或在地图既有 EquipmentEntranceSamples 的 EquipmentNames 追加 ID，用 E 拾取、B 穿戴。本次不改出生房间布置、不自动塞入玩家存档。
- 验证：NoviceAssets 从磁盘加载资产，每件16个种子，检查生成、穿戴属性、快照恢复不叠加及卸下全部属性还原。运行报告在 Saved/Automation/Equipment；当前结果见后续记录。实际键鼠/双端 PIE 未验收。

验证结果：Editor构建成功，装备/背包47项回归通过（46无警告、1带警告），0失败；NoviceAssets覆盖10件×16种子。重跑脚本新增数均为0，三张表哈希完全一致。当前沿用稀有品质的一前缀/一后缀上限，而旧前缀仅匹配旧样例Profile；这些新装备目前可用词缀主要为后缀，稀有装备也可能只抽到一条。需要两条后缀时应为新Profile配置独立品质规则，避免改变旧品质表。

已刷新 UBT 构建清单并确认 NoviceAssets 实际运行成功，见 EquipmentTests.log。首次46项为未包含新增测试的旧清单，最终以47项报告为准。
