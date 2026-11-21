# 血液表现资源目录与实施方案

> 审计修订于 2026-11-19。用户选择先实现视觉效果，持续流血保留可配置入口。
> 本节是当前实施方案。下方原始美术交付记录保留作资源来源与参数快照。

## 审计结果与接入修订

| 原方案条目 | 核查结果与当前做法 |
|---|---|
| 改 BloodEffect 即覆盖玩家近战 | 玩家武器共享动作是 Combat.CharacterStrike。它在逐目标实际扣血后触发表现，旧 Melee.Contact 也接同一表现服务。 |
| 命中与死亡血斑放胸口 | 交付系统为平面血斑。保留 NS_BloodImpact 的空中血花，血包资产通过静态地面投射显示。 |
| 组件 SetLifeSpan | UActorComponent 没有 Actor 的 SetLifeSpan。世界血迹管理器持有独立 Niagara 组件并按到期时间销毁。 |
| Pitch=-90 贴地 | 原始记录称局部法线为 Y。旋转由局部 Y 轴映射到地面法线，支持坡面并以 PIE 核验。 |
| 血泊跟随尸体 | 血迹按世界空间独立生成，尸体隐藏或复用后保留至到期。 |
| 本地 OnDeath 绑定覆盖联机 | Actor 死亡表现通过复制事件和池租约去重，覆盖普通死亡与池化死亡。Mass 使用原 Crowd 表现通道。 |
| 持续流血附着循环面片 | 由服务器开关复制流血视觉状态，每秒在当前脚下独立生成短血迹，移动不拖动历史血斑。 |
| AssetTags 开关状态特效 | 未来 GE 接入应使用 Granted/Owned Tags。当前提供 SetBleeding 视觉接口，伤害规则按现有链路执行。 |
| 乘性染色保证任意换色 | 已回读 Color A/B，最终颜色须做游戏视口检验，不能只凭写入回执宣称效果正确。 |
| 地面面片的实机可读性 | PIE 中原半透明面片过淡。新增 M_BloodGroundDecal，复用原包 T_VFX_Splat_Radial_01 的 G 遮罩，直接设置红/绿底色。贴花与 Niagara 共用预算、期限和清理入口，不修改源包母材质。 |
| 血泊数与寿命 | 普通 20 秒、精英/Boss 40 秒，管理器最多保留 20 个血泊和 64 个短特效。粒子本身寿命同步限制。 |

## 本轮实现与验收

Actor 的 BloodFX 组件读取美术配置并承担生命去重和复制的滴血状态。
本地世界管理器负责贴地、数量限制和组件清理。
命中依据共享动作的 ActualDamage > 0；同一攻击对同一目标只触发一次。
死亡只由原死亡入口触发。池租约变化时清除旧生命视觉状态。
原死亡冲量、尸体阶段、溶解及回收接口继续承担角色生命周期。

验收覆盖正常命中、零伤害、连续命中、致死、双生命池复用、贴地法线、血泊保留与到期清理。
PIE 确认红/绿可读性、真实玩家近战、Mass 死亡及新生命没有残余滴血。
联机覆盖范围以本次验证记录为准。

### 使用与配置

- Actor：角色蓝图的 `BloodFX` 组件 → `Style`，配置开关、红/绿血、尺寸及 Niagara 引用。现有 Enemy 默认绿血，Player 默认红血。
- PureMass：MonsterType 资产的 `BloodFX` 配置独立于 Actor。当前默认红血，可按怪物类型改为绿血。
- 持续流血：服务器调用 `BloodFX.SetBleeding(true/false)`。每秒采样一次，只生成视觉，不扣血；死亡、池睡眠和新生命会清除状态。默认关闭。
- 地面贴花：`/Game/Art/BloodFX/Materials/M_BloodGroundDecal`，`BloodOpacity` 控制浓度，运行时 `BloodTint` 根据血色配置设置。贴花与地面 Niagara 组成一份管理记录。
- 重击资产保留在 Style 的 `HeavyStain` 和底层 ShowHit 的 bHeavy 入口。本轮没有新增暴击判定或修改原技能规则。
- 命中位置采用目标位置和胶囊半径估算受击侧，未新增逐骨骼武器碰撞采样。

### 本轮验证记录（2026-11-19）

- 完整 `test_25dEditor Win64 Development` 构建成功。
- 自动化回归 24 项全部通过，其中新增 BloodFX 6 项，覆盖真实共享近战、零伤害门槛、攻击去重、池生命隔离、复制状态先于唤醒、Mass 合批和玩家死亡停止流血。
- 单人 PIE 使用正式共享 `Combat.CharacterStrike`：0 伤害保持 100 HP 且 hit=0；10 伤害变为 90 HP 且 hit=1；致死后 health=0、death=1、bleeding=0。探针调用共享动作，未模拟鼠标输入与完整蒙太奇前端。
- 同一临时敌人池复用后 life=2，下一次命中正常扣血并生成血花，第二次死亡计数变为 2，滴血没有恢复。旧生命的绿色血迹仍留在原地，第二次红色血迹可同时观察。
- 运行场景中的 Actor 与 Mass 死亡均进入世界血迹管理。25 次血泊请求只保留 20 份，到期后 pools=0。普通 20 秒已实机检查，精英/Boss 40 秒为配置路径，未单独等待到期取证。
- 游戏视口已检查空中绿血、地面红/绿血、死亡后保留与到期清除。证据在 `Saved/BloodFXQA/`，日志为 `automation-final.log`、`build.log`、`pie-bloodfx.log`。
- 独立代码审查发现的 Mass 重复命中、RPC 合批、复制/唤醒顺序和玩家死亡状态清理问题已修正，并纳入测试。
- 未完成的验收范围：独立远端客户端画面、丢包/迟加入场景与用户本人手动 PIE 美术验收。复制入口和生命隔离已有实现与自动化验证，不将这些等同于联机画面验收。

---

## 原始美术交付记录

以下条目记录艺术资产交付当时的方案与参数，其实现建议按上方审计修订执行。
# 近战流血 + 敌人死亡血液特效 资源目录（Blood FX）+ 接入说明

> 生成方式：Unreal Editor MCP（SOMOLMCP 3.13.5，UE 5.8.1）实测复制、改参、编译、保存。
> 生成时间：2026-11-19
> 适用对象：负责 GA / GameplayCue / 蓝图实现的**工作 Agent**。
> **本次交付边界：只有艺术资产（Niagara 系统）。没有改任何 C++、GA、GE、GameplayCue、蓝图。**
> 本文档的作用是告诉接入方「有哪些资产、参数是什么、在哪里挂、怎么挂」。

---

## 0. TL;DR（接入方只看这 8 行就够开干）

1. **8 个 Niagara 系统**全部在 `/Game/Art/BloodFX/Systems/`，全部 `ready_to_run = true`、参数默认值已按用途预设、已编译并保存到磁盘。
2. **近战命中流血 = 零代码**：`GC_MeleeImpact` 已经内置「取受击者 `GetBloodEffect()` → `SpawnSystemAtLocation`」。只要把 `BP_EnemyBase` 的 `BloodEffect` 指向 `NS_Blood_Hit_Splash_Green`、`BP_Player` 指向 `NS_Blood_Hit_Splash`，立刻生效；`MyMeleeAttack.cpp:111` 无需改动。
3. **死亡效果**：`BaseCharacter::MulticastHandleDeath` 目前**一个粒子都没有**（只有音效 + 武器脱手 + 布娃娃 + Dissolve）。要么在敌人 BP 里订阅 `OnDeath` / `OnDeathDelegate` 自己 spawn（零 C++），要么后续加 C++ 字段（本包不做）。
4. **死亡推荐组合**：`NS_Blood_Death_Burst(_Green)` 打在角色中心/骨盆 + `NS_Blood_Pool_Large` 打在脚下（**必须独立 spawn、自设 lifespan**，不能挂在会被对象池回收的尸体上）。
5. **持续流血 DoT**：`NS_Blood_Drip_Loop` 挂角色脚部，用现成的 `UDebuffNiagaraComponent`（照抄 `BurnDebuffComponent` 的写法，`DebuffTag` 换成流血标签）。
6. 颜色是**乘性染色**：`Color A / Color B` 默认白色 = 保持原始血红色；绿色变体已把两个参数改成 `(0, 1, 0.46)`。
7. **本包只有「贴面血斑」形态，没有空中飞溅液滴**（见 §6.1）。不要指望它做出「血雾喷溅」。
8. 血包源资产（60 个系统）在本项目里引用次数为 0；本包的 8 个系统是从中复制并调参的**独立包**，删/改源包不会影响它们，但**改源材质会影响全部**（见 §6.3）。

---

## 1. 交付资产清单

统一根目录：`/Game/Art/BloodFX/Systems/`（共 8 个）

| # | 资产名 | 用途 | 来源（血包原资产） | Scale | Lifetime Multiply | Spawn Burst Min/Max | 染色 |
|---|---|---|---|---|---|---|---|
| 1 | `NS_Blood_Hit_Splash` | 普通命中血斑（红/玩家） | `Particles/Once/NS_Blood_Splat_Directional_01_Floor_Once` | 1.2 | 0.7 | 1 / 2 | 白（原始血红） |
| 2 | `NS_Blood_Hit_Splash_Green` | 普通命中血斑（绿血敌人） | 同上 | 1.2 | 0.7 | 1 / 2 | `(0,1,0.46)` |
| 3 | `NS_Blood_Hit_Heavy` | 重击 / 暴击命中 | `Particles/Once/NS_Blood_Splat_Directional_01_Floor_Extra_Once` | 1.5 | 1.0 | 2 / 3 | 白 |
| 4 | `NS_Blood_Death_Burst` | 死亡炸血（红） | `Particles/Once/NS_Blood_Splat_Directional_01_Floor_Bunch_Once` | 2.2 | 1.2 | 4 / 7 | 白 |
| 5 | `NS_Blood_Death_Burst_Green` | 死亡炸血（绿） | 同上 | 2.2 | 1.2 | 4 / 7 | `(0,1,0.46)` |
| 6 | `NS_Blood_Pool_Small` | 普通尸体血泊 | `Particles/Once/NS_Blood_Splat_01_Floor_Rot_Once` | 0.9 | 40 | 1 / 1 | 白 |
| 7 | `NS_Blood_Pool_Large` | 精英 / Boss 血泊 | `Particles/Once/NS_Blood_Splat_02_Floor_Rot_Once` | 1.8 | 40 | 1 / 2 | 白 |
| 8 | `NS_Blood_Drip_Loop` | 持续流血滴落（循环） | `Particles/NS_Blood_Splat_01_Floor_Rot`（循环版） | 0.6 | 0.8 | 1 / 1 | 白 |

所有系统结构一致：**1 个 emitter（名为 `Splat`）**、`sim_target = gpu`、`local_space = true`（粒子跟随组件移动）。

> `local_space = true` 的含义很重要：
> - 挂在**会动的角色**上（持续流血）→ 血斑跟着角色走，正是我们要的。
> - 打到**静止位置**（命中点、地面血泊）→ 必须用 `SpawnSystemAtLocation` 生成独立组件，不要 Attach 到角色，否则角色跑开/倒地翻滚时血泊会被拖走。

---

## 2. 用户参数速查

每个系统都暴露以下用户参数（不同源系统参数个数 22 / 28，常用的是这 6 个）：

| 参数名 | 类型 | 作用 | 本包默认值 | 运行时覆盖 API |
|---|---|---|---|---|
| `Scale` | float | 整体缩放（最常用） | 见 §1 表 | `SetFloatParameter("Scale", v)` |
| `Scale Min` / `Scale Max` | float | 单粒子尺寸随机区间 | 1 / 1 | `SetFloatParameter` |
| `Scale 2D Min` / `Scale 2D Max` | vector2 | 非等比拉伸（压扁地面血斑很好用） | (1,1) | `SetVector2DParameter` |
| `Lifetime Multiply` | float | **寿命倍数**，越大留存越久 | 0.7 ~ 40 | `SetFloatParameter` |
| `Spawn Burst Min` / `Max` | int | 一次喷几片血斑 | 1 ~ 7 | `SetIntParameter` |
| `Loop Duration` | float | 循环周期（仅 `NS_Blood_Drip_Loop` 有意义） | 1.0 | `SetFloatParameter` |
| `Color A` / `Color B` | linear color | **乘性染色**，白 = 原始血红 | 白 / 绿 | `SetVariableLinearColor("User.Color A", C)` |
| `Rotation Min` / `Max` / `Offset` | float | 血斑自转随机 | 0 | `SetFloatParameter` |
| `Dyn Random Offset Min/Max` | float | 贴图随机偏移，避免每片一模一样 | 0 / 69 | 一般不用动 |

**改颜色时注意参数前缀**：Niagara 组件上是 `User.Color A`（带 `User.`），User Parameter 面板里是 `Color A`。

---

## 3. 场景化接入配方

### 3.1 近战命中流血（零代码，改 BP 默认值即可）

现有链路（已存在，不要重写）：

```
GA_MeleeAttack (C++)  Source/test_25d/Private/AbilitySystem/Ability/MyMeleeAttack.cpp:111
    └─ SendGameplayEvent / GameplayCue: "GameplayCue.MeleeImpact"（参数: Location, EffectCauser, MontageTag）
        └─ Content/BluePrints/AbilitySystem/GameplayCueNotify/GC_MeleeImpact
              ├─ GetBloodEffect(受击者) → SpawnSystemAtLocation
              └─ PlaySoundAtLocation
BaseCharacter::GetBloodEffect_Implementation() → 返回 BP 上的 BloodEffect 字段（Source/.../BaseCharacter.cpp:85）
```

**接入动作（工作 Agent 只需做这一步）**：

| 蓝图 | 字段 | 现在指向 | 改成 |
|---|---|---|---|
| `Content/BluePrints/Characters/BP_EnemyBase` | `BloodEffect` | `/Game/Assets/Effects/Combat/NS_BloodImpact_green` | `/Game/Art/BloodFX/Systems/NS_Blood_Hit_Splash_Green` |
| `Content/BluePrints/Characters/Player/BP_Player` | `BloodEffect` | `/Game/Assets/Effects/Combat/NS_BloodImpact` | `/Game/Art/BloodFX/Systems/NS_Blood_Hit_Splash` |

旧的 `NS_BloodImpact*` 不要删，留着回滚对照。

**重击/暴击想用更重的效果**：在 `GC_MeleeImpact` 里按伤害或 `MontageTag` 分支，重击时改 spawn `NS_Blood_Hit_Heavy`（这是唯一需要动一点蓝图的地方，可选）。

**若不想改 BP 默认值**（想按怪物类型区分颜色）：在 `GC_MeleeImpact` 里 spawn 后立刻 `SetVariableLinearColor("User.Color A", TintColor)`，用同一个 `NS_Blood_Hit_Splash` 覆盖红/绿/紫多种血。

### 3.2 敌人死亡效果（当前无任何粒子，需新增一处 spawn）

现状：`ABaseCharacter::MulticastHandleDeath` 做的是——死亡音效 → 武器脱手 → 布娃娃 → `Dissolve()`（`MI_*_Dissolve`）→ `OnDeath` 广播。**没有任何 Niagara**。
而且 `MulticastHandleDeath` 是 C++ 原生 virtual（不是 `BlueprintNativeEvent`），**蓝图不可覆写**。

两条路（本包只提供资产，不含实现）：

**方案 A（推荐，零 C++）——订阅 `OnDeath`**
1. 在 `BP_EnemyBase`（或具体怪物 BP）的 `BeginPlay` 里 `Bind Event to OnDeath`（`ICombatInterface::GetOnDeathDelegate()`）。
2. 回调里做两件事：
   - `SpawnSystemAtLocation(NS_Blood_Death_Burst_Green, 角色中心（胶囊中心或骨盆 socket）, 随机 Yaw)` —— 位置用 `GetActorLocation() + (0,0,CapsuleHalfHeight*0.6)`。
   - `SpawnSystemAtLocation(NS_Blood_Pool_Large, 脚下地面, 0)`，**并给返回的组件 `SetLifeSpan(60)`**。
3. 地面高度用 `LineTraceByChannel` 从角色中心往下打到 `ECC_WorldStatic`，取命中点 Z（避免斜坡/台阶上血泊悬空或埋进地板）。

**方案 B（要改 C++，本包不做，供后续排期）**
在 `BaseCharacter.h` 加 `UPROPERTY(EditDefaultsOnly) TObjectPtr<UNiagaraSystem> DeathVFX;` 与 `DeathBloodPool;`，在 `MulticastHandleDeath` 里 spawn。好处是 C++ 侧统一、怪物 BP 不用各写一份；坏处是要编译。

**必须注意（对象池）**：本项目的敌人走对象池（`DungeonPoolManaged` / `PoolCorpseSeconds`）。尸体被回收时挂在它身上的组件会被一起销毁 —— 所以**血泊必须是独立 spawn、自己带 lifespan 的组件**，绝不能 Attach 到尸体上。

### 3.3 持续流血 DoT（GE + Debuff 组件，照抄 Burn / Stun）

现成范式：`UDebuffNiagaraComponent`（`Source/.../AbilitySystem/Debuff/DebuffNiagaraComponent.h`），已由 `BurnDebuffComponent` / `StunDebuffComponent` 使用（`BaseCharacter.cpp:37-43`），特性是：

- `DebuffTag` 命中即 `Activate`，标签移除即 `Deactivate`；
- `OnOwnerDeath` / `EndPlay` 自动清理；
- 晚注册 ASC、客户端复制都处理过（这点自己写很容易漏）。

流血接入步骤：

1. 在 `BaseCharacter` 加一个组件（照抄 37-39 行）：
   ```cpp
   BleedDebuffComponent = CreateDefaultSubobject<UDebuffNiagaraComponent>("BleedDebuffComponent");
   BleedDebuffComponent->SetupAttachment(GetRootComponent());
   BleedDebuffComponent->DebuffTag = GameplayTags.Debuff_Bleed;   // 见下
   ```
2. 标签：`MyGameplayTags.h:130-133` 现有 `Debuff_Burn / Debuff_Stun / Debuff_Arcane / Debuff_Physical`，**没有 `Debuff_Bleed`**。
   要么复用 `Debuff_Physical`，要么新增 `Effects.Debuff.Bleed`（`MyGameplayTags.cpp` 里 `UE_DEFINE_GAMEPLAY_TAG_COMMENT`）。
3. `GE_Bleed`：`Duration 5s`、`Period 0.5s`、`Damage.Physical`（GE 里的 `AssetTags` 里加上流血标签，Debuff 组件靠它开关）。**GA 侧只需在命中时 `ApplyGameplayEffectSpecToTarget`，不用碰特效。**
4. 特效绑定：组件上 `SetAsset(NS_Blood_Drip_Loop)`，或用 BP 里的 `BleedDebuffComponent` 默认值直接指定。
5. 挂点：`SetupAttachment(GetRootComponent())` 会挂在胶囊根节点（脚下），正是滴血位置；若模型有 `foot_l/foot_r` socket 更精确，可 `SetupAttachment(GetMesh(), "foot_l")`，但注意布娃娃后 socket 位置会乱（流血应在死亡前结束，`OnOwnerDeath` 已经会 `Deactivate`）。

### 3.4 效果 → 资产 速查表

| 游戏事件 | 用哪个资产 | 生成位置 / 朝向 | 备注 |
|---|---|---|---|
| 普通近战命中 | `NS_Blood_Hit_Splash` / `_Green` | 命中点，朝向 = 命中面法线 | 走 `GC_MeleeImpact`，零代码 |
| 重击 / 暴击 / 处决 | `NS_Blood_Hit_Heavy` | 同上 | 可叠 1~2 次 |
| 远程/法术命中（若接线） | `NS_Blood_Hit_Splash` | 同上 | 与近战共用 |
| 敌人死亡（普通） | `NS_Blood_Death_Burst(_Green)` | 角色中心，随机 Yaw | 一次性 |
| 敌人死亡（精英/Boss） | `NS_Blood_Death_Burst(_Green)` ×1 + `NS_Blood_Pool_Large` | 中心 + 脚下地面 | 血泊 lifespan ≥ 60s |
| 尸体残留血泊 | `NS_Blood_Pool_Small` / `NS_Blood_Pool_Large` | 脚下地面射线命中点，Pitch 贴地 | 独立 spawn，不 Attach |
| 流血 DoT 持续表现 | `NS_Blood_Drip_Loop` | 角色脚下（组件挂 root） | 循环，跟随角色 |

---

## 4. 尺寸 / 朝向 / 数量约定（血包的面片是「立起来」的，必须注意）

1. **面片默认朝向不是贴地**。实测：把系统以 `Rotation = (0,0,0)` 放进场景时，血斑面片是**竖直立着的**（法线朝 ±Y），不是平铺在地面。
   - 地面血泊 / 地面血斑：`Rotation = (Pitch=-90, Yaw=随机, Roll=0)`（让其平躺在 XY 平面）。
   - 墙面 / 角色身上贴花：用命中面的法线构造旋转，`Rotation = (-ImpactNormal).Rotation()`（法线反方向作为面片正朝向）。
   - **接入方第一次放场景时请先目视确认一次朝向**（UE 里旋转 ±90 试一下即可），比反复调参数快。
2. **尺寸基准**：`Scale = 1` 时血斑直径约 1 个角色身宽（≈100 uu 量级）。本包的 `Scale` 已按用途给好；同一效果挂在不同体型怪物上时，建议按 `胶囊半径 / 42` 做一个简单缩放系数，不要一个个手调资产。
3. **同时存在数量上限**（性能 + 视觉）：
   - 命中血斑：0.2s 内去重（同一目标同一帧多次命中只播 1 次），否则多段攻击会糊成一片；
   - 死亡炸血：每具尸体 1 次；
   - 血泊：同屏建议 ≤ 20 个（`NS_Blood_Pool_*` 的 `Lifetime Multiply = 40` ≈ 20s 量级留存，可据此估算）；
   - 流血 loop：每个角色 1 个（组件天然保证）。
4. **血泊寿命**：需要「永久留在关卡里」时不要靠 `Lifetime Multiply`（它只是拉长粒子寿命），而是让独立 Actor/组件 `SetLifeSpan(你想要的时间)`，超时销毁。若要做「血迹永久累积」的 roguelike 表现，建议单独做一个血迹贴花管理器（Decal + 池），不要用 Niagara 堆积。

---

## 5. 与现有系统的关系（哪些东西**不该动**）

| 现有资产 / 代码 | 关系 | 处理 |
|---|---|---|
| `/Game/Assets/Effects/Combat/NS_BloodImpact`、`NS_BloodImpact_green` | 旧命中血花，被 `BP_Player` / `BP_EnemyBase` 的 `BloodEffect` 引用 | 保留，仅替换指向 |
| `GC_MeleeImpact`（GameplayCueNotify） | 命中表现总线（特效 + 音效） | 尽量不改，改 BP 默认值即可 |
| `BaseCharacter::MulticastHandleDeath` | 死亡总线，目前无粒子 | 新增订阅或后续加字段，勿删 Dissolve 逻辑 |
| `MI_*_Dissolve`（尸体溶解材质） | 与血泊无关，尸体溶解时血泊**不应**跟着消失 | 血泊独立 spawn 即可自然解耦 |
| 血包源目录 `Content/resources/Vefects/VFX_Free_Blood/` | 60 个原始系统，**项目引用数 = 0** | 不要改；本包 8 个系统是复制出来的独立包 |
| `LVL_Free_Blood_Overview`（血包演示关卡） | 仅用于预览，不属于游戏内容 | 本次验证后已逐字节还原 |

---

## 6. 已知限制（接入前必读）

### 6.1 只有「贴面血斑」，没有「空中飞溅液滴」
血包的 60 个系统全是 `..._Floor` / `Splat` 形态 —— 平铺血斑 + 侵蚀淡出，**没有带重力的飞行液滴发射器**。
所以：
- 命中 → 表现为「命中面上瞬间出现一片血斑」，**不是**喷溅雾；
- 死亡 → 表现为「尸体周围炸开多片血斑」，**不是**血柱/血雾。

要真正的飞溅液滴，需要自己搭 emitter（sprite + gravity + collision）或另找资源；本包不做。动感不足时用 `Spawn Burst Max`（多片）+ 随机 Yaw 补偿，比调材质有效。

### 6.2 材质是折射型半透明 Lit 材质，编辑器视口颜色不可信
`MI_VFX_Fake_Blood_01_Lit` 的父材质 `M_VFX_Free_Blood_Lit_01` 带 `RefractionDepthBias`（折射型半透明），参数只有 `Normal Intensity` / `Erosion Intensity` / 法线贴图 / 侵蚀贴图，**没有颜色参数**。
后果：
- 在编辑器视口 / 暗场景里它偏白偏淡，看着「不像血」——**这是材质特性，不是资产坏了**；
- 最终颜色由主材质 + 粒子 `Color A/B` 共同决定，**请以 PIE 内实机为准**。

### 6.3 换色只有两条正路（MCP 侧有已知 bug）
- ✅ 改用户参数 `Color A/B`（乘性染色，本包绿色变体就是这么做的，已验证写入生效）；
- ✅ 复制材质实例（`MI_*`）改参数再指定给渲染器 —— 但 **`niagara_set_renderer_properties` 的 `OverrideMaterials` 在本项目 5.8 下「写入成功、渲染不生效」**（上一轮做元素球时已实测确认的坑），**不要在这条路上浪费时间**。
- ⚠️ 改共享 MI 会影响所有引用它的源系统（上一轮改法球的颜色就是这么污染源美术资产的），本包**没有**动任何共享 MI。

### 6.4 本轮未做 PIE 内视觉验收
已完成：复制 → 改参 → 编译 → 保存 → 参数回读 → 关卡内生成（`ready_to_run = true` 全通过；在演示关卡里生成成功、Actor 正常出现）。
**未完成**：PIE 内的最终视觉确认（原因见 §6.2：编辑器视口对该折射材质表现不稳定，暗场景下几乎看不出血）。
**请接入方按下面 3 步做一次验收**（约 2 分钟）：
1. PIE 用玩家攻击一只怪 → 命中处出现 1~2 片血斑（绿），0.5s 内淡出；
2. 击杀一只怪 → 尸体中心炸开 4~7 片血斑 + 脚下出现 1 片大血泊，血泊不随尸体回收消失（60s 后自动清）；
3. 给怪上流血 GE → 脚下每 1s 出现一次小血斑，GE 结束/怪物死亡后停止。

---

## 7. 生成与验收记录

- 引擎：`5.8.1-56057345+++UE5+Release-5.8`
- 工具链：`asset_duplicate_v2` → `niagara_set_user_parameter_default`（39 次，全部 succeeded）→ `niagara_compile_system`（`force=true`）→ `asset_save`
- 参数回读（`niagara_list_user_parameters`）与预设完全一致：

| 资产 | Scale | Lifetime Multiply | Burst | Loop Duration | Color A |
|---|---|---|---|---|---|
| NS_Blood_Hit_Splash | 1.2 | 0.7 | 1–2 | 2 | 1,1,1 |
| NS_Blood_Hit_Splash_Green | 1.2 | 0.7 | 1–2 | 2 | 0,1,0.46 |
| NS_Blood_Hit_Heavy | 1.5 | 1.0 | 2–3 | 4 | 1,1,1 |
| NS_Blood_Death_Burst | 2.2 | 1.2 | 4–7 | 2 | 1,1,1 |
| NS_Blood_Death_Burst_Green | 2.2 | 1.2 | 4–7 | 2 | 0,1,0.46 |
| NS_Blood_Pool_Small | 0.9 | 40 | 1–1 | 2 | 1,1,1 |
| NS_Blood_Pool_Large | 1.8 | 40 | 1–2 | 2 | 1,1,1 |
| NS_Blood_Drip_Loop | 0.6 | 0.8 | 1–1 | 1 | 1,1,1 |

- 编译诊断：8/8 `ready_to_run = true`，0 error。
- **关卡零污染**（重要，本项目对地图被改写很敏感）：
  - `Content/RoguelikeGeneration/Maps/L_RoguelikeRun.umap` md5 `e050712e56d83f34ec3b80afea9ae918`（= 基线，未被写）
  - `Content/resources/Vefects/VFX_Free_Blood/Maps/LVL_Free_Blood_Overview.umap` md5 `d21cc58ba5c768da94db3c6967ad6604`（= 备份基线，验证后逐字节还原）
  - 备份：`Saved/WBTmp/LVL_Free_Blood_Overview_backup_20261119_0228.umap`

---

## 8. 本次没有做的事（明确边界，避免重复劳动）

- ❌ 没有创建 `GE_Bleed` / `GA_*` / `GameplayCue`
- ❌ 没有改任何 C++（`BaseCharacter` 的 `DeathVFX` 字段等都要后续排期）
- ❌ 没有改任何蓝图（`BP_EnemyBase.BloodEffect` 等属接入方动作）
- ❌ 没有新增 GameplayTag `Effects.Debuff.Bleed`
- ❌ 没有做血迹永久累积 / Decal 管理系统
- ❌ 没有做空中飞溅液滴发射器（血包本身没有）

