# Damage number UI profiling and aggregation

User authorized continuation of the measured optimization plan. Scope: isolate active UI work, implement local Actor display aggregation and an importance reserve, preserve server damage and existing Mass batching. No new graphics pipeline or asset edits.

- [x] Add profiling-only normal/static/hidden display modes.
- [x] Implement bounded 100 ms Actor batching keyed by local player, target identity, class and critical/blocked flags; retain last confirmed location after target destruction.
- [x] Reserve active slots for critical/blocked feedback; keep a configurable overall cap and explicit drop counters.
- [x] First CSV experiment identifies active screen UI as the dominant observable difference; stopping animation alone gives little improvement.
- [x] Functional test exposes stale Blueprint block/critical color state; reset Blueprint scalar presentation defaults on reuse.
- [x] Repeat final CSV capture after the style correction.
- [x] Verify aggregation sums, target lifetime, style separation, reused normal color, reserved slots and multiplayer ownership.
- [x] Publish final measurements with Editor/PIE and single-target limitations, restore editor state.

Diagnostic CVar must remain 0 in normal gameplay. Pool comparison scripts explicitly disable merging/reserve so future measurements remain comparable. A lower visible count intentionally changes presentation and must not be represented as a pure pooling speedup.
