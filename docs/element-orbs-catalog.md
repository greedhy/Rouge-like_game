# 元素法球特效资源目录（Element Orbs）+ GA 接入说明

> 生成方式：Unreal Editor MCP（SOMOLMCP，引擎 5.8.1）实测创建并验证。
> 生成时间：2026-11-19
> 适用对象：负责技能（GA / AbilityPrototype）实现的工作 Agent 与美术。

---

## 0. TL;DR

- 新建 **5 个互相独立的 Niagara 系统**，放在 `/Game/Art/ElementOrbs/Systems/`，分别对应冰霜 / 熔岩 / 泥沼 / 剧毒 / 藤蔓五种元素。
- 五个系统**全部编译通过**（`ready_to_run = true`，0 error），已在 `L_RoguelikeRun` 中实际生成并截图确认可渲染。
- 另备 **2 个绿色材质实例**（酸绿 / 翠绿），用于把「剧毒 / 藤蔓」换成严格绿色（见 §6 已知限制）。
- 接入 GA 的关键字段是 `AbilityPrototype` 的 **`LoopVFX` / `ImpactVFX` / `LoopVFXScale` / `ImpactVFXScale`**，不是蓝图里的组件引用。

---

## 1. 交付资产清单

### 1.1 五个元素法球系统

| 元素 | 系统资产路径 | 来源设计（原资产） | 主色（实测） | 发射器 | 用户参数 |
|---|---|---|---|---|---|
| 冰霜 Frost | `/Game/Art/ElementOrbs/Systems/NS_Orb_Frost` | `MayuOrbs/Orb_2/NS_Orb_2` | 冰蓝（青） | Orb_Base / Sparks / Sparks001 / Embers | `SpriteSize`、`Camera Offset Amount` |
| 熔岩 Lava | `/Game/Art/ElementOrbs/Systems/NS_Orb_Lava` | `MayuOrbs/Orb_4/NS_Orb_4` | 橙红火焰 + 火星 | Orb_Base / Embers / Smoke | `SpriteSize`、`Camera Offset Amount` |
| 泥沼 Mud | `/Game/Art/ElementOrbs/Systems/NS_Orb_Mud` | `MayuOrbs/Orb_5/NS_Orb_5` | 棕褐烟团 | Orb_Base / Smoke | 无 |
| 剧毒 Toxic | `/Game/Art/ElementOrbs/Systems/NS_Orb_Toxic` | `MayuOrbs/Orb_3/NS_Orb_3` | 酸绿（已改，原品红） | Orb_Base / Embers | `SpriteSize`、`Camera Offset Amount` |
| 藤蔓 Vines | `/Game/Art/ElementOrbs/Systems/NS_Orb_Vines` | `MayuOrbs/Orb_1/NS_Orb_1` | 金属外壳 + 翠绿核心（**体型明显偏小**，见下） | NE_Orb / Embers / NE_Orb001(禁用) | `SpriteSize` |

> **藤蔓形态说明**：`Orb_1` 原设计是一颗小型结晶（金属外壳 + 发光内核），不是大法球。
> 已把它的颜色改成翠绿，可作为「藤蔓种子 / 藤蔓结晶」表现；若需要和其它四个同等体量的球，
> 两个办法：① `LoopVFXScale=(1.8,1.8,1.8)` 放大；② 复制 `NS_Orb_Toxic` 另存为藤蔓版并换色（同源不同名）。

### 1.2 为达成元素配色而调整的两个共享材质实例（可回滚）

| 材质实例 | 原值 | 现值 | 用途 |
|---|---|---|---|
| `MayuOrbs/Orb_3/Materials/MI_orb_3_Plasma` | COlor 1 `(0.0573,0,0.0259)`、Color 2 `(0.8594,0,0.5936)` | COlor 1 `(0.02,0.16,0.01)`、Color 2 `(0.55,1.0,0.08)` | 剧毒→酸绿 |
| `MayuOrbs/Orb_1/Materials/MI_Orb_1_purple` | Color 1 `(0.0899,0,0.125)`、Color 2 `(0.2476,0,1)`、Final Intensity `1.313`、Center Glow `0.811`、Inner `0.032` | Color 1 `(0.01,0.075,0.02)`、Color 2 `(0.25,0.90,0.35)`、Final Intensity `4.0`、Center Glow `2.5`、Inner `0.3` | 藤蔓→翠绿 |

注意：这两个 MI 被原始导入系统（`NS_Orb_3` / `NS_Orb_1` 及各自 `_Small`）共享，改色后原资产也会跟着变。
`MI_orb_3_Plasma` 的 1 号参数名是 `COlor 1`（大写 O，资产本身的拼写），脚本里别写成 `Color 1`。

设计意图：**一个元素一个独立资产**，互不共享包，后续要单独调色/调尺寸/换形态不会互相牵连。

### 1.3 备用绿色材质（独立制作，可替代上面两个共享 MI）

| 资产 | 用途 | 父材质 | 关键颜色 |
|---|---|---|---|
| `/Game/Art/ElementOrbs/Materials/MI_Green_Acid` | 剧毒（酸绿） | `M_Orb_2`（Unlit） | 4way Color `(0.03,0.25,0.02)` / Center Glow And Ring Color `(0.35,1.0,0.10)` |
| `/Game/Art/ElementOrbs/Materials/MI_Green_Emerald` | 藤蔓（翠绿） | `M_Orb_2`（Unlit） | 4way Color `(0.02,0.20,0.05)` / Center Glow And Ring Color `(0.20,0.85,0.30)` |

> 这两个是**独立 MI**，不污染导入美术资产。但当前只能人工在 Niagara 编辑器里指定到渲染器，原因见 §6.2。

---

## 2. 玩法定位：五个法球分别对应什么 GA

设计原则：**法球是「元素载体」，同一个球可以服务三种形态**——环绕（Orbit）、投射（Projectile）、场地（Field）。先做环绕，因为它和现有 `GA_Prototype_Orbit` 完全同构。

| 元素 | 元素语义 | DamageType 标签（项目已存在） | 命中状态建议 | 定位 |
|---|---|---|---|---|
| 冰霜 | 减速 / 冻结 | `Damage.Water` | 叠 3 层 → 定身 1.5s（`State.Enemy.Hitstun`） | 控场 |
| 熔岩 | 持续燃烧 | `Damage.Fire` | 每秒 1 跳，持续 3s（DoT） | 持续输出 |
| 泥沼 | 沉重 / 拉拽 | `Damage.Physics` | 移速 -40%，持续 2s；可叠加「下陷」 | 削弱 / 聚怪 |
| 剧毒 | 腐蚀 / 扩散 | `Damage.Magic`（或新增 `Damage.Poison`） | 中毒 4s，死亡时向邻近敌人扩散 | 清群 |
| 藤蔓 | 缠绕 / 定身 | `Damage.Magic` | 命中定身 1s，期间受伤 +15% | 单体锁 |

推荐的**首个可玩版本**（成本最低、验证最快）：

1. **冰霜环绕**：复制 `DA_Prototype_Orbit` → `DA_Orbit_Frost`，`DamageType=Damage.Water`、`Count=3`、`Radius=250`、`OrbitSettings.OrbitDegreesPerSecond=90`，`LoopVFX=NS_Orb_Frost`。
2. **熔岩投射**：复制 `DA_Prototype_Field` 或复用 `GA_FireBolt` 管线，`ImpactVFX=NS_FireExplosion`，`LoopVFX=NS_Orb_Lava`。
3. **剧毒地场**：`Kind=Field`，`LoopVFX=NS_Orb_Toxic`，`bGroundPresentation=true`，`SurfaceClass` 指向毒地表。

其余两个元素（泥沼 / 藤蔓）照抄同一模板换 `LoopVFX` + `DamageType` 即可，无需新代码。

---

## 3. GA 侧接线：确切的字段名

`AbilityPrototype`（`/Script/test_25d.AbilityPrototype`）里与特效直接相关的字段（实测自 `DA_Prototype_Orbit`，共 35 个 CPF_Edit 属性）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `Kind` | `EAbilityPrototypeKind` | `Orbit` / `Field` / 其它，决定执行器分支 |
| `LoopVFX` | `TObjectPtr<UNiagaraSystem>` | **持续表现**：放本目录的 `NS_Orb_*` |
| `ImpactVFX` | `TObjectPtr<UNiagaraSystem>` | **命中/结束爆点**：见 §3.2 |
| `LoopVFXScale` / `ImpactVFXScale` | `FVector` | 特效缩放，默认 `(1,1,1)` |
| `bGroundPresentation` | `bool` | 贴地表现（Field 类建议 true） |
| `SurfaceClass` | `TSubclassOf<ATerrainSurface>` | 地面残留（配合主题地表） |
| `DamageType` | `FGameplayTag` | 用 `Damage.Fire` / `Damage.Water` / `Damage.Physics` / `Damage.Magic` |
| `Radius` / `Duration` / `Period` / `Damage` / `Count` | float/int | 与 VFX 尺寸对齐（法球本体直径 ≈ 半径的 1/5 观感最好） |
| `OnHit` / `OnPeriod` / `OnExpire` | `UAbilityPrototype*` | 元素连锁：例如剧毒 `OnExpire` 触发扩散 |

已有可复用资产（不要新建重复的）：

| 用途 | 资产 |
|---|---|
| 环绕技能原型 | `/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Orbit`、`DA_Prototype_OrbitSettings` |
| 场地技能原型 | `.../Prototypes/DA_Prototype_Field` |
| 投射物 + 环绕弹 | `.../PassiveSpell/OrbitingFireBolt/DA_OrbitingFireBolt`、`BP_OrbitingFireBolt` |
| 伤害/消耗 GE | `.../Player/Effect/GE_Damage`、`GE_Cooldown_*`、`GE_Cost_*` |

### 3.2 推荐配对的爆点特效（同元素复用现成资产）

| 元素 | `ImpactVFX` 建议 |
|---|---|
| 冰霜 | `/Game/Art/CompositionEffects/NS_Frost_Impact` |
| 熔岩 | `/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion` |
| 泥沼 | `/Game/CompositionBatch6/NS_EarthShockwave` |
| 剧毒 | `/Game/Assets/Effects/Combat/NS_BloodImpact_green`（绿色溅射，读作毒性爆点） |
| 藤蔓 | `/Game/Art/CompositionEffects/NS_Bramble_Warning` |

---

## 4. 运行时控制（不需要改资产）

- **缩放**：优先改 Actor/Component 的 `SetRelativeScale3D`；`LoopVFXScale` 是编辑器静态值。
- **尺寸微调**：4 个系统暴露了 `SpriteSize`（float，默认 1）。运行时：
  `NiagaraComponent->SetFloatParameter("SpriteSize", 1.4f)`。
  **注意**：`NS_Orb_Mud` 没有该参数（源资产如此），只能靠组件缩放。
- **颜色**：系统里没有暴露颜色 user parameter（颜色烘焙在材质实例里）。要按层数/充能改色，必须先在材质里加颜色参数并在渲染器里绑定——当前 MCP 做不了（§6.2），需美术配合。

---

## 5. 复现方式（MCP 调用序列）

创建过程全部通过 MCP 完成，等价命令序列如下（`_project_path` / `_instance_uuid` 由客户端自动注入）：

```text
# 1) 目录必须先存在（见 §6.3）
Content/Art/ElementOrbs/{Systems,Materials,Emitters}/

# 2) 复制美术原版系统为元素系统
asset_duplicate_v2  asset_path=/Game/resources/MayuOrbs/02_Orbs/Orb_2/NiagaraSystem/NS_Orb_2
                    destination_asset_path=/Game/Art/ElementOrbs/Systems/NS_Orb_Frost
                    execute=true save_asset=true
# 同法：Orb_4→NS_Orb_Lava、Orb_5→NS_Orb_Mud、Orb_3→NS_Orb_Toxic、Orb_1→NS_Orb_Vines

# 3) 编译 + 保存 + 自检
niagara_compile_system        system_asset_path=<system> force=true wait_for_completion=true
asset_save                    asset_path=<system>
niagara_compile_diagnostics   system_asset_path=<system>   # 期望 ready_to_run=true, error=0
niagara_system_inspect        asset_path=<system>          # 期望 emitters 列表非空
```

验证渲染：

```text
niagara_spawn_actor  asset_path=<system> location={x,y,z}
editor_viewport_set_realtime enabled=true      # 否则视口冻结，截图会重复
editor_screenshot_viewport
actor_delete         actor=<label>             # 验完即删，别留在关卡里
```

---

## 6. 已知限制（重要，避免重复踩坑）

1. **PIE 运行中无法保存资产**。所有写操作（创建/保存/复制/改名）在 PIE 中会返回
   `Failed to save asset`。先 `pie_stop`，再写。
2. **`niagara_set_renderer_properties` 的 `OverrideMaterials` 在本项目不生效**（实测）。
   写入返回 success、磁盘上也能 grep 到新的 MI 路径，但渲染结果不变；`niagara_add_mesh_renderer`
   的 `override_material_paths` 同样无效。因此 **不要指望用 MCP 给现有 emitter 换材质**。
   要换色只有三条路：
   - 改共享 MI（影响所有引用者）；
   - 新建系统（本目录做法）；
   - 美术在 Niagara 编辑器里手动指定（针对 §1.2 的两个绿色 MI）。
3. **资产目录必须先存在于磁盘**。`material_instance_create` 在库中显示创建成功，但保存会失败，
   若 `Content/<新目录>` 不存在。先 `mkdir` 再调 MCP。
4. **着色器编译期间材质显示为灰/银**。新建 MI 或 `RECOMPILESHADERS` 之后，视口里球体会变成
   灰色金属球（默认材质回退），编译完成后才恢复正确颜色。判断「换色失败」前先确认没有在编译。
5. **导入包里的发射器是内联的**，无法按系统单独换材质；换色只能整包新建或用共享 MI。
6. `NS_Orb_Vines` 的来源设计 `Orb_1` 是「小型结晶」而非大法球，实测体型约其它四个的 1/3。
   要统一体量请在 DA 里设 `LoopVFXScale=(1.8,1.8,1.8)`，或改用 §1.1 说明里的替代方案。
7. 每次改完材质/系统记得 `asset_save`；只改不存，PIE 里看不到，重启编辑器还会丢。

---

## 7. 验证记录

| 检查 | 结果 |
|---|---|
| 5 个系统 `niagara_compile_diagnostics` | 全部 `ready_to_run=true`，`vm_compile_event_error_count=0` |
| 5 个系统 `niagara_system_inspect` | 发射器数量 2~4，无空系统 |
| 关卡内实际生成 | 已生成并截图，5 个均可渲染（青 / 火 / 酸绿 / 棕 / 翠绿结晶） |
| 截图留档 | `Saved/SOMOLMCP/Screenshots/20261118/20261118T173501Z_editor_screenshot_viewport_*.png` |
| 收尾 | 测试用 NiagaraActor 已全部删除，关卡未保存改动 |

---

## 8. 待办（给下一个 Agent）

- [ ] 新建 5 个 `DA_Prototype` 变体，`LoopVFX` 指向本目录五个系统（§2 模板），`ImpactVFX` 按 §3.2 配。
- [ ] 藤蔓统一体量：DA 里 `LoopVFXScale=(1.8,1.8,1.8)`；如需彻底脱离导入资产，可复制 `NS_Orb_Toxic` 另存并改色。
- [ ] 若要「绿色不污染导入资产」：人工把 `MI_Green_Acid` / `MI_Green_Emerald` 指到对应渲染器（MCP 做不了，§6.2）。
- [ ] 给 5 个元素各建 1 个 `GameplayCue`（目前全项目仅 5 个 Cue），承接命中音效/镜头反馈。
- [ ] 冰霜/剧毒做「叠层 → 引爆」：复用 `OnPeriod` + `Stack` 计数，`OnExpire` 触发扩散。
- [ ] 若要做拖尾：`resources/Knife_light` 的 5 条 `NE_attack0*` 现成可用（当前未接入）。
