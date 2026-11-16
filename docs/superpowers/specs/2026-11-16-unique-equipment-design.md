# 暗金装备：百分比代价与技能构筑

日期：2026-11-16。用户要求联网分析同类游戏设计、给出项目方案并完成一定范围实现；以下首批数值为实现者的验证基线，不是正式平衡结论。研究来源和完整装备方案见 `docs/2026-11-16-unique-equipment-guide.md`。

## 目标与范围

复用现有目录、Soul 拾取、背包、服务器装备事务和 PlayerState ASC，制作五件固定暗金、法师与近战各一个技能变体、一个两件套联动。用户已明确首批两职业都需要技能变体。所有倍率和技能数值引用 CurveTable；不改已有随机装备的 RollData 版本，不给所有 GA 批量加分支，不改正式掉落概率。

| 稳定 ID | 名称 / 槽位 | 正面与代价 | 技能 / 套装 |
|---|---|---|---|
| UniqueBloodglassRing | 血玻戒指 / 任一戒指 | 护甲穿透 ×2.2，最大生命 ×0.70 | 物理输出取舍；穿透是现有属性倍率，不承诺最终伤害 ×2.2 |
| UniqueStonePactArmor | 磐石契甲 / 护甲 | 护甲 ×2.5，最大法力 ×0.60 | 高防御低资源；沿用现有护甲减伤算法 |
| UniqueAshenCrown | 灰烬冠 / 头盔 | 最大法力 ×2.0，法力恢复 ×2.0，最大生命 ×0.65 | 灰烬誓约第 1 件 |
| UniqueAshenPrism | 灰烬棱晶 / 魔法石 | 最大法力 ×0.75 | FireBolt 额外 2 枚、每弹基础伤害和燃烧伤害 ×0.55、最小散射角 40°、取消追踪；与灰烬冠同穿变为 ×0.70 |
| UniqueCinderOathBoots | 熔誓战靴 / 鞋子 | 护甲 ×0.70 | 默认/装备剑盾的 Attack1 接触伤害转火焰，基础伤害 ×1.80；范围、次数、动画不变 |

首批固定暗金不抽随机词缀；GUID 沿用现有物品身份，属性曲线在装备/重建Spec时读取，技能曲线在每次动作时按生效Spec等级读取，不具有随机装备“生成数值永久冻结”的语义。两枚同种戒指仍按现有双戒指规则独立生效，正负倍率均逐件相乘。两件套要求两种不同件的 Tag，不能靠同件数量凑套装。

## 方案取舍

1. 仅现有加法 GE：接入最少，但无法实现明确百分比或技能构筑，淘汰。
2. **选择：专用固定暗金 GE + 原有共享技能动作**。保留单实例一个 GE 的成熟事务；专用类型受限允许乘法，技能扩展只改变单次发射的局部参数。
3. 全面随机暗金、通用触发图、套装计数与额外 GE 事务：后续可发展，但需版本化 RollData、触发预算、效果归属与存档迁移。本批不建设这些尚无验收需求的系统。

用户已授权有限实现，本批按第二项执行。研究、实现和验证均在本地 main，保留已有未提交修改，不提交或推送远端。

## 属性与 GE 合同

新增 `UUniqueEquipmentGameplayEffect : UEquipmentGameplayEffect`。普通固定及随机装备规则保持原状；只有专用暗金固定 GE 允许静态/曲线 `Channel2 + MultiplyCompound`，倍率有限且在 [0.05, 10] 内。Channel1 AddBase 继续允许。禁止当前 Health/Mana、伤害/经验元属性、周期、堆叠、Execution 与任意有副作用组件；仅保留 TargetTags 组件。

最终值为 `Channel1 输出 × m1 × m2 …`，Channel0 仍为基础属性，Channel1 为装备加法。倍率 2 表示 +100%，0.7 表示 -30%，与百分比点加法明确区分。Modifier 仍由静态 GE 定义，数值引用 `/Game/Equipment/Unique/CT_UniqueEquipmentStats`。

## 技能规则合同

暗金 GE 持有 `FEquipmentProjectileRule`：`AbilityTag`、`ExtraProjectiles`、`DamageMultiplier`、`MinimumSpread`、`bDisableHoming`、`RequiredSetTags`、`SetDamageMultiplier`。四个数值用 FScalableFloat（额外数量向下取整）；无 AbilityTag 表示没有技能规则。附 `UniqueDescription` 和 `SetDescription` 供装备详情显示，数值描述由实际曲线生成以避免改表后文案过时。

共享动作配置 `FAbilityActorProjectileSettings` 新增 `EquipmentAbilityTag`，默认为空，不改变旧资产。FireBolt 前端设置自身现有 AbilityTag；`AbilityTraditionalActions::SpawnProjectiles` 在来源权威校验后读取本 ASC 当前生效的暗金 GE，以 Spec 等级解算规则。先算旧等级限制数量，再加额外弹数；最终数量上限 32，额外数量每条限制 0–16，倍率和角度校验，防止资产配置失控。

新服务 `EquipmentAbilityModifiers` 只解析/组合规则并修改局部 Settings/Damage；图与原 FireBolt 前端共用现有发射函数。每条规则只应用一次，不按 GrantedTag 数量复制；多个规则额外弹数相加、伤害倍率相乘、散射取最大、禁止追踪取逻辑或。同套装两个 Tag 存在时用该规则 SetDamageMultiplier 替代普通倍率。

每次发射创建快照：卸装影响下一次发射，已飞出弹体保留伤害/追踪配置。基础伤害与 DebuffDamage 同比缩放，触发概率与持续时长保持现有规则；多弹依然有多次命中机会。效果不增加 Commit、不激活嵌套 GA、不改伤害执行器、不绕过服务器来源令牌。

近战新增 `FEquipmentMeleeRule`：`AbilityTag`、`DamageMultiplier`（曲线）、`DamageTypeOverride`（已注册伤害类型）、`HitEffect`（现成 Niagara）。`StanderWeaponActionAbility` 将真实 ActionTag 传入共享 `ExecuteStrike`，该动作在接触时读取规则一次并冻结，保留几何/去重/来源校验。只有精确匹配 `Abilities.StanderWeapons.Sword_Shield.Attack1` 才生效，其他武器与技能不变；默认剑盾与实际装备剑盾本来就共用这一入口。火焰命中沿角色已有多播接口，非本地单播。没有新增燃烧或额外伤害次数。

## 资产、展示与可试玩入口

独立作者脚本创建五个 GE 与一张数值曲线表，只向现有 DT_Equipment 合并缺失行，保留旧行、药水、武器和设计师调参。使用已经查看过的本地图标。独立 commandlet 读回验证，再重复脚本验证幂等。

装备详情显示暗金身份、实际百分比、技能与套装说明，不改现有 UI 布局。提供显式开发控制台命令在当前玩家附近生成五个 Soul 供手动拾取；不自动写出生地图或给正式背包注入物品。现有正式掉落池仅选随机装备，因此不会意外混入这五件。

## 验证

- GE 安全校验拒绝非法操作/通道/倍率，普通和随机 GE 不获得暗金权限。
- 实际基础 GE + 加法 + 两倍率，穿脱、两件独立回收、重复恢复、降低上限夹取、不免费补满、药剂保留。
- FireBolt 等级 1：1→3 枚；等级 3：3→5 枚；55%→两件套70%，无追踪；未 opt-in 的动作保持原行为。
- 拆套下一次恢复55%，卸棱晶下一次恢复原状；已生成弹体不变；原 DA/CDO 不变。
- 近战 Attack1 转火且倍率1.8，其他ActionTag不变；命中一次、不增加范围；卸装恢复物理，Actor/Mass通路观察实际伤害和类型。
- 真实资产载入、表格曲线/图标/Tag/目录引用、作者脚本幂等、相关回归与全项目测试。
- NullRHI 不作为双客户端画面验收；手动 PIE、网络延迟及正式平衡单列待验收。
