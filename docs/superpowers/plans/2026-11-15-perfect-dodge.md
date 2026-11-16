# Perfect Dodge / Reusable World Slow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Execution status (2026-11-15):** Implementation, isolated asset readback and code review completed. Latest build10 succeeds; all25 new tests pass. Full test_25d executed424 cases:423 pass,1 existing timeline configuration check fails (also reproduced alone). User PIE remains pending; see `Saved/GAAuthoring/PerfectDodge/20261115-implementation/progress.md`. Checked steps mean executed, not that the full project suite is green.

**Goal:** 默认翻滚触发完美判定，复用可独立调用的世界减速机制及 GA。
**Architecture:** 每 World 的时间租约服务负责倍率、恢复与补偿；统一注册节点和独立 GA 都调用同一服务。翻滚前端使用共享防御组件/注册动作与已有 GroundDash，固定 Startup 输入，不占技能槽。
**Tech Stack:** UE 5.8.1 / C++ / GAS / Schema 2 / Unreal Python。
**Spec:** ../specs/2026-11-15-perfect-dodge-design.md（用户已确认，补充要求独立世界减速可复用）。

## Global Constraints

- 本地 main；不提交或推送；不覆盖已有 Content/代码修改。
- Startup 默认授予 Mage/Stander；不学习、不装备、不增加可替换槽位。
- 单机首版，世界0.2倍；进入0.05、保持1.20、退出0.15动作秒；原GAS冷却沿世界时间。
- 真实威胁确认完美窗口0.12；位移420cm/0.30秒，无敌0.24秒，动作0.42秒；费用0，冷却0.80世界秒。
- 新策略注册、配置Validate、类型化端口、生命周期和行为测试齐全；不嵌套完整GA。

## Review Focus

- 非1世界基准及重叠请求结束顺序，恢复不覆盖其他请求。
- 暂停/低帧率/来源换生命，不延长窗口或补跑暂停时间。
- 真正位移离开后仍能奖励，空滚/隔墙/取消攻击不可奖励。
- 旧档/重复初始化/武器变更，不重复授予或丢失基础技能。
- 玩家弹道、独立组件和GAS计时的差异，不以角色补偿冒充全链验证。

### Task 1: World time service, node and standalone GA

Files: new `Public|Private/AbilitySystem/Time/AbilityWorldTimeSubsystem.*`, `Public|Private/AbilitySystem/Unified/Nodes/AbilityWorldTimeNodes.*`, `Public|Private/AbilitySystem/Ability/WorldSlowAbility.*`, `Private/Tests/WorldSlowTests.cpp`; modify `AbilityNodeRegistry.cpp`.

Interfaces: `FAbilityWorldSlowSettings` (Scale=.2, BlendIn=.05, Hold=1.2, BlendOut=.15, bCompensateSource=true); `UAbilityWorldTimeSubsystem::Acquire(const FAbilitySourceToken&, const FAbilityWorldSlowSettings&, UObject* Owner, FString&) -> FGuid`, `Release(FGuid)`, `IsActive(FGuid)`, `GetGameplayRealTime() -> double`. Owner是弱生命周期持有者；来源每帧重验。服务公开Blueprint友好请求接口捕获权威来源，节点与GA调用同一Acquire。

- [x] Tests first: actual WorldSettings倍率/CustomTimeDilation、0.5基准、重叠0.5/0.2、重复释放、来源失效、暂停时钟、独立GA和注册图。
- [x] Full Editor build: observe missing API before implementation, then implement bounded requests/min包络/恢复/补偿。
- [x] Run `test_25d.UnifiedGA.WorldSlow` with `Scripts/GA/Unified/test.ps1` and inspect fresh report; zero/failed/notRun fails。

### Task 2: Dodge behavior and real attack adapters

Files: new `AbilitySystem/Defense/PerfectDodgeComponent.*`, `Ability/PerfectDodgeAbility.*`, registered defense node, behavioral tests; targeted existing direct-hit/geometry paths only.

Interfaces: common runtime component owns per-life generation/window/witness/movement/reward lease; GA opens one execution after Commit and closes on cancellation. Uses Task1 Acquire and explicit Release, never activates WorldSlowAbility inside roll. New defense node and GA call same component behavior. Threat adapters submit actual attack geometry and original source identity; before-GE interception consumes confirmed dodge outcome.

- [x] Tests first for empty/early/late/within-window witness, actual Health+Debuff suppression, multiple callbacks, collision, source replacement, same Pawn death/revive and cleanup。
- [x] Implement registered defense config and shared action, GroundDash ownership and real-time windows; explicit actor and Mass adapters retain original damage paths。
- [x] Build/run `test_25d.UnifiedGA.PerfectDodge`; report supported attack paths separately。

### Task 3: Startup/input and assets

Files: `MyGameplayAbility.*`, `MyAbilitySystemComponent.*`, `PlayerCharacter.cpp`, native tags/input config; `Scripts/GA/PerfectDodge/author.py`, `verify.py`; dedicated Blueprint assets under `/Game/UnifiedAbilities` and precise Pawn/Input edits。

- [x] Test PressOnly once, permanent base Spec not equip-replaceable, old-save fallback and deduplication, no saved base ability duplication。
- [x] Add generic intrinsic/press-only policy; fixed InputTag.Dodge and verified free physical key; server Ensure runs after load and Avatar initialization; save skips intrinsic specs。
- [x] Build before reflected assets. Author idempotently with exact whitelist backups; inspect both effective Pawn Startup arrays, actual input chain, cost/cooldown and effects in separate process。

### Task 4: Integration and review

- [x] Full Editor build, focused gameplay+asset tests, then required full `test_25d` suite; logs/report contain actual run IDs。
- [x] Independent code review against spec, fix material findings, rerun affected tests。
- [x] Update design/ledger with actual implementation, asset paths and remaining PIE/network boundaries; provide direct standalone WorldSlow invocation and default-roll trial instructions。

No commits are planned: project/user instruction overrides skill's commit defaults. Native coordinator builds once per coherent source revision; subtask tests may be written first but UE test execution waits for coordinated complete modules to avoid racing UHT/linker.
