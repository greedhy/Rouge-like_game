# Performance baseline implementation plan

Goal: reproducible first CPU bottleneck report, targeting 60 FPS (16.667 ms), without changing simulation behavior.

Architecture: isolated copy of existing test map; existing PureMass Goblin and player; CSV/Insights scopes in existing hot paths. 0.1 s is network snapshot interval, not simulation interval. Measure these separately. Preserve original gameplay assets and saves. No Actor/Mass equivalence or packaged performance claim from PIE.

Approaches: use existing editor PIE for the first measured diagnostic, then packaged Development for release-representative validation. A synthetic loop alone would omit real rendering and world queries. First delivery uses PIE with its limitations stated.

- [x] Add disabled-by-default CSV category and Insights scopes for simulation, queries, navigation, collision and presentation.
- [x] Build fresh Editor binary safely after checking unsaved packages.
- [x] Create isolated benchmark map and reproducible 0/100/200 entity scenarios; verify counts and simulation activity.
- [x] Capture warmed runs, calculate frame-time percentiles and scope totals; retain raw evidence.
- [x] Report measured hotspots, 60 FPS budget and unmeasured GPU/network/packaged boundaries.
