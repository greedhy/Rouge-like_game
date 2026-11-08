# Stander 武器与动作实时盘点

实时采样完成：15组，428个动作。初始PIE=False，未保存内容=[]，未保存地图=[]。本脚本不启停PIE、不保存资产。

每个攻击候选采61帧root、双手及双足姿态。下列候选为去除root位移后的实际轨迹速度峰；不等于已确认伤害帧。投射物释放、双刃多段、空中技能和格挡反击需行为设计与画面核对，不能直接按名称生成伤害。

完整数据：`Saved/GAAuthoring/StanderWeapons/inventory.json`；可机读制作映射：`Scripts/GA/StanderWeapons/clip-design-mapping.json`。

## 实际模型与骨骼

- `/Game/PlatformAnimation/Models/Stander.Stander` → `/Game/PlatformAnimation/Models/Stander_Skeleton.Stander_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon.Weapon_Bow` → `/Game/PlatformAnimation/Models/Weapon_Bow_Skeleton.Weapon_Bow_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade` → `/Game/PlatformAnimation/Models/Weapon_DualBlade_Skeleton.Weapon_DualBlade_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer` → `/Game/PlatformAnimation/Models/Weapon_Hammer_Skeleton.Weapon_Hammer_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol` → `/Game/PlatformAnimation/Models/Weapon_Pistol_Skeleton.Weapon_Pistol_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon.Weapon_Shield` → `/Game/PlatformAnimation/Models/Weapon_Shield_Skeleton.Weapon_Shield_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon.Weapon_Spear` → `/Game/PlatformAnimation/Models/Weapon_Spear_Skeleton.Weapon_Spear_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon.Weapon_Staff` → `/Game/PlatformAnimation/Models/Weapon_Staff_Skeleton.Weapon_Staff_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon.Weapon_Sword` → `/Game/PlatformAnimation/Models/Weapon_Sword_Skeleton.Weapon_Sword_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades` → `/Game/PlatformAnimation/Models/Weapon_DoubleBlades_Skeleton.Weapon_DoubleBlades_Skeleton`
- `/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier` → `/Game/PlatformAnimation/Models/Weapon_Rapier_Skeleton.Weapon_Rapier_Skeleton`

## 00_Base

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Crouch_Wait | 2.2667 | auxiliary |  |
| Stander_Crouch_Walk | 0.8667 | movement_lifecycle |  |
| Stander_Dodge_Dash | 0.8667 | movement_lifecycle |  |
| Stander_Dodge_Roll | 0.6667 | movement_lifecycle |  |
| Stander_Idle | 2.2667 | auxiliary |  |
| Stander_Jog | 0.8000 | auxiliary |  |
| Stander_Jump | 0.8333 | movement_lifecycle |  |
| Stander_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Jump_Roll | 0.6667 | movement_lifecycle |  |
| Stander_Jump_Roll_Original | 0.6667 | movement_lifecycle |  |
| Stander_Jump_Run | 0.8333 | movement_lifecycle |  |
| Stander_Jump_Run_Original | 0.8333 | movement_lifecycle |  |
| Stander_Run | 0.6667 | movement_lifecycle |  |
| Stander_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_RunTurn | 0.5667 | movement_lifecycle |  |
| Stander_Sliding | 0.5333 | auxiliary |  |
| Stander_WalkTurn | 0.4000 | movement_lifecycle |  |
| Stander_Walk_B | 1.1333 | movement_lifecycle |  |
| Stander_Walk_F | 1.1333 | movement_lifecycle |  |

## 01_Sword_Shield

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Sword_Shield_Attack1 | 1.0000 | melee_candidate | hand_r:0.1167, hand_l:0.1333 |
| Stander_Sword_Shield_Attack2 | 1.0000 | melee_candidate | hand_r:0.0167, hand_l:0.0333 |
| Stander_Sword_Shield_Attack3 | 1.1333 | melee_candidate | hand_r:0.2456, hand_l:0.2267 |
| Stander_Sword_Shield_Block | 3.5000 | guard_lifecycle |  |
| Stander_Sword_Shield_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_Sword_Shield_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Sword_Shield_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Sword_Shield_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_Sword_Shield_Block_Start | 3.5000 | guard_lifecycle |  |
| Stander_Sword_Shield_Dodge | 0.8667 | movement_lifecycle |  |
| Stander_Sword_Shield_DodgeAttack | 0.6667 | melee_candidate / displacement | hand_r:0.0889, hand_l:0.0778 |
| Stander_Sword_Shield_DodgeRoll | 0.6667 | movement_lifecycle |  |
| Stander_Sword_Shield_Idle | 1.6667 | auxiliary |  |
| Stander_Sword_Shield_Jump | 0.8333 | movement_lifecycle |  |
| Stander_Sword_Shield_JumpAttack1 | 0.4000 | melee_candidate / airborne | hand_r:0.0667, hand_l:0.0467 |
| Stander_Sword_Shield_JumpAttack2 | 0.5000 | melee_candidate / airborne | hand_r:0.0500, hand_l:0.0083 |
| Stander_Sword_Shield_JumpAttack3 | 0.6000 | melee_candidate / airborne | hand_r:0.1500, hand_l:0.2100 |
| Stander_Sword_Shield_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Sword_Shield_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Sword_Shield_Run | 0.6667 | movement_lifecycle |  |
| Stander_Sword_Shield_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_Sword_Shield_Skill1_Float | 1.2000 | melee_candidate | hand_r:0.2000, hand_l:0.0200 |
| Stander_Sword_Shield_Skill2_Slow | 0.9333 | melee_candidate | hand_r:0.2022, hand_l:0.0156 |
| Stander_Sword_Shield_Skill3_Stun | 0.8000 | melee_candidate | hand_r:0.3200, hand_l:0.0133 |
| Stander_Sword_Shield_Skill4_Push | 1.3333 | melee_candidate | hand_r:0.3556, hand_l:0.0222 |
| Stander_Sword_Shield_Skill5_Pull | 0.9333 | melee_candidate | hand_r:0.2178, hand_l:0.2178 |
| Stander_Sword_Shield_Skill6_Move | 1.3333 | melee_candidate / displacement | hand_r:0.3556, hand_l:0.3556 |
| Stander_Sword_Shield_Skill7_Around | 1.3333 | melee_candidate / radial | hand_r:0.3556, hand_l:0.0889 |
| Stander_Sword_Shield_Skill8_Air | 1.2000 | melee_candidate / airborne | hand_r:0.3000, hand_l:0.9000 |

## 02_Hammer

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Hammer_Attack1 | 1.0000 | melee_candidate | hand_r:0.0167, hand_l:0.0333 |
| Stander_Hammer_Attack2 | 1.1667 | melee_candidate | hand_r:0.2917, hand_l:0.2917 |
| Stander_Hammer_Attack3 | 1.3333 | melee_candidate | hand_r:0.0222, hand_l:0.0222 |
| Stander_Hammer_Block | 3.5000 | guard_lifecycle |  |
| Stander_Hammer_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_Hammer_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Hammer_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Hammer_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_Hammer_Block_Start | 0.2000 | guard_lifecycle |  |
| Stander_Hammer_Dodge | 0.6000 | movement_lifecycle |  |
| Stander_Hammer_DodgeAttack | 1.0000 | melee_candidate / displacement | hand_r:0.1833, hand_l:0.1833 |
| Stander_Hammer_DodgeRoll | 0.7333 | movement_lifecycle |  |
| Stander_Hammer_Idle | 1.5333 | auxiliary |  |
| Stander_Hammer_Jump | 0.8333 | movement_lifecycle |  |
| Stander_Hammer_JumpAttack1 | 0.5000 | melee_candidate / airborne | hand_r:0.0667, hand_l:0.0417 |
| Stander_Hammer_JumpAttack2 | 0.5333 | melee_candidate / airborne | hand_r:0.2133, hand_l:0.1867 |
| Stander_Hammer_JumpAttack3 | 0.6333 | melee_candidate / airborne | hand_r:0.0211, hand_l:0.0211 |
| Stander_Hammer_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Hammer_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Hammer_Run | 0.8000 | movement_lifecycle |  |
| Stander_Hammer_RunEnd | 0.5000 | movement_lifecycle |  |
| Stander_Hammer_Skill1_Float | 1.0667 | melee_candidate | hand_r:0.3911, hand_l:0.3200 |
| Stander_Hammer_Skill2_Slow | 1.0000 | melee_candidate | hand_r:0.3500, hand_l:0.4333 |
| Stander_Hammer_Skill3_Stun | 0.8000 | melee_candidate | hand_r:0.2533, hand_l:0.2667 |
| Stander_Hammer_Skill4_Push | 1.4667 | melee_candidate | hand_r:0.0244, hand_l:0.0244 |
| Stander_Hammer_Skill5_Pull | 0.9333 | melee_candidate | hand_r:0.2333, hand_l:0.0156 |
| Stander_Hammer_Skill6_Move | 1.5000 | melee_candidate / displacement | hand_r:0.4250, hand_l:0.4250 |
| Stander_Hammer_Skill7_Around | 1.5333 | melee_candidate / radial | hand_r:0.6389, hand_l:0.6389 |
| Stander_Hammer_Skill8_Air | 1.2667 | melee_candidate / airborne | hand_r:0.4011, hand_l:0.9500 |

## 03_DualBlades

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_DualBlades_Attack1 | 0.9333 | melee_candidate / multi_hand_review | hand_r:0.1867, hand_l:0.0156 |
| Stander_DualBlades_Attack2 | 1.2000 | melee_candidate / multi_hand_review | hand_r:0.0200, hand_l:0.2200 |
| Stander_DualBlades_Attack3 | 1.1667 | melee_candidate / multi_hand_review | hand_r:0.0583, hand_l:0.0583 |
| Stander_DualBlades_Block | 3.5000 | guard_lifecycle |  |
| Stander_DualBlades_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_DualBlades_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_DualBlades_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_DualBlades_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_DualBlades_Block_Start | 0.2000 | guard_lifecycle |  |
| Stander_DualBlades_Dodge | 0.8000 | movement_lifecycle |  |
| Stander_DualBlades_DodgeAttack | 1.1667 | melee_candidate / displacement,multi_hand_review | hand_r:0.0194, hand_l:0.0194 |
| Stander_DualBlades_DodgeRoll | 0.8667 | movement_lifecycle |  |
| Stander_DualBlades_Idle | 2.2667 | auxiliary |  |
| Stander_DualBlades_Jump | 0.8333 | movement_lifecycle |  |
| Stander_DualBlades_JumpAttack1 | 0.4000 | melee_candidate / airborne,multi_hand_review | hand_r:0.2067, hand_l:0.0800 |
| Stander_DualBlades_JumpAttack2 | 0.5000 | melee_candidate / airborne,multi_hand_review | hand_r:0.0667, hand_l:0.1750 |
| Stander_DualBlades_JumpAttack3 | 0.6667 | melee_candidate / airborne,multi_hand_review | hand_r:0.2222, hand_l:0.0222 |
| Stander_DualBlades_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_DualBlades_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_DualBlades_Run | 0.5333 | movement_lifecycle |  |
| Stander_DualBlades_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_DualBlades_Skill1_Float | 0.8667 | melee_candidate / multi_hand_review | hand_r:0.1300, hand_l:0.1878 |
| Stander_DualBlades_Skill2_Slow | 0.8000 | melee_candidate / multi_hand_review | hand_r:0.2800, hand_l:0.0267 |
| Stander_DualBlades_Skill3_Stun | 0.8000 | melee_candidate / multi_hand_review | hand_r:0.0267, hand_l:0.0267 |
| Stander_DualBlades_Skill4_Push | 1.3333 | melee_candidate / multi_hand_review | hand_r:0.3556, hand_l:0.4000 |
| Stander_DualBlades_Skill5_Pull | 0.9333 | melee_candidate / multi_hand_review | hand_r:0.2178, hand_l:0.1867 |
| Stander_DualBlades_Skill6_Move | 1.3333 | melee_candidate / displacement,multi_hand_review | hand_r:0.5333, hand_l:0.3556 |
| Stander_DualBlades_Skill7_Around | 1.3333 | melee_candidate / radial,multi_hand_review | hand_r:0.3556, hand_l:0.3333 |
| Stander_DualBlades_Skill8_Air | 1.2000 | melee_candidate / airborne,multi_hand_review | hand_r:0.3000, hand_l:0.9000 |

## 04_Bow

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Bow_Attack | 3.3333 | projectile_candidate | hand_r:0.6111, hand_l:0.0556 |
| Stander_Bow_Attack1 | 0.8667 | projectile_candidate | hand_r:0.0144, hand_l:0.0289 |
| Stander_Bow_Attack1_Air | 0.8667 | projectile_candidate / airborne | hand_r:0.0144, hand_l:0.0289 |
| Stander_Bow_Attack2 | 1.2667 | projectile_candidate | hand_r:0.2533, hand_l:0.1267 |
| Stander_Bow_Attack2_Air | 1.2667 | projectile_candidate / airborne | hand_r:0.2533, hand_l:0.1267 |
| Stander_Bow_Attack3 | 1.2000 | projectile_candidate | hand_r:0.2600, hand_l:0.0200 |
| Stander_Bow_Attack3_Air | 1.2000 | projectile_candidate / airborne | hand_r:0.2600, hand_l:0.0200 |
| Stander_Bow_Block | 3.5000 | guard_lifecycle |  |
| Stander_Bow_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_Bow_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Bow_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Bow_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_Bow_Block_Start | 0.2000 | guard_lifecycle |  |
| Stander_Bow_Dodge | 0.6000 | movement_lifecycle |  |
| Stander_Bow_DodgeAttack | 1.1333 | projectile_candidate / displacement | hand_r:0.0378, hand_l:0.0189 |
| Stander_Bow_DodgeRoll | 0.6667 | movement_lifecycle |  |
| Stander_Bow_Idle | 2.2667 | auxiliary |  |
| Stander_Bow_Jump | 0.8333 | movement_lifecycle |  |
| Stander_Bow_JumpAttack1 | 0.6000 | projectile_candidate / airborne | hand_r:0.1300, hand_l:0.6000 |
| Stander_Bow_JumpAttack1_Ground | 0.6000 | projectile_candidate / airborne | hand_r:0.1600, hand_l:0.5500 |
| Stander_Bow_JumpAttack2 | 0.6333 | projectile_candidate / airborne | hand_r:0.0106, hand_l:0.0211 |
| Stander_Bow_JumpAttack2_Ground | 0.6333 | projectile_candidate / airborne | hand_r:0.0211, hand_l:0.0633 |
| Stander_Bow_JumpAttack3 | 0.8000 | projectile_candidate / airborne | hand_r:0.0267, hand_l:0.0133 |
| Stander_Bow_JumpAttack3_Ground | 0.8000 | projectile_candidate / airborne | hand_r:0.0267, hand_l:0.0133 |
| Stander_Bow_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Bow_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Bow_Run | 0.6667 | movement_lifecycle |  |
| Stander_Bow_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_Bow_Skill1_Float | 0.8000 | projectile_candidate | hand_r:0.2800, hand_l:0.1467 |
| Stander_Bow_Skill2_Slow | 1.3333 | projectile_candidate | hand_r:0.0222, hand_l:0.0222 |
| Stander_Bow_Skill3_Stun | 0.8000 | projectile_candidate | hand_r:0.2533, hand_l:0.0267 |
| Stander_Bow_Skill4_Push | 1.6000 | projectile_candidate | hand_r:0.8267, hand_l:0.0267 |
| Stander_Bow_Skill5_Pull | 0.9333 | projectile_candidate | hand_r:0.1867, hand_l:0.2178 |
| Stander_Bow_Skill6_Move | 1.3333 | projectile_candidate / displacement | hand_r:0.3333, hand_l:0.3333 |
| Stander_Bow_Skill7_Around | 1.0333 | projectile_candidate / radial | hand_r:0.3961, hand_l:0.8956 |
| Stander_Bow_Skill8_Air | 1.2000 | projectile_candidate / airborne | hand_r:1.0200, hand_l:0.1200 |

## 05_Pistol

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Pistol_Attack | 2.3333 | projectile_candidate | hand_r:0.0778, hand_l:0.1167 |
| Stander_Pistol_Attack0 | 0.8667 | projectile_candidate | hand_r:0.0578, hand_l:0.0578 |
| Stander_Pistol_Attack0_Air | 0.8667 | projectile_candidate / airborne | hand_r:0.0578, hand_l:0.0578 |
| Stander_Pistol_Attack1 | 0.8333 | projectile_candidate | hand_r:0.0972, hand_l:0.0972 |
| Stander_Pistol_Attack1_Air | 0.8333 | projectile_candidate / airborne | hand_r:0.0972, hand_l:0.0972 |
| Stander_Pistol_Attack2 | 0.9333 | projectile_candidate | hand_r:0.0311, hand_l:0.0933 |
| Stander_Pistol_Attack2_Air | 0.9333 | projectile_candidate / airborne | hand_r:0.0622, hand_l:0.0933 |
| Stander_Pistol_Attack3 | 0.9333 | projectile_candidate | hand_r:0.1556, hand_l:0.1244 |
| Stander_Pistol_Attack3_Air | 0.9333 | projectile_candidate / airborne | hand_r:0.1556, hand_l:0.1244 |
| Stander_Pistol_Block | 3.5000 | guard_lifecycle |  |
| Stander_Pistol_Block_Damage | 1.6000 | guard_reaction |  |
| Stander_Pistol_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Pistol_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Pistol_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_Pistol_Block_Start | 3.5000 | guard_lifecycle |  |
| Stander_Pistol_Dodge | 0.6000 | movement_lifecycle |  |
| Stander_Pistol_DodgeAttack | 1.2667 | projectile_candidate / displacement | hand_r:0.0633, hand_l:0.0633 |
| Stander_Pistol_DodgeRoll | 0.6667 | movement_lifecycle |  |
| Stander_Pistol_Idle | 2.2667 | auxiliary |  |
| Stander_Pistol_Jump | 0.8333 | movement_lifecycle |  |
| Stander_Pistol_JumpAttack0 | 0.6000 | projectile_candidate / airborne | hand_r:0.5300, hand_l:0.0500 |
| Stander_Pistol_JumpAttack0_Ground | 0.6000 | projectile_candidate / airborne | hand_r:0.5300, hand_l:0.5100 |
| Stander_Pistol_JumpAttack1 | 0.6000 | projectile_candidate / airborne | hand_r:0.5600, hand_l:0.5500 |
| Stander_Pistol_JumpAttack1_Ground | 0.6000 | projectile_candidate / airborne | hand_r:0.6000, hand_l:0.5500 |
| Stander_Pistol_JumpAttack2 | 0.8000 | projectile_candidate / airborne | hand_r:0.1200, hand_l:0.7333 |
| Stander_Pistol_JumpAttack2_Ground | 0.8000 | projectile_candidate / airborne | hand_r:0.1200, hand_l:0.7333 |
| Stander_Pistol_JumpAttack3 | 1.0667 | projectile_candidate / airborne | hand_r:0.1600, hand_l:0.0356 |
| Stander_Pistol_JumpAttack3_Ground | 1.0667 | projectile_candidate / airborne | hand_r:0.0356, hand_l:0.0356 |
| Stander_Pistol_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Pistol_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Pistol_Reload | 1.0667 | auxiliary |  |
| Stander_Pistol_Run | 0.7333 | movement_lifecycle |  |
| Stander_Pistol_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_Pistol_Skill1_Float | 0.8000 | projectile_candidate | hand_r:0.2800, hand_l:0.1467 |
| Stander_Pistol_Skill2_Slow | 0.9333 | projectile_candidate | hand_r:0.0156, hand_l:0.0311 |
| Stander_Pistol_Skill3_Stun | 0.8000 | projectile_candidate | hand_r:0.2533, hand_l:0.0267 |
| Stander_Pistol_Skill4_Push | 1.6000 | projectile_candidate | hand_r:0.8267, hand_l:0.8000 |
| Stander_Pistol_Skill5_Pull | 0.9333 | projectile_candidate | hand_r:0.2178, hand_l:0.2178 |
| Stander_Pistol_Skill6_Move | 1.3333 | projectile_candidate / displacement | hand_r:0.6667, hand_l:0.6667 |
| Stander_Pistol_Skill7_Around | 1.0333 | projectile_candidate / radial | hand_r:0.3961, hand_l:0.3961 |
| Stander_Pistol_Skill8_Air | 1.2333 | projectile_candidate / airborne | hand_r:0.5344, hand_l:0.5344 |

## 06_Magic

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Magic_Attack1 | 0.9333 | projectile_candidate | hand_r:0.0933, hand_l:0.0933 |
| Stander_Magic_Attack1_Air | 0.9333 | projectile_candidate / airborne | hand_r:0.0933, hand_l:0.0933 |
| Stander_Magic_Attack2 | 1.2000 | projectile_candidate | hand_r:0.0200, hand_l:0.0200 |
| Stander_Magic_Attack2_Air | 1.2000 | projectile_candidate / airborne | hand_r:0.0200, hand_l:0.0200 |
| Stander_Magic_Attack3 | 1.1333 | projectile_candidate | hand_r:0.2267, hand_l:0.2267 |
| Stander_Magic_Attack3_Air | 1.1333 | projectile_candidate / airborne | hand_r:0.2267, hand_l:0.2267 |
| Stander_Magic_Block | 3.5000 | guard_lifecycle |  |
| Stander_Magic_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_Magic_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Magic_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Magic_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_Magic_Block_Start | 0.2000 | guard_lifecycle |  |
| Stander_Magic_Dodge | 0.6000 | movement_lifecycle |  |
| Stander_Magic_DodgeAttack | 1.2000 | projectile_candidate / displacement | hand_r:0.1000, hand_l:0.1000 |
| Stander_Magic_DodgeRoll | 0.6667 | movement_lifecycle |  |
| Stander_Magic_Idle | 2.2667 | auxiliary |  |
| Stander_Magic_Jump | 0.8333 | movement_lifecycle |  |
| Stander_Magic_JumpAttack1 | 0.5000 | projectile_candidate / airborne | hand_r:0.0500, hand_l:0.0083 |
| Stander_Magic_JumpAttack1_Ground | 0.5000 | projectile_candidate / airborne | hand_r:0.0583, hand_l:0.0083 |
| Stander_Magic_JumpAttack2 | 0.6333 | projectile_candidate / airborne | hand_r:0.0106, hand_l:0.1267 |
| Stander_Magic_JumpAttack2_Ground | 0.6333 | projectile_candidate / airborne | hand_r:0.0106, hand_l:0.0317 |
| Stander_Magic_JumpAttack3 | 0.7000 | projectile_candidate / airborne | hand_r:0.2333, hand_l:0.1983 |
| Stander_Magic_JumpAttack3_Ground | 0.7000 | projectile_candidate / airborne | hand_r:0.2333, hand_l:0.1983 |
| Stander_Magic_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Magic_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Magic_Run | 0.6000 | movement_lifecycle |  |
| Stander_Magic_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_Magic_Skill1_Float | 0.8000 | projectile_candidate | hand_r:0.2800, hand_l:0.1467 |
| Stander_Magic_Skill2_Slow | 1.3333 | projectile_candidate | hand_r:0.3333, hand_l:0.3333 |
| Stander_Magic_Skill3_Stun | 0.8000 | projectile_candidate | hand_r:0.2533, hand_l:0.0267 |
| Stander_Magic_Skill4_Push | 1.6000 | projectile_candidate | hand_r:0.8267, hand_l:0.8000 |
| Stander_Magic_Skill5_Pull | 0.9333 | projectile_candidate | hand_r:0.2178, hand_l:0.2178 |
| Stander_Magic_Skill6_Move | 1.3333 | projectile_candidate / displacement | hand_r:0.6667, hand_l:0.3333 |
| Stander_Magic_Skill7_Around | 1.0333 | projectile_candidate / radial | hand_r:0.0344, hand_l:0.8956 |
| Stander_Magic_Skill8_Air | 1.2000 | projectile_candidate / airborne | hand_r:1.0200, hand_l:1.0200 |

## 07_Spear

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Spear_Attack1 | 1.0000 | melee_candidate | hand_r:0.1833, hand_l:0.1833 |
| Stander_Spear_Attack2 | 1.0667 | melee_candidate | hand_r:0.2311, hand_l:0.2311 |
| Stander_Spear_Attack3 | 1.1667 | melee_candidate | hand_r:0.0389, hand_l:0.3306 |
| Stander_Spear_Block | 3.5000 | guard_lifecycle |  |
| Stander_Spear_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_Spear_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Spear_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Spear_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_Spear_Block_Start | 0.2000 | guard_lifecycle |  |
| Stander_Spear_Dodge | 0.5000 | movement_lifecycle |  |
| Stander_Spear_DodgeAttack | 0.8667 | melee_candidate / displacement | hand_r:0.0289, hand_l:0.0578 |
| Stander_Spear_DodgeRoll | 0.7333 | movement_lifecycle |  |
| Stander_Spear_Idle | 1.8667 | auxiliary |  |
| Stander_Spear_Jump | 0.8333 | movement_lifecycle |  |
| Stander_Spear_JumpAttack1 | 0.5000 | melee_candidate / airborne | hand_r:0.0667, hand_l:0.0583 |
| Stander_Spear_JumpAttack2 | 0.4000 | melee_candidate / airborne | hand_r:0.0933, hand_l:0.0867 |
| Stander_Spear_JumpAttack3 | 0.5333 | melee_candidate / airborne | hand_r:0.0267, hand_l:0.0889 |
| Stander_Spear_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Spear_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Spear_Run | 0.8000 | movement_lifecycle |  |
| Stander_Spear_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_Spear_Skill1_Float | 1.0667 | melee_candidate | hand_r:0.2311, hand_l:0.3022 |
| Stander_Spear_Skill2_Slow | 1.0000 | melee_candidate | hand_r:0.0167, hand_l:0.3333 |
| Stander_Spear_Skill3_Stun | 0.8000 | melee_candidate | hand_r:0.2667, hand_l:0.0267 |
| Stander_Spear_Skill4_Push | 1.4667 | melee_candidate | hand_r:0.4889, hand_l:0.0244 |
| Stander_Spear_Skill5_Pull | 0.9333 | melee_candidate | hand_r:0.1867, hand_l:0.2178 |
| Stander_Spear_Skill6_Move | 1.5000 | melee_candidate / displacement | hand_r:0.6250, hand_l:0.7250 |
| Stander_Spear_Skill7_Around | 1.5333 | melee_candidate / radial | hand_r:0.1022, hand_l:0.5367 |
| Stander_Spear_Skill8_Air | 1.2667 | melee_candidate / airborne | hand_r:0.4011, hand_l:0.9500 |

## 08_Staff

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Staff_Attack1 | 0.6000 | projectile_candidate | hand_r:0.0300, hand_l:0.1800 |
| Stander_Staff_Attack2 | 0.6667 | projectile_candidate | hand_r:0.0111, hand_l:0.0222 |
| Stander_Staff_Attack3 | 1.0667 | projectile_candidate | hand_r:0.0178, hand_l:0.0178 |
| Stander_Staff_Block | 3.5000 | guard_lifecycle |  |
| Stander_Staff_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_Staff_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Staff_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Staff_Block_Parry | 0.3333 | guard_reaction |  |
| Stander_Staff_Block_Start | 0.2000 | guard_lifecycle |  |
| Stander_Staff_Dodge | 1.0000 | movement_lifecycle |  |
| Stander_Staff_DodgeAttack | 0.5333 | projectile_candidate / displacement | hand_r:0.1511, hand_l:0.1600 |
| Stander_Staff_DodgeRoll | 0.6667 | movement_lifecycle |  |
| Stander_Staff_Idle | 2.0000 | auxiliary |  |
| Stander_Staff_Jump | 0.8333 | movement_lifecycle |  |
| Stander_Staff_JumpAttack1 | 0.4000 | projectile_candidate / airborne | hand_r:0.0133, hand_l:0.0267 |
| Stander_Staff_JumpAttack2 | 0.4000 | projectile_candidate / airborne | hand_r:0.1333, hand_l:0.1200 |
| Stander_Staff_JumpAttack3 | 0.5333 | projectile_candidate / airborne | hand_r:0.0089, hand_l:0.1778 |
| Stander_Staff_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Staff_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Staff_Run | 0.8000 | movement_lifecycle |  |
| Stander_Staff_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_Staff_Skill1_Float | 1.0000 | projectile_candidate | hand_r:0.0167, hand_l:0.0333 |
| Stander_Staff_Skill2_Slow | 0.9000 | projectile_candidate | hand_r:0.0150, hand_l:0.0150 |
| Stander_Staff_Skill3_Stun | 0.8000 | projectile_candidate | hand_r:0.2667, hand_l:0.0267 |
| Stander_Staff_Skill4_Push | 1.4667 | projectile_candidate | hand_r:0.5622, hand_l:0.0244 |
| Stander_Staff_Skill5_Pull | 0.9333 | projectile_candidate | hand_r:0.1867, hand_l:0.2178 |
| Stander_Staff_Skill6_Move | 1.5000 | projectile_candidate / displacement | hand_r:0.4250, hand_l:0.7250 |
| Stander_Staff_Skill7_Around | 1.4000 | projectile_candidate / radial | hand_r:0.0700, hand_l:0.0467 |
| Stander_Staff_Skill8_Air | 1.3333 | projectile_candidate / airborne | hand_r:1.0667, hand_l:1.0222 |

## 09_Fighter

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Fighter_Attack1 | 0.6667 | melee_candidate | hand_r:0.0333, hand_l:0.1444, foot_r:0.0667, foot_l:0.0667 |
| Stander_Fighter_Attack2 | 0.7333 | melee_candidate | hand_r:0.0856, hand_l:0.0611, foot_r:0.0611, foot_l:0.0489 |
| Stander_Fighter_Attack3 | 1.0000 | melee_candidate | hand_r:0.0333, hand_l:0.1833, foot_r:0.0667, foot_l:0.0333 |
| Stander_Fighter_Block | 3.5000 | guard_lifecycle |  |
| Stander_Fighter_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_Fighter_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Fighter_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Fighter_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_Fighter_Block_Start | 0.2000 | guard_lifecycle |  |
| Stander_Fighter_Dodge | 0.5000 | movement_lifecycle |  |
| Stander_Fighter_DodgeAttack | 0.8333 | melee_candidate / displacement | hand_r:0.0278, hand_l:0.0278, foot_r:0.1250, foot_l:0.5972 |
| Stander_Fighter_DodgeRoll | 0.6667 | movement_lifecycle |  |
| Stander_Fighter_Idle | 1.0000 | auxiliary |  |
| Stander_Fighter_Jump | 0.2667 | movement_lifecycle |  |
| Stander_Fighter_JumpAttack1 | 0.6000 | melee_candidate / airborne | hand_r:0.0900, hand_l:0.1300, foot_r:0.0800, foot_l:0.0300 |
| Stander_Fighter_JumpAttack2 | 0.6000 | melee_candidate / airborne | hand_r:0.0300, hand_l:0.1500, foot_r:0.2000, foot_l:0.1800 |
| Stander_Fighter_JumpAttack3 | 0.6000 | melee_candidate / airborne | hand_r:0.1600, hand_l:0.0100, foot_r:0.1500, foot_l:0.1500 |
| Stander_Fighter_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Fighter_Run | 0.7333 | movement_lifecycle |  |
| Stander_Fighter_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_Fighter_Skill1_Float | 1.0000 | melee_candidate | hand_r:0.2500, hand_l:0.2500, foot_r:0.0667, foot_l:0.0500 |
| Stander_Fighter_Skill2_Slow | 1.0667 | melee_candidate | hand_r:0.0533, hand_l:0.2667, foot_r:0.2311, foot_l:0.8178 |
| Stander_Fighter_Skill3_Stun | 0.8000 | melee_candidate | hand_r:0.3200, hand_l:0.0267, foot_r:0.2667, foot_l:0.2667 |
| Stander_Fighter_Skill4_Push | 1.2667 | melee_candidate | hand_r:0.0211, hand_l:0.4011, foot_r:0.3167, foot_l:0.0633 |
| Stander_Fighter_Skill5_Pull | 0.9333 | melee_candidate | hand_r:0.2178, hand_l:0.2178, foot_r:0.0156, foot_l:0.1867 |
| Stander_Fighter_Skill6_Move | 1.3333 | melee_candidate / displacement | hand_r:0.4667, hand_l:0.3556, foot_r:0.5556, foot_l:0.4667 |
| Stander_Fighter_Skill7_Around | 1.0333 | melee_candidate / radial | hand_r:0.3961, hand_l:0.3961, foot_r:0.0172, foot_l:0.0172 |
| Stander_Fighter_Skill8_Air | 1.2667 | melee_candidate / airborne | hand_r:0.2956, hand_l:0.9500, foot_r:0.2956, foot_l:0.9500 |

## 10_Rapier

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Rapier_Attack1 | 0.8000 | melee_candidate | hand_r:0.2000, hand_l:0.1867 |
| Stander_Rapier_Attack2 | 0.9333 | melee_candidate | hand_r:0.0311, hand_l:0.1867 |
| Stander_Rapier_Attack3 | 1.0333 | melee_candidate | hand_r:0.0344, hand_l:0.2239 |
| Stander_Rapier_Block | 3.5000 | guard_lifecycle |  |
| Stander_Rapier_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_Rapier_Block_End | 0.5000 | guard_lifecycle |  |
| Stander_Rapier_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_Rapier_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_Rapier_Block_Start | 0.2000 | guard_lifecycle |  |
| Stander_Rapier_Dodge | 0.4667 | movement_lifecycle |  |
| Stander_Rapier_DodgeAttack | 0.8333 | melee_candidate / displacement | hand_r:0.1250, hand_l:0.1250 |
| Stander_Rapier_DodgeRoll | 0.6667 | movement_lifecycle |  |
| Stander_Rapier_Idle | 1.6667 | auxiliary |  |
| Stander_Rapier_Jump | 0.8333 | movement_lifecycle |  |
| Stander_Rapier_JumpAttack1 | 0.5333 | melee_candidate / airborne | hand_r:0.1422, hand_l:0.1600 |
| Stander_Rapier_JumpAttack2 | 0.5333 | melee_candidate / airborne | hand_r:0.2222, hand_l:0.2222 |
| Stander_Rapier_JumpAttack3 | 0.5333 | melee_candidate / airborne | hand_r:0.0089, hand_l:0.2222 |
| Stander_Rapier_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_Rapier_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_Rapier_Run | 0.8000 | movement_lifecycle |  |
| Stander_Rapier_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_Rapier_Skill1 | 1.0667 | melee_candidate | hand_r:0.3200, hand_l:0.3200 |
| Stander_Rapier_Skill2 | 0.9333 | melee_candidate | hand_r:0.2022, hand_l:0.1867 |
| Stander_Rapier_Skill3 | 0.8000 | melee_candidate | hand_r:0.2533, hand_l:0.0267 |
| Stander_Rapier_Skill4 | 1.3333 | melee_candidate | hand_r:0.4667, hand_l:0.4889 |
| Stander_Rapier_Skill5 | 0.9333 | melee_candidate | hand_r:0.1867, hand_l:0.2178 |
| Stander_Rapier_Skill6 | 1.3333 | melee_candidate | hand_r:0.3556, hand_l:0.3556 |
| Stander_Rapier_Skill7 | 1.5333 | melee_candidate | hand_r:0.0256, hand_l:0.0256 |
| Stander_Rapier_Skill8 | 1.2000 | melee_candidate | hand_r:0.2800, hand_l:0.3000 |

## 11_DoubleBlades

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_DoubleBlades_Attack1 | 1.0000 | melee_candidate / multi_hand_review | hand_r:0.2500, hand_l:0.0167 |
| Stander_DoubleBlades_Attack2 | 1.1333 | melee_candidate / multi_hand_review | hand_r:0.3211, hand_l:0.2267 |
| Stander_DoubleBlades_Attack3 | 1.4667 | melee_candidate / multi_hand_review | hand_r:0.5133, hand_l:0.5133 |
| Stander_DoubleBlades_Block | 3.5000 | guard_lifecycle |  |
| Stander_DoubleBlades_Block_Damage | 0.3333 | guard_reaction |  |
| Stander_DoubleBlades_Block_In | 0.2000 | guard_lifecycle |  |
| Stander_DoubleBlades_Block_Loop | 1.6000 | guard_lifecycle |  |
| Stander_DoubleBlades_Block_Out | 0.5000 | guard_lifecycle |  |
| Stander_DoubleBlades_Block_Parry | 0.8667 | guard_reaction |  |
| Stander_DoubleBlades_Dodge | 0.4667 | movement_lifecycle |  |
| Stander_DoubleBlades_DodgeAttack | 0.8000 | melee_candidate / displacement,multi_hand_review | hand_r:0.1200, hand_l:0.0133 |
| Stander_DoubleBlades_DodgeRoll | 0.6667 | movement_lifecycle |  |
| Stander_DoubleBlades_Idle | 2.2667 | auxiliary |  |
| Stander_DoubleBlades_Jump | 0.8333 | movement_lifecycle |  |
| Stander_DoubleBlades_JumpAttack1 | 0.6000 | melee_candidate / airborne,multi_hand_review | hand_r:0.1600, hand_l:0.1500 |
| Stander_DoubleBlades_JumpAttack2 | 0.6667 | melee_candidate / airborne,multi_hand_review | hand_r:0.2889, hand_l:0.0889 |
| Stander_DoubleBlades_JumpAttack3 | 0.8000 | melee_candidate / airborne,multi_hand_review | hand_r:0.2800, hand_l:0.2800 |
| Stander_DoubleBlades_JumpFall | 0.2667 | movement_lifecycle |  |
| Stander_DoubleBlades_Jump_Original | 0.8333 | movement_lifecycle |  |
| Stander_DoubleBlades_Run | 0.7333 | movement_lifecycle |  |
| Stander_DoubleBlades_RunEnd | 0.4333 | movement_lifecycle |  |
| Stander_DoubleBlades_Skill1 | 1.0667 | melee_candidate / multi_hand_review | hand_r:0.3911, hand_l:0.0178 |
| Stander_DoubleBlades_Skill2 | 1.0000 | melee_candidate / multi_hand_review | hand_r:0.4333, hand_l:0.0167 |
| Stander_DoubleBlades_Skill3 | 0.8000 | melee_candidate / multi_hand_review | hand_r:0.2533, hand_l:0.0133 |
| Stander_DoubleBlades_Skill4 | 1.8000 | melee_candidate / multi_hand_review | hand_r:0.5700, hand_l:0.0300 |
| Stander_DoubleBlades_Skill5 | 0.9333 | melee_candidate / multi_hand_review | hand_r:0.2333, hand_l:0.1867 |
| Stander_DoubleBlades_Skill6 | 1.3333 | melee_candidate / multi_hand_review | hand_r:0.3556, hand_l:0.3556 |
| Stander_DoubleBlades_Skill7 | 1.3333 | melee_candidate / multi_hand_review | hand_r:0.3556, hand_l:0.0222 |
| Stander_DoubleBlades_Skill8 | 1.2667 | melee_candidate / multi_hand_review | hand_r:0.9711, hand_l:0.9711 |

## 50_Adventure

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_ClimbCliff_In | 0.7333 | auxiliary |  |
| Stander_ClimbCliff_Loop | 2.2667 | auxiliary |  |
| Stander_ClimbCliff_Out | 0.9333 | auxiliary |  |
| Stander_ClimbLadder_Down | 0.6667 | auxiliary |  |
| Stander_ClimbLadder_In | 0.3333 | auxiliary |  |
| Stander_ClimbLadder_Left_In | 0.3333 | auxiliary |  |
| Stander_ClimbLadder_Left_Loop | 1.3333 | auxiliary |  |
| Stander_ClimbLadder_Left_Out | 0.6667 | auxiliary |  |
| Stander_ClimbLadder_Loop | 0.6667 | auxiliary |  |
| Stander_ClimbLadder_Right_In | 1.6667 | auxiliary |  |
| Stander_ClimbLadder_Right_Loop | 1.3333 | auxiliary |  |
| Stander_ClimbLadder_Right_Out | 0.6667 | auxiliary |  |
| Stander_ClimbLadder_Up | 0.6667 | auxiliary |  |
| Stander_Swim_Idle | 2.0000 | auxiliary |  |
| Stander_Swim_Run | 1.8000 | movement_lifecycle |  |
| Stander_Swim_RunEnd | 0.5333 | movement_lifecycle |  |
| Stander_Swim_Walk_B | 1.8000 | movement_lifecycle |  |
| Stander_Swim_Walk_F | 1.8000 | movement_lifecycle |  |
| Stander_WallJump_In | 0.6000 | movement_lifecycle |  |
| Stander_WallJump_Loop | 1.6000 | movement_lifecycle |  |
| Stander_WallJump_Out | 0.2667 | movement_lifecycle |  |

## 98_Damage

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_DmgAir_Down | 1.3333 | auxiliary |  |
| Stander_DmgAir_Down_Original | 1.3333 | auxiliary |  |
| Stander_DmgAir_End | 0.7333 | auxiliary |  |
| Stander_DmgAir_Fall | 0.2667 | movement_lifecycle |  |
| Stander_DmgAir_Heavy | 1.6667 | auxiliary |  |
| Stander_DmgAir_Light | 1.6667 | auxiliary |  |
| Stander_DmgAir_Start | 1.3333 | auxiliary |  |
| Stander_DmgAir_Start_Original | 1.3333 | auxiliary |  |
| Stander_DmgGround_Down_B_Heavy | 1.1667 | auxiliary |  |
| Stander_DmgGround_Down_B_Light | 0.6667 | auxiliary |  |
| Stander_DmgGround_Down_F_Heavy | 1.3333 | auxiliary |  |
| Stander_DmgGround_Down_F_Light | 1.0000 | auxiliary |  |
| Stander_DmgGround_Heavy | 0.5333 | auxiliary |  |
| Stander_DmgGround_Heavy_Original | 0.5333 | auxiliary |  |
| Stander_DmgGround_Light | 0.5333 | auxiliary |  |
| Stander_DmgGround_Light_Original | 0.5333 | auxiliary |  |
| Stander_DmgGround_Pull | 0.8000 | auxiliary |  |
| Stander_DmgGround_Push | 0.8000 | auxiliary |  |
| Stander_LyingBack | 1.3333 | auxiliary |  |
| Stander_LyingBack_GetUp | 0.6667 | auxiliary |  |
| Stander_LyingFront | 1.3333 | auxiliary |  |
| Stander_LyingFront_GetUp | 0.6667 | auxiliary |  |
| Stander_Pulled | 0.2667 | auxiliary |  |
| Stander_Pushed | 0.2667 | auxiliary |  |
| Stander_Slowed | 1.0667 | auxiliary |  |
| Stander_Stunned | 2.0000 | auxiliary |  |

## 99_Sub

| 动作 | 时长s | 行为类别 | 轨迹峰候选s |
|---|---:|---|---|
| Stander_Sub_Drink | 1.6667 | auxiliary |  |
| Stander_Sub_Enter_In | 3.4000 | auxiliary |  |
| Stander_Sub_Enter_Out | 2.0000 | auxiliary |  |
| Stander_Sub_Get | 1.6000 | auxiliary |  |
| Stander_Sub_HeroLanding | 1.5000 | auxiliary |  |
| Stander_Sub_Idle1 | 2.2667 | auxiliary |  |
| Stander_Sub_Idle2 | 2.2667 | auxiliary |  |
| Stander_Sub_Pick | 0.5333 | auxiliary |  |
| Stander_Sub_Set | 1.0000 | auxiliary |  |
| Stander_Sub_Sit1 | 3.6667 | auxiliary |  |
| Stander_Sub_Sit2 | 3.8333 | auxiliary |  |
| Stander_Sub_Spell1 | 1.5000 | auxiliary |  |
| Stander_Sub_Spell2 | 1.0000 | auxiliary |  |
| Stander_Sub_Spell3 | 1.0000 | auxiliary |  |
| Stander_Sub_Spell4 | 1.3333 | auxiliary |  |
| Stander_Sub_Talk1 | 2.0000 | auxiliary |  |
| Stander_Sub_Talk2 | 2.0000 | auxiliary |  |
| Stander_Sub_Throw1 | 0.7333 | auxiliary |  |

## 示例武器挂点

以下从本次加载的原始示例地图读取，不是猜测挂点。每组选择与实际攻击姿态匹配的示例，不能假设所有武器共用剑盾偏移。

- Weapon_Sword_14：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000001907348633, 5.99998664855957, 2.09808349609375e-05], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Idle.SkeletalMeshComponent0' (0x00000296CE67A000) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield_17：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [24.99999237060547, -1.9999847412109375, 1.9999942779541016], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Idle.SkeletalMeshComponent0' (0x00000296CE67A000) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword10：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000072479248047, 5.999852180480957, -8.296966552734375e-05], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Jump_Original.SkeletalMeshComponent0' (0x00000296CE731400) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield10：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [24.999841690063477, -2.0000133514404297, 1.9999306201934814], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Jump_Original.SkeletalMeshComponent0' (0x00000296CE731400) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword11：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.999746322631836, 5.999892234802246, -8.821487426757812e-05], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE732800) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield11：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [24.999710083007812, -2.0002880096435547, 2.0000553131103516], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE732800) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword12：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.0, 6.000034332275391, -0.00014209747314453125], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE733C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield12：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [24.99992561340332, -2.0001397132873535, 2.0000412464141846], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE733C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword13：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.999717712402344, 5.9999494552612305, 0.0001544952392578125], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_JumpAttack3.SkeletalMeshComponent0' (0x00000296CE735000) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield13：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [24.999982833862305, -2.0001001358032227, 2.000229835510254], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_JumpAttack3.SkeletalMeshComponent0' (0x00000296CE735000) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword14：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.999979019165039, 5.999911308288574, 0.00012969970703125], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_JumpFall.SkeletalMeshComponent0' (0x00000296CE736400) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield14：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [24.999813079833984, -2.0002591609954834, 2.0000553131103516], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_JumpFall.SkeletalMeshComponent0' (0x00000296CE736400) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword16：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.99986743927002, 5.999955177307129, -1.621246337890625e-05], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_RunEnd.SkeletalMeshComponent0' (0x00000296CE738C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield16：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [24.999582290649414, -1.9999828338623047, 2.000143051147461], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_RunEnd.SkeletalMeshComponent0' (0x00000296CE738C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword17：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000061988830566, 5.999999046325684, -7.864773215260357e-05], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill1_Float.SkeletalMeshComponent0' (0x00000296CE73A000) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword18：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000286102294922, 5.999763488769531, 0.0002613067626953125], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill2_Slow.SkeletalMeshComponent0' (0x00000296CE73B400) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield18：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [25.000648498535156, -1.9995918273925781, 2.0002870559692383], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill2_Slow.SkeletalMeshComponent0' (0x00000296CE73B400) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword19：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000286102294922, 5.999763488769531, 0.00026607513427734375], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill8_Air.SkeletalMeshComponent0' (0x00000296CE753C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield19：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [24.999860763549805, -2.0003414154052734, 2.0001602172851562], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill8_Air.SkeletalMeshComponent0' (0x00000296CE753C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword20：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000286102294922, 5.999764442443848, 0.00026416778564453125], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill3_Stun.SkeletalMeshComponent0' (0x00000296CE73C800) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield20：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [25.000656127929688, -1.9995918273925781, 2.0002822875976562], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill3_Stun.SkeletalMeshComponent0' (0x00000296CE73C800) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword21：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.00033187866211, 5.999821662902832, 0.00022602081298828125], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill4_Push.SkeletalMeshComponent0' (0x00000296CE73DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield21：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [25.00066566467285, -1.9996013641357422, 2.000284194946289], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill4_Push.SkeletalMeshComponent0' (0x00000296CE73DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword22：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000280380249023, 5.999755859375, 0.000263214111328125], "rotation": [6.088522241741977e-08, -0.13052633249009818, 8.9930868173909e-09, 0.991444842906896], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill5_Pull.SkeletalMeshComponent0' (0x00000296CE750000) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield22：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [25.000648498535156, -1.9996070861816406, 2.000293731689453], "rotation": [0.9299565714198694, 0.1112835655518298, -0.051892333391424544, 0.34656013770522637], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill5_Pull.SkeletalMeshComponent0' (0x00000296CE750000) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword23：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000287055969238, 5.999761581420898, 0.00026035308837890625], "rotation": [1.4656690040025144e-07, -0.1305263324901031, -2.080924053715182e-08, 0.9914448429068863], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE751400) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield23：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [25.00065040588379, -1.999593734741211, 2.0002870559692383], "rotation": [0.9299565484261698, 0.11128344861222735, -0.051892241341670683, 0.34656025073974067], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE751400) Class 'SkeletalMeshComponent'>`。
- Weapon_Sword24：`/Game/PlatformAnimation/Models/Weapon.Weapon_Sword`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000329971313477, 5.999825954437256, 0.00022411346435546875], "rotation": [1.4656690040025144e-07, -0.1305263324901031, -2.080924053715182e-08, 0.9914448429068863], "scale": [1.0, 1.0, 0.9999995231628418]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE752800) Class 'SkeletalMeshComponent'>`。
- Weapon_Shield24：`/Game/PlatformAnimation/Models/Weapon.Weapon_Shield`，socket `lowerarm_l소켓`，变换 `{"translation": [25.00067901611328, -1.9996089935302734, 2.0003252029418945], "rotation": [0.9299565484261698, 0.11128344861222735, -0.051892241341670683, 0.34656025073974067], "scale": [1.0, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Sword_Shield_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE752800) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer_106：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592926025390625, 5.088130474090576, 0.6583271026611328], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999231696128845]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Idle.SkeletalMeshComponent0' (0x00000296CE5DB400) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer2：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592924118041992, 5.088129997253418, 0.6583266258239746], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Attack1_154.SkeletalMeshComponent0' (0x00000296CE5D2800) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer7：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592893600463867, 5.088153839111328, 0.6582040786743164], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_DodgeRoll.SkeletalMeshComponent0' (0x00000296CE5DA000) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer10：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592920303344727, 5.088364124298096, 0.6581673622131348], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Jump_Original.SkeletalMeshComponent0' (0x00000296CE5DDC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer11：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592896461486816, 5.087889194488525, 0.6583271026611328], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE571400) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer12：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592893600463867, 5.088248252868652, 0.658287763595581], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE5E0000) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer14：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.59284782409668, 5.087825775146484, 0.65854811668396], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_JumpFall.SkeletalMeshComponent0' (0x00000296CE5E2800) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer15：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.59286880493164, 5.087862014770508, 0.6581878662109375], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Run.SkeletalMeshComponent0' (0x00000296CE5E3C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer17：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592961311340332, 5.087865352630615, 0.6580853462219238], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Skill1_Float.SkeletalMeshComponent0' (0x00000296CE5E6400) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer18：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592880249023438, 5.088000297546387, 0.658116340637207], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Skill2_Slow.SkeletalMeshComponent0' (0x00000296CE5E7800) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer19：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592885971069336, 5.088002681732178, 0.6581153869628906], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Skill3_Stun.SkeletalMeshComponent0' (0x00000296CE5E8C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer20：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.59296989440918, 5.087862968444824, 0.6580815315246582], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Skill4_Push.SkeletalMeshComponent0' (0x00000296CE5EA000) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer21：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592881202697754, 5.08799934387207, 0.6581215858459473], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Skill5_Pull.SkeletalMeshComponent0' (0x00000296CE5EB400) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer22：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592887878417969, 5.088001251220703, 0.6581172943115234], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE5EC800) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer23：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592897415161133, 5.0879974365234375, 0.6581172943115234], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE5EDC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Hammer24：`/Game/PlatformAnimation/Models/Weapon.Weapon_Hammer`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.592880249023438, 5.088001251220703, 0.6581254005432129], "rotation": [0.010937749146161564, -0.21813658615376957, -0.0024923684720510207, 0.975853771588856], "scale": [1.0, 1.0, 0.9999235272407532]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Hammer_Skill8_Air.SkeletalMeshComponent0' (0x00000296CE600000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade_160：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999999046325684, -4.999990940093994, 1.1444091796875e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998974800109863, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Idle_157.SkeletalMeshComponent0' (0x00000296CE58C800) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade2_163：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000011444091797, 5.0000152587890625, 2.86102294921875e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975992202759]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Idle_157.SkeletalMeshComponent0' (0x00000296CE58C800) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade3：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.99997329711914, -4.999979496002197, 3.0517578125e-05], "rotation": [0.9990482299311444, 3.2142937608910146e-07, 0.04361919613290095, 2.515216044288538e-07], "scale": [0.9998974800109863, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Attack1.SkeletalMeshComponent0' (0x00000296CE583C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade4：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000011444091797, 5.0000152587890625, 2.86102294921875e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Attack1.SkeletalMeshComponent0' (0x00000296CE583C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade5：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999999046325684, -4.999990940093994, 1.1444091796875e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Attack2.SkeletalMeshComponent0' (0x00000296CE585000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade13：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999895095825195, -4.999972343444824, 7.915496826171875e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Dodge.SkeletalMeshComponent0' (0x00000296CE588C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade15：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999887466430664, -5.000001430511475, -4.100799560546875e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_DodgeAttack.SkeletalMeshComponent0' (0x00000296CE58A000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade21：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999847412109375, -4.999937057495117, 2.47955322265625e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Jump_Original.SkeletalMeshComponent0' (0x00000296CE590000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade22：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.99993896484375, 4.999809741973877, -1.5735626220703125e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Jump_Original.SkeletalMeshComponent0' (0x00000296CE590000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade23：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [10.000118255615234, -4.999983310699463, -3.910064697265625e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE591400) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade24：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000186920166016, 5.000004768371582, -6.67572021484375e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE591400) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade25：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999990463256836, -4.999924182891846, 0.0001392364501953125], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE592800) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade28：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.999982833862305, 5.000088691711426, -0.00011539459228515625], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_JumpAttack3.SkeletalMeshComponent0' (0x00000296CE593C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade29：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [10.000049591064453, -5.000053882598877, -9.775161743164062e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_JumpFall.SkeletalMeshComponent0' (0x00000296CE595000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade30：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.99992561340332, 5.000120162963867, -0.00017261505126953125], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_JumpFall.SkeletalMeshComponent0' (0x00000296CE595000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade31：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [10.000158309936523, -5.000064849853516, -0.00012922286987304688], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Run.SkeletalMeshComponent0' (0x00000296CE596400) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade32：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000072479248047, 5.000057220458984, 0.00014972686767578125], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Run.SkeletalMeshComponent0' (0x00000296CE596400) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade33：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999882698059082, -4.999995708465576, -4.1961669921875e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_RunEnd.SkeletalMeshComponent0' (0x00000296CE597800) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade34：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.999878883361816, 4.999999046325684, -1.682290167082101e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_RunEnd.SkeletalMeshComponent0' (0x00000296CE597800) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade35：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999897956848145, -4.999982833862305, 7.62939453125e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill1_Float.SkeletalMeshComponent0' (0x00000296CE598C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade36：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000033378601074, 4.9998698234558105, 5.3882598876953125e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill1_Float.SkeletalMeshComponent0' (0x00000296CE598C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade37：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999985694885254, -4.999913215637207, 0.00017547607421875], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill2_Slow.SkeletalMeshComponent0' (0x00000296CE59A000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade38：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.999934196472168, 4.999804496765137, -1.2874603271484375e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill2_Slow.SkeletalMeshComponent0' (0x00000296CE59A000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade39：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999897003173828, -4.999983310699463, 7.82012939453125e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill3_Stun.SkeletalMeshComponent0' (0x00000296CE59B400) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade40：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000040054321289, 4.999871730804443, 4.863739013671875e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill3_Stun.SkeletalMeshComponent0' (0x00000296CE59B400) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade42：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000039100646973, 4.999872207641602, 4.9591064453125e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill4_Push.SkeletalMeshComponent0' (0x00000296CE59C800) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade43：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999985694885254, -4.9999098777771, 0.000171661376953125], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill5_Pull.SkeletalMeshComponent0' (0x00000296CE59DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade44：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.99992847442627, 4.9998016357421875, -1.239776611328125e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill5_Pull.SkeletalMeshComponent0' (0x00000296CE59DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade45：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [10.000003814697266, -4.999929428100586, 0.0001888275146484375], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE5B0000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade46：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.999934196472168, 4.999804973602295, -1.049041748046875e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE5B0000) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade47：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999897956848145, -4.999983787536621, 8.20159912109375e-05], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE5B1400) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade48：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.000045776367188, 4.999876022338867, 4.7206878662109375e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE5B1400) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade49：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_l소켓`，变换 `{"translation": [9.999980926513672, -4.999905586242676, 0.0001735687255859375], "rotation": [0.9990482272085601, 1.9063221133474364e-07, 0.04361925849129819, 2.572322927894256e-07], "scale": [0.9998975396156311, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill8_Air.SkeletalMeshComponent0' (0x00000296CE5B2800) Class 'SkeletalMeshComponent'>`。
- Weapon_DualBlade50：`/Game/PlatformAnimation/Models/Weapon.Weapon_DualBlade`，socket `ik_hand_r소켓`，变换 `{"translation": [-9.999935150146484, 4.999804496765137, -1.2874603271484375e-05], "rotation": [-0.08707263684759303, -0.04345334085525323, -0.0038015729957823286, 0.9952465579562092], "scale": [1.0, 1.0, 0.9998975396156311]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DualBlades_Skill8_Air.SkeletalMeshComponent0' (0x00000296CE5B2800) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol_244：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725852012634277, 6.665111541748047, 0.47389793395996094], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.9998817443847656]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Attack_241.SkeletalMeshComponent0' (0x00000296CE666400) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol2：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725852012634277, 6.665111541748047, 0.47389793395996094], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Attack0.SkeletalMeshComponent0' (0x00000296CE64B400) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol4：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725914001464844, 6.664974212646484, 0.4738950729370117], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Attack1_Air.SkeletalMeshComponent0' (0x00000296CE660000) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol9：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.72598648071289, 6.664885520935059, 0.47420310974121094], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Attack3_Air.SkeletalMeshComponent0' (0x00000296CE665000) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol10：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725696563720703, 6.6651763916015625, 0.47393321990966797], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Block.SkeletalMeshComponent0' (0x00000296CE667800) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol11：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725988388061523, 6.664880752563477, 0.4742107391357422], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Dodge.SkeletalMeshComponent0' (0x00000296CE668C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol12：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725802421569824, 6.664989471435547, 0.4736483097076416], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_DodgeAttack.SkeletalMeshComponent0' (0x00000296CE66A000) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol13：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725982666015625, 6.664892196655273, 0.47420692443847656], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_DodgeRoll.SkeletalMeshComponent0' (0x00000296CE66B400) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol15：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.72584342956543, 6.664915084838867, 0.47363853454589844], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Jump.SkeletalMeshComponent0' (0x00000296CE66DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol16：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725663185119629, 6.665143966674805, 0.47405529022216797], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Jump_Original.SkeletalMeshComponent0' (0x00000296CE670000) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol17：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725475311279297, 6.6650896072387695, 0.4738759994506836], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpAttack0.SkeletalMeshComponent0' (0x00000296CE671400) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol18：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725889205932617, 6.664986610412598, 0.47374629974365234], "rotation": [-0.08596429336431458, -0.027521939915596382, 0.9915469244073385, 0.09320611454193133], "scale": [1.0, 1.0, 0.9998813271522522]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE673C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol20：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.726029396057129, 6.664804458618164, 0.47392702102661133], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpAttack0_Ground.SkeletalMeshComponent0' (0x00000296CE672800) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol21：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725883483886719, 6.6650872230529785, 0.4736442565917969], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE676400) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol22：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.726075172424316, 6.665037631988525, 0.4735576808452606], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpAttack2_Ground.SkeletalMeshComponent0' (0x00000296CE677800) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol23：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.726262092590332, 6.665227890014648, 0.4735541343688965], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpAttack3.SkeletalMeshComponent0' (0x00000296CE678C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol24：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.726259231567383, 6.665229797363281, 0.47355222702026367], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpAttack3_Ground.SkeletalMeshComponent0' (0x00000296CE67B400) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol25：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725740432739258, 6.664971351623535, 0.4739093780517578], "rotation": [-0.08596423968615885, -0.027521938369239968, 0.9915469164880831, 0.09320624875272147], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpFall.SkeletalMeshComponent0' (0x00000296CE67C800) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol26：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725738525390625, 6.66485595703125, 0.47356224060058594], "rotation": [-0.08596437492798971, -0.027522137247032594, 0.9915469014344919, 0.09320622543721739], "scale": [1.0, 1.0, 0.9998813271522522]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Reload.SkeletalMeshComponent0' (0x00000296CE67DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol27：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725773811340332, 6.665072917938232, 0.4738517999649048], "rotation": [-0.08596437492798971, -0.027522137247032594, 0.9915469014344919, 0.09320622543721739], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Run.SkeletalMeshComponent0' (0x00000296CE680000) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol28：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725996971130371, 6.665128231048584, 0.47376468777656555], "rotation": [-0.08596437492798971, -0.027522137247032594, 0.9915469014344919, 0.09320622543721739], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_RunEnd.SkeletalMeshComponent0' (0x00000296CE681400) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol29：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725737571716309, 6.664859771728516, 0.47356224060058594], "rotation": [-0.08596437492798971, -0.027522137247032594, 0.9915469014344919, 0.09320622543721739], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Skill1_Float.SkeletalMeshComponent0' (0x00000296CE682800) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol30：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725738525390625, 6.664856433868408, 0.4735593795776367], "rotation": [-0.08596437492798971, -0.027522137247032594, 0.9915469014344919, 0.09320622543721739], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Skill2_Slow.SkeletalMeshComponent0' (0x00000296CE683C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol33：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725736618041992, 6.664860248565674, 0.47356224060058594], "rotation": [-0.08596437492798971, -0.027522137247032594, 0.9915469014344919, 0.09320622543721739], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Skill5_Pull.SkeletalMeshComponent0' (0x00000296CE687800) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol34：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725648880004883, 6.666085720062256, 0.47386646270751953], "rotation": [-0.08596412652830122, -0.027522264721298765, 0.9915469685593785, 0.09320570280590974], "scale": [1.0, 1.0, 0.9998813271522522]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE688C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol35：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725652694702148, 6.666090965270996, 0.4738645553588867], "rotation": [-0.08596412652830122, -0.027522264721298765, 0.9915469685593785, 0.09320570280590974], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE68A000) Class 'SkeletalMeshComponent'>`。
- Weapon_Pistol37：`/Game/PlatformAnimation/Models/Weapon.Weapon_Pistol`，socket `ik_hand_r소켓`，变换 `{"translation": [-11.725854873657227, 6.665216445922852, 0.4739828109741211], "rotation": [-0.08596429336431458, -0.027521939915596382, 0.9915469244073385, 0.09320611454193133], "scale": [1.0, 1.0, 0.999881386756897]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Pistol_JumpAttack1_Ground.SkeletalMeshComponent0' (0x00000296CE675000) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow_331：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877659797668457, -6.96477746963501, -0.5305614471435547], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.999898374080658, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Attack_328.SkeletalMeshComponent0' (0x00000296CE502800) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow2：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877659797668457, -6.96477746963501, -0.5305614471435547], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Attack1.SkeletalMeshComponent0' (0x00000296CE1FB400) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow4：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877659797668457, -6.96477746963501, -0.5305614471435547], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998984932899475, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Attack2.SkeletalMeshComponent0' (0x00000296CE1FC800) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow6：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877718925476074, -6.9647345542907715, -0.5306625366210938], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Attack3.SkeletalMeshComponent0' (0x00000296CE500000) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow7：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877558708190918, -6.964820384979248, -0.5305156707763672], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Attack3_Air.SkeletalMeshComponent0' (0x00000296CE501400) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow8：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87755298614502, -6.964797019958496, -0.5304794311523438], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Block.SkeletalMeshComponent0' (0x00000296CE503C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow9：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87741756439209, -6.964881420135498, -0.5302524566650391], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Dodge.SkeletalMeshComponent0' (0x00000296CE505000) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow10：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87768840789795, -6.9648847579956055, -0.5308189392089844], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_DodgeAttack.SkeletalMeshComponent0' (0x00000296CE506400) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow11：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87739086151123, -6.964865207672119, -0.5302696228027344], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_DodgeRoll.SkeletalMeshComponent0' (0x00000296CE507800) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow13：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877411842346191, -6.964877128601074, -0.5302524566650391], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Jump.SkeletalMeshComponent0' (0x00000296CE50A000) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow14：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87729549407959, -6.964720726013184, -0.5302371978759766], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Jump_Original.SkeletalMeshComponent0' (0x00000296CE50B400) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow15：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877442359924316, -6.964842319488525, -0.5305979251861572], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE50C800) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow16：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87741470336914, -6.964698791503906, -0.5302839279174805], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_JumpAttack1_Ground.SkeletalMeshComponent0' (0x00000296CE50DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow17：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87725830078125, -6.964699745178223, -0.5304908752441406], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE510000) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow18：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87756633758545, -6.964768886566162, -0.5303955078125], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_JumpAttack2_Ground.SkeletalMeshComponent0' (0x00000296CE511400) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow19：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.878046989440918, -6.964716911315918, -0.5307016372680664], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_JumpAttack3.SkeletalMeshComponent0' (0x00000296CE512800) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow20：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87767219543457, -6.964710235595703, -0.5307884216308594], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_JumpAttack3_Ground.SkeletalMeshComponent0' (0x00000296CE513C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow21：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877095222473145, -6.964574337005615, -0.52972412109375], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_JumpFall.SkeletalMeshComponent0' (0x00000296CE515000) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow22：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877898216247559, -6.964630126953125, -0.5310630798339844], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Run.SkeletalMeshComponent0' (0x00000296CE516400) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow23：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877535820007324, -6.96476411819458, -0.5310468673706055], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_RunEnd.SkeletalMeshComponent0' (0x00000296CE517800) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow24：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877362251281738, -6.964690685272217, -0.5301017761230469], "rotation": [1.2173310029140383e-05, -0.13054625024289232, 9.057204564378205e-05, 0.9914422162668053], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Skill1_Float.SkeletalMeshComponent0' (0x00000296CE518C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow25：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877283096313477, -6.964719772338867, -0.5302352905273438], "rotation": [1.239218123461756e-05, -0.13054648950625494, 9.104889529721776e-05, 0.9914421847158436], "scale": [0.9998984932899475, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Skill2_Slow.SkeletalMeshComponent0' (0x00000296CE51A000) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow30：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877367973327637, -6.964693069458008, -0.5300884246826172], "rotation": [1.239218123461756e-05, -0.13054648950625494, 9.104889529721776e-05, 0.9914421847158436], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Skill3_Stun.SkeletalMeshComponent0' (0x00000296CE51B400) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow31：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.87735652923584, -6.964765548706055, -0.5301799774169922], "rotation": [1.239218123461756e-05, -0.13054648950625494, 9.104889529721776e-05, 0.9914421847158436], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Skill4_Push.SkeletalMeshComponent0' (0x00000296CE51C800) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow32：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877961158752441, -6.96447229385376, -0.53106689453125], "rotation": [1.239218123461756e-05, -0.13054648950625494, 9.104889529721776e-05, 0.9914421847158436], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Skill5_Pull.SkeletalMeshComponent0' (0x00000296CE51DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow33：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877359390258789, -6.964695453643799, -0.5300827026367188], "rotation": [1.239218123461756e-05, -0.13054648950625494, 9.104889529721776e-05, 0.9914421847158436], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE520000) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow34：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877285957336426, -6.964719295501709, -0.5302200317382812], "rotation": [1.267809111100482e-05, -0.13054654723783438, 9.146798554926509e-05, 0.9914421770719424], "scale": [0.9998984932899475, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE521400) Class 'SkeletalMeshComponent'>`。
- Weapon_Bow35：`/Game/PlatformAnimation/Models/Weapon.Weapon_Bow`，socket `ik_hand_l소켓`，变换 `{"translation": [11.877350807189941, -6.964765548706055, -0.5301837921142578], "rotation": [1.267809111100482e-05, -0.13054654723783438, 9.146798554926509e-05, 0.9914421770719424], "scale": [0.9998985528945923, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Bow_Skill8_Air.SkeletalMeshComponent0' (0x00000296CE522800) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear_113：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.8232421875, -5.636474132537842, 1.9725871086120605], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999063611030579, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_DodgeRoll.SkeletalMeshComponent0' (0x00000296CE6C1400) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear2：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823121070861816, -5.6365203857421875, 1.9725923538208008], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Attack1.SkeletalMeshComponent0' (0x00000296CE6B8C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear3：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.8232421875, -5.636596202850342, 1.9725871086120605], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Attack2.SkeletalMeshComponent0' (0x00000296CE6BA000) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear6：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.8232421875, -5.636474132537842, 1.9725871086120605], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Dodge.SkeletalMeshComponent0' (0x00000296CE6BDC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear7：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823236465454102, -5.636415958404541, 1.9724817276000977], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_DodgeAttack.SkeletalMeshComponent0' (0x00000296CE6C0000) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear9：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823406219482422, -5.636627674102783, 1.9724111557006836], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Jump_Original.SkeletalMeshComponent0' (0x00000296CE6C5000) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear10：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823238372802734, -5.63669490814209, 1.972447395324707], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE6C6400) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear11：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823238372802734, -5.636697292327881, 1.972445011138916], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE6C7800) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear12：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823482513427734, -5.636523246765137, 1.9722070693969727], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_JumpAttack3.SkeletalMeshComponent0' (0x00000296CE6C8C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear13：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823419570922852, -5.6366658210754395, 1.9726343154907227], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Run.SkeletalMeshComponent0' (0x00000296CE6CA000) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear15：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823217391967773, -5.636747360229492, 1.9723091125488281], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Skill1_Float.SkeletalMeshComponent0' (0x00000296CE6CC800) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear17：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823563575744629, -5.636500835418701, 1.9720182418823242], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Skill3_Stun.SkeletalMeshComponent0' (0x00000296CE6D0000) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear18：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.822944641113281, -5.636674880981445, 1.9727973937988281], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Skill4_Push.SkeletalMeshComponent0' (0x00000296CE6D1400) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear19：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.823569297790527, -5.636497497558594, 1.972020149230957], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Skill5_Pull.SkeletalMeshComponent0' (0x00000296CE6D2800) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear20：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.82294750213623, -5.636679649353027, 1.9727954864501953], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE6D3C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear21：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.822945594787598, -5.636678218841553, 1.9727940559387207], "rotation": [-0.02000956681072983, 0.9772898648239872, 0.12460341405152224, 0.17022962889892013], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE6D5000) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear22：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.822953224182129, -5.636674404144287, 1.972801685333252], "rotation": [-0.020009283875244203, 0.9772898867447559, 0.12460323858260644, 0.17022966474720932], "scale": [0.999906599521637, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Skill8_Air.SkeletalMeshComponent0' (0x00000296CE6D6400) Class 'SkeletalMeshComponent'>`。
- Weapon_Spear23_236：`/Game/PlatformAnimation/Models/Weapon.Weapon_Spear`，socket `ik_hand_l소켓`，变换 `{"translation": [9.822998046875, -5.636611461639404, 1.9726366996765137], "rotation": [-0.020009283875244203, 0.9772898867447559, 0.12460323858260644, 0.17022966474720932], "scale": [0.9999066591262817, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Spear_Idle.SkeletalMeshComponent0' (0x00000296CE6C2800) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff_242：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305326461791992, -4.872910022735596, 0.27954721450805664], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_Attack1.SkeletalMeshComponent0' (0x00000296CE6D7800) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff5：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305292129516602, -4.872734069824219, 0.2795748710632324], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_Dodge.SkeletalMeshComponent0' (0x00000296CE6DC800) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff6：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305419921875, -4.872801780700684, 0.27954745292663574], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_DodgeAttack.SkeletalMeshComponent0' (0x00000296CE6DDC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff7：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305288314819336, -4.872731685638428, 0.2795701026916504], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_DodgeRoll.SkeletalMeshComponent0' (0x00000296CE6E0000) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff10：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305353164672852, -4.872920036315918, 0.27939367294311523], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_Jump_Original.SkeletalMeshComponent0' (0x00000296CE6E3C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff11：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305631637573242, -4.872864723205566, 0.2795219421386719], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE6E5000) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff12：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305353164672852, -4.872942924499512, 0.2793681025505066], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE6E6400) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff13：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305550575256348, -4.872888565063477, 0.27948427200317383], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_JumpAttack3.SkeletalMeshComponent0' (0x00000296CE6E7800) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff14：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305456161499023, -4.8730316162109375, 0.2795219421386719], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_JumpFall.SkeletalMeshComponent0' (0x00000296CE6E8C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff15：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305546760559082, -4.872980117797852, 0.27936649322509766], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_Run.SkeletalMeshComponent0' (0x00000296CE6EA000) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff17：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305349349975586, -4.872929573059082, 0.2793893814086914], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_Skill1_Float.SkeletalMeshComponent0' (0x00000296CE6EC800) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff18：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305414199829102, -4.8731279373168945, 0.2798347473144531], "rotation": [-0.9911548583116514, -0.0016501045845921138, -0.127553109411402, 0.036599566657995505], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_Skill2_Slow.SkeletalMeshComponent0' (0x00000296CE6EDC00) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff22：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305414199829102, -4.8731279373168945, 0.2798347473144531], "rotation": [-0.99115485804857, -0.0016502626000117638, -0.12755310357649394, 0.03659958699322111], "scale": [0.9998785853385925, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_Skill6_Move.SkeletalMeshComponent0' (0x00000296CE702800) Class 'SkeletalMeshComponent'>`。
- Weapon_Staff23：`/Game/PlatformAnimation/Models/Weapon.Weapon_Staff`，socket `ik_hand_l소켓`，变换 `{"translation": [10.305414199829102, -4.8731279373168945, 0.2798347473144531], "rotation": [-0.99115485804857, -0.0016502626000117638, -0.12755310357649394, 0.03659958699322111], "scale": [0.9998786449432373, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Staff_Skill7_Around.SkeletalMeshComponent0' (0x00000296CE703C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier_223：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.335010528564453, -5.737753868103027, 0.5468044281005859], "rotation": [7.800889816435039e-05, -0.9717300058259568, -9.456341914893612e-05, -0.2360948554074336], "scale": [0.9999539852142334, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_Idle_216.SkeletalMeshComponent0' (0x00000296CE698C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier2：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.335023880004883, -5.737753868103027, 0.5467967987060547], "rotation": [7.800887064077004e-05, -0.9717301040694131, -9.45634029356922e-05, -0.2360944510521713], "scale": [0.9999536871910095, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_Attack1_277.SkeletalMeshComponent0' (0x00000296CE690000) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier3：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.334920883178711, -5.737765312194824, 0.5468559265136719], "rotation": [7.820110473503585e-05, -0.9717302297851692, -9.48536074052858e-05, -0.23609393344396418], "scale": [0.9999537467956543, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_Attack2_278.SkeletalMeshComponent0' (0x00000296CE691400) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier4：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.334970474243164, -5.737758636474609, 0.5468263626098633], "rotation": [7.807170660726684e-05, -0.9717302455144764, -9.482216700652349e-05, -0.23609386875973848], "scale": [0.9999538064002991, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_Block.SkeletalMeshComponent0' (0x00000296CE693C00) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier5：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.334970474243164, -5.737758636474609, 0.5468263626098633], "rotation": [7.81031333334736e-05, -0.97173028479908, -9.495155424598116e-05, -0.2360937070072006], "scale": [0.9999538064002991, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_Dodge_279.SkeletalMeshComponent0' (0x00000296CE695000) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier6：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.334970474243164, -5.737758636474609, 0.5468263626098633], "rotation": [7.810311340016822e-05, -0.9717303555340412, -9.495154260067048e-05, -0.23609341587133229], "scale": [0.9999538064002991, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_DodgeRoll.SkeletalMeshComponent0' (0x00000296CE697800) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier7：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.334970474243164, -5.737758636474609, 0.5468263626098633], "rotation": [7.810311340016822e-05, -0.9717303555340412, -9.495154260067048e-05, -0.23609341587133229], "scale": [0.9999538660049438, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_DodgeAttack_280.SkeletalMeshComponent0' (0x00000296CE696400) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier14：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.334867477416992, -5.737553596496582, 0.5469694137573242], "rotation": [7.849127466605618e-05, -0.9717304262376378, -9.504584405886166e-05, -0.23609312469749325], "scale": [0.9999538064002991, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_Run_287.SkeletalMeshComponent0' (0x00000296CE6A5000) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades_296：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.830020904541016, 5.940818786621094, -0.3953132629394531], "rotation": [-0.12699571514498317, -0.16632583090225722, 0.5928878893130468, 0.7776192879651381], "scale": [1.0, 1.0, 0.9999169111251831]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Attack1.SkeletalMeshComponent0' (0x00000296CE562800) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades2：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.830020904541016, 5.940818786621094, -0.3953132629394531], "rotation": [-0.1269959220127459, -0.1663266823306662, 0.5928877706404939, 0.7776191625480635], "scale": [1.0, 1.0, 0.9999168515205383]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Idle.SkeletalMeshComponent0' (0x00000296CE56B400) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades3：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.83002758026123, 5.94077205657959, -0.39542341232299805], "rotation": [-0.1269957714001861, -0.16632598432364235, 0.5928879559461491, 0.7776191951586847], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Attack2.SkeletalMeshComponent0' (0x00000296CE563C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades4：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.82992172241211, 5.940881729125977, -0.3955416679382324], "rotation": [-0.12699622556865595, -0.16632600010043216, 0.5928878378825867, 0.7776192076285269], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Attack3.SkeletalMeshComponent0' (0x00000296CE565000) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades5：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829839706420898, 5.940950393676758, -0.39564990997314453], "rotation": [-0.12699593537825798, -0.16632557577691906, 0.5928880071008945, 0.777619216760882], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Block.SkeletalMeshComponent0' (0x00000296CE566400) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades6：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829778671264648, 5.940993309020996, -0.39572715759277344], "rotation": [-0.12699580127751142, -0.16632507653149617, 0.5928880323201831, 0.7776193262169974], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Dodge.SkeletalMeshComponent0' (0x00000296CE567800) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades7：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829838752746582, 5.940937519073486, -0.3956594467163086], "rotation": [-0.12699543014513884, -0.1663245188764979, 0.5928882376509429, 0.7776193495521518], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_DodgeAttack.SkeletalMeshComponent0' (0x00000296CE568C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades8：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829776763916016, 5.940979957580566, -0.3957347869873047], "rotation": [-0.12699519503700346, -0.16632393750345098, 0.5928882858165947, 0.7776194755742181], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_DodgeRoll.SkeletalMeshComponent0' (0x00000296CE56A000) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades9：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829776763916016, 5.940979957580566, -0.3957347869873047], "rotation": [-0.1269951809052292, -0.16632317830476653, 0.5928884314665963, 0.7776195292162668], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Jump.SkeletalMeshComponent0' (0x00000296CE56C800) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades10：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829776763916016, 5.940979957580566, -0.3957347869873047], "rotation": [-0.12699499802701444, -0.16632259893233572, 0.5928885860692592, 0.7776195651276444], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Jump_Original.SkeletalMeshComponent0' (0x00000296CE56DC00) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades11：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829778671264648, 5.940823554992676, -0.3955681324005127], "rotation": [-0.12699538745216069, -0.1663228360105032, 0.5928885723240521, 0.7776194613015074], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_JumpAttack1.SkeletalMeshComponent0' (0x00000296CE570000) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades12：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.82970142364502, 5.940615653991699, -0.39548206329345703], "rotation": [-0.1269954747728898, -0.16632283096943237, 0.592888572447533, 0.7776194480249634], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_JumpAttack2.SkeletalMeshComponent0' (0x00000296CE572800) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades13：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829626083374023, 5.940492153167725, -0.39566898345947266], "rotation": [-0.12699612915628394, -0.16632301227186125, 0.5928886773842699, 0.7776192223689343], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_JumpAttack3.SkeletalMeshComponent0' (0x00000296CE573C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades14：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829697608947754, 5.940600395202637, -0.39545297622680664], "rotation": [-0.12699606472597436, -0.16632307770247562, 0.5928885534123872, 0.7776193134177287], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_JumpFall.SkeletalMeshComponent0' (0x00000296CE575000) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades15：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829788208007812, 5.940749168395996, -0.39560651779174805], "rotation": [-0.12699657186734506, -0.1663231727804031, 0.5928885214852453, 0.7776192346008701], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Run.SkeletalMeshComponent0' (0x00000296CE576400) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades16：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.82999038696289, 5.940784454345703, -0.39551806449890137], "rotation": [-0.12699663394428828, -0.16632339714285244, 0.5928885694163074, 0.7776191399298027], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_RunEnd.SkeletalMeshComponent0' (0x00000296CE577800) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades17：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829890251159668, 5.940695285797119, -0.3953514099121094], "rotation": [-0.12699654974669067, -0.16632291904709037, 0.5928885345557419, 0.7776192825184036], "scale": [1.0, 1.0, 0.9999167919158936]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Skill1.SkeletalMeshComponent0' (0x00000296CE578C00) Class 'SkeletalMeshComponent'>`。
- Weapon_DoubleBlades18：`/Game/PlatformAnimation/Models/Weapon_DoubleBlades.Weapon_DoubleBlades`，socket `ik_hand_r소켓`，变换 `{"translation": [-10.829890251159668, 5.940695285797119, -0.3953514099121094], "rotation": [-0.12699654974669067, -0.16632291904709037, 0.5928885345557419, 0.7776192825184036], "scale": [1.0, 1.0, 0.9999168515205383]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_DoubleBlades_Skill2.SkeletalMeshComponent0' (0x00000296CE57A000) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier23：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.334920883178711, -5.737765312194824, 0.5468559265136719], "rotation": [7.820110473503585e-05, -0.9717302297851692, -9.48536074052858e-05, -0.23609393344396418], "scale": [0.9999538660049438, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_Attack3.SkeletalMeshComponent0' (0x00000296CE692800) Class 'SkeletalMeshComponent'>`。
- Weapon_Rapier24：`/Game/PlatformAnimation/Models/Weapon_Rapier.Weapon_Rapier`，socket `ik_hand_l소켓`，变换 `{"translation": [9.334867477416992, -5.737553596496582, 0.5469694137573242], "rotation": [7.849127466605618e-05, -0.9717304262376378, -9.504584405886166e-05, -0.23609312469749325], "scale": [0.9999538660049438, 1.0, 1.0]}`，父 `<Object '/Game/PlatformAnimation/Maps/Platform_Animations.Platform_Animations:PersistentLevel.Stander_Rapier_RunEnd_349.SkeletalMeshComponent0' (0x00000296CE6A6400) Class 'SkeletalMeshComponent'>`。

## 批量制作语义约束

`Skill2_Slow`、`Skill3_Stun`、`Skill5_Pull`是素材原名，当前没有对应控制事件实现；游戏显示使用武器“招式2/3/5”，不宣传减速、眩晕或牵引。Guard拼接起手/循环/收手；空中动作使用源Jump、主攻击、存在的Air后续及Ground/Fall阶段，事件时间包含段偏移。MoveHeight=100事件计划让胶囊实际抛物线腾空并在落地段前回落，须运行验证，不以动画拼接代替物理验证。

这些阶段目前是可执行脚本中的制作方案，完整组合仍需实际姿态/手感验收；双持候选多段取不同手轨迹峰，不能宣称源素材已提供明确伤害通知。

## 资产制作与独立读回（2026-11-04）

本轮已保存11组191个GA/ActionConfig及对应动作、每组移动AnimBP、WeaponSet，DT_Equipment按Name保留其它行合并11种训练武器。共享Stander技能目录保留原六项并追加191项。独立readback.json确认11/191配置、蒙太奇事件范围、GA类引用、图标及装备行。训练武器使用专属Infinite EquipmentGameplayEffect固定Armor+1。

已修复的制作问题：AnimSegment只读属性通过struct import设置；多段蒙太奇由原生rebuild_action_montage重算长度；独立Config/Tags文件使用GameplayTagList=而非+GameplayTagList=，修复空标签后再次独立读回通过。Projectile半径Bow12/Pistol8/Magic及Staff24，避免将近战180/240范围错误用于弹体Sweep。

展示地图L_StanderWeapons已保存11拾取，坐标与照明专用修复脚本待下一次执行。8方向远程环绕LaunchYaw补丁已准备、需新版反射加载后执行。上述属于资产验证，不代表191技能运行期、换装事务或用户视觉验收通过。

最新补丁已执行：Bow/Pistol/Magic/Staff四个环绕技能各8方向投射，LaunchYaw为0/45/90/135/180/225/270/315度、每弹原伤害的1/4；showroom-readback.json确认11拾取网格坐标和物品定义，添加定向光后保存且dirty为空；11组191技能再次独立读回通过。尚未执行本阶段PIE。

目录图标补丁已执行：191技能按动作语义选用现有纹理，每组默认六槽图标读回均不重复，装备物品保留family图标。展示拾取PrimaryAssetId已按BackpackItem:名称格式修复，11个非空定义逐项读回通过；此前空身份报告已由最新showroom-readback.json替代。
