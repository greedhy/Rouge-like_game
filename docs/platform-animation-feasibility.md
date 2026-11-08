# PlatformAnimation 近战素材可行性检查（2026-10-31）

本轮仅检查资产，未创建角色/GA，未修改原始动画或Skeleton。原始读回位于Saved/GAAuthoring/PlatformAudit/audit.json与extra.json。

## 骨骼结论

Stander模型使用独立的Stander_Skeleton，有68根骨骼。与工程内StoneGolem/Demo/Characters/Mannequin_UE4/Meshes/SK_Mannequin逐骨比较，名称集合、数量、父子关系完全一致。它属于UE4标准Mannequin结构，而非UE5 Manny结构。相同层级不等于引用同一个Skeleton资产，也不能代替参考姿势、比例和实际播放验证。

工程内Manny有161根骨骼，包含上述68个同名骨骼，但增加了脊柱、扭转/修正等结构。不能直接把Stander的动画当成Manny原生动画。TinyHero只有43根骨骼，也是独立结构。

整个目录含428个AnimSequence，全部引用Stander_Skeleton。11个SkeletalMesh和11个Skeleton中包括武器资源，不能理解为11种角色。已有IKR_Stander，读取到pelvis重定向根与21条重定向链；尚未验证它与Manny的配对输出。

## 动作与玩法适配

剑盾、重锤、双刀、长枪、法杖、刺剑、双刃各有29段，拳斗28段，另有基础移动、冒险、受伤及通用动作。剑盾包括三段普攻、防御及格挡反应、闪避/翻滚、跳跃攻击和8个Skill片段。目录名称只是动作分类，不自动提供减速、击晕或击飞等GAS行为。

抽查剑盾29段，以及重锤/双刀/长枪/拳斗/刺剑18段，共47段：没有AnimNotify，EnableRootMotion均为false，但很多片段的root轨道存在位移。例如剑盾Attack1约80cm、Attack2约184.6cm，Dodge约500cm，DodgeRoll约400cm；多类Skill6_Move末端约500cm。不开启Root Motion不等于动画为原地动作。

接入可沿用现有DungeonPlayerCharacter、PlayerState ASC、伤害GE、房间与复活通路；需要专属AnimBP、Slot和蒙太奇。伤害与特效时间必须逐段通过武器轨迹确定，不能直接按时长百分比猜测。可调参数继续放UE数据资产。

当前SwordShieldAbility在攻击时DisableMovement，适用于当前原地近战方案。位移动作不能直接套用：需决定保留Root Motion并通过CharacterMovement移动胶囊，或导出原地副本并由GA控制位移；同时处理墙体、房间边界、取消与倒地。跳跃攻击还需空中/落地状态衔接。

## 推荐顺序

先以包内Stander为动作和GA展示基准，避免一开始把重定向与战斗逻辑问题混在一起。验证普攻、盾防/反击、旋转斩和一段位移攻击后，再把同一小组动作重定向到工程已有Manny，校验手脚接触、握柄和位移距离，最后批量处理其他武器类型。

如果目标是最低适配成本的新模型，优先筛选明确支持UE4 Mannequin骨架且人体比例接近的模型；只标注“UE5兼容”的素材并不表示使用Manny或Stander骨架。选择Manny也可行，但应保留独立IK Retargeter与派生动画，不修改素材包源骨架。

尚未执行：全428段逐帧视觉质量验收、Manny重定向实际结果、武器握持与脚滑修正、展示GA实现、最终特效/手感及网络验证。

技术依据：[Epic IK Rig重定向文档](https://dev.epicgames.com/documentation/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine) 支持不同骨骼数量、名称与方向间通过链映射重定向；仍需按角色比例和接触约束调校。
