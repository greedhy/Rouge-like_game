# Dungeon mixed Mass encounters

User scope: fix DungeonPlayer slot1 missing icon and slot2 Burst lightning source; enable Mass melee Goblins in L_RoguelikeRun, other monsters Actor/GAS, health10,50 per fighting room, ~16ms snapshots and bounded ragdoll death.

Decisions: use profile opt-in, preserve other modes; 50 total per room (ordinary two waves25+25, elite/boss one50), preserve other Actor enemies. Cosmetic ragdolls are bounded and temporary; live Mass entities retain no individual Actor/ASC. Actual network sends remain frame/network-budget constrained.

- [x] Diagnose and repair actual equipped slot catalog and lightning endpoints; preserve skill identity.
- [x] Implement/verify reserved ledger and budgeted Mass spawn, room bounds and cleanup.
- [x] Implement60Hz snapshot intent and bounded killed-only cosmetic ragdolls.
- [x] Author independent health10 type and opt-in active dungeon profile, exclude Actor melee roster.
- [x] Fresh build, native regression and two-client room/skill/death acceptance.
- [x] Final review and report unverified boundaries accurately.

Integration findings: real remote casts required replicated encounter membership (Actor Tags are server-local), and late clients needed local navigation readiness before spawning closed doors. Both fixed; native regression now 56/56. Two-client room lifecycle passed. The remaining Niagara issue was local Beam Start versus world Beam End; a cloned asset now uses absolute endpoints. Final remote input and screenshots passed. See docs/2026-11-02-dungeon-hybrid-mass.md for evidence and remaining packaged/latency/performance acceptance limits.
