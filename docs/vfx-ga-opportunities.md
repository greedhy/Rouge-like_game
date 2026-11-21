# 特效资产盘点 & GA 开发机会（2026-11-19）

> 数据来源：对 `Content/` 全量 6846 个 uasset/umap 做名字级引用扫描（6.9 GB，一次全库 grep，约 10s）。
> 统计对象：247 个粒子系统资产（`NS_` / `NE_` = Niagara，`PS_` / `P_` = Cascade）。
> 本文是给**工作 Agent** 的选型与改造清单，**没有修改任何资产**。

---

## 0. TL;DR

| 分桶 | 数量 | 含义 |
|---|---|---|
| **真在用** | **83** | 被 BP / GA / GC / DA / GE / 动画 Montage 引用 = 进了游戏逻辑 |
| **只在展示内容里** | **78** | 只被「资源包自带的 showcase 关卡 / 展示 BP / 包内 DataTable」引用 → 游戏里等于没用，但**资产是成体系的好东西** |
| **零引用** | **86** | 全库没有任何资产提到它 → 纯闲置 |
| 合计 | 247 | 约 **66% 的粒子资产没进游戏逻辑** |

三条最有价值的结论：

1. **同质化的根因找到了**：`NS_CombatImpact` 一个特效被 **110 个 DA（技能原型）**共用 —— 110 个技能打上去手感一样。改造它（改 DA 字段，不改逻辑）比新建 10 个 GA 收益大。
2. **47 个 Paragon Grux 技能级特效（AAA 品质，Cast/Impact/Trail/Loop 成套）零引用**，而 Grux 的三个 GA 现在用的是 `NS_CombatImpact` / `NS_GroundSummon` 这种通用件。
3. **但这些 Grux 特效是 Cascade**，项目的 `LoopVFX / ImpactVFX` 字段要 `NiagaraSystem` → **必须先决策转换方案**（§3），否则接不进去。

---

## 1. 盘点方法与口径（可复现）

```bash
find Content -name "*.uasset" | grep -Ei "/(NS_|NE_|PS_|P_)[A-Za-z0-9_]+\.uasset$"   # 候选
grep -raoHE "(NS|NE|PS|P)_[A-Za-z0-9_]+" --include=*.uasset --include=*.umap Content > tokens.txt
# 再按「文件 → 内含 token」反查，排除自引用，并按引用方前缀分类
```

分类规则（关键）：

| 引用方 | 记为 |
|---|---|
| `BP_` / `GA_` / `GC_` / `DA_` / `CT_` / `GE_` / `AM_`（动画 Montage） | **真在用** |
| `__ExternalActors__` + `resources/` `Free_Spells/Map/` `LootDropVFX/Maps/` `MayuOrbs/01_Level` 下的关卡 | **只在展示内容里**（资源包 showcase） |
| 无任何引用 | **零引用** |

已知偏差（不影响结论方向）：
- 只看名字表/硬引用，运行时用字符串拼出来的软引用不计（本项目少见）；
- 同包内部互相引用（如 Color_Variations 引用基版）会被算成「被引用」，所以分桶时剔除了同包互引。

明细：工作区 `docs/vfx-refs.json`（每个资产的 `ref_total` / `ref_gameplay` / 引用方路径）、`docs/vfx-buckets.json`（三桶名单）、`_tokens2.txt`（原始扫描）。

---

## 2. 现状

### 2.1 复用度 Top 12（谁在撑场面 = 谁在制造同质化）

| 引用数 | 资产 | 引用方 | 说明 |
|---|---|---|---|
| **110** | `NS_CombatImpact` | 110 × DA | **几乎所有攻击/技能原型的唯一命中特效** |
| 59 | `NS_SolarFireballFlame` | 59 × DA | 火焰系主力拖尾 |
| 18 | `NS_Fire` | BP 12 / VFX 5 / 关卡 6 | 环境火/篝火 |
| 17 | `NS_Halo` | DA 11 / BP 6 | 光环通用件 |
| 15 | `NS_ArcaneShards` | DA 13 / GC 2 | 奥术碎片 |
| 13 | `NS_FireExplosion` | DA 12 / BP 1 | 爆炸 |
| 12 | `NS_Fireball` | 12 × DA | 火球 |
| 11 | `NS_Stars_EnemyStun` | BP 9 / DA 2 | 眩晕星 |
| 7 | `NS_SolarExplosionFlame` | DA 6 / GE 1 | 爆炸火焰 |
| 7 | `P_ky_storm` | BP 7 | 雷电天气（Cascade） |
| 6 | `NS_BloodImpact` | BP 6 | 旧命中血花（已被 `/Game/Art/BloodFX` 替代，待切） |
| 6 | `NS_Frost_Impact` | DA 6 | 冰霜命中 |

> 读法：`NS_CombatImpact` 被 110 个 DA 引用 = **110 个技能手感一模一样**。
> 问题不是「资产不够」，是「资产没分流」。

### 2.2 只在展示内容里（78 个）—— 被低估的一批好资产

这些资产质量高、成体系、还带展示场景，但**游戏逻辑一次都没用过**：

| 组 | 数量 | 目录 | 资产 | 判定 |
|---|---|---|---|---|
| **血包 颜色变体** | 32 | `Vefects/.../Particles/Color_Variations` | `NS_Blood_Splat_*_GS_Pure_*_{Black,Blue,Green,Orange,Pink,Purple,White,Yellow}` | 8 色 × 2 形态；**换色其实只需改 `Color A/B`，这 32 个不必逐个接** |
| **血包 循环版** | 14 | `Vefects/.../Particles` | `NS_Blood_Splat_01_Floor(_Rot/_Extra/_Extra_02)`、`Directional_01_Floor(_Bunch/_Extra)` 等 | 已被 `/Game/Art/BloodFX` 8 个系统覆盖，不必再接 |
| **Free_Spells 光环/增益** | 14 | `Free_Spells/VFX_Niagara` | `Aura_{Fire,Healing,Lightning,Notes}`、`Buff_{Air,Fire,Healing,Lightning,Mana,Notes,Soul}`、`Circle`、`Explosion`、`LevelUp` | **最推荐启用**：常驻视觉，挂上就有效果，不需要新 GA 逻辑 |
| **掉落品质光柱** | 6 | `LootDropVFX/Systems/Rarity` | `NS_LootDrop_{Common,Uncommon,Rare,Epic,Legendary,Mythic}` | 6 档品质成套，掉落系统做「品质一眼可辨」现成方案 |
| **刀光** | 3 | `Knife_light/VFX` | `NE_attack02 / 04 / 05` | 三套刀光，可直接给近战换手感 |
| **法球本体** | 5 | `MayuOrbs/02_Orbs/Orb_{1..5}` | `NS_Orb_1` ~ `NS_Orb_5` | 已被 `Art/ElementOrbs` 5 个独立系统替代 |
| 其它 | 4 | — | `NE_Refract`（折射拖尾）、`NS_FireEffect`（火焰）、`NS_Embers`（余烬） | 拖尾件，可给冲刺/闪避用 |

> 注意 Free_Spells 里已有 4 个进入了玩法（`Aura_Air` / `Aura_Mana` / `Aura_Soul` / `Shockwave`），
> 说明这套资产**已经在用了，只是没铺开** —— 补齐剩下 14 个的边际成本很低。

### 2.3 零引用（86 个）—— 纯闲置

| 组 | 数量 | 目录 | 资产 | 可用性 |
|---|---|---|---|---|
| **Grux 岩浆皮肤** | 24 | `ParagonGrux/.../Skins/Grux_Beetle_Magma` | `P_Grux_Magma_{GroundInteraction,Dash_CameraLens,Melee_Impact,Regular_Trail,Spawn,Strong_Trail,Ultimate_Cast,Whirlwind_Impact,Removal…}`、`P_MagmaGruxRoar`、`P_MagmaHardknocks_AOE`、`P_MagmaSwingMesh` | **Cascade**，成套（施法/命中/拖尾/收招），做元素皮肤首选 |
| **Grux 大招/处决** | 7 | `.../Abilities/Ultimate/FX` | `P_ExecuteBuff_Loop`、`P_Execute_Roar_Shockwave`、`P_Execute_WeaponClang(V2)`、`P_Execute_WeaponEnd`、`P_Execute_WeaponLoop`、`P_MeleeTrails_Ultimate` | Cascade，处决流程完整 |
| **Grux 冲锋** | 6 | `.../Abilities/Stampede/FX` | `P_Stampede_{Cast,Intro,Intro_CameraFX,Intro_Flames,Step,Trail}` | Cascade，位移技能全套 |
| **Grux 普攻** | 5 | `.../Abilities/Primary/FX` | `P_Grux_ApplyBleed`、`P_Grux_Melee_ShockwaveImpact`、`P_Grux_Melee_SucessfulImpact`、`P_MeleeTrails_{Regular,Strong}` | Cascade，近战手感件 |
| **Grux 击飞** | 3 | `.../Abilities/HardKnocks/FX` | `P_Grux_Knockup`、`P_Hardknocks_AOE`、`P_SwingMesh` | Cascade，控制技能 |
| **Grux 大招 Rework** | 3 | `.../Ultimate/FX/Rework` | `P_Execute_WeaponStart`、`P_GruxUlt_GroundInteraction`、`P_GruxUlt_Roar` | 与主版本重复，二选一 |
| **Grux 涟漪重击** | 2 | `.../Abilities/RipplingSmash/FX` | `P_Grux_RipplingSmash_ImpactedTrails`、`P_RipplingSmash_Aligned_Initial` | Cascade，地面冲击波 |
| **血包 Once 版** | 14 | `Vefects/.../Once` | `NS_Blood_*_Once` | 已被 `/Game/Art/BloodFX` 替代，不必再接 |
| **血液新系统（待接）** | 6 | `/Game/Art/BloodFX/Systems` | `NS_Blood_Hit_Heavy` / `Death_Burst(_Green)` / `Drip_Loop` / `Pool_Small` / `Hit_Splash_Green` | 2026-11-19 新做，等接入（见 `blood-fx-catalog.md`） |
| **元素法球（待接）** | 5 | `/Game/Art/ElementOrbs/Systems` | `NS_Orb_{Frost,Lava,Mud,Toxic,Vines}` | 新做，等 GA 接 `LoopVFX/ImpactVFX` |
| **火球变体** | 4 | `Assets/Effects/Projectiles/FireBolt` | `NS_Fire_1` ~ `NS_Fire_4` | Niagara，4 个不同火球（现役是 Cascade `P_FireBolt`） |
| **灵魂拾取** | 3 | `Assets/Effects/Soul` | `NS_Glow`、`NS_Soul`、`NS_SoulPickup` | Niagara，拾取链路现成 |
| **太阳系** | 2 | `CompositionBatch6` | `NS_SolarExplosion`、`NS_SolarFireball` | Niagara，与已用的 `NS_SolarFireballFlame` 同源 |
| **掉落主入口** | 1 | `LootDropVFX/Systems` | `NS_LootDrop` | Niagara，**整个掉落包 7 个系统全都没进游戏** |
| **法力吸取** | 1 | `Assets/Effects/ManaDrain` | `NS_ManaDrain` | Niagara，敌人吸蓝 |

---

## 3. 先决策：Cascade 还是 Niagara（47 个 Grux 特效的准入门槛）

| 事实 | 影响 |
|---|---|
| `P_*` = Cascade（二进制特征确认：内含 `ParticleSystem`、无 `NiagaraSystem`） | `DA_AbilityInfo` / `AbilityPrototype` 的 `LoopVFX / ImpactVFX` 是 `NiagaraSystem*` → **塞不进去** |
| `NS_*` / `NE_*` = Niagara | 可直接接字段，也可 `SpawnSystemAtLocation` |

三条路，按成本排序：

1. **BP 包一层（1~2 小时/个）**
   `BP_FX_CascadeWrapper`：`SpawnActor` → 挂 `ParticleSystemComponent` → `SetTemplate(P_…)` → 播完 `Destroy`。
   GA / DA 里引用这个 BP。代价：多一层 Actor；参数要用 Cascade 的 `SetFloatParameter`，不能走 Niagara 的用户参数。
   适合一次性/命中类（`P_Execute_WeaponClang`、`P_Stampede_Step`）。
2. **Cascade → Niagara 转换（中等成本，需人工修）**
   引擎转换器对「Sprite + 简单重力」尚可，对「Mesh 发射器 / 事件 / SubUV」基本要重做。
   **建议先拿 1 个资产试转、逐项核对（贴图/材质/颜色曲线/拖尾）再批量** —— 这一步我没有实测，不给成功率承诺。
3. **Niagara 重做（成本最高、最可控）**
   只给长期复用的核心技能（处决、冲锋拖尾）。

**建议**：Grux 三件套走路线 1 先跑起来；处决/冲锋这类长镜头核心技能走路线 3；岩浆皮肤 24 个走路线 2 试批量。

---

## 4. 可开发的 GA（按投入产出比排序）

### 4.1 ★★★ 命中特效分流（不是新 GA，收益最大）

- **现状**：`NS_CombatImpact` 被 110 个 DA 引用。
- **做法**：不改 GA 逻辑，只改 `DA_*` 的 `ImpactVFX`，按三维度分流：

| 维度 | 规则 | 用哪个资产 |
|---|---|---|
| 体型 | 小（Goblin/Shroom）/ 中 / 大（Golem/Grux） | 中型留 `NS_CombatImpact`；大型换 `NS_FireExplosion` 缩到 0.7 或 `P_Hardknocks_AOE`（需包装） |
| 武器 | 利器 / 钝器 / 徒手 | 利器 → `NS_Blood_Hit_Splash(_Green)`（已备好）；钝器 → `NS_Shock` 系 |
| 元素 | 火 / 冰 / 雷 / 毒 | 现成：`NS_Frost_Impact`、`NS_DungeonElectricBeam`、`NS_FireExplosion` |

- **收益**：110 个技能手感立刻分化，0 行代码。
- **验收**：抽 5 个不同武器技能，命中特效肉眼可区分。

### 4.2 ★★★ Grux 三件套换真特效（零新 GA）

| DA | 现在 | 换成 | 动作 |
|---|---|---|---|
| `DA_Grux_Charge` | `NS_CombatImpact` | `P_Stampede_Trail`（拖尾）+ `P_Stampede_Step`（踏地） | §3 路线 1 包装后接 `LoopVFX` |
| `DA_Grux_Combo` | `NS_CombatImpact` | `P_MeleeTrails_Regular` / `_Strong` + `P_Grux_Melee_SucessfulImpact` | 轻/重击分别取 Regular / Strong |
| `DA_Grux_Roar` | `NS_GroundSummon` | `P_GruxUlt_Roar` 或 `P_Execute_Roar_Shockwave` + `P_GruxUlt_GroundInteraction` | 同上 |

- **验收**：冲锋时脚下连续踏地 + 烟尘拖尾；三连击每段拖尾不同；咆哮有地面环形冲击。

### 4.3 ★★★ 掉落 / 拾取 / 升级链补 VFX（资产现成，系统已有）

| 场景 | 资产 | 挂点 |
|---|---|---|
| 掉落品质光柱 | `NS_LootDrop_{Common..Mythic}`（6 档）+ `NS_LootDrop`（主入口） | 掉落物 Actor `OnSpawn` 按品质选一档 |
| 灵魂拾取 | `NS_SoulPickup`（飞行）+ `NS_Soul`（本体）+ `NS_Glow`（待拾取高亮） | 灵魂挂 `NS_Glow`；被吸时切 `NS_SoulPickup` |
| 升级 | `NS_Free_Spells_LevelUp` | 升级 GE 触发时 `SpawnSystemAtLocation`（脚下） |

- **现状提醒**：掉落包 7 个系统 **一个都没进游戏**，当前掉落反馈靠别的资产凑（`NS_Halo` 之类）。
- **验收**：不同品质掉落光柱颜色/形态可区分；灵魂有「升起 → 飞向玩家」过程（`NS_SoulPickup` 若不自带位移需 BP 插值）。

### 4.4 ★★☆ 光环 / 增益体系（14 个可补，同包已有 4 个在用）

- **载体**：已有 GA `GA_ElementalWard`、`GA_HaloOfProtection`、`GA_StormBrand`、`GA_ArcaneMeditation`。
- **做法**：照 `UDebuffNiagaraComponent` 的机制（见 `blood-fx-catalog.md` §3.3），按 buff 标签挂对应 Aura：

| Buff | Aura |
|---|---|
| 持续治疗 | `Aura_Healing` / `Buff_Healing` |
| 火焰附魔 | `Aura_Fire` / `Buff_Fire` |
| 雷附魔 | `Aura_Lightning` / `Buff_Lightning` |
| 法力回复 | `Buff_Mana` |
| 灵魂增益 | `Buff_Soul` |
| 灵感/音符机制 | `Aura_Notes` / `Buff_Notes` |
| 移速（风） | `Buff_Air` |

- **验收**：同角色叠 2 个 buff 时两个光环都可见、不互相穿模（必要时错开挂点高度）。
- **性能**：常驻循环系统，20 个敌人全挂会明显吃 GPU —— 只给精英/Boss，小怪用图标。

### 4.5 ★★☆ 击飞 / 硬控 GA（`P_Grux_Knockup` + `P_Hardknocks_AOE`）

- **新 GA**：`GA_Knockup`
  - 逻辑：`LaunchCharacter`（Z 600~900）+ `State.Enemy.Hitstun` 1.2s；
  - 表现：起手 `P_Hardknocks_AOE`（脚下）+ 空中 `P_Grux_Knockup`（跟随被击飞者）+ 落地 `NS_CombatImpact`；
  - 可复用 `GA_Prototype_Displace` 的位移代码。
- **验收**：特效跟着被击飞者上升，落地有尘。

### 4.6 ★★☆ 地震波 GA（`P_RipplingSmash_*`）

- **新 GA**：`GA_SeismicWave`
  - 逻辑：以自身为圆心沿地面扩散（改 `GA_EarthShockwave` 或 `GA_Prototype_Wave`）；
  - 表现：起手 `P_RipplingSmash_Aligned_Initial`（砸地）→ 扩散路径 `P_Grux_RipplingSmash_ImpactedTrails`；
  - 起点参数：波速 1200~1600 uu/s、半径 600、`Damage.Physical`。
- **验收**：能看清波「走」出去的路径，不是全屏一亮。

### 4.7 ★★☆ 处决 / 斩杀 GA（7 个 Ultimate FX）

- **新 GA**：`GA_Execute`（对 HP < 20% 的敌人可用）
  - 流程：`P_Execute_WeaponStart` → `P_ExecuteBuff_Loop`（蓄力）→ `P_Execute_WeaponClang` + `P_Execute_Roar_Shockwave` → `P_Execute_WeaponEnd`
  - 状态：`State.Enemy.Executed`（不可被抢断）；可配 `GA_WorldSlow` 做 0.3 倍速特写
- **验收**：全程 1.5~2.0s；中途被打断要有失败分支。

### 4.8 ★★☆ 岩浆皮肤 / 岩浆地板（24 个，做「主题」不做「技能」）

- **用途**：给 Grux（或 Boss）做**元素变体**，替换普通 `P_*` → `P_*_Magma_*`。
- **roguelike 价值**：`Theme_Lava` 楼层整层刷岩浆版敌人，与现有主题标签体系对齐。
- **副产物**：`P_Grux_Magma_GroundInteraction` + `P_Grux_Magma_RipplingS_Cast_inst` 可直接当**地面持续伤害区**（配 `GA_Prototype_Field`）。
- **参数起点**：地面类贴地缩放 1.4~1.8、`Pitch=-90`。
- **验收**：同屏普通 Grux 与岩浆 Grux 一眼可分。

### 4.9 ★☆☆ 法力吸取 Debuff（`NS_ManaDrain`）

- **新 GA**：`GA_ManaDrain`：3s 内每 0.5s 抽 10 蓝并回补自身；特效需要在施法者与被吸者之间做两点连线（BP 里设位置/朝向）。
- **风险**：`NS_ManaDrain` 若是原地爆发形态，连线效果要做插值 —— 先看资产再定。
- **验收**：玩家能明确感知「蓝在被抽」。

### 4.10 ★☆☆ 刀光 / 火球变体（`NE_attack02/04/05`、`NS_Fire_1~4`）

- **做法**：不改 GA，只做**外观变体**，按装备/天赋切换 `LoopVFX`（装备系统已有 spell-build 机制）。
- **收益**：同一技能换装换外观，成本≈0。
- **验收**：换装后外观肉眼不同。

---

## 5. 「已有 GA，但特效是占位/缺失」

| GA / DA | 现在 | 缺什么 |
|---|---|---|
| `DA_Grux_{Charge,Combo,Roar}` | 通用 `NS_CombatImpact` / `NS_GroundSummon` | 专用特效（§4.2） |
| `GA_Demon_{Claw,TailStab,Tailspin}` | 无 VFX 引用 | Demon 包无专用 FX，可复用 `P_MeleeTrails_Regular` |
| `GA_StoneGolem_{Quake,HeavyHammer,ShieldBash}` | 无 VFX 引用 | 地震类用 `P_RipplingSmash_*`；重锤用 `P_Hardknocks_AOE` |
| `GA_Shroom_{Slam,Spore}` | 无 VFX 引用 | 孢子可用 Free_Spells 绿系（需改色） |
| `GA_MeleeAttack` | 走 `GC_MeleeImpact` + 受击者 `BloodEffect` | 已由 `blood-fx-catalog.md` 覆盖 |

> 先给这 10 个「已有逻辑、没有专属表现」的 GA 补特效，比从零设计新 GA 快得多。

---

## 6. 不建议动 / 低优先

| 资产 | 原因 |
|---|---|
| 血包 `Color_Variations` 32 个 | 换色只需改 `Color A/B`，别复制 32 个系统 |
| 血包 `Once` 14 个 | 已被 `/Game/Art/BloodFX` 覆盖 |
| `Ultimate/FX/Rework` 3 个 | 与 `Ultimate/FX` 主版本重复，二选一 |
| `NE_Orb` | 是 `NS_Orb_1` 的内部发射器，不是独立系统 |
| `NS_SolarFireballExplosion` 等 Batch6 重复件 | 与已用的 `NS_SolarFireballFlame` 重叠，除非做太阳主题 |

---

## 7. 建议推进顺序

**冲刺 1（1~2 天，0 代码 / 0 新 GA）**
1. Grux 三件套换特效（§4.2，含 Cascade 包装 BP）
2. 掉落品质光柱 + 灵魂拾取 + 升级（§4.3）
3. 血包 8 个系统接入（`blood-fx-catalog.md` §3）

**冲刺 2（2~3 天，改 DA 字段 + 少量 BP）**
4. 命中特效分流（§4.1）
5. 光环/增益补齐（§4.4）

**冲刺 3（3~5 天，新 GA）**
6. 击飞（§4.5）→ 地震波（§4.6）→ 处决（§4.7）
7. 岩浆皮肤整层替换（§4.8，跟 `Theme_Lava` 一起）

---

## 8. 数据

- `docs/vfx-refs.json`：247 条明细（引用数 / 玩法引用数 / 引用方路径）
- `docs/vfx-buckets.json`：三桶名单（真在用 83 / 只展示 78 / 零引用 86）
- `_tokens2.txt`：原始 token 扫描结果

> 所有标「待实测」的参数（Cascade 转换成功率、包装后参数可调性、`NS_ManaDrain` 形态）都需要先在引擎里验证 1 个样本再批量。
