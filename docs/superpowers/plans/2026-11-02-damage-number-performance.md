# Damage number performance implementation plan

Goal: measure existing damage-number overhead, implement bounded local reuse and aggregation, and compare identical workloads without changing damage calculations.

Architecture: retain the current visual style and legacy path as a baseline. A local presentation owner manages reusable components, expiry and counters. Actor and Mass notifications feed the same presentation service; existing Mass server batching remains intact. Inspect Blueprint lifecycle before choosing the release hook, because delayed DestroyComponent and animation callbacks must not affect reused slots.

Tech stack: UE 5.8 C++, UMG, Python editor automation, Unreal Insights/CSV.

- [x] Inspect Blueprint animation/destruction and create repeatable warmed baseline workload.
- [ ] Add measured scopes and counters; record creation cost separately from full-frame cost.
- [ ] Implement reusable presentation with cap, style-safe reset, aggregation window, shutdown cleanup and baseline toggle.
- [ ] Test reuse, expiry, overload, target deletion, local ownership and world teardown.
- [ ] Run matched baseline/optimized workloads and actual combat PIE; report frame percentiles, creation counts, limitations and raw evidence.

No assumed speedup, no invented packaged-game results. Existing unrelated workspace changes must be preserved. This work is authorized by the user's request to proceed with the previously described plan; routine design choices do not require another approval.

2026-11-03: native Insights scopes, counters, local bounded reuse and baseline toggle implemented and compiled. Matched PIE comparison and 6 single-player + 6 two-client lifecycle checks passed. See docs/2026-11-03-damage-number-pooling.md. Actor aggregation, per-importance overflow policy, packaged/GPU/memory profiling remain future stages; existing Mass aggregation preserved.
