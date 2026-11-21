# 第四批暗金：针焰、束雷与异武近战

针焰晶瓶保留火球弹道并强化每一发。选型参考《暗黑 IV》无尽怒火法杖与照明者手套分别强化指定施法和改变弹道的设计。 [暴雪 1.0–1.2 补丁](https://news.blizzard.com/en-us/article/24092662/diablo-iv-patch-notes-1-0-1-2) · [暴雪 2.0 补丁](https://news.blizzard.com/en-gb/article/24140808/diablo-iv-patch-notes-2-0)

束雷回路用两件装备强化引导输出，重锤和双刃用接触动作触发短时周身效果。近身波纹的视觉参考来自《暗黑 III》Chantodo 两件套的毁灭波。 [Chantodo's Will](https://us.diablo3.blizzard.com/en-us/item/chantodos-will-Unique_Wand_012_x1)

| 装备定义 | 槽位与图标 | 收益与代价 |
|---|---|---|
| `UniqueNeedleflamePrism` 针焰晶瓶 | 魔法石，`44778_1` | 火球直击和燃烧基础伤害 ×1.65，最大法力 ×0.70，沿用原弹数、散射与追踪 |
| `UniqueStormLens` 束雷导管 | 魔法石，`41490_1` | 闪电引导基础伤害 ×1.28、二次索敌半径 ×0.82，最大法力 ×0.82 |
| `UniqueStormweaverMantle` 蓄雷法衣 | 护甲，`42069_1` | 法力恢复 ×1.75、护甲 ×0.65，参与束雷回路 |
| `UniqueFaultlineTreads` 断层战靴 | 鞋子，`51613_1` | 重锤环击（默认键 1）接触伤害 ×1.75，脚下震波 0.55 秒，护甲 ×0.70 |
| `UniqueRimefangHelm` 霜牙战盔 | 头盔，`54873_1` | 双刃突进（默认键 2）接触伤害转为水系并提高至 ×1.45，霜雾随身 0.75 秒，最大生命 ×0.82 |
| `UniqueEmberheartElement` 赤脉符印 | 元素 1，`55316_1` | 力量 ×1.55、生命恢复 ×1.80，最大法力 ×0.65 |

**束雷回路**同时穿戴束雷导管和蓄雷法衣时，导管的技能伤害倍率由 ×1.28 提高到 ×1.65。单枚分雷戒与齐套组合时，闪电引导伤害 ×1.188、二次索敌半径 ×1.0004，并多接入两个目标。原引导每周期费用沿用技能配置。

针焰晶瓶与灰烬棱晶占用同一个魔法石槽。前者在等级 1 保留一发追踪弹，后者提供三发散射弹。新近战规则精确指向 `Hammer.Skill7_Around` 与 `DualBlades.Skill6_Move`，与既有剑盾暗金可以跨槽同穿。两种周身 Niagara 在原接触时点开始，震波使用 `NS_EarthShockwave`，霜雾使用 `NS_GlacialStorm`，伤害范围和命中次数沿用 GA。赤脉符印占用元素 1，效果是三项持久属性倍率。

元数据在 `/Game/Equipment/DT_Equipment`，倍率在 `/Game/Equipment/Unique/CT_UniqueEquipmentStats`，规则选择器和 Niagara 引用在各自的 GE。制作和读回遵循[暗金与套装开发流程](unique-equipment-production-workflow.md)。在 `L_RoguelikeRun` 的开发控制台执行 `Equipment.SpawnUniques` 可投放固定暗金 Soul，靠近后按 E 拾取。

## 验证记录

| 验证 | 结果 |
|---|---|
| 制作和独立读回 | 新增 6 个 GE、6 个物品行和 21 条曲线。保留原 41 个物品行，原 14 个暗金 GE 的包哈希一致。重复执行无写入，另起进程读回 20 件暗金 |
| 真实资产与属性 | 覆盖六件装备的属性乘法、穿脱、派生值与生命法力比例。针焰测试检查实际生成弹体的数量、追踪、直击和燃烧；束雷测试检查两个拆套方向、重复读档和分雷戒组合 |
| 主工程拾取 | 单人 PIE 生成 20 个 Soul，六件新品均显示近身提示，拾取后进入指定槽位且各有一个生效 GE。束雷导管替换针焰晶瓶后，晶瓶以原实例返回背包，原 GE 已移除 |
| 近战周身特效 | 离屏 D3D12 自动化执行生产重锤和双刃 GA，均在挥空时按接触次数生成挂接组件，且到期回收。移除对应装备后重放动作，两种组件均未再次生成 |
| 作者校验与全量回归 | 只读保护验证覆盖 20 件暗金、两个反例和所有包哈希。装备 33/33 通过，其中暗金 17/17。全量 462/468 通过，六个失败项与第三批主工程基线一致 |

报告在 `Saved/Automation/UniqueEquipment/Batch4`。`Author.json` 记录两张共享表的备份路径，`Idempotent.json` 与 `Verify.json` 记录重跑和独立读回，`Pickup.json` 记录地图中的实际拾取事务，`BodyRHI/index.json` 记录周身特效生命周期测试，`FinalTests/index.json` 记录全量回归。最终画面和双客户端远端表现仍待联机 PIE 验收。
