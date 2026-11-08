# Stander six-ability extension

Draft defaults, 2026-11-02. Lift: 45 physical damage, 180 cm, 100 degrees, 18 mana, 6 s cooldown, input 3. ShieldBash: 35 physical damage, 150 cm, 90 degrees, 10 mana, 4 s cooldown, input 4. Source motions and contact timing must be authored after actual animation inspection.

Both modes share existing server-only commit, montage timing, cancellation and movement cleanup. All six use mixed Actor/Mass targets with unique identities, geometry, visibility and room checks. Guard accepts typed blocked Mass incoming damage only, followed by live entity resolution. Death impulses are independently configured for three combo stages, counter, spin, dash, lift and shield bash; local X is locked forward (Spin uses the radial direction from caster to each target), Y right and Z up. Lift defaults (450,0,1600), shield bash (1600,0,200). These are death impulses, not live-target knockback. Mass uses existing bounded replicated ragdoll presentation.

Required verification: native build and tests, source animation selection, six catalog icons and actual HUD binding, input 3/4, save restoration, mixed damage and cancellation, Actor/Mass lethal ragdoll impulses, final user PIE acceptance. This record is not completion evidence.


## Executed evidence (2026-11-02)

Assets: `Saved/GAAuthoring/StanderMass/readback.json` confirms all six classes, icons, startup tags and both dedicated Stander HUD controllers resolve the Stander catalog. `hud-runtime.json` verifies Enhanced Input keys Three/Four map to InputTag.3/.4. `physics-hud.json` reads all six live Image_SpellIcon brush resources and the actual Stander Overlay controller catalog.

Animation selection: source pose samples in `inspect.json`; Lift uses Skill1_Float at 0.1833333 seconds (sword hand rises from height 105 at .15 to 170 at .20). ShieldBash uses Skill4_Push at .4333333 seconds (shield hand extending forward between .389 and .50). Root motion disabled and root locked in owned copies. Pose sampling supports selection but does not replace user visual acceptance.

`actor-runtime.json`: seven cases pass (three combo phases, empty Guard, Spin, Lift, ShieldBash), including one damage event, friend/range exclusions, and expected mana costs. `actor-special-runtime.json`: five cases pass (real Actor counter, 500cm dash, wall, ledge, interruption).

`mass-runtime.json`: six skills damage real Mass entities for 25/35/55/45/45/35 respectively; Guard receives actual Mass incoming damage and plays CounterStrike. Lift/ShieldBash cancellation gives zero damage; both lethal casts remove entities. Costs are 0/0/15/12/18/10. Tests use transient stationary fixtures and are not balance testing.

`mass-physics-runtime.json`: actual Goblin physics Hips body velocities after lethal Lift include (96.87,-62.75,217.29) cm/s; ShieldBash includes (468.02,-0.19,-60.47), with forward +X. Positive lift Z and strong forward shield velocity confirm actual physics impulse, not merely a death counter. The earlier probe used nonexistent pelvis bone and is superseded for velocity evidence. Physics samples are post-solver and need not equal authored impulse.

`restored-editor.json`: original L_RoguelikeRun restored, no PIE and no dirty content/maps. Test map and transient fixtures were not saved.

Remaining validation: native suite is coordinated separately; actual Actor ragdoll physics, save/load restoration of the two newly added startup skills for pre-existing Stander saves, network clients, and user visual/input acceptance are not claimed by these probes.

## 原生回归最终结果

2026-11-02：Editor Development构建成功；Stander + SwordShield + PureMass相关套件38项成功（其中18项带警告），0失败、0未运行。新鲜报告：Saved/GAAuthoring/StanderMass/NativeFinal/index.json；日志Saved/Logs/StanderMassNativeFinal.log。

六GA为单人Stander展示角色的初始技能：LMB连击、RMB盾反、1回旋、2突进、3上挑、4盾击。作用力当前为致死后的布娃娃冲量，不代表对存活目标施加击退/浮空控制。Spin按目标径向向外，其余以锁定朝向定义本地前/右/上向量。参数位于DA_Stander。
