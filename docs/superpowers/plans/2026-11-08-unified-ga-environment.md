# Unified GA Environment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有基线合入 GitHub 主分支后，交付统一 GA 环境的首个可运行切片，并保留完整迁移任务为未完成。

**Architecture:** 共享目录、图契约、验证、生命令牌与有界执行器；先提取现有石化服务，再用 Delay/Petrify 两个节点完成真实 GAS 激活。旧执行器和资产保持兼容，后续按迁移矩阵逐族收敛。

**Tech Stack:** UE 5.8、C++、GAS、Actor/Mass、UE DataAsset、现有 GameplayEffect/复制通路。

**Spec:** `docs/superpowers/specs/2026-11-08-unified-ga-environment.md`

## Global Constraints

- 使用项目当前 UE 5.8、C++、GAS 和 Actor/Mass 通路，不引入第二套 ASC、背包或伤害结算系统。
- 保留已有 GA 路径、技能 Tag、输入槽语义、装备实例身份与存档身份；迁移只改变经过验证的内部执行实现。
- 共享 DataAsset 是只读配置；运行状态放入每次执行实例，不在施法时修改共享 DA/CDO。
- 服务器决定目标、费用、概率、控制和伤害；首实现包不增加客户端预测图。
- 必须遵守 `docs/cpp-readability-standard.md`、`.clang-format`；涉及敌人状态或池复用须遵守 `docs/monster-development-standard.md`。
- P0 主分支合并完成前不编写新框架产品代码或创建新框架资产。
- 首实现包完成不等于整体目标完成；迁移矩阵未清零前不得宣称“全部 GA 已统一”。

## Review Focus

- 目标 Actor 在 Delay 中被回收并复用：旧事件不得石化新生命（Task 3/6）。
- 同一 PlayerState ASC 的 Avatar 在 Delay 中替换：旧源不能借新 Avatar 身份继续施法（Task 3/6）。
- 节点完成回调同步取消整个图：不得重入追加完成/重复石化（Task 5）。
- 致死目标和客户端伪造身份：Petrify 不支持位置快照替代活目标，必须拒绝（Task 3/4）。
- 世界退出或 GA 已结束但子节点仍等待：明确的资源所有者必须回收任务，不能依赖 UObject Outer 自动保活（Task 5/6）。

## 文件边界与统一验证命令

以下均相对工程根目录。新模块放入现有 `test_25d`，不另建插件；首包无需更改引擎。

- `Public/Private/AbilitySystem/Unified/`：图类型、注册表、验证器、生命令牌、状态服务、执行实例和 GA 入口。
- `Private/Tests/AbilityGraph*Tests.cpp`：独立逻辑、世界夹具及真实 GA 回归。
- `Scripts/GA/Unified/`：目录审计、首包样例资产制作、PIE 验证；不格式化其他 Python。
- `docs/ga-unified-migration-matrix.json`：机器可读逐资产迁移清单；`docs/ga-unified-authoring-guide.md`：后续制作约束。

完整构建：

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat' test_25dEditor Win64 Development '-Project=C:/Users/greedy/Documents/Unreal Projects/test_25d/test_25d.uproject' -WaitMutex -NoHotReloadFromIDE -NoUBA
```

自动化统一由 Task 1 创建 `Scripts/GA/Unified/test.ps1 -Filter <filter>`。脚本使用 UnrealEditor-Cmd、`-unattended -NullRHI`、`Automation RunTests`、`-TestExit="Automation Test Queue Empty"`，每次写独立的 UTC 时间戳报告目录到 `Saved/Automation/UnifiedGA/`。必须读取 index.json 的测试数量/失败数，零测试或预期测试缺失均退出失败；不能只信进程退出码。日志单独写 Saved/Logs。DLL 锁是构建阻塞，不触发强制结束用户编辑器。

## Task 0: 审核并合并现有基线

**Files:** 当前待合并的生产文件清单；只在审核后确定，不使用无差别 `git add .`。

**Interfaces:** Produces：远端主分支合并 SHA、候选提交清单、准确验证报告与 `codex/` 新开发分支。

- [x] 按 spec 的 P0 条目核对远端 HEAD、工作区差异、资产依赖、LFS 与忽略规则，保存可审阅清单。
- [x] 构建准确候选树；运行与现有改动对应的原型、扩展、Batch6、时间轴、Stander/装备、召唤/遭遇回归，登记失败和未验证项。不同测试不因名称相似自动视为覆盖。
- [x] 审核后提交现有基线，按仓库规则推送并合入 GitHub 主分支；不强推或绕过审批。
- [x] `git fetch` 后确认远端主分支包含候选提交，记录 SHA/PR 链接。若未合入，到此停止产品开发。
- [x] 从已确认基线创建 `codex/unified-ga-environment` 或未占用的同前缀分支。隔离工作树只在能完整保留当前必要资产时采用。

## Task 1: 可追溯目录和迁移矩阵

**Files:**
- Create: `Scripts/GA/Unified/audit_catalog.py`
- Create: `Scripts/GA/Unified/test.ps1`
- Create: `docs/ga-unified-migration-matrix.json`
- Create: `docs/ga-unified-authoring-guide.md`

**Interfaces:**
- Consumes：UE AssetRegistry、`UGameplayAbility` 类继承、UAbilityPrototype/UExtensionNode、GA/动作配置实际属性。
- Produces：JSON schemaVersion=1；每行 `assetPath, generatedClass, parentClass, family, configPaths, targetSupport, lifecycle, compatibilityProfile, migrationStatus, replacementPath, evidence, issues`。
- `family`：ActorPrototype/MixedPrototype/Extension/Batch6Special/TimelineWeapon/Traditional/Unknown。
- `migrationStatus`：Inventoried/AdapterRequired/Adapting/Verified/Retired；Unknown 永远不能自动 Verified。

- [ ] 为审计脚本增加自检：同一路径重复行、未知父类被遗漏、失效配置引用、直接覆盖已有人工 evidence 均失败；保留人工状态，输出差异供合并。
- [ ] 读取全项目 Blueprint 资产与派生关系，包含间接子类及敌人/装备 GA；审计不保存任何 UE 资产。原型与 GA 分别建行，通过 configPaths 连接。
- [ ] 首次运行输出实际统计与未识别项；矩阵每一行先保持 Inventoried/AdapterRequired，不因存在源码就 Verified。
- [ ] 创建上述 test.ps1；用无匹配过滤器验证零测试是失败，再跑一个已知旧测试验证报告解析正确。
- [ ] 提交目录、指南、脚本和矩阵，提交消息 `docs: inventory gameplay ability migration baseline`。

## Task 2: V1 图契约、注册表和静态验证

**Files:**
- Create: `Source/test_25d/Public/AbilitySystem/Unified/AbilityGraphTypes.h`
- Create: `Source/test_25d/Public/AbilitySystem/Unified/AbilityGraphDefinition.h`
- Create: `Source/test_25d/Private/AbilitySystem/Unified/AbilityGraphDefinition.cpp`
- Create: `Source/test_25d/Public/AbilitySystem/Unified/AbilityNodeRegistry.h`
- Create: `Source/test_25d/Private/AbilitySystem/Unified/AbilityNodeRegistry.cpp`
- Create: `Source/test_25d/Private/Tests/AbilityGraphValidationTests.cpp`

**Interfaces:**
- `EAbilityGraphEndReason {Completed, Expired, Blocked, TargetLost, Cancelled, Failed, BudgetExceeded, SourceInvalid}`。
- `EAbilityGraphCancelScope {Node, Subtree, Cast}`；首包仅实现 Cast，验证拒绝配置未实现范围，不假装支持全部。
- `FAbilityGraphBudget`：MaxInvocations=64、MaxEvents=256、MaxActiveNodes=32、MaxDepth=8、MaxEventsPerTick=64。
- `UAbilityNodeConfig : UObject` 为 Abstract/EditInlineNew/DefaultToInstanced；`UAbilityDelayNodeConfig.DelaySeconds`、`UAbilityPetrifyNodeConfig.DurationSeconds/StoneMaterial` 为首包派生配置。
- `FAbilityGraphNode`：FGuid NodeId、FName TypeId、Instanced TObjectPtr<UAbilityNodeConfig> Config。
- `FAbilityGraphEdge`：FGuid From/To、FName OutputPort；首包一个执行输入，端口携带同一已捕获目标。
- `UAbilityGraphDefinition : UDataAsset`：SchemaVersion=1、EntryNodeId、Nodes、Edges、Budget、CompatibilityProfile=`UnifiedV1`。
- `FAbilityNodeDescriptor`：TypeId、ConfigClass、OutputPorts、支持目标标志、是否异步。
- `FAbilityNodeRegistry::Register(const FAbilityNodeDescriptor&, FString& Error)`；`Find(FName) const`；`RegisterBuiltins()` 注册 `Control.Delay` 与 `Status.Petrify`。
- `bool UAbilityGraphDefinition::Validate(const FAbilityNodeRegistry&, TArray<FString>& Errors) const`。

- [ ] 写 `test_25d.UnifiedGA.Validation.*`：有效 Delay→Petrify 通过；未知 TypeId、错 ConfigClass、重复/空 NodeId、失效 Entry/边、未知端口、环、不可达节点、非有限/非正 Duration、超预算均失败；同一子节点被两条 DAG 分支引用合法。
- [ ] 构建/运行测试确认未实现时失败，再实现上述契约；错误包含 NodeId/端口，不修改输入 DA。
- [ ] Registry 只声明已实现能力；首包 Delay 输出 Completed/Cancelled/Failed，Petrify 输出 Completed/Failed；业务执行图禁止连接 Cancelled/Failed 到效果节点，避免取消后补效果。
- [ ] 运行 `test.ps1 -Filter test_25d.UnifiedGA.Validation`，全部用例存在且通过。
- [ ] 提交 `feat: define validated unified ability graphs`。

## Task 3: 来源与目标生命令牌

**Files:**
- Create: `Source/test_25d/Public/AbilitySystem/Unified/AbilityExecutionIdentity.h`
- Create: `Source/test_25d/Private/AbilitySystem/Unified/AbilityExecutionIdentity.cpp`
- Create: `Source/test_25d/Private/Tests/AbilityGraphIdentityTests.cpp`

**Interfaces:**
- `FAbilitySourceToken`：弱 World、Avatar、ASC；可选 DungeonRunId/源 Enemy Lease。绑定 Capture 时 ASC Avatar，拒绝后续 Avatar 变化。
- `FAbilityTargetToken`：弱 World、现有 FPureMassCombatTarget、Actor Lease（适用时）、MassEntityHandle、可选 DungeonRunId。Mass handle 通过当前 UPureMassSubsystem::GetEntity 捕获，不新增 ID 分配机制。
- `AbilityExecutionIdentity::CaptureSource(AActor*, FAbilitySourceToken&, FString&)`。
- `AbilityExecutionIdentity::CaptureTarget(const FAbilitySourceToken&, const FPureMassCombatTarget&, FAbilityTargetToken&, FString&)`。
- `AbilityExecutionIdentity::ResolveSource(const FAbilitySourceToken&, AActor*&)`。
- `AbilityExecutionIdentity::ResolveTarget(const FAbilitySourceToken&, const FAbilityTargetToken&, FVector&)`。

- [ ] 写 `test_25d.UnifiedGA.Identity.*`：同生命有效；Actor 休眠/负 Lease/下一 Lease 失败；同世界 Mass 原 handle 有效、移除后失败；另一世界同 ID 失败；同 ASC Avatar 替换失败；RunId 变化失败；敌对与房间权限仍走 PureMassCombat::Resolve。
- [ ] 先运行证明缺少接口/行为，再实现；非池对象使用弱 UObject 身份，不虚构 Lease；没有 DungeonRunDirector 的普通世界可用。
- [ ] Token 仅服务器 Capture，客户端路径返回失败；新令牌只用于统一环境，不静默改变所有旧调用的目标结构。
- [ ] 运行 `test.ps1 -Filter test_25d.UnifiedGA.Identity`；检查无对象生命周期泄漏或残余 WorldContext。
- [ ] 提交 `feat: guard ability execution with lifetime identities`。

## Task 4: 提取石化共享服务并保持旧 GA 行为

**Files:**
- Create: `Source/test_25d/Public/AbilitySystem/Unified/AbilityStatusService.h`
- Create: `Source/test_25d/Private/AbilitySystem/Unified/AbilityStatusService.cpp`
- Modify: `Source/test_25d/Private/AbilitySystem/Batch6/Batch6Petrify.cpp`（GA 目标分支；组件实现保持）
- Create: `Source/test_25d/Private/Tests/AbilityGraphPetrifyServiceTests.cpp`
- Existing regression: `Source/test_25d/Private/Tests/Batch6PetrifyTests.cpp`

**Interfaces:**
- `bool AbilityStatusService::ApplyPetrify(AActor* Source, const FPureMassCombatTarget& Target, float Duration, UMaterialInterface* Material)`。
- 服务负责服务器/参数/活目标校验和 Actor 组件创建或 Mass ApplyPetrify；不 Commit、不重复范围查询、不主动添加额外伤害或 VFX。
- 图节点先 ResolveTarget，再调用服务；旧 GA 保留原 Gather/Visible/Commit/End 顺序，只替换每目标效果应用分支。

- [ ] 写 `test_25d.UnifiedGA.PetrifyService.*`：Actor 获得 Stun/暂停/材质并可解除；Mass bPetrified 生效/到期；失效/友方/跨房间/NaN/非正持续时间失败；重复 Apply 沿用原延长期限，不创建多个组件。
- [ ] 运行红测试，再提取服务；只复制一次已存在的组件注册逻辑，避免把控制规则复制到图节点。
- [ ] 运行 `test.ps1 -Filter "test_25d.UnifiedGA.PetrifyService+test_25d.Batch6"`；旧石化资产、费用、半径及空放语义必须保持。
- [ ] 提交 `refactor: share petrify behavior across ability entry points`。

## Task 5: 最小有界运行器与两个节点

**Files:**
- Create: `Source/test_25d/Public/AbilitySystem/Unified/AbilityGraphInstance.h`
- Create: `Source/test_25d/Private/AbilitySystem/Unified/AbilityGraphInstance.cpp`
- Create: `Source/test_25d/Private/Tests/AbilityGraphRuntimeTests.cpp`

**Interfaces:**
- `FAbilityGraphEvent`：ExecutionId、NodeInvocationId、EventSequence、OutputPort、TargetToken、Location、EndReason。
- `UAbilityGraphInstance : UObject`：`bool Start(UAbilityGraphDefinition*, const FAbilitySourceToken&, const FAbilityTargetToken&, FString& Error)`；`void Advance(float DeltaSeconds)`；`void Cancel(EAbilityGraphEndReason Reason)`；`bool IsFinished() const`；终止原生委托 `OnFinished`。
- 图实例由所属 AbilityTask 的 UPROPERTY 持有；自己不使用全局 singleton/裸定时器保活。Start 复制节点配置为瞬时实例快照，原 DA 修改不改变正在执行的图。
- Delay 记录实例内 Remaining；Petrify 先验证源/目标令牌，再调 Task 4 服务；节点完成只向有界队列入事件，不在回调中递归执行任意深度子节点。
- `NodeInvocationId` 每次执行节点生成；同一调用同一 EventSequence 只消费一次；DAG 不同调用可重复同一节点配置。

- [ ] 写 `test_25d.UnifiedGA.Runtime.*`：0.25 秒前不施加、到点一次；大 Delta 不重复；相同事件去重；不同调用不误去重；取消关闭队列；Completed 回调同步 Cancel 不产生二次终止；预算耗尽按 BudgetExceeded 结束。
- [ ] 写源/目标在 Delay 中失效测试；不可达/非法图 Start 失败且无副作用；Start 前无权限失败。
- [ ] 运行红测试，再实现快照、事件队列、预算及幂等 Cancel；先关入口再释放节点，不给客户端执行玩法。
- [ ] 运行 `test.ps1 -Filter test_25d.UnifiedGA.Runtime`；确认所有测试都验证实际组件/Mass 状态，而非仅检查布尔标志。
- [ ] 提交 `feat: execute bounded delay and petrify graphs`。

## Task 6: 真实 GAS 入口、资产与端到端验证

**Files:**
- Create: `Source/test_25d/Public/AbilitySystem/Unified/AbilityTask_RunGraph.h`
- Create: `Source/test_25d/Private/AbilitySystem/Unified/AbilityTask_RunGraph.cpp`
- Create: `Source/test_25d/Public/AbilitySystem/Unified/UnifiedGraphAbility.h`
- Create: `Source/test_25d/Private/AbilitySystem/Unified/UnifiedGraphAbility.cpp`
- Create: `Source/test_25d/Private/Tests/AbilityGraphIntegrationTests.cpp`
- Create: `Scripts/GA/Unified/author_petrify_sample.py`
- Create: `Scripts/GA/Unified/verify_petrify_pie.py`
- Create UE assets: `/Game/UnifiedAbilities/Samples/DA_DelayedPetrify`、`GA_DelayedPetrify`、`GE_DelayedPetrifyCost`、`GE_DelayedPetrifyCooldown`

**Interfaces:**
- `UAbilityTask_RunGraph::RunAbilityGraph(UGameplayAbility*, UAbilityGraphDefinition*, FAbilitySourceToken, FAbilityTargetToken)` 原生工厂；任务 TickTask 调 Advance，OnDestroy 先取消再清引用；动态 OnFinished(bool bCancelled)。
- `UUnifiedGraphAbility : UMyGameplayAbility`：EditDefaultsOnly Graph 与 CastRange=1200；每 Actor 实例化，入口 LocalPredicted 以运行本地目标任务，但只有服务器回调 Commit/Start 图。客户端发送目标后等待服务器结束；不能改成只在服务器激活却仍等待未创建的客户端目标任务。沿用标准 GAS CostGameplayEffectClass/CooldownGameplayEffectClass，首包不继承旧 Prototype 的 Kind/伤害类校验。
- `ActivateAbility` 创建 UPureMassTargetData，三秒超时；服务器回调验证混合目标/距离/可见性与 Graph，捕获令牌后 Commit 一次，启动 RunGraph；正常/异常完成均 EndAbility。结束清超时及任务，不保留图实例。
- 样例 Graph：Delay=0.25，Petrify Duration=1.0，材质 `/Game/CompositionBatch6/M_PetrifiedStone`；费用 GE=Mana -10，冷却 GE=2 秒，冷却 Tag=`Cooldown.Unified.DelayedPetrify`；技能身份使用 `Abilities.Unified.DelayedPetrify`，两者在 `UnifiedGraphAbility.cpp` 原生注册，不得占用现有 GA Tag。

- [ ] 写 `test_25d.UnifiedGA.Integration.*`：通过 ASC 激活测试 GA，以真实目标任务数据路径送达服务器；费用只减 10、冷却一次、0.25 秒后石化、1 秒后解除；GA 结束后无活动图/任务/超时。
- [ ] 增加取消、Actor 回收复用、同 ASC Avatar 变更、Mass 移除、世界退出的端到端测试；运行红测试再实现入口/任务。
- [ ] 完整构建后运行幂等资产脚本，仅创建缺失样例，重跑保留调参；读回类/Graph/GE/材质引用。样例不自动授予玩家，不改原石化 GA 资产。
- [ ] 运行 `test.ps1 -Filter "test_25d.UnifiedGA+test_25d.Batch6+test_25d.Prototype+test_25d.Extension+test_25d.AbilityTimeline"`，以实际注册测试路径校正过滤器；零覆盖不能视为通过。
- [ ] PIE 脚本在临时测试环境授予样例并通过 ASC 激活，分别验证 Actor/Mass 与取消；恢复临时授予/世界，不保存用户资产。双端验证客户端仅发请求、服务器单次费用/效果、另一端看到石化解除。若未执行，明确保留为未验证，不把 NullRHI 当成联网/视觉验收。
- [ ] 更新矩阵，仅将真实完成的样例/服务行置 Verified；原有未迁移行保持状态。提交 `feat: integrate delayed petrify graph with GAS`。

## Task 7: 首包审查与后续阶段计划

- [ ] 对照 spec 全局约束和 Review Focus 审查实际 diff、报告、资产引用与清理路径，修复真实缺陷后重跑相关测试。
- [ ] 更新 `docs/ga-unified-authoring-guide.md`，记录如何新增配置类型/注册描述/节点测试/生命周期与网络能力声明。
- [ ] 输出首包完成项、仍未完成的迁移矩阵数量、用户 PIE 待验收项；不关闭整体统一目标。
- [ ] 根据此时稳定接口为 P2 旧 Kind 行为迁移建立下一份精确计划，再依次推进 P3/P4/P5。每份都必须包含旧行为金样、资产迁移脚本及回滚/版本识别。

## 自检结果

- 总体目标分为 P0–P5；本计划精确实现 P0/P1，后续仍是必须完成的工作，不是假定已实现。
- 五项 Review Focus 均有指定任务和断言；所有新增接口的消费关系按 Task 2 → 3 → 4 → 5 → 6 排序。
- 首包明确不允许任意旧 GA 嵌套激活，不增加第二套费用、伤害、石化或复制系统。
- 本轮实施状态以 [P1 验证记录](../../2026-11-09-unified-ga-p1-verification.md) 为准；下表区分源码/自动化与人工验收，不把未执行的 PIE 勾为通过。

## 2026-11-09 实施状态

| Task | 实际结果 |
|---|---|
| 0 | 已完成：PR #1、main `f21c74e`、新分支已验证 |
| 1 | 审计/矩阵/指南/验证驱动已完成；656 行，零测试拒绝和已知旧测试实际执行 |
| 2 | 契约/注册表/验证器实现；11 项 Validation 通过 |
| 3 | 生命令牌实现；5 项 Identity 通过，世界清理边界已修复 |
| 4 | 共享服务及旧入口提取实现；4 项服务测试、3 项实际旧 GA 回归通过 |
| 5 | 执行实例/Delay/Petrify 实现；6 项 Runtime 通过 |
| 6 | 真实 GAS 入口、资产及 8 项 Integration 通过；单机/双端 PIE 与视觉未执行，保留待验收 |
| 7 | 代码复核、问题修复、新鲜 300 项全回归完成；P2 精确计划已整理，P2–P5 未实现 |

首包按完整依赖树集中构建和测试。初次缺接口编译、世界清理测试以及来源换角复现均保留红结果；没有为每个独立服务重复运行一轮未实现时的行为测试。实现后的逐族行为与完整回归报告已记录，不能把过程检查框当作不存在的测试证据。
