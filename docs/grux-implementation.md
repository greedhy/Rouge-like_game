# Grux Boss 制作记录

2026-10-31，遵循怪物开发规范和 StoneGolem 制作路径。传统 Actor/GAS，资源目录 `/Game/BluePrints/Characters/Grux/Expansion`。已接入现有游戏 BossClass；自动化通过，用户PIE手感与视觉验收待完成。

## 模型与动画

原始 Paragon Grux 模型自带双斧，不再增加重复武器组件。胶囊半径55、高度半值130，模型相对位置Z=-130、旋转Yaw=-90。专属 `ABP_Grux_Enemy` 复用已验证的速度状态机与DefaultSlot，替换为Grux骨骼及独立移动混合空间；源玩家AnimBP存在玩家依赖及普通Character下除零，未复用其事件图。

独立AS/AM：Left、Right、Charge、Uppercut、Roar。源资产未修改，旧Notify清除，Root Motion和ForceRootLock关闭。下表接触时间是蒙太奇位置，不含动画延迟；所有伤害由服务端GA读取实际蒙太奇位置执行，不由Notify重复结算。

| GA / 阶段 | 动作 | 动画延迟 | 锁定 | 接触位置 | 阶段结束 | 作用与提示 |
|---|---|---:|---:|---:|---:|---|
| Combo 第一斧 | PrimaryAttack_LA | 0.35 | 0.25 | 0.25 | 1.50 | 半径240、110°扇区，感叹号+红色范围 |
| Combo 第二斧 | PrimaryAttack_RA | 0.30 | 0.20 | 0.25 | 1.50 | 新预警和新锁定，形状相同 |
| Charge 冲锋 | Stampede | 0.60 | 0.40 | 0.05 | 1.35 | 最长600、宽240路径，静态墙和房间边界裁短，0.50秒扫掠位移 |
| Charge 挑飞 | Stampede_Knockup | 0.30 | 0.20 | 0.30 | 1.90 | 新预警，半径200、90°扇区，水平200/向上350击退 |
| Roar | Ultimate_Roar | 0.40 | 0.25 | 0.30 | 1.85 | 感叹号；无伤害；自身护甲+25，持续6秒 |

第二阶段时间均相对第二阶段开始。默认伤害45，倍率依次0.8、1.0、0.5、1.1；冷却Combo4.5、Charge10、Roar12秒。每次GA只Commit一次，连击阶段不会再次扣费或挂冷却。配置在 `DA_Grux_Combo`、`DA_Grux_Charge`、`DA_Grux_Roar`；总配置 `DA_GruxBoss` 控制移动速度320、基础伤害、模型、AI和技能授予。

## 粒子与音效表现蓝图

`BP_GruxBoss` 的 `Combat Presentation Class` 引用 `Expansion/Presentation/BPC_GruxCombatPresentation`。打开该蓝图的 Class Defaults 可调整 `Presets` 中的 Cascade 粒子、SoundCue、挂接、偏移、缩放和音量。其他角色可创建同一原生组件的蓝图子类，以不同预设调用 `Play Presentation` 和 `Stop Presentation`。

| 预设 | 粒子 | 音效及位置 |
|---|---|---|
| AxeImpact | `P_Grux_Melee_SucessfulImpact` | 普攻用力音。两斧各按武器落点播放一次。 |
| Charge | `P_Stampede_Intro` | 冲锋用力音。冲锋危险区进入生效阶段时挂到 Grux 模型。 |
| Uppercut | `P_Grux_Knockup` | 挑飞用力音。跟随对应段的实际作用位置。 |
| Roar | `P_GruxUlt_Roar` | 大招用力音。怒吼生效时播放一次。 |
| RoarArmor | `P_ExecuteBuff_Loop` | 挂在模型上的循环粒子，持续期间不重复播放怒吼音。 |

攻击粒子和声音由复制的攻击阶段触发，并按攻击序号去重。攻击结束、切段、超时、取消或回收时清理该槽。怒吼护甲拥有独立槽，GE 成功添加后才启动表现，正常的怒吼收招不会提前关闭它。GE 到期、取消、死亡、回收及 Actor 结束时移除对应表现。原 Niagara 字段保留为预设缺失时的回退。

## 生命周期和集成策略

沿用 `TelegraphedEnemyCharacter`、ASC、技能选择器、阵营/房间过滤、伤害GE和对象池。Grux默认作为非池化Boss，仍测试显式入池后的重复生命。死亡保留现有布娃娃/回收合同，不接入源示例角色的死亡销毁脚本。

冲锋危险区与目标查询共用Rectangle形状；预警锁定后不改变方向，服务端每次移动扫掠，沿途同一目标至多一次冲锋伤害。被阻挡会停止移动；挑飞仍需新预警。攻击阶段提前中断则取消后续阶段；生效后的怒吼被取消会移除自身拥有的护甲句柄。自然结束保留护甲直至到期，死亡/回收立即移除，旧句柄不能移除新生命的护甲。

`DungeonRunDirector::UpdateBoss` 对Grux跳过旧脉冲和半血召唤，避免追加不在这三个GA中的攻击；保留Boss死亡后的可选目标清理及遭遇结算。其他Boss继续旧行为。

## 已有证据与待验收

- `GruxDesign/runtime-animation.json`：4种实际攻击蒙太奇输出采样；独立AnimBP工作。
- `GruxDesign/combat-runtime.json`：Combo两次伤害与两次预警、Charge实际位移及冲锋/挑飞两次结算、Roar实际+25护甲并到期，3项通过。
- `GruxDesign/NativeDelegates/index.json`：19项原生回归，0失败（包括既有怪物/池）；部分无模型夹具产生警告。
- `GruxDesign/lifecycle-summary.json`：12项通过，含前摇/第二段/命中后取消、怒吼取消、晚期回收、死亡、墙前停止和路径外安全。原始报告保留了夹具修正前证据：自然混出后的Montage_Stop在UE中不操作非活动实例，不能把这个空操作当作真实取消；晚期取消通过Sleep/Wake验证。
- `GruxDesign/ai-runtime.json`：动态控制器/行为树、真实导航移动和脚部姿态变化通过，速度320。
- `GruxDesign/room-runtime.json`：正式Boss配置下10项通过，行为树自主执行了Left/Right/Charge/Uppercut/Roar；血条创建；无旧脉冲/半血召唤；死亡后可选目标清理，Boss房间Cleared，运行进入Victory。
- `GruxDesign/integration.json`：Boss入口备份及读回。保留1500血量、原有Boss数量、普通名单和池容量12，Boss等级3。
- 首版截图00012～00015用于复核站立、冲锋矩形、挑飞及临时怒吼表现。此前画面发现Rotator位置参数导致Pitch=-90，已改为显式Yaw=-90。感叹号偏移调整为110，血条145。源资源未改。
- 原包表现接入验证保存在 `Saved/GAAuthoring/GruxPresentation/`。独立进程读回 Boss 与五项预设，单机 PIE 实际激活连斧、冲锋挑飞和怒吼，观察到对应 Cascade 和音效组件。怒吼收招后护甲粒子仍在，到期后清除。原生回归覆盖攻击结束清理、外部移除 GE、回收和旧句柄隔离。
- 双人监听服务器 PIE 验证了远端怒吼预警、爆发表现与护甲并存、收招后护甲继续、自然到期、带状态回收以及下一条命的干净状态。结果见 `GruxPresentation/network.json`。Grux 四项和共用石像怪五项原生用例通过。
- 运行中修复：冲锋受阻后动画中断仍进挑飞，改用带阶段/租约保护的蒙太奇中断委托；完整冲锋与自然混出重叠，缩短位移至0.50秒并增加配置校验。

## 用户PIE验收

在 `L_RoguelikeRun` 开始新局，进入Boss房间。连击两斧各有红色扇区和斧击表现；冲锋前显示矩形，开始移动时感叹号消失，持续危险区仍在，挑飞重新显示扇区。靠墙时检查实际危险区是否与冲锋路径一致。怒吼时听到短时声效并看到爆发粒子，单独的护甲粒子随角色持续到 GE 到期。死亡后检查音效和剩余护甲粒子是否移除，再确认随从清理和通关状态。

最终镜头可读性和音量仍需实际试玩验收。属性伤害受现有护甲和等级计算影响。
