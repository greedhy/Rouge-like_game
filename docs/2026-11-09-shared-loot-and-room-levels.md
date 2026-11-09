# 全模式共享掉落与房间等级

## 当前规则

| 来源 | 消耗品 | 装备 | 等级来源 |
|---|---|---|---|
| PureMass 实体 | 无 | 无 | 房间出生请求传入实体等级 |
| 普通 Actor 怪物 | 独立 10% 生命药水、10% 魔法药水，可同时掉落 | 无 | 房间等级；地图直接放置的敌人沿用自身 Level |
| 精英 | 必定 1–3 个，逐个等概率红/蓝药水 | 1 件，至少一条随机词缀 | 怪物等级 |
| Boss | 必定 2–4 个，逐个等概率红/蓝药水 | 2 件，每件至少一条随机词缀 | 怪物等级 |
| 战斗/精英/Boss 房清理 | 保留原个人金币奖励 | 队伍共享地面 2 件，每件至少一条随机词缀 | 房间等级 |

Boss 自身奖励与 Boss 房清理奖励分别结算，合计 4 件装备。非战斗房、失败、回收、销毁、召唤物退休不会产生清房或死亡奖励。召唤物真正被击杀时按其自身身份执行同一规则。

Goblin Shaman（旧版与 ShamanExpansion）、Ghoul（旧版与 LegacyExpansion）统一识别为精英；StoneGolemElite 保持精英；GruxBoss 为 Boss。身份由全局类路径表解析，支持蓝图子类继承；不重写这些蓝图的技能、血量或动画。现有房间出怪列表与数量保持，列表中的 Shaman/Ghoul 也使用精英身份与奖励。这里的 `NormalRoster` 是既有生成/池配额列表名称，不再代表其中每个类都是普通掉落身份。

## 等级

| 参数 | 默认值/算法 | 配置位置 |
|---|---|---|
| 房间等级 | `BaseRoomLevel + 入口最短路径深度 × RoomLevelStep`，范围 1–10000 | `Config/DefaultGame.ini` 的 `[/Script/test_25d.SharedLootSettings]` |
| 基础房间等级/每层增长 | 1 / 1 | `BaseRoomLevel` / `RoomLevelStep` |
| 普通怪等级 | 房间等级 | 同上 |
| 精英/Boss 额外等级 | +1 / +2 | `EliteLevelBonus` / `BossLevelBonus` |
| 装备物品等级 | 来源等级，限制在 1–20 | `MaximumEquipmentLevel=20` |

房间等级保存在复制的 `FDungeonRoomProgress.RoomLevel`，HUD 显示当前房间等级。分支房间按图深度计算，避免房间数组顺序影响等级。Actor 在 FinishSpawning 或池唤醒前设置 Level，原 GAS 默认属性、能力授予和按等级的结算继续使用它；房间 Profile 明确覆盖的血量仍保留策划值，本次没有额外发明血量增长曲线。Mass 使用每实体 Level，护甲结算读取该等级，不修改共享 MonsterType。房间外的出生逻辑保留已有等级配置。

装备等级上限 20 对应目前词缀表的 1–9、10–20 层级范围。提高此配置前，先扩展 `DT_EquipmentAffixes` 所有候选配置的等级覆盖与数值曲线，并运行真实资产测试。不会为了凑数量把无法生成词缀的装备静默降级成白装。

## 配置与调用链

| 内容 | 入口 | 行为 |
|---|---|---|
| 概率、数量、等级增量 | `USharedLootSettings` / `DefaultGame.ini` | 所有 GameMode 共用一份默认配置 |
| 类身份 | `USharedLootSettings.EliteClasses/BossClasses` | 构造函数内为当前项目默认路径；可用 ini 数组覆盖；敌人 `LootRank=None` 可明确禁止掉落 |
| 敌人死亡 | `AEnemyCharacter::Die` → `USharedLootSubsystem::QueueEnemyLoot` | 服务器、bDead 置位后、池化分支返回前排队；旧 `SpawnLoot` 蓝图事件留作兼容但死亡不再调用 |
| 清房奖励 | `CompleteEncounter` → `QueueRoomLoot` | FinishEncounter 成功后入队，RunId + RoomId 另行去重 |
| 装备池 | 现有物品目录和 `DT_Equipment` 中的随机装备 | 按稳定 ID 排序后等概率选择；固定装备不进入随机奖励池 |
| 品质/词缀 | `FEquipmentGenerator::Generate(..., bRequireAffix=true)` | 排除零词缀品质，保留其他品质相对权重；仍遵守词缀等级、互斥组、前后缀配额 |
| 落地/拾取 | `SharedLootSubsystem::Tick` → `WorldPickupComponent` | 装备用 Soul/E 拾取，药水使用目录原拾取蓝图与拾取方式，全部服务器生成并复制 |
| 装备应用 | 既有背包与 EquipmentComponent | 保留实例 GUID、RollData，独立 GE Spec/Handle；拾取、穿戴不重新抽词缀 |

队列在请求时固定来源等级、位置和种子，生成成功后保存实际 RollData。每帧最多处理 8 件，失败项每秒重试、首次报错；已落地项移出队列，不会因重试重复发放。世界退出会清理未落地项与房间账本，本轮未新增跨地图保存待发奖励功能。实际配置错误会显示 `SharedLoot: reward pending`，必须修复目录、拾取类或词缀表。

## 验证与 PIE 验收

自动化入口 `test_25d.Loot.*`：概率分布、精英/Boss 数量、真实随机装备全等级词缀生成、类身份、池化两次生命去重、房间奖励去重、Mass 实际击杀无掉落、地面实例 GUID 与等级、分支房间深度。

2026-11-09 验证：Editor Development 编译成功；新增 4 项全部通过（2 项带警告），装备/背包/Roguelike/PureMass 回归 125 项全部通过（35 项带警告），0 失败、0 未运行。报告分别位于 `Saved/Automation/SharedLoot/index.json`、`Saved/Automation/SharedLootRegression/index.json`。警告包括已有 Grux Rig 依赖缺失和无骨骼测试夹具的插槽提示，未将这些结果等同于视觉验收。

用户 PIE 验收：

1. 单人进入 L_RoguelikeRun，观察 HUD 房间等级，击杀 Shaman/Ghoul，检查 1–3 个药水与 1 个 Soul。
2. E 拾取装备，B 打开背包查看词缀/等级，穿戴并核对实际属性增量。
3. 清完普通战斗房检查额外 2 件 Soul，回访该房间不重复发奖。
4. Boss 击杀检查 2–4 个药水 + 2 件装备，清房再出现 2 件。
5. 双客户端检查同一地面掉落、竞抢只成功一次、各自背包/属性一致；在 Mass 地图击杀实体确认无掉落。

自动化不替代最终掉落位置、特效、拾取可读性和双客户端 PIE 验收。
