# 地牢 Mass 与 Actor 混合遭遇

## 配置与运行规则

目标地图为 `/Game/RoguelikeGeneration/Maps/L_RoguelikeRun`。其 `DA_DungeonGameplay` 启用 `bUseMassMelee`，`MassPerCombatRoom=50`，引用独立的 `DA_DungeonMassMelee`（生命值 10、攻击前摇 0.35 秒）。原怪物类型资产不覆盖。

普通近战哥布林从 Actor 普通怪名册移除，由 Mass 实体管理。投石哥布林、食尸鬼、恶魔战士、萨满、蘑菇怪及精英/Boss 保留 Actor/GAS 通路。玩家仍为 BP_DungeonPlayer。

每个战斗房间共生成 50 只 Mass：普通 Combat 房间沿用两波，各 25 只；Elite/Boss 房间一波 50 只。其他 Actor 数量沿用已有规则，所以房间总敌人数大于 50。生成请求预先登记遭遇计数，再按现有每帧数量/耗时预算逐批落地；未生成的请求也阻止提前开门。回收与击杀使用不同结算原因。

## 网络与死亡表现

`APureMassCrowd` 将快照目标间隔由 0.1 秒改为 `1/60` 秒（16.67ms），NetUpdateFrequency/MinNetUpdateFrequency 为 60，保留累积时间余量。服务端模拟仍逐帧执行；快照仅在状态有变化时标脏。客户端继续插值显示。此设置不保证每 16ms 收到包，也不等于已经达到稳定 60FPS，实际受服务器帧率、复制调度和网络预算影响。

死亡在服务端先移除实体并结算；共享 Crowd 每帧批量广播短暂死亡表现。每个客户端最多分配 12 个可复用 SkeletalMeshComponent，物理布娃娃保留 1.5 秒后关闭物理、碰撞、Tick 并隐藏。超出容量省略表现；不可靠表现消息可能丢失，实体死亡/房间结算不依赖它。活体 Mass 不新增逐怪 Actor 或 ASC。尸体与静态地形碰撞，不作为活体目标。当前未从 GPU 实例骨骼复制死亡瞬间姿态，布娃娃从骨骼组件姿态进入物理。

近战前摇显示共享实例化的头顶感叹号。换目标、脱离距离/视线、石化或死亡取消前摇；重新进入需要完整前摇。

## 技能栏和闪电

Dungeon HUD 使用 `DA_MassAbilityInfo`，其中缺少原 FireBolt 条目。补入主技能目录中的 FireBolt 元数据后，两个实际 PIE 客户端的 LMB Image_SpellIcon 均正确引用 `FireBolt.FireBolt`，透明度为 1。

实际启动映射为 FireBolt=LMB、Burst=1、Homing=2、Chain=3、Orbit=4、Beam=RMB。资产中的 Burst 使用本地空间 ArcaneShards，没有闪电端点参数；闪电 Beam 修正施法起点为玩家位置加高度偏移，在 Niagara 激活前写入起止点，并在 FinishSpawning 前初始化目标端点，避免首帧使用素材原始固定点。

进一步视觉检查发现 `NS_ElectricBeam` 的 Beam Emitter Setup 采用 `Absolute Beam Start=false`、`Absolute Beam End=true`。运行时传入的世界起点被再次按局部坐标转换，数值正确但画面错误。采用独立副本 `/Game/PureMass/Effects/NS_DungeonElectricBeam`，将起止点统一为世界坐标契约；原始素材保留。

发现并修复后加入客户端的加载顺序问题：服务端 Ready 已复制时，客户端本地导航检查可能尚未通过；此时创建关闭的战斗房门会使导航检查失败并锁住输入。现在本地导航就绪后才创建房门表现。

双端实际施法还发现：动态添加的 `DungeonEncounter` Actor 标签不会复制，客户端混合目标查询因此拒绝合法 Actor 怪物。现用显式复制的 `AEnemyCharacter::bDungeonEncounter` 表达归属，普通、精英、Boss 和召唤接入均设置此标志；服务器继续检查遭遇状态、活体状态和房间边界，不放宽伤害权限。池化怪物在本地牢池中复用时保持归属。

## 验证记录

- Editor Development 编译通过。
- 最终 56 项 Roguelike 与 PureMass.Mixed 原生测试通过（27 成功、29 带现有 fixture 警告、0 失败），覆盖混合计数、排队目标、前摇取消、无动态 Actor 标签的复制身份、闪电首帧端点等。报告：`Saved/GAAuthoring/DungeonHybrid/native-complete/index.json`。
- 双客户端实际普通战斗房间通过：两波各 25 只、生命值 10、保留其他 Actor、两端实例数一致、两端死亡表现、最多 12 个尸体组件、1.5 秒后回收、共 50 个不同实体 ID、怪物全部结算后开门。报告：`Saved/GAAuthoring/DungeonHybrid/7da7eee5b62b4229a79771f6afcad995/rooms.json`。
- HUD 实际读回：`Saved/GAAuthoring/DungeonHybridFix/hud-live.json`。
- 修正后的双客户端真实远端 EnhancedInput 测试通过：FireBolt 投射物、Burst Runtime、Beam 起点跟随玩家加 30 高度、终点对应目标，以及双端活跃特效。报告：`Saved/GAAuthoring/DungeonHybridFix/363d255274bb46b49d9eb3e62a4de89b/input-pie.json`。
- 最终截图 `Saved/PureMass/FeedbackUI/DungeonSkills_RMB.png` 已人工检查：光束仅连接玩家与目标，无屏幕外延伸。两个启用发射器都改为绝对起点；Beam、Chain 数据资产引用副本，原始 Niagara 未修改。
- 最终配置读回通过：`Saved/GAAuthoring/DungeonHybrid/config-final.json`。临时测试血量已移除，后台节流恢复，PIE 已停止。

尚未将打包独立进程、丢包/延迟条件、Elite/Boss 完整战斗流程或稳定 60FPS 作为已验证结果。最终预警可读性、布娃娃观感及技能操作请在 PIE 验收。
