# 美术驱动技能批次

正式资产位于 `/Game/UnifiedAbilities/ArtBatch`，技能标签前缀为 `Abilities.Art`，在 Spells 菜单的 Prototypes 页学习，再装备到主动槽。当前数值固定，新增技能不占初始按键。入口复用已有菜单文本 GA 类，战斗执行仍为 Schema 2 图和注册策略。

| 技能 | 组成与实际参数 | MP / CD |
| --- | --- | --- |
| 冰棱裂爆 | 冰棱以 850 速度前进，最大 1100 距离，穿透最多 5 个敌人。首击 22 魔法伤害，后续每次乘 0.9。飞行结束后 300 范围爆裂造成 30 魔法伤害，并使移速降为 60%，持续 1.5 秒。 | 20 / 5 秒 |
| 烈焰风暴 | 落点 330 半径火旋风持续 4 秒，每 0.5 秒造成 8 火伤，到期 360 范围爆燃造成 32 火伤。 | 30 / 9 秒 |
| 雷狱结界 | 落点 360 半径雷域维持 3 秒，每秒 15 雷伤并把移速降为 70%，到期在 430 范围造成 35 雷伤。 | 25 / 8 秒 |
| 鬼斩·残月 | 面朝方向 360 距离、100 度扇形造成 30 物伤，0.3 秒后追加 410 距离、100 度扇形的 22 魔法伤害。 | 12 / 4 秒 |
| 血气爆发 | 自身 300 半径造成 38 物伤，随后原位置留下 280 半径血池，2 秒内每 0.5 秒造成 8 物伤。 | 18 / 6 秒 |
| 暗月剑阵 | 自身原位置布置 350 半径灵魂阵，3 秒内每 0.5 秒造成 9 魔法伤害，到期 420 范围造成 32 魔法伤害。 | 24 / 8 秒 |

近战三项采用鬼剑士的剑气、血气和鬼阵思路，当前实现为法术式释放的近身攻击。角色完整挥刀连段、武器专属蒙太奇尚未接入。

## 资产与复用

刀光实际是 NiagaraSystem，可直接使用 `/Game/resources/Knife_light/VFX/NE_attack01` 和 `NE_attack03`。鬼斩的两段伤害分别使用这两份刀光。血气使用 `/Game/Art/BloodFX/Systems` 的血液溅射与血池，剑阵使用 Free_Spells 的灵魂光环和冲击波。法师使用 CompositionEffects 冰霜与雷域，以及现有 Firenado 旋风。

图标读取现存 `/Game/resources/Skill_Icons` 和 `/Game/resources/KiroshoSkIconsPk0`。本批通过节点独立 VFXScale 调整表现大小。修改参数只影响本批的配置，仍需在目标场景核对视觉范围与实际伤害范围。

37 个资产由六个 GA、六个 Graph、十三个动作 DA 和十二个 Cost/Cooldown GE 组成。目录逐标签追加，保留原有行。三个目录分别为玩家默认、PureMass 和第五批测试目录。制作入口为 `Scripts/GA/ArtBatch/author_and_verify.py`，配置为同目录 `skills.json`，通过复用 MageBatch 制作程序执行，作者模式使用环境变量 `MAGE_ASSET_MODE=author`，默认只验证。

## 本次验证

- 已通过 UE 独立进程读回六个 GA 的父类、身份、Schema 2、端口连接、配置、特效引用、图标、费用、冷却和目录。
- `test_25d.UnifiedGA.ArtBatch.ActorCompositions` 对六个正式 GA 分别授予并激活，使用正式 Actor GAS 伤害曲线，验证实际生命减少、单次耗蓝、冷却、有限时间内释放图以及结束后停止伤害。
- 原有 `MageBatch.ActiveCompositions` 和 `MageBatch.PassiveOwnership` 同时通过。本轮共三个测试组通过并带测试夹具/旧资产警告，报告为 `Saved/Automation/ArtBatchFinal/index.json`。
- 双玩家 Listen Server PIE 对六项技能完成双方菜单显示、图标、描述、远端学习消耗、远端装备请求和双端槽位同步。回执为 `Saved/GAAuthoring/ArtBatch/menu.json`。
- 待用户 PIE 验收逐技能特效尺寸、刀光朝向、动画节奏和联机施法画面。当前结果不包含逐技能的完整视觉与网络战斗验收。

参考 [Warcraft III 大法师](https://classic.battle.net/war3/human/units/archmage.shtml) 和 [DNF 男鬼剑士](https://www.dfoneople.com/gameinfo/character/Slayer%28M%29)。具体参数按本项目范围和已有动作调整。
