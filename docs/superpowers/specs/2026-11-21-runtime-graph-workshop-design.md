# Runtime Graph Workshop — free gameplay test version

User requested an in-game GUI displaying real GA nodes/edges, with capability fragments replacing or extending behavior. The latest instruction authorizes a free test version first: complete graph assembly and real casting, no inventory costs. The production rule remains consumable orbs: successful installation consumes the orb and removal does not refund it; elite drops, inventory transactions and persistence are deferred, not implemented by this test version.

## Scope and approach

Reuse the existing Mage catalog, `RuntimeRecipe` entry, temporary preview Spec, T input, server recipe compiler and original GA frontends. This is the test execution mode, whose zero-edit preview already freezes the exported fine graph. It avoids changing formal zero-contribution behavior and avoids claiming a normalized display is the topology executed by an unenrolled formal skill. No new ASC, damage executor, saved combined GA, or production inventory is introduced.

Alternatives considered: extending only the existing base-node dropdown cannot select added nodes or exact edges; a fully unrestricted graph editor would expose unsupported parameters/lifetimes. The selected implementation is a real, interactive graph canvas backed by the server-accepted graph, with registered bounded edit templates and precise rejection reasons. User has explicitly requested proceeding with this test version.

## Required behavior

- Open in PIE through RuntimeRecipe, choose a real Mage class variant, begin free testing, and see its server-accepted nodes, named typed ports and actual edges. The original temporary input/close cleanup contract remains intact.
- Click a node, output port or exact edge `(From, OutputPort, To)`. Inspect its real TypeId and select compatible existing templates. Accepted and draft graphs are visually distinct.
- Preview an edit on the server without publishing it; apply only the exact pending draft at the same revision. Errors/stale drafts leave the previous graph unchanged. No resources are charged.
- Replace a node, append a branch to a port, or insert a registered state fragment on only one selected edge. Parallel append is not described as serial insertion; sibling edges and shared tails survive insertion.
- Newly inserted nodes appear and can be targeted by the next edit. Each new edit is replayed after earlier edits with stable frozen branch definitions. Removal of a dependency is rejected with a readable error rather than silently dropping descendants; undo-last and full reset remain available.
- Hiding the panel permits original T/target/animation/Notify casting. Apply/remove/reset affect future casts only; explicit close cancels this preview's lease and returns only its grants/resources. Other players and formal abilities remain isolated.
- First supported module library reuses existing scaling, typed parameters, Sequential/Forked, Petrify/Slow, trusted Beam branch replacement, plus new exact-edge insertion of registered state fragments. Unsupported original-projection rewrites and payload/lifetime combinations are rejected, not made permissive.

## Architecture and interface contracts

`AbilityRuntimeRecipeGraphView.h` defines pure-value node/port/edge snapshots; developer state owner-only replicates accepted graph, draft graph, draft token and applied source IDs. No graph/config UObject or executable code is sent by a client.

New `FAbilityRuntimeDeveloperEdit` fields: `TargetNodeId`, `TargetEdgeTo`, `SourceId`; existing `OutputPortId` identifies the exact source port. Nonempty TargetNodeId selects the current accepted graph. The old NodeIndex path remains for existing probes, with its original base-relative meaning.

Developer component adds `PreviewGraphEdit(Edit, Error)`, `ApplyGraphDraft(Token, ExpectedRevision, Error)`, `RemoveGraphSource(SourceId, ExpectedRevision, Error)` and matching owned server RPCs. It freezes one pending contribution, validates current binding/revision before commit, and uses the existing SetContribution transaction. This is sufficient while edits are free; production inventory consumption will need a separate coordinated prepare/commit transaction.

`UAbilityRuntimeGraphCanvas` consumes accepted/draft views and emits `(NodeId, PortId, EdgeTo)` selection. It uses runtime UMG/Slate painting, not editor-only graph modules. Layout is automatic; node/port widgets and wire hit-testing select real server identifiers. Pan/scroll/zoom allow larger graphs.

Compiler adds `InsertOnEdge` at the end of the patch-operation enum. A patch identifies source NodeId, OutputPort, TargetEdgeTo, BranchGraph and explicit continuation node/port. It replaces only that edge and connects the fragment's continuation to the old destination. A registered synchronous pass-through adapter retains the original typed payload, waits for its registered effect subtree, then resumes the selected original successor. It cannot discard PreparedInventory responsibility or grant new target permissions. Original-projection edges require explicit supported remapping and otherwise reject.

## Example

```text
Prepare → Scatter → Flight --Hit--> ResolveImpact --TargetFound--> InputDamage

Replace Flight with Sequential:
Prepare → Scatter → Sequential --Hit--> ResolveImpact → InputDamage

Insert Target.Petrify on the exact ResolveImpact→InputDamage edge:
ResolveImpact → PassThrough.LiveTarget --Apply--> Status.Petrify
                    └─ Continue (after effect subtree) → InputDamage
```

The same test GUI can append a post-damage Petrify branch using InputDamage.DamageResolved→ResolveDamageTarget→Petrify. These are different timings and must be labelled accurately.

## Verification and delivery

Baseline hashes and dirty state are captured first. Observe an initial failing feature test, then verify exact-edge selection, payload/continuation, siblings/shared tails, invalid/protected edges, cancellation, accepted/draft rollback, next edit targeting a new node, dependency removal, two owners and original Spec cleanup. Run focused tests, full test_25d with fresh manifests and matching RHI/actual PIE checks. A real UMG session must select an actual wire/node, preview/apply, cast through original input, edit an inserted node, undo/reset, and close cleanly; a server-only test is not sufficient.

Existing unrelated baseline failures stay explicit. Work on local main; preserve dirty changes, do not stage/commit/push or write Content assets. Only root owns editor/build/MCP lifecycle. Final animation/art/readability/feel acceptance belongs to the user.

## Implementation evidence

本免费测试版已实现并通过专项35/35、渲染14/14；全量1034项中1033通过，唯一失败为既有Stander资产基线。同一最终DLL的单机100项及Listen远端108项检查通过并清理，覆盖真实界面精确选边、新节点时长编辑与原前端施法。完整边界和原始报告见[验证记录](../../runtime-graph-workshop-validation.md)。用户视觉与手感验收待进行。
