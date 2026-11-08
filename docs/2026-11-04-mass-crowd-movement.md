# Mass 哥布林拥堵、重叠与朝向修复

遵循 `monster-development-standard.md`，范围是已有 PureMass 移动/表现，不改 Actor/GAS 敌人、生成数量和伤害规则。

## 武器配置问题与边界

此前检查六个 MonsterType：DA_BenchmarkGoblin、DA_MassQAGoblin、DA_Batch6QAGoblin 漏配 WeaponMesh，已由 `Scripts/PureMass/complete_goblin_weapon_defaults.py` 补齐。正式地牢的 DA_DungeonMassMelee 原本已有武器引用，因此漏配不能解释正式关卡武器不可见。

SKM_MassGoblin 是身体，SKM_MassGoblinSpear 实际是独立长矛，名字不表示“身体带长矛”。当前身体和武器分别使用共享 InstancedSkinnedMeshComponent，由 GPU 骨骼插槽附着；没有为每只怪物创建 SkeletalMeshComponent。需要管理动画轨道、实例 ID 到密集索引映射、删除后的重绑定及渲染更新。这是当前表示方案的复杂性，并非 Mass 天生要求如此复杂。

武器数量检查曾通过，但正常截图仍不可见；修改编辑器属性触发更新后可见，具体渲染更新根因仍未确定，不能归结为已证明的 bounds 问题。本次不宣称武器显示已修复。固定使用长矛的普通怪，更简洁的后续方案是在制作阶段合并身体与武器、将武器顶点绑定手骨，再复用同一动画；需要真实创建并验证合并资产，不能仅修改数据资产名称。

## 根因和修改

- 原移动只有软排斥，Mass 实体没有物理碰撞体。普通胶囊 Sweep 看不到其他 Mass 实体，因此实体可相互穿透，在俯视图中表现为身体/头部重叠。
- 原方向为 normalize(寻路方向 + 排斥力)，排斥力较大时会反转方向，下一帧又追向玩家，形成往复。现在保留非负前进分量，拥堵时采用统一侧向绕行，不把很小的合力放大成满速后退。
- 渲染端原来每个网络样本直接替换 Facing。现在低于 10 cm/s 保持朝向，目标朝向和显示朝向分离，显示转速不超过 240°/s。
- 新增移动末端的平面圆盘扫掠，使用空间索引查询邻居并截断到最早接触点，保留 1 cm 间距；不同高度且胶囊不相交的实体不互相阻挡。已有重叠允许向外分离。这不是逐怪 Actor 碰撞。
- 原贴地查询把 ECC_WorldStatic 用作 TraceChannel，不等于只查询 WorldStatic 对象。改为对象类型查询，角色和动态尸体不会被当作地面。
- 与 Pawn 接触的滑动和穿透修正使用平面法线；静态斜坡保留完整法线，避免禁止正常上坡。移动平台暂不属于此静态地面查询的支持范围。

## 验证入口

`test_25d.PureMass.Movement.CrowdConstraints`：长步长不穿透、接触阻挡、已重叠可分离、侧向通过、拥堵不反转、交替方向样本下的转速限制。

`test_25d.PureMass.Movement.WorldConstraint`：调用真实 Subsystem::Move 与空间索引，验证前排阻挡、重复推动、侧向通过、不同楼层不互挡。

`Scripts/PureMass/verify_crowd_movement.py`：双客户端 PIE，进入 L_RoguelikeRun 的第一个战斗房间，只临时提高玩家生命；记录服务端实体间距、Z 变化、移动/攻击数量。退出 PIE 丢弃临时状态，不修改地图或生成资产。脚本统计不是完整自动化视觉验收。

第一轮 PIE：20 秒、172 个样本、第一波 25 个 Mass 怪物；最小边界间距约 1 cm，Z 变化为 0，最大追踪位移约 1076 cm，结束时仍有移动及攻击实体。该波另有 Actor 怪物，未更改当前波次配置。第一轮截图 `Saved/PureMass/FeedbackUI/CrowdMovement.png`。

最终 Development Editor 编译成功。`Saved/Lifecycle/MassMovementFinal/index.json` 中两个移动测试和四个 LegacyFireBolt 回归测试均为 Success（6/6，0 error）。其中五个用例含既有无网格 Actor 测试夹具的 WeaponHandSocket 警告，不能表述为无警告通过；实际 PIE 也记录了 BP_GoblinSlinger 初始化的同类警告，本次没有修改该 Actor 资产。

最终二次双客户端 PIE：`Saved/PureMass/Movement/ffac224103f74a8288287fe42b3f238f/runtime.json`，20.094 秒、172 个样本、25 个 Mass 怪物，最小边界间距 0.9999993 cm、Z 变化 0、最大追踪位移 982.7 cm；结束时围堵停止移动、4 个实体处于攻击/前摇。截图为 `Saved/PureMass/FeedbackUI/CrowdMovement_C0.png` 和 `CrowdMovement_C1.png`。停止 PIE，编辑器保留在 L_RoguelikeRun。

## 验收边界

固定侧绕行是局部避障，不是全局人群流量求解器，也不保证所有狭窄门洞、对向人流和出生即完全重叠的场景都能无死锁。NavMesh 仍负责静态障碍路径。应在用户实际布局中继续验收狭窄入口、斜坡、玩家移动和大量怪物围堵。

模型轮廓可超过配置碰撞半径；视角造成的遮挡不能等同于实体重叠。客户端位置插值/量化也不能等同于服务端硬约束。最终动画观感由用户 PIE 验收。
