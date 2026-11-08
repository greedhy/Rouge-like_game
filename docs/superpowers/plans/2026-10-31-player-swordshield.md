# SwordShield Player Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement task-by-task in this session.

**Goal:** 可在生成房间中选择体验的TinyHero剑盾玩家，具有三段普攻、防御及旋转斩。
**Architecture:** DungeonPlayerCharacter + PlayerState ASC；专属配置、动画和GA；单人选择入口与原玩法隔离。
**Tech Stack:** Unreal 5.8 C++、传统GAS、Python资产制作、UE自动化。
**Spec:** [首版设计](../../player-swordshield-design.md)

## 全局约束

不改源素材、不改旧法师技能/存档、不切换Mass；共享工作区已有修改保持原样。不自动提交。参数与表现使用专属DA；真实动画命中、生命周期和玩家PIE分别验证。

## 实施任务

- [x] 1. 采样TinyHero武器动作、读回现有玩家初始化/输入，确定模型缩放、挂点和攻击时序。证据Saved/GAAuthoring/SwordShield。
- [x] 2. 创建SwordShieldConfig、SwordShieldPlayer和三个GA；先写几何/配置/状态测试观察失败，再实现并构建。逐段挥砍、冷却、定时防御与取消清理。
- [x] 3. 创建独立移动BlendSpace/AnimBP、攻击蒙太奇、GA/GE/DA、玩家蓝图。编译与读回，真实模型播放与视觉校准。
- [x] 4. 创建单人剑盾试炼入口，保留原法师模式；设置正确输入、技能描述与可见提示。
- [x] 5. 自动化实际伤害、目标过滤、消耗/冷却、打断、死亡/复活与房间接入；修复失败项。
- [x] 6. 只读独立审查、更新交付文档、恢复编辑器环境；用户PIE验收待办独立列出。

## 文件职责

`Public/Player/SwordShieldConfig.h`：技能和视觉配置及几何/连段校验。
`Public/Character/SwordShieldPlayer.h`、Private实现：模型挂点和近战状态。
`Public/AbilitySystem/Ability/SwordShieldAbility.h`、Private实现：共享近战执行器及三个身份。
`Private/Tests/SwordShieldTests.cpp`：原生回归。
`Scripts/GA/SwordShield/`：检查、制作、运行验证和恢复。
入口采用RoomMenuWidget/RoomSubsystem与GameMode最小扩展，具体读取后记录实现决策。

## 审查重点

鼠标无命中、动画中断、连段超时、格挡与装备GE同时存在、旧法师入口、跨房间/倒地恢复、模型静态武器挂点。

## 决策记录

Ruling: 首版单人剑盾试炼作为明确选择入口，保留旧存档职业；不把完整职业存档与多人角色选择夹带进近战玩法实现。

## 当前执行记录

已读取输入、PlayerState ASC、房间复活、现有菜单与近战伤害接口。`Scripts/GA/SwordShield/inspect_animation.py` 已在编辑器只读执行，报告 `Saved/GAAuthoring/SwordShield/animation-inspection.json`：TinyHero模型高度约155cm；源Sword模板绕X旋转90度，Shield模板绕Z旋转-90度；已采样5段动作各33帧。候选剑尖计算已考虑Sword模板旋转，但尚未做带武器运行预览和socket局部偏移校准，因此任务1仍未勾选。

用户关闭编辑器后已完成红绿验证：Red报告复现3项几何/连段断言失败；随后已实现配置校验、几何、连段、角色和三个GA，无占位实现残留。21个专属资产已创建，独立技能目录避免剑盾角色误学法师技能；独立GameMode用于单人入口。

运行首轮确认三段普攻与旋转斩实际造成一次伤害，发现玩家已有Secondary属性GE的Override导致临时BlockChance Override未生效。Ruling: 防御改为独立限时GE授予State.SwordShield.Guarding；共享ExecCalc仅对该标记强制走原有格挡减半分支，不覆盖装备属性。GuardFix报告中真实伤害100→50、结束恢复100、其他GE保留，加法师ArcaneNova回归共8项通过。

测试夹具修正：Native瞬态敌人未BeginPlay需显式注册AttributeSet；临时地图需TravelPlayerStart且RG_Entrance分组；鼠标朝向在自动化中保持真实逻辑，按锁定后方向摆放边界靶，避免把鼠标位置当作角色固定朝向。上述与玩法缺陷分开记录。

## 最终实施与审查记录

任务1–5完成。运行证据：combat 5/5，lifecycle 6/6，cooldown 2/2，movement、入口、房间清场、资产读回和四张动作采样均通过。房间测试暂停敌人AI并将血量设为1，由真实剑盾GA击杀10个Actor并完成房间，不代表战斗难度验收。

修复记录：BlendSpace重采样前滑行、后脚骨摆动通过；Guard淡出原0.94秒，新增持续时间断言失败后调整蒙太奇尾部淡出与GA1秒结束，回归通过；源Sword组件四元数+X90与UE Rotator Roll符号相反，修正为-90并重拍四段，首段命中0.15秒。原生构造亦修正并加入挂点旋转回归。基础速度由600修正450，避免复活RefreshMovementSpeed覆盖组件值。

独立只读审查：review_swordshield（新上下文）未发现Critical/Important；曾怀疑旧技能加载，读取PureMassPlayerCharacter::PossessedBy与空SaveProgress后撤回。审查提出单次命中/动作结束后冷却测试较弱，补充damage_events==1与独立cooldown运行检查均通过。最终扣血仍走真实抗性/格挡等，不以裸伤数值替代实际管线。

Final: Ruling: 视觉与手感由作者采样、用户PIE最终验收；本轮已检查源挂点和实际截图，不能代替用户手感偏好。远端多人、打包和职业存档迁移保持首版范围之外，代价是尚不能声称正式多人可选职业已完成。

Final: Ruling: 对同步伤害回调的取消重入增加逐目标有效性/存活/IsActive检查；现有订阅者未复现此类取消，作为生命周期防御处理，不将其描述为已复现缺陷。

未提交、未推送，所有新增资产位于独立SwordShield目录，源资源未修改。原地图恢复后保留编辑器供用户PIE。

最终完整Editor构建成功；`Final/index.json` 15/15成功（7无警告、8带既有测试靶无模型/socket等警告），0失败/未运行。集合：SwordShield 4、ArcaneNova 4、Rooms 5、Roguelike.Combat 2。进程退出码0。手动PIE验收仍待用户完成，不勾选为已验收。

## 2026-10-31 新存档入口崩溃修复

用户新建存档后点击剑盾试炼，FindArrivalStart仍读取GI的Dungeon1PlayerStart1，生成地图只存在RG_Entrance，导致InitNewPlayer失败及引擎Fatal。此前入口探针未设置菜单初始化的PlayerStartTag，遗漏该路径。

修复仅在试炼OpenLevel选项增加ArrivalGroup=RG_Entrance，不清空/修改存档出生标记，不改全局出生点回退规则。增加Rooms.SwordTrialOverridesLegacySaveArrival：调用真实入口、检查旅行URL及实际出生点解析、确认GI标记保持原值和原单人模式无额外覆盖。测试夹具需把完整URL解析为选项后传给GameMode，避免与InitGame合同不符。ArrivalFinal覆盖Rooms、Travel、SwordShield共15项全部成功，进程退出0。运行入口探针也改为先设置新存档标记。

实际PIE验证通过：先设置GI->PlayerStartTag= Dungeon1PlayerStart1，再调用StartSwordShieldTrial，日志11:24:26确认Player slot 0 -> TravelPlayerStart_0，生成BP_SwordShieldPlayer与专属GameMode，证据arrival-runtime.json。已结束测试PIE，保留编辑器。
