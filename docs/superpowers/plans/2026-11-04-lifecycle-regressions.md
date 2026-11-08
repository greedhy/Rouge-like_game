# Lifecycle native regression execution

Authorized scope: real radial GE subscription scope; UI controller receiver lifetime/rebind; PlayerCharacter death timer premature teardown; pooled enemy old callback during second life. Use existing dirty feature workspace; no commits/assets. Separate test-only RED build from minimal production fixes, then GREEN and existing pool regression suite. Tests avoid intentionally broadcasting a dangling stack reference in the old implementation.

- [x] Add tests and compile old implementation.
- [x] Record actual failures and existing passing lease isolation.
- [x] Minimal fixes for demonstrated bugs.
- [x] Rebuild and run lifecycle plus existing pool tests.
- [x] Record limits and restore editor.

Delegation: independent UI and player timer tests; root owns radial and pool tests, build and editor state. All modifications preserve unrelated work.

RED: radial leftover subscriptions reproduced three times; UI eight binding/detach failures. Initial death failure inconclusive because actor fixture skipped initialization; retest required. Pool callback first failed because timer pending activation needs additional frames; fixed test clock, existing production lease guard passes. GREEN1: 9/10 pass, death fixture correction pending.

Corrected Death RED confirmed timer remains with initialized actor and routed EndPlay. Final 10/10 Success. Review found no important normal-path flaw; strengthened pool timer to originate before death after review noted coverage limitation. Deferred: invalid target during radial synchronous call, other UI categories and network/visual validation.
