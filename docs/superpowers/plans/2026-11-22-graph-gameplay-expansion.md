# 技能图玩法扩展实施计划

> 执行方式：superpowers:executing-plans；本地main直接开发，保留现有未提交工作，不提交或推送。用户“按照计划进行开发”已授权本规范范围。

**Goal:** 完成传播可反复编辑、真实并发分裂，以及可组合的选敌和条件能力。

**Architecture:** 继续使用Config/NodeExecution/Registry、类型化边及同一GraphInstance；原GA处理输入、费用、动画。新行为通过可信片段与原配方事务接入工作台。

**Spec:** `docs/superpowers/specs/2026-11-22-graph-gameplay-expansion.md`

## 全局约束

不修改现有Content资产或正式技能授予；保持旧Forked串行缺省；新参数枚举只追加；运行图冻结、原生命、单次费用和同根预算不变；所有失败/取消归还本调用的资源。

## 任务及接口

- [x] **1. 基线和失败测试。** 保存Source/Content状态；新增 `AbilityGraphGameplayExpansionTests.cpp`，用真实投递和注册表观察旧版不支持并发、参数及新节点。记录red证据。
- [x] **2. 传播与并发。** 修改 `AbilityPropagationNodes`、`AbilityForkedPropagationNodes`、`AbilityGraphInstance` 和模板。新增明确参数轴，支持原位修改和策略互换；共用一个多投递调度器保留串行兼容，支持只读物理准入状态及并发位等待。测试真实同帧多弹体、取消/预算/原生命去重。
- [x] **3. 选敌与条件。** 新增共享 `AbilityTargetSelectionService` 及 `AbilityGameplayRuleNodes`，登记有限同步Query/DamageGate/Chance策略。使用实际Actor/Mass生命与属性，工作台可信片段追加、参数修改及后继组合。测试最近/最远/低血、击杀/未击杀、概率端点及错误载荷。
- [x] **4. 能力覆盖及游戏验证。** 注册表只读枚举，自动报告实际目录节点和参数能力，覆盖表链接本次证据。扩展第四页真实脚本，核验新Forked并发、多策略参数、条件后续；运行专项、全量及真实RHI/单机/远端；只读终审、文档截图和用户PIE说明，恢复编辑器设置。

## 审查重点

1. 多投递同帧回执、同步取消和GC：逐步身份与精确lease独立清理。
2. 已命中/预留目标交叉：同一原生命不重复载荷；复用后的新生命不借旧令牌命中。
3. 并发饱和与累计耗尽区分：前者等待，后者真实预算终结。
4. 条件后的历史位置不升级为活目标权限，查询必须重新捕获真实候选。
5. 静态参数存在不等于工作台生效；真实配置读回与执行结果均验证。
