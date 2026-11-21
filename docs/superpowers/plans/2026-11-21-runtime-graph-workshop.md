# Runtime Graph Workshop Implementation Plan

> **For agentic workers:** Use coordinated implementation with scoped subagents and independent review. Steps use checkbox syntax. Only root owns build/editor/MCP operations.

**Goal:** Deliver a free in-game real-graph editing GUI that previews, applies and reverses registered node/edge changes and casts the resulting GA through its original frontend.

**Architecture:** Extend the existing RuntimeRecipe developer preview rather than production skill enrollment. Server snapshots project the accepted/preview graph as owner-only values. Exact-edge insertion and registered pass-through effects reuse the same compiler/executor.

**Tech Stack:** UE5.8 C++, GAS, runtime UMG/Slate, existing graph compiler/recipe component and Unreal automation.

**Spec:** [Design](../specs/2026-11-21-runtime-graph-workshop-design.md).

## Global Constraints

- Localmain, no branch/commit/stage/push; preserve unrelated dirty files and Content. Root alone launches/builds/stops UE.
- Free test library; no elite/drop/inventory/persistence changes. Production consumable-orb decision is preserved for later work.
- Existing gameplay budgets, exact life identities, original target/animation/cost paths and typed validation remain strict.
- Server-accepted graph is the visible truth; drafts cannot claim acceptance. Old base-index probes retain their old interpretation.
- Source contracts/ownership are fixed below before dependent edits; root controls source freeze during manifests.

## Review Focus

1. A second edit targets a node introduced by the first: replay order and frozen identity must work.
2. One port fans out to two destinations: inserting on one edge cannot rewrite its sibling or shared tail.
3. GE cancels/retires source synchronously: no late continuation, resource leak or new-life capture.
4. Stale preview/another player/base conflict: reject without replacing accepted graph or grants.
5. Hiding versus closing: hide permits original casting; close releases the exact temporary lease only.

## Task 1 — baseline and observed missing feature

- [x] Capture Source/Config, DLL and protected package hashes plus git dirty state in Saved/GAAuthoring/RuntimeGraphWorkshop/20261121-implementation.
- [x] Add/run a feature test proving the old panel lacks the accepted-graph canvas/value projection; confirm the expected RED.

## Task 2 — precise edge insertion and typed continuation

**Owner:** compiler subagent. **Files:** AbilityRuntimeRecipe.h/.cpp; new Nodes/AbilityGraphPassThroughNodes.h/.cpp; register in AbilityNodeRegistry.cpp; new AbilityRuntimeGraphEdgeTests.cpp. Existing modifier template file is coordinated with root.

- [x] Add behavior tests for exact-edge insertion, sibling/shared-tail retention, actual effect-before-successor, invalid payload, protected projection, source retirement/cancel and preserved budget.
- [x] Add enum-tail InsertOnEdge, TargetEdgeTo and explicit continuation fields. Implement selected-edge replacement through existing branch copying/validation.
- [x] Implement/register typed synchronous pass-through adapters using PublishWithContinuation; never accept PreparedInventory or recapture a target.
- [x] Supply a small factory for wrapping an existing registered branch with typed Apply/Continue edges, then run RED/GREEN with root.

## Task 3 — accepted/draft graph state and edit transactions

**Owner:** root. **Files:** new AbilityRuntimeRecipeGraphView.h/.cpp; DeveloperComponent.h/.cpp; ModifierTemplates integration; new AbilityRuntimeGraphWorkshopTests.cpp.

- [x] Publish bounded pure-value accepted nodes/ports/edges, revision/hash and source history using CreateCastSnapshot.
- [x] Implement current-graph TargetNodeId editing while preserving legacy base NodeIndex behavior.
- [x] Implement server preview token and exact revision commit; keep a frozen contribution so preview/apply/new-node identity matches.
- [x] Give successive graph edits deterministic ordering, explicit source removal, safe dependency rejection, undo-last/reset, draft invalidation and cleanup.
- [x] Test accepted/draft equality, no mutation on failed/stale edits, second edit on inserted node, two-owner isolation and formal grants unchanged.

## Task 4 — clickable runtime graph canvas and panel workflow

**Owner:** UI subagent. **Files:** new UI/Widget/AbilityRuntimeGraphCanvas.h/.cpp; existing AbilityRuntimeRecipePanel.h/.cpp; new AbilityRuntimeGraphCanvasTests.cpp.

- [x] Build an auto-layout canvas for exact node/port/wire selection with scrolling/panning/zoom and selected-state feedback.
- [x] Integrate accepted versus draft views, selection details, free module choices, preview/apply/discard, history undo and reset into RuntimeRecipe panel.
- [x] Keep existing widget names and close/focus/input semantics for legacy probes; new graph selection sends exact IDs rather than base indices.
- [x] Verify canvas geometry hit selection and delegate requests; do not call gameplay damage or invent client graph state.

## Task 5 — build, real gameplay and review

- [x] Build and run focused/new tests plus existing recipe/panel/structural suites; fix real failures under source ownership.
- [x] Run full test_25d and rendered checks with frozen input manifests; classify known baseline issues independently.
- [x] In Standalone and Listen remote, drive the actual graph UI, preview/apply, cast using original T/target/Notify, select a newly inserted node, undo/reset and close with exact cleanup.
- [x] Independent spec/code review, update usage/spec/example/pseudocode docs and evidence ledger; no success claim from compiler or server-only tests alone.

Ledger: Saved/GAAuthoring/RuntimeGraphWorkshop/20261121-implementation/ledger.md. User authorization is the explicit request to develop the free test version; no additional design approval pause is needed for reversible work in this scope.

## Final delivery status — 2026-11-21

免费测试版实施与验证完成。专项 `221219-4f716b4d` 为35/35；全量 `221324-935dc808` 执行1034项、1033通过，唯一失败是既有Stander资产引用基线，0未执行；渲染 `221455-f39ab1cb` 为14/14。

最终同一DLL下，Standalone报告 `cb2864b4499e4b8aaa54dd1b7d8cf15f` 的100项检查和Listen远端 `90bde10039264b9c9710f7ebb4f0d5d7` 的108项检查全部通过并完成清理。进程PID139704的加载模块SHA已核验；1533个验证输入和773个保护包未变化。原地图、节流设置已还原，无PIE或未保存资产。

实际PIE曾暴露单目标Petrify缺少时长接口，初始批量状态测试不能代表该路径。现已补齐真实Petrify/Slow的时长消费及独立Actor到期测试，并重新运行专项、全量、渲染和两种实际界面模式。评审发现的身份、正式技能隔离与会话清理问题均已修复并纳入回归。

[操作](../../runtime-graph-workshop-guide.md) · [验证记录](../../runtime-graph-workshop-validation.md) · [元素球需求与伪代码](../../2026-11-21-element-orbs-and-graph-workshop.md)。正式掉落、物品消耗、配方存档依照最新要求留到后续；用户表现与手感验收待进行。本地main未暂存/提交/推送，未保存Content资产。UnLua测试环境条件保留在验证记录。
