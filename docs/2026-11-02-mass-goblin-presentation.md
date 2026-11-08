# Mass 近战哥布林的武器与攻击表现

遵循 `monster-development-standard.md`，本次仅补齐现有 Mass 普通近战哥布林表现；不改变房间数量、血量、伤害值或玩家/其他怪物的 Actor 路径。

对照 `BP_GoblinSpear`：身体为 SKM_Goblin，武器为 SKM_Spear，挂接 WeaponHandSocket，武器相对变换为单位变换。Mass 原动画库已经包含 Idle_Spear2、Run_Spear_fix、Attack_Spear，但只绘制身体。Attack_Spear 长约 1.2083 秒，原代码在伤害发生后才播放攻击且最多保留 0.35 秒，明显截断动作。

新增独立的 SKM_MassGoblinSpear 及材质副本，保留原素材。每种渲染批次共享一个武器 InstancedSkinnedMeshComponent，每只怪物一个实例，使用 UE GPU 骨骼实例附件跟随手部。组件初始化保留插槽骨骼；石化批次同步使用石化材质。死亡时武器从活体实例中移除，由现有最多 12 个短暂尸体槽位的武器组件附在布娃娃手上，随尸体隐藏回收。

UE 5.8 接入注意：无独立动画的武器使用参考姿势索引 0，不能传 INDEX_NONE。当前引擎 SetInstanceBoneAttachment 的父实例参数直接作为密集下标使用；删除实例的 RemoveAtSwap 会改变下标，因此仅在实例增删或批次切换时重建 ID 到密集下标的映射并重绑武器，避免死亡后挂接错位。不修改引擎源码。

攻击动画从前摇开始；按 Actor 长矛攻击的 0.29 秒接触点对齐 Mass 的 0.35 秒前摇，播放速率约 0.829。命中阶段不重启动画，剩余收招约 1.11 秒；命中后目标离开不会截断收招。前摇被取消则切出动作，石化清除攻击表现计时。伤害仍只由服务器的原攻击通路结算一次，GPU 动画不发送伤害 Notify。

配置位于 PureMassMonsterType：WeaponMesh、WeaponSocket、WeaponOffset、AttackContactTime、AttackClipDuration。应用于 DA_MassGoblin、DA_DungeonGoblin、DA_DungeonMassMelee；保留各自数值。

验证脚本：`Scripts/PureMass/verify_goblin_presentation.py`，覆盖双端武器数量、前摇/攻击状态、击杀移除与尸体回收。测试中临时增加玩家血量并停止 Actor 敌人 AI，仅存在于 PIE，测试结束关闭 PIE 清除。

2026-11-02 验证结果：Editor Development 编译成功；38 项 Mass 相关原生自动化用例通过（21 项无警告，17 项有测试环境警告，0 失败）。双客户端 L_RoguelikeRun 验证通过：各 25 个身体/武器实例，双方观察到预警与攻击阶段；击杀 5 只后均剩 20 个武器，尸体呈现并到期回收。再补刷 5 只后双方恢复 25 个身体/武器实例。已检查房主与客户端截图中的长矛、攻击姿势和删除后表现。

运行证据：`Saved/GAAuthoring/MassGoblinArmed/8a0fc63a88854f43b3d672892cb2f46a/runtime.json`、`Saved/GAAuthoring/MassGoblinArmed/respawn.json`；截图位于 `Saved/PureMass/FeedbackUI/ArmedGoblin_C*.png`。这些是本地验证产物。尚待用户在正常游玩中验收动作观感；本次未做性能压测或丢包条件测试。
