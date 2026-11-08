# Stander Showcase Implementation Plan

> 使用superpowers:executing-plans在当前会话逐项执行，末尾一次独立只读审查。

**Goal:** 可选择进入生成房间的Stander剑盾动作基准与四种传统GA。
**Architecture:** 复用DungeonPlayerCharacter、PlayerState ASC、FSwordStrike、现有伤害/格挡GE；独立角色配置与生命周期执行器。
**Tech Stack:** UE5.8 C++、GAS、编辑器Python、原生及PIE自动化。
**Spec:** [设计](../../stander-showcase-design.md)

## 约束与决策

用户已选择上一轮提出的展示基准路线并授权开发，直接实施；本轮不再要求方案确认。沿用共享工程以使用真实资产与已有未提交源码，不创建会丢失这些依赖的新checkout；不提交，不改素材包原资源。

Ruling: 首批剑盾四GA、单人独立展示入口；不扩到全部武器。采用动画原地副本+服务端Sweep位移，方便边界与取消验证；成本是需校准时间曲线，尚不支持任意RootMotion动画直接拖入。

## 任务

- [x] 1. 源动作/挂点采样，记录接触候选、冲刺阶段，保存设计与构建清单。
- [x] 2. 新配置、角色与GA生命周期，原生测试先红后绿；复用旧伤害与格挡。
- [x] 3. 制作独立动画、蒙太奇、AnimBP、DA、GA、角色/GameMode及技能目录，保存编译读回。
- [x] 4. 接入模式入口并验证包含新存档遗留出生标记的跳转。
- [x] 5. 实际伤害、格挡反击、位移/取消/复活与表现运行测试，修复并重验。
- [x] 6. 一次独立审查、相关回归、恢复环境、更新验收说明。

## 文件边界

Public/Player/StanderCombatConfig.h与Private/Player实现：派生SwordShieldConfig，冲刺/反击数据和校验。
Public/Character/StanderPlayer.h与Private/Character实现：模型/武器组件，仅复用父类ASC。
Public/AbilitySystem/Ability/StanderAbility.h与Private实现：四种身份与统一实例执行器、守卫阶段反击回调。
Private/Tests/StanderTests.cpp：配置/回调/入口测试。
Scripts/GA/Stander：只读审计、资产制作、运行测试和恢复。
RoomSubsystem、RoomMenuWidget：独立入口小幅扩展。

## Review Focus

格挡有效伤害与普通属性GE回调区别；反击只能一次且结束解绑。
冲刺碰墙、跨房间、低帧率以及取消不补位移/伤害。
倒地时不重新启用移动，复活后能力/ASC仍可用。
新存档带Dungeon1PlayerStart1、普通法师及TinyHero入口。
动画根位移移除、剑盾握持、命中与特效时序一致。

## Ledger

开始：素材已审核428动画全部Stander_Skeleton；本轮源动作精查进行中。编辑器最初未运行，启动专用检查会话。

完成：源动作、源示例武器相对变换及实际PIE姿态采样完成；26个专属资产编译保存读回通过。旧Parry试验副本无引用后清理。
Task 2: DashPhase先以stub观察预期RED，再实现阶段算法；最终浮点断言显式1e-6容差，NativeVerified 14/14 GREEN。新增CounterRequiresBlockedDamage与AuthoredAssets通过。
Task 4: StanderTrialOverridesLegacySaveArrival与旧Sword入口通过，真实PIE生成地图成功。
Task 5: combat/special/lifecycle/cooldown/movement/room-combat/room-boundary全部passed=true；反击源Block_Parry仅举盾，最终改独立Attack1 CounterStrike；突进0.3333–0.5秒，0.5167命中。
Final: 独立只读review_stander_final审查无Critical/Important、无延期Minor；原生14/14通过；编辑器恢复LoadMenu且无未保存资产。用户PIE验收待进行。不提交共享工作区，不删除计划记录。
