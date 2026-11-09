# 原型、数值与生命周期

当前制作路线以工程 `docs/ga-unified-authoring-guide.md`、`docs/ga-framework-reference.md` 和实际注册表为准。以下旧技能合同用于兼容维护；新增行为放入配置/注册策略，专属 GA 仅保留输入、费用和动画前端。相同动作必须由原前端与节点调用同一共享实现，不能复制本文件描述的旧循环来新增另一套执行器。

## 瞬发范围（ArcaneNova 路径）

阅读 MyArcaneNova 与 MyDamageGameplayAbility。纯服务端瞬发技能可使用 InstancedPerActor + ServerOnly；不要把这个策略套到需要本地选点/预测的技能。

处理顺序：验证 ActorInfo/Avatar/ASC 与必要配置 → 父类 CanActivateAbility 检查资源/标签 → 服务端一次 CommitAbility → 获取目标 → 每个目标一次项目伤害通路 → 一次 Cue → EndAbility。失败也结束。Cost/Cooldown 是否必填由需求决定；免费或无冷却技能不要照搬 Nova 的强制非空检查。

`GetLivePlayersWithinRadius` 是碰撞候选查询；它不是完整的敌人过滤器。按需求再检查自身、CombatInterface、存活、ASC、`IsNotFriend`、距离/视线，AddUnique 去重。明确球形三维/平面距离、中心距离/碰撞体范围、边界和穿墙规则。Nova 的规则是三维中心距离、允许穿墙、允许空放。

伤害采用 `MakeDamageEffectParamsFromClassDefaults(Target)` → 覆盖本次 BaseDamage/行为 → `UMyAbilitySystemLibrary::ApplyDamageEffect`。范围筛选后逐目标全额伤害时 `bIsRadialDamage=false`，不要意外叠加径向衰减。基类默认 DebuffChance 不是零；不需要 Debuff/击退时明确设为零。基类 CauseDamage 路径较简略，需要完整伤害上下文的技能优先使用 Params 通路。

Nova 的 `GetNovaDamage` 将 Damage 曲线和 BonusDamagePerLevel 相加；继承新技能时选择一种明确成长规则，避免双重增长。描述硬编码 ARCANE NOVA/arcane damage，派生蓝图不会自动生成新文案；必要时新增原生描述覆盖或合适的数据驱动描述。

## 投射物/鼠标选点

参阅 MyProjectileSpell、MyFireBolt、Actor/MyProjectile，以及真实 GA_FireBolt 执行流。C++ `ActivateAbility` 调 Super 可能是为了派发蓝图逻辑；不要误以为 SpawnProjectile 辅助方法已构成完整技能。

- 在正确的本地控制端获取 TargetData，经 `UTargetDataUnderMouse` 传递。服务端按技能规则验证有限数值、射程、有效目标/视线，而非照单接受客户端命中。
- 接收端绑定 AbilityTargetDataSetDelegate，并用 CallReplicatedTargetDataDelegatesIfSet 处理“数据先到”，随后 ConsumeClientReplicatedTargetData。无目标/取消时结束等待。
- Commit 放在设计的施法时间点，只支付一次。蒙太奇/Notify 对应发射事件，失败/中断也结束。
- 服务端 SpawnActorDeferred，设置 DamageEffectParams 再 FinishSpawning；实际命中由 MyProjectile 的权威分支结算。预测表现和权威伤害分开核对，避免双生成/双伤害。
- 检查 CombatSocketTag、网格 Socket、蒙太奇、ProjectileClass、碰撞和生命周期，不能只有 GA 类能编译。

## 近战

参阅 MyMeleeAttack、GA_MeleeAttack 的图和 CombatInterface 的 FTaggedMontage。按照动画事件开启/关闭命中窗口，用本次挥击的目标集合去重；连续技重置集合的时机由设计决定。范围/扇形/轨迹与武器 Socket 匹配。中断、死亡或取消要关闭碰撞/Trace 并清理任务，实际伤害仍走权威 GE。

## 光束、引导、持续伤害

参阅 MyBeamSpell 与 GA_Electrocute。明确首次目标、追加目标数量/距离、周期、单次或持续支付、松键/丢失目标/死亡/资源不足的终止方式。限制计时器仅在有权威的执行端结算。

结束时终止 AbilityTask/计时器、解除主/副目标死亡委托、移除持续 Cue 与临时效果。MyBeamSpell::OnEndAbility 展示了委托移除；必须保证所有退出路径调用相同清理。用重复激活和取消测试发现遗留委托。

## 被动、Buff、光环

参阅 MyPassiveAbility、ASC 的 ServerEquipAbility/IsPassiveAbility/AddCharacterAbilitiesFromSaveData，以及 PassiveSpell 下已存在蓝图。MyPassiveAbility 只提供停用委托框架，并未实现任意增益。

- 菜单可装备被动的目录类型为 Passive，使用 `InputTag.Passive.*` 槽；装备激活。当前 ServerEquipAbility 在替换已占用槽时广播 DeactivatePassiveAbility，但没有通用卸下 RPC；ClearSlot 仅移除槽标签，不能当作完整卸下。
- 用户要求可卸下或支持存档时，核对并补齐服务器状态转换：停用旧能力/表现、移除对应 GE、清槽、将旧 Spec 的 Equipped 改为合适的 Unlocked、MarkAbilitySpecDirty 并通知 UI。当前替换逻辑可能留下旧 Equipped；读档又按 Equipped 激活，会使旧被动复活。针对替换后存档/读档及显式卸下分别测试，不把追加目录行当成这些流程已经存在。
- 持续运行直到停用；保存施加的 GE handle 和目标集合，在离开光环/卸下/取消/死亡/销毁时按设计移除。重新进入与重复装备不叠加意外副本。
- 派生实现检查并成对解绑 DeactivatePassiveAbility 委托；父类样例中的 AddUObject 不能代替清理设计。
- 初始后台监听能力可走 startup_passive_ability，但该路径没有主动技能的 StartupInputTag 拷贝逻辑。若要求默认装备到被动槽，需要明确相应 Spec 槽位接入，不声称追加数组即完成菜单装备。
- 当前旧档已 Equipped 被动用 GiveAbilityAndActivateOnce 恢复，但本机 UE 5.8 此调用设置 RemoveAfterActivation=true，能力结束会移除 Spec。可反复装备的已学被动应在其存档路径持久 GiveAbility，再 TryActivateAbility；只在明确一次性授予时沿用 ActivateOnce。验证 `load → unequip/replace → reequip → save/load`，同时断言 Spec 仍存在及效果只一份。若结束回调可能移除能力，用 SpecHandle 重新定位，不继续使用失效 Spec 指针。
- 表现由 MulticastActivatePassiveEffect/PassiveNiagaraComponent 联动；恢复/取消同步需要与实际激活结果一致，不仅广播“已激活”。
- 纯持续 Buff 可以没有 Cost、Cooldown、伤害 GE；不能套用 Nova 的瞬时结束与强制配置。

## 召唤

参阅 SummonAbility、敌人 GA_SummonAbility。服务端选择可生成位置并 Spawn，核查地面/遮挡/上限、阵营与拥有者、AI/能力初始化，确定生命周期与计数回收。不默认接入玩家法术菜单。

## 仅修改参数

从目标 GA 读真实 Cost/Cooldown/Damage/曲线引用，不按名字猜 GE。查询引用者；专用资产可原位改，共享 GE/曲线则仅隔离目标需求涉及的引用或修改明确指定行。修改固定耗蓝时确认 Mana + Additive + 负值，清除该 magnitude 的曲线表/RowName；不要清空整张共享表。保存读回并验证实际扣蓝，确认文案采用同一数据源。
