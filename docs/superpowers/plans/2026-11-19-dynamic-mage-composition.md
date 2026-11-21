# Dynamic Mage Composition Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for coordinated execution; independent implementation/test tasks may be delegated. Steps use checkbox syntax. Build/UE mutations belong to one explicitly assigned execution owner at a time; the root may transfer exclusive ownership to a validation worker.

**Goal:** Preserve every audited mage GA while exposing its reusable behavior through runtime-generated Schema 2 recipes and a real-casting development panel.

**Architecture:** Original frontends keep animation, targeting and cost contracts. Shared selection, cadence, delivery, propagation and payload services are consumed by original adapters and graph nodes. PlayerState-owned recipe state supplies immutable per-cast definitions to the existing graph executor, with no nested GA activation.

**Tech Stack:** UE 5.8 C++, GAS, UMG, existing unified graph/Actor GAS/PureMass services, Unreal automation and Python readback.

**Spec:** [Approved design](../specs/2026-11-19-dynamic-mage-composition-design.md). User approved implementation on 2026-11-19.

## Global Constraints

- Work on local main; no automatic commit/push or replacement of unrelated edits.
- [Inventory](../../ga-dynamic-mage-inventory.json): 60 mage identities / 86 catalog classes, four related protected classes. Audit status is not implementation status.
- Preserve old identity, assets/tuning, animation points, profile-specific costs/cadence and cancellation. No new implementation of placeholder siphon/protection skills.
- Keep one graph executor and one damage owner; use original source/target life tokens and existing movement/status services.
- New finite graphs: 64 calls, 256 events, 32 active nodes, depth 8, 64 events/tick; explicitly registered sustained behavior must retain bounded resources without truncating old indefinite channels.
- Every task uses test-first behavior verification; staging changes is not authorization to commit. Tests and source may be staged neither implicitly nor wholesale.
- Development panel + real casting; no production rune drops/choice/save migration.

## Review Focus

1. First hit kills its target: historical impact must still authorize propagation by position, never a new target life (Tasks 3/4).
2. Synchronous damage/cancellation/reflection: old callback must not advance a replacement cast or double damage (Tasks 3/4/8).
3. Two players using the same original GA: recipe, held inputs and resource cleanup stay isolated (Tasks 5/9).
4. Existing channel survives more than 256 events with bounded queues and correct cost/cleanup (Tasks 6/8).
5. Failed recipe edit or UI close: preserve last valid cast definition and original grants/slots/equipment (Tasks 5/9).

## Task 1: Freeze current baseline and enumerate exact contracts

**Files:** Saved/GAAuthoring/DynamicMage/20261119-implementation; existing Scripts/GA/Unified/test.ps1; docs/ga-dynamic-mage-inventory.json.

- [x] Capture current Source/Config hashes, selected original package hashes, git dirty list and source copies. Record DLL fingerprint separately.
- [x] Build current Editor Development tree; run fresh full `test_25d` automation with manifest. Any pre-existing failures remain reported and are investigated before refactoring the affected contract.
- [x] Read actual Blueprint graphs and CDOs from approved audit; map all identities to original profile and behavior fragments. Reconcile newly discovered assets without inventing a passing status.

Expected: immutable baseline report and reproducible per-family targets, no changes to original Content.

## Task 2: Shared query, geometry and cadence contracts

**Create:** Public/Private/AbilitySystem/Unified/AbilityBehaviorPrimitives.h/.cpp (under Source/test_25d).
**Modify:** LegacyActorAbilityDriver.cpp, LegacyMixedAbilityDriver.cpp, LegacyExtensionAbilityDriver.cpp and applicable Nova/shape/shard nodes only in explicitly assigned portions.
**Tests:** Private/Tests/AbilityBehaviorPrimitiveTests.cpp; existing LegacyDrivers/shape cadence tests.

**Interfaces:** Provide original-life target candidate ordering/filtering and deterministic cadence advancement with explicit compatibility policy, no damage or UObject state ownership in pure helpers. Exact public signatures are recorded in the implementation ledger before consumers change.

- [x] Add tests for nearest visible nonvisited target, tie ordering, shape boundary/height and Actor/Mixed/Extension catch-up/tail policies; run RED against missing/old behavior.
- [x] Extract existing algorithms into shared primitives and call them from original drivers without changing traversal or cost points.
- [x] Expose selection/shape/schedule nodes using the same implementation; run positive/invalid/cancel cases and original family regressions.

Expected: behavior can be reused without invoking a whole Legacy driver; original profile outputs unchanged.

## Task 3: Real delivery request/result ownership

**Create:** Public/Private/AbilitySystem/Unified/AbilityDeliveryService.h/.cpp and Nodes/AbilityDeliveryNodeExecutions.h/.cpp; Private/Tests/AbilityDeliveryTests.cpp.
**Modify:** Actor/MyProjectile.h/.cpp, PureMass/PureMassLegacyProjectile.h/.cpp only through coordinated integration; registry registration through root.

**Interfaces:** A per-delivery request carries original source token, origin/direction/optional target, immutable damage/presentation settings, root/step identity. Result carries Hit/Blocked/Expired/TargetLost/Cancelled and optional confirmed impact. Exactly one result can close a delivery. New types use the `FAbilityDelivery*` prefix.

- [x] Add failing tests for real flight delaying Hit until contact, wall/expiry/target loss, cancellation and duplicate callback rejection; cover Actor and Mass.
- [x] Implement a graph-owned delivery lifetime and explicit internal/external payload ownership. Original projectiles retain original mode by default; new mode cannot damage internally and in graph twice.
- [x] Reuse collision/hostile/visibility/source lifetime services, retain original FireBolt adapter behavior, reflection/dodge interactions and presentation.
- [x] Verify native firebolt parity and exact once damage/receipt; no static combined GA assets.

Evidence: delivery/stream/profile tests and actual FireBolt authority-transport frontend cases are GREEN in `20261120T004924-44f5921d`. This closes the action/service task; actual local cursor and remote network remain Task 9.

## Task 4: Pluggable bounded propagation

**Create:** Public/Private/AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h/.cpp; Private/Tests/AbilityPropagationTests.cpp.
**Modify:** Batch6 chain/shared chain behavior where compatible; root registration and graph lifecycle integration.

**Interfaces:** Propagation config selects delivery strategy/payload with jump count/delay/radius/falloff; runtime state owns root identity, original-life visited set, remaining budget and current delivery ID. It waits for Task 3 receipts. It never activates another GA.

- [x] RED: A lethal hit followed by delay spawns B's real projectile, no C before B Hit; mixed Actor/Mass; blocked/expired/no-target stop.
- [x] Implement finite state machine using Tasks 2/3, historical impact anchoring and shared root budget. Graph remains acyclic.
- [x] Test branching/shared count, falloff, synchronous cancellation, stale target life and source retirement. Compare original lightning behavior with original profile adapters.

Evidence: managed Sequential/Forked service behavior and original lightning comparison were GREEN in `20261120T061450-f1eb9994`; actual remote sequential Fire is separately GREEN in `RealCasting/99f8703e5d8046fa9338c1f5405cbf3b`. Trusted developer Forked wiring, payload replacement and shared-budget checks subsequently passed in `20261120T090852-39fdc764`; the earlier RED remains in its original report.

## Task 5: Runtime recipe compiler and per-player revisions

**Create:** Public/Private/AbilitySystem/Unified/AbilityRuntimeRecipe.h/.cpp, AbilityRuntimeRecipeComponent.h/.cpp; Private/Tests/AbilityRuntimeRecipeTests.cpp.
**Modify:** UnifiedGraphAbility.h/.cpp, MyGameplayAbility.h/.cpp only for frozen per-activation recipe lookup; PlayerState component integration through a minimal coordinated patch.

**Interfaces:** `FAbilityRuntimeRecipe` identifies base ability, ordered source contributions and revision; compiler returns a transient `UAbilityGraphDefinition` plus diagnostics. Component applies/removes source contributions transactionally, returns a read-only cast snapshot and last error. Components are discovered from existing ASC owner.

- [x] RED: runtime-only graph, deterministic ordering, unknown/invalid fragments, forbidden target/port, conflict, rollback on failed edit, two-owner isolation and immutable in-flight snapshot.
- [x] Implement catalog + validation/compiler using existing graph registry; no product dependency on editor/Python.
- [x] Resolve one immutable recipe before input/target handling; freeze into activation state. Zero modifiers follows original frontend behavior. Do not mutate CDO/shared Graph.
- [x] Ensure original equipment rule values are consumed once at their defined stage, not multiplied by both compiler and action.

Evidence: transactional recipe, exact preview ownership, native per-cast frames, actual source withdrawal and independent main/DOT scalar cases were GREEN in `20261120T004924-44f5921d` and remain covered by subsequent integration. Every-class default reconstruction is separately recorded under Tasks 6–8/10, not inferred from this component test.

## Task 6: Separate distribution, motion, shape and temporal effects

**Files:** Tasks 2/3 node family plus Legacy drivers, AbilityNovaNode, AbilityArcaneShardNode, relevant state/slow actions; tests in AbilityDynamicShapeTests.cpp and AbilityDynamicCadenceTests.cpp.

- [x] RED tests for scatter+homing, piercing+payload, returning phase history, wave+status, trap/delay+different payload, field+period/end and emitter+alternate delivery.
- [x] Extract and register replaceable distribution/motion/piercing/returning/wave/anchor/arming/repeat behavior. Maintain old numeric and tail/catch-up policies.
- [x] Recompose default recipes for all 18 prototype kinds and eight existing composition identities in both versions; run per-asset real effect tests.
- [x] Explicit sustained profile admission/rate/resource budget tests beyond 256 events; preserve finite safety budgets.

Evidence: native/Prototype325-period tests and the actual parser cross-axis cases are GREEN in final full `20261120T100009-47fec444`; correct-value real UMG batch `c657575f02e94822aeeaf697718c4f9a` separately passes all14 scenes on090852. A later real Follow DamageResult append uncovered an additional compatibility-helper admission omission; both new regressions now pass, including325 actual periods,650 real damage facts/status applications, originalHP/Mana parity and original-life/source/cancel gates. It is not inferred from the earlier Period→Area proof.

## Task 7: Separate attached, cross-cast stack and movement behavior

**Files:** ExtensionNode/ExtensionWorldSubsystem/LegacyExtensionAbilityDriver shared interfaces and new typed node adapters; Private/Tests/AbilityDynamicStateTests.cpp.

- [x] RED tests: attached alternate periodic payload and transfer, stack alternate threshold payload across casts, movement completion->area effect, cancellation/lease replacement.
- [x] Extract query/anchor/cadence/payload boundaries while retaining existing cross-cast owner, EffectId and original-life keys. Reuse movement ownership and terrain rules.
- [x] Recompose all nine Batch5 entries and verify expiry/threshold/death/transfer/no-double-trigger with real state and resources.

Evidence: shared State behavior plus three actual `BeginPreview → Append.Registered → runtime parser` cases are GREEN in `20261120T061450-f1eb9994`, including original-life/source-retirement controls and real Actor/Mass effects. User visual acceptance remains separate.

## Task 8: Traditional spell frontends, passives and new composed batches

**Files:** MyFireBolt/MyBeamSpell/MyArcaneShardAbility/MyArcaneNova/MyOrbitingFireBoltAbility shared action bridges; applicable unified Nodes; MageGraphAbility; Private/Tests/AbilityDynamicMageParityTests.cpp.

- [x] RED/characterization tests for original Notify, costs and final hit, Shards tail and historical friendly path, orbit inventory/attack event/cost cleanup, passive handle isolation.
- [x] Wire opt-in recipe execution at original frontend action times, sharing Tasks 2–7 primitives; keep original empty-modifier profile.
- [x] Restore all seven Batch6, six MageBatch and three mage ArtBatch defaults via runtime recipes; keep placeholder/infrastructure behavior protected.
- [x] Verify all affected catalog class variants independently; registered TypeId alone never closes this task.

Default-reconstruction evidence: all82 gameplay definitions fine,65 exact unchanged Prototype/Batch5/field action traces and68 actual Prototype-derived frontend comparisons (including3 Batch6 special frontends) passed; all9 Mage/Art default frontend comparisons and the native FireBolt/Nova/Shards/Beam/Orbit suites also passed. These cases remain GREEN in `20261120T090852-39fdc764`; earlier `050402` failures belong to its then-current new-feature/fixture stage. Dynamic combination, actual network and final user visuals retain their own evidence and are not inferred from default reconstruction.

## Task 9: Real-casting development panel and presentation/network snapshots

**Create:** Public/Private/UI/Widget/AbilityRuntimeRecipePanel.h/.cpp, a development controller/console entry and runtime probes; Scripts/GA/DynamicMage readback/PIE verification.
**Tests:** Private/Tests/AbilityRuntimeRecipePanelTests.cpp and multiplayer probe reports.

- [x] RED: invalid edit preserves previous recipe; close restores owned temporary grants/slots/focus/resources; another player's changes isolated.
- [x] Build UMG panel selecting actual base identity, typed behavior fragments and bounded parameters; show compiler errors/revision/events. Server RPC validates registered IDs and authority.
- [x] Bind actual mage input/target/animation frontends, never call damage directly as a substitute. Development commands explicitly control temporary test setup and restoration.
- [x] Copy stable presentation references plus cast values (not temporary UObject references); compare visible size/direction to damage geometry.
- [x] Run Standalone core combinations; Listen Server remote input/recipe/fire-arrow propagation, field and passive/state cases. Record individual visual/user acceptance states.

## Task 10: Full per-asset reconstruction and delivery audit

**Files:** docs/ga-dynamic-mage-inventory.json, ga-unified-authoring-guide.md, ga-framework-reference.md, dynamic composition implementation/usage documentation; Scripts/GA/DynamicMage verification.

- [x] Run exact original compatibility + default runtime reconstruction cases for every applicable class in inventory. Tests report which real asset/profile was exercised and actual attributes/resources.
- [x] Independently read original assets and verify protected fields/package hashes. No hidden static combined GA used as dynamic test evidence. Final SHA readback:773/773 unchanged,0missing in `protected-package-final-check.json`; row-level original frontend/profile evidence remains separate.
- [x] Complete Development build and full `test_25d` with source/binary/config/package manifest; distinguish baseline failures, gameplay automation, PIE/network and user visual acceptance.
- [x] Review code against spec; update ledger, row-level evidence and remaining Pending visual items. No claim that the initial fire-arrow slice completes this plan.

## Execution ledger and verification commands

Ledger: `Saved/GAAuthoring/DynamicMage/20261119-implementation/ledger.md` (durable across context compaction). It records interface rulings, file ownership, RED/GREEN reports and exact baseline hashes. No commit steps are authorized.

Build: UE_5.8 `Build.bat test_25dEditor Win64 Development -Project=<absolute project> -WaitMutex -NoHotReloadFromIDE -NoUBA`, redirected to a unique Saved log.

Tests: `Scripts/GA/Unified/test.ps1 -SkipBuild -Filter <exact prefix> -TimeoutSeconds 900`; expected nonzero case count, zero failed/notRun and unchanged input manifest. Full suite uses `-Filter test_25d`.

Plan self-review: Tasks 1–10 cover all approved spec sections, including panel/network and full reconstruction rather than just core propagation. Interface names above are fixed contracts at the family boundary; exact helper signatures are logged before dependent edits. No new user scope decision is needed to start execution.

## Final implementation status — 2026-11-20

All implementation tasks above are complete within the approved scope. Final full `20261120T100009-47fec444`:1015 executed,1014 passed,1 known baseline failure (`AllConfigurationAssets`:9 pre-existing Stander montage references),0notRun. The suite is not described as fully green. Final rendered `20261120T100137-3bd3175f`:5/5. Both input manifests unchanged.

Final DLL remote7/7, remote basicUMG43, standaloneFollow32 and standalone basicUMG38 checks passed with cleanup. Exact82-class default smoke and14UMG scenes retain their earlier recorded build boundaries, not claimed as final-DLL reruns. Independent bounded review found no concrete remaining P1/P2. Original773packages and final1520validation inputs unchanged. Localmain, no staging/commit/push.

Final report: `Saved/GAAuthoring/DynamicMage/20261119-implementation/final-validation-summary.json`. OriginalL_RoguelikeRun/throttle restored; noPIE or unsaved assets remain. Test commands excluded the unrelated importedUnLua plugins as described in the delivery evidence. **User animation/VFX/readability/feel acceptance remains Pending**; see [PIE steps](../../ga-dynamic-mage-acceptance.md).
