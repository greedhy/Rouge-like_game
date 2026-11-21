# 法师组合技能首批

本批用 Schema 2 图组装四个主动技能。技能入口提交一次费用和冷却后，将有限时间的图移交世界子系统。节点沿用已有目标、墙体和 Actor/Mass 伤害通路。施法者生命失效或世界退出时，剩余动作由框架关闭。

| 技能 | 组合 | 首批数值 |
|---|---|---|
| 陨火余烬 | 蓄势提示 → 点位爆发 → 地面脉冲 | 延迟 0.65 秒。火伤 60，落地余火每 0.75 秒火伤 9，持续 3 秒。28 MP，8 秒冷却。 |
| 霜冻符域 | 霜冻脉冲 → 到期爆发 | 4 秒内每秒魔法伤害 11，移速变为 65%，到期魔法伤害 28。22 MP，7 秒冷却。 |
| 穿云雷球 | 穿透弹体 → 终点范围爆发 | 最多 4 个直接命中，首击雷伤 24，后续逐次乘 0.85，爆裂雷伤 26。20 MP，5 秒冷却。 |
| 奥术哨塔 | 置地索敌 → 重复直线束 | 持续最多 5 秒，每 0.8 秒尝试索敌，最多发射 5 次。每束魔法伤害 15，可穿过线上多个敌人。24 MP，9 秒冷却。 |
| 奥术冥想 | 自身装备被动 → 持有无限 GE | 法力回复属性 +2。 |
| 元素护体 | 自身装备被动 → 持有无限 GE | 火焰抗性、雷电抗性各 +12。 |

主动原型包括可视延时、瞬发范围、固定地面脉冲、周期减速、有限穿透、定期索敌和线形打击。每个主动节点有独立的参数快照及可复制的表现 DataAsset。兼容节点 on_expire/on_period 引用用于旧配置校验，真正执行由 Schema 2 图边调度。不要为同一个事件额外激活旧 GA。

被动使用 Status.OwnedEffect 的 until_graph_ends 选项。该选项要求 Infinite 且不堆叠的 GE。技能槽换出走 ASC 的 DeactivatePassiveAbility，图停止时释放自己的 GE 句柄。一个被动结束不会移除其他来源的效果。

首批数值固定。打开技能菜单的 Prototypes 页，点选技能、消耗技能点学习，再选择对应的主动或被动槽位。资产位于 /Game/UnifiedAbilities/MageBatch。编辑器制作和读回使用 Scripts/GA/MageBatch/author_and_verify.py 与 skills.json。两人 PIE 菜单验证使用同目录的 verify_menu_pie.py，脚本以 L_Batch6Test 为测试地图。

设计借鉴分波刷新的暴风雪、地面区域与沿途多目标覆盖、持续造物，以及在主动输出之外提供资源和防护的被动。参考 [Warcraft III 大法师](https://classic.battle.net/war3/human/units/archmage.shtml)、[WoW 法师更新](https://worldofwarcraft.blizzard.com/en-us/news/24179386/undermined-content-update-notes)、[Diablo IV 法师补丁](https://news.blizzard.com/en-us/article/24215995/diablo-iv-patch-notes-2-3) 和 [Last Epoch 符文大师](https://support.lastepoch.com/hc/en-us/articles/46363095981723-Runemaster-Skills)。
