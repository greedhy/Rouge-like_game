# Unique Equipment Implementation Plan

> **For agentic workers:** Use the existing project workflow and coordinated subagents; checklist records this implementation.

**Goal:** 五件可穿脱的暗金、表格驱动百分比、FireBolt 两件套和剑盾 Attack1 火焰变体。

**Architecture:** 专用安全 GE 类型复用 EquipmentComponent 的单实例句柄；共享投射物动作按装备快照改局部参数；固定暗金独立作者脚本。

**Tech Stack:** UE 5.8 C++、GAS、GameplayTags、DataTable/CurveTable、UE Python、Automation。

**Spec:** `docs/superpowers/specs/2026-11-16-unique-equipment-design.md`

## Global Constraints

在本地 main；保留已有修改；不提交或推送；数值引用曲线表；不改随机 RollData 和正式掉落规则。严格服务器权威，资源不补满，清理按实例。不改原 GA 费用和伤害执行通路。

## Review Focus

普通/随机 GE 意外获得乘法权限；0/非有限倍率导致不可恢复属性；等级上限吞掉额外投射物；同件重复 Tag 伪造套装；卸装影响在途投射物或重复恢复叠加。

## Task 1: 安全百分比 GE 与展示

- [x] 先新增测试，验证专用 GE 可接收 Channel2 MultiplyCompound，旧 GE 拒绝，并观察旧实现失败。
- [x] 新增 `Equipment/UniqueEquipmentGameplayEffect.h/.cpp`，实现设计的规则字段与范围校验。
- [x] 局部修改 `EquipmentGameplayEffect.h/.cpp` 的虚校验入口和 `EquipmentGenerator.cpp` 固定说明分支，保留已有改动。
- [x] 新增 `Private/Tests/UniqueEquipmentTests.cpp`：真实属性穿脱、两因子、上限、恢复与非法数据。

## Task 2: FireBolt 技能变体

- [x] 新增共享动作行为测试，先记录无变体时数量/伤害不满足的失败。
- [x] 新增 `EquipmentAbilityModifiers.h/.cpp`，仅读取生效暗金 GE，解析设计约定规则；校验与组合上限。
- [x] 扩展传统投射物 Settings 的 opt-in Tag，FireBolt 设置现有身份，原/图统一动作注入局部参数。
- [x] 验证1→3、3→5、55%/70%、禁追踪、空Tag无影响、卸装下一次恢复与在途快照。

## Task 2b: 用户指定的近战变体

- [x] `UniqueEquipmentMeleeTests.cpp` 实际Mass抗性测试先失败，报告 `Saved/Automation/UniqueEquipment/MeleeRed`。
- [x] `EquipmentMeleeModifiers.h/.cpp` 读取生效暗金，冻结本次接触伤害/类型/特效，冲突转换失败关闭。
- [x] `StanderWeaponActionAbility` 传精确ActionTag，`ExecuteStrike`使用本次快照，不改几何/次数/时间与旧取消保护。
- [x] 验证原伤害20遇50%物抗为10，战靴转火后36；非目标技能/卸装恢复10。
- [x] 独立审查后增加真实 `GA_Attack1 → Timeline → 接触动作 → Mass扣血` 回归；穿戴与卸装均通过。额外移除接线的变异验证因并行模块编译错误未执行，未冒充通过。

## Task 3: 真实资产与测试入口

- [x] 制作 `Scripts/Equipment/author_unique_equipment.py`：缺失创建、合并保留、只读验证模式与报告。
- [x] 新增显式开发控制台样例生成命令，服务器生成五个 Soul，不改正式地图或库存。
- [x] 构建后运行作者脚本，独立进程验证、幂等重跑、真实资产测试。

## Task 4: 回归与交付

- [x] 将网络研究与方案保存 `docs/2026-11-16-unique-equipment-guide.md`，同步装备制作流程当前支持边界。
- [x] 运行装备/传统节点回归和全项目测试，记录失败及实际覆盖；独立审查后补齐真实近战GA接线回归。全项目既有目录失败有历史哈希对比证据，未掩盖。
- [x] 文档记录实际结果、控制台入口、调参方式和待 PIE/联网验收，不将资产读回冒充视觉通过。

## 交付边界

本批暗金与真实资产的成功构建/测试见制作指南。测试后工作区另一个任务继续开发格挡/弹反，新增代码引发访问受保护成员的编译错误；用户明确要求保留并行修改，本任务未修改该模块。当前全部源码不能宣称完整构建通过。手动PIE、双客户端画面及实战平衡仍待验收。
