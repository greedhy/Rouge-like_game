# Stander 近战展示基准

2026-11-01，用户选定先用Stander并按现有流程开发。首批采用此前推荐的剑盾动作组：三段普攻、防御反击、旋转斩、冲刺攻击。沿用传统Actor GAS，不做Manny重定向、不替换TinyHero；本轮展示角色独立入口，后续以此作为重定向对照。

## 行为合同

| GA | 输入 | 首版数值与行为 |
|---|---|---|
| StanderCombo | LMB（直接释放，无需Shift） | 三段25/30/40物理伤害，扇形180cm，100/110/120度，0耗蓝；每次激活一段，结束后0.9秒内继续连段 |
| StanderGuard | RMB | 0.8秒举盾期间复用格挡减半；被有效敌方伤害命中后自动反击一次，35物理伤害，180cm/110度；无命中不反击，冷却3秒，无无敌/方向弹反承诺 |
| StanderSpin | 1 | 55物理伤害，周围220cm，每目标一次，耗蓝15，冷却5秒 |
| StanderDash | 2 | 锁定鼠标方向，最大500cm冲刺，末端180cm/100度斩击45物理伤害，耗蓝12，冷却4秒；墙体、角色及房间边界阻挡；不是沿途反复伤害 |

Contact、蒙太奇、PlayRate、伤害/范围/费用/冷却、冲刺起止时刻与距离、特效均独立DA配置。数值为本轮默认，不代表用户已验收平衡。

普通攻击、旋转及反击制作原地动画副本；冲刺使用原地副本与服务器Sweep控制胶囊位移，按源动作推进阶段匹配时间，避免只移动模型或双重位移。所有攻击锁定开始方向，释放后可取消；倒地、眩晕、蒙太奇中断、换图清理Timer、Guard GE、伤害监听和移动限制。对多目标伤害回调检查重入有效性。沿用现有敌我、不同楼层、房间与墙体过滤。

## 接入与表现

StanderPlayer继承现有房间玩家路径/PlayerState ASC，不新建ASC。剑盾使用包内模型和显式挂点；独立AnimBP/BlendSpace及Slot。独立技能目录防止混入法师技能，独立“Stander动作试炼”单人入口明确RG_Entrance。旧存档职业与旧两种入口保持现有行为。

挥击表现与实际命中阶段一致，命中反馈只在实际命中后发出；优先复用现有NS_CombatImpact与适合的Free_Spells特效。不能用特效掩盖错误骨骼/武器朝向。每段真实姿态采样、挂点和视觉证据后确认Contact。

## 验收

原生：配置校验、阶段推进、防御回调筛选、位移边界、入口出生与旧路径回归。运行：四种GA、三段各一次命中、真实防御反击、无敌方伤害不触发、资源不足/冷却、中断/倒地/复活、冲刺碰墙/边界、移动动画、真实房间入口及清场。最终用户PIE验收，远端多人、打包、全428动作展示和Manny重定向留后续。

## 动画校准（2026-11-01）

源目录 `/Game/PlatformAnimation/Animation/01_Sword_Shield`。所有副本保留原源文件，关闭RootMotion并ForceRootLock=Zero。剑盾挂接从源示例地图的Weapon_Sword_14 / Weapon_Shield_17读取：剑ik_hand_r，偏移(-10,6,0)，四元数(0,-0.130526,0,0.991445)；盾lowerarm_l，偏移(25,-2,2)，四元数(0.929957,0.111284,-0.051892,0.346560)。不能把仅有挂点名当作完整武器配置。

| GA阶段 | 源动作 | 长度 | 命中/阶段（动画秒） |
|---|---|---|---|
| 连击1 | Attack1 | 1.00 | 0.1833，剑刃向前扫过 |
| 连击2 | Attack2 | 1.00 | 0.2000，反向挥砍 |
| 连击3 | Attack3 | 1.1333 | 0.2500，下砸经过目标高度 |
| 举盾 | Block_Loop | 1.60 | 以2倍速播放到0.8秒；真实有效受击才反击 |
| 反击 | Attack1独立蒙太奇CounterStrike | 1.00 | 0.1833；源Block_Parry是抬盾卸力，未拿来冒充攻击 |
| 回旋 | Skill7_Around | 1.3333 | 0.3333，范围一次结算 |
| 突进 | Skill6_Move | 1.3333 | 0.3333–0.5000位移；0.5167斩击 |

源突进在0.33秒前有后撤蓄力；首版原地化这段后撤，保留身体姿态，0.33秒后才推进胶囊。500cm位移按阶段线性推进，最多20cm一段Sweep；静态地面、墙、角色碰撞和房间索引限制。反击、连击和突进均是前方扇形判定，并非逐三角形武器碰撞；回旋斩是一次范围冲击，不按剑刃逐个扫到的时刻重复伤害。

首版命中特效复用`/Game/Assets/Effects/Combat/NS_CombatImpact`，只在实际目标命中后生成。未随意附加持续光环或爆炸；后续可在配置里替换命中Niagara。所有调试数据在`/Game/BluePrints/Characters/Stander/DA_Stander`，角色、GA、动画、GameMode均在同一Stander目录。

## 试玩入口

从LoadMenu创建或选择存档后，点击“Stander动作试炼”。进入L_RoguelikeRun，显式使用RG_Entrance，规避旧存档Dungeon1PlayerStart1影响。左键直接连击（无需Shift）、右键格挡反击、1回旋、2突进。展示入口使用独立技能目录；本轮不宣称职业选择、装备换装、联机与打包已经验收。

## 本轮交付验证

- Editor Development完整构建成功。`Saved/GAAuthoring/Stander/NativeVerified/index.json`：14项通过、0失败（含2项带警告成功），覆盖配置、有效格挡伤害识别、阶段比例、旧剑盾伤害/Guard及新旧入口。
- `combat-runtime.json`：三段各一次命中、友方/范围外排除、空挡不反击、回旋耗蓝15；`special-runtime.json`：真实敌方伤害触发一次反击、突进约500cm/耗蓝12、墙前停止、地面边缘停止、打断不补伤害。
- `lifecycle-runtime.json`：背后/墙遮挡、攻击中断、资源不足、格挡中断、倒地清理与同ASC复活后再激活，6例通过。
- `cooldown-runtime.json`：Guard/Spin/Dash动画结束后仍受冷却限制，冷却后可再次释放。`movement-runtime.json`：实际移动速度和足部姿态变化通过。
- `room-runtime.json`：真实RoomSubsystem跳转到L_RoguelikeRun，Stander Pawn/GameMode正确、Overlay与DungeonRunWidget可见。`room-combat-runtime.json`：两次真实连击处理10个低血量测试敌人，房间转Cleared/Exploring；仅测试期间降低血量与暂停AI，不代表平衡性测试。
- `room-boundary-runtime.json`：本次PIE临时高架地面隔离墙碰撞，位移约66cm后停在当前房间索引内。测试地面随PIE结束丢弃，不保存地图。
- `poses.json`与pose-1..4.png：实际GA蒙太奇、接触时刻截图与武器位置；原始挂接证据demo-attachments.json；资产读回readback.json。截图与采样不能代替用户对动作手感、打击视觉的验收。
- 独立只读审查：未发现有证据的Critical/Important问题，无延期Minor。`restored-editor.json`确认LoadMenu、无PIE、无未保存资产。

待用户PIE验收：输入手感、三段衔接、盾防反击可读性、突进与碰撞、打击范围/特效。未覆盖Manny重定向、远端联机、打包与全部428动作。首版范围与服务器受控位移是明确实现选择，替换为其他带RootMotion动作时必须重新校准阶段和命中。
