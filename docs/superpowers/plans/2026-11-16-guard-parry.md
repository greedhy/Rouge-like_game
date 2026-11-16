# Guard Parry Implementation Plan

Goal: extend existing Stander RMB guard through shared execution with reflected ranged attacks, melee stun and successful-parry world slow (latest user addition).
Spec: ../specs/2026-11-16-guard-parry-design.md

- [x] Inspect real fallback/weapon guard assets and capture current baseline.
- [x] Implement shared opt-in parry settings/component, GuardWindow integration, Stander frontends, actual melee adapters and Actor/Mass stun. Add behavior/lifecycle tests.
- [x] Implement reflection on MyProjectile/EnemySling with source ownership, original shooter collision, wall/terminal safety and behavior tests.
- [x] Reuse the independent world-time service for earned parry rewards, with fixed-window dedup, source compensation and lifecycle tests.
- [x] Build Editor with source frozen; author exact guard configuration whitelist and verify in independent process.
- [x] Independent code review, targeted regressions and full test_25d; fix task-related failures.
- [x] Publish implementation/reuse notes, evidence and explicit PIE/unsupported attack boundaries.

Final evidence: Build04 succeeded; 12 configs independently read back; targeted17/17 with unchanged inputs. Full455 tests executed,452 succeeded,3 failed (two parallel equipment asset checks awaiting Storm Helm authoring, one pre-existing catalog check with9 missing timeline montage refs). All42 parry/dodge/worldslow tests succeeded. Full regression is explicitly not green; user PIE and remote visuals remain unverified.

Ownership: parry_control owns core and melee/control code; parry_projectiles owns projectile classes/reflection tests; root coordinates builds and review; parry_assets owns scripts. Source/Content may not mutate during build/test. Local main, no commits/push. Existing changes including unique equipment remain intact.
