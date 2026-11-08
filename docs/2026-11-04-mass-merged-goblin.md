# Mass 默认持矛哥布林：制作阶段合并

遵循怪物开发规范；采用用户指定的离线合并方案，保留现有 Mass 战斗、攻击预警、生命与死亡流程，不创建逐怪 Actor。

## 交付资产

目录 `/Game/PureMass/Rendering/Merged`：

- `SKM_MassGoblin_Armed`：身体与长矛合并的骨骼模型。
- `AB_MassGoblin_Armed`：配套动画库，复用 Idle_Spear2、Run_Spear_fix、Attack_Spear。
- `AB_MassGoblin_Armed_ASTP`：与新模型匹配的实例化动画变换提供者。

现有资产只有 LOD0：身体 2740 个源顶点，长矛 1268 个源顶点，合并后 4008 个源顶点、7792 个三角形。源顶点数不等于 GPU 顶点数，材质接缝等会拆分渲染顶点。保留两个材质槽，所以“一个模型”不等于“只有一个 draw call”。

沿用 `SK_Goblin` 骨骼和 `PA_Goblin` 物理资产。长矛全部权重绑定到既有 `WeaponHandSocket` 对应的 **Hand-L（骨骼索引 11）**，不是凭左右手习惯猜测骨骼名称。身体权重保留。

## 制作方法

`UPureMassAssetAuthoring::BakeRigidWeapon` 是编辑器制作工具。目标必须是 `/Game/PureMass/Rendering/Merged/` 下的独立复制品；不会改写源模型或保存原素材。

1. 复制现有 Mass 身体模型的可编辑 MeshDescription。
2. 累乘手骨及其父骨骼的参考姿势，得到手骨在模型空间中的变换。
3. 长矛顶点采用 `WeaponOffset × SocketLocal × HandReference` 变换到身体参考姿势空间；同步变换法线与切线，保留 UV。
4. 追加长矛几何与独立材质槽，将新增顶点蒙皮设为 Hand-L 单骨骼 100% 权重。
5. 提交可编辑源数据、重新构建骨骼网格并更新包含长矛的边界；保留身体物理资产。
6. 生成匹配新模型的动画库与动画提供者，然后修改兼容的 Mass 类型。

运行时身体和长矛一起蒙皮，没有额外的长矛实例、GPU 插槽附着或逐怪组件。尸体仍使用既有有限数量的 SkeletalMeshComponent 布娃娃池，武器作为身体模型的一部分随手骨运动；没有增加独立武器刚体。

制作脚本：`Scripts/PureMass/author_merged_goblin.py`。它拒绝覆盖已有合并资产，并在修改类型前备份所有目标数据资产。再次制作应明确使用新输出资产名称，不能直接对派生模型执行原身体 FBX 的 Reimport，否则可能覆盖合并结果。

## 配置接入

以下六个类型切换到新模型及动画提供者，`WeaponMesh=None`，避免重复武器；保留各自生命、速度、伤害、攻击周期、偏移和预警配置：

- `/Game/PureMass/DA_MassGoblin`
- `/Game/PureMass/DA_MassQAGoblin`
- `/Game/Performance/DA_BenchmarkGoblin`
- `/Game/CompositionBatch6/QA/DA_Batch6QAGoblin`
- `/Game/RoguelikeGeneration/Gameplay/DA_DungeonGoblin`
- `/Game/RoguelikeGeneration/Gameplay/DA_DungeonMassMelee`（生命仍为 10）

旧身体、独立长矛及其动画资产保留，Actor 哥布林配置不变。旧 `author_goblin_presentation.py` 和 `complete_goblin_weapon_defaults.py` 属于历史分体制作路径，不应对已迁移类型再次执行。旧 `verify_goblin_presentation.py` 的独立武器计数断言也不适用于新模型，改用下述合并验证脚本。

## 验证与用户验收

`Scripts/PureMass/verify_merged_goblin.py`：双客户端 L_RoguelikeRun 实测，每端 25 个合并身体、0 个独立武器；两端均经历待机、移动、攻击前摇和攻击；击杀 5 个后剩余 20 个身体，尸体产生并按期回收。所有脚本检查通过，截图已确认正常运行时长矛可见，无需修改组件属性触发刷新。

运行证据：`Saved/GAAuthoring/MassMergedGoblin/7a01190ae1254ac2a6ea5b186d5a3bb3/runtime.json`。截图：`Saved/PureMass/FeedbackUI/MergedGoblin_C0_3.png`、`MergedGoblin_C1_3.png`、`MergedGoblin_C1_Death.png`。

原生保存后加载检查入口：`test_25d.PureMass.Assets.MergedGoblin`，验证身体顶点/权重不变、武器顶点位置匹配原 Actor 插槽放置、单手骨满权重、源几何数量、骨骼和物理资产引用。

最终结果：Development Editor 编译成功；保存后重新加载测试 Success，0 error、0 warning。报告 `Saved/Lifecycle/MassMergedAsset/index.json`。资产制作记录与迁移前备份位于 `Saved/GAAuthoring/MassMergedGoblin/7f8c4573b92a47b2843fde3c98c5e1db`。本次不修改全局渲染设置，也未提交或推送 Git。

用户最终 PIE 验收：进入战斗房间，观察跑动及攻击时长矛是否始终握在手中；击杀后观察长矛随尸体运动并一起消失。脚本状态和静态截图不能替代完整动画观感验收。
