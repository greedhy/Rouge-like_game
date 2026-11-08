# 第六批组合技能

本批为玩家主动栏提供六种技能。资产集中在 `/Game/CompositionBatch6`，配方是 `Scripts/GA/Batch6/specs.json`。只增加可学习目录项，不替换玩家已装备槽位。

当前范围按用户最新指示收敛到 Actor/GAS：以原 `BP_Player` 和 Actor 怪物完成行为、表现、技能页面和联机输入。Mass 适配接口及已写逻辑保留，进一步完善和验收延后，不作为本批交付前置。

| 技能 | 实现方式 | 主要配置 |
|---|---|---|
| 星焰护环 | 跟随玩家的 Field 开关原型 | 300 范围，每秒伤害；开启 15 MP，持续 5 MP/s；再次按键关闭 |
| 石化凝视 | Actor 控制组件 / Mass 状态片段 | 650 范围，3 秒石化，冻结行动并覆盖石质材质，无直接伤害 |
| 熔日火球 | Projectile → OnHit → Burst | 750 飞行速度，380 爆炸范围，墙体阻断 |
| 极寒风暴 | Delay → OnExpire → Field | 0.8 秒准备，450 范围，持续 5 秒；周期伤害，50% 减速持续 1.25 秒并刷新 |
| 裂变雷链 | 有界广度优先传导 | 每目标最多 2 个分支，3 跳，最多 12 个目标；400 跳跃距离，每层保留 80% 伤害 |
| 大地震波 | 扩散 Wave 波前 | 扩散至 800 范围，速度 450；经过目标时命中一次 |

数值以当前 DA 为准。六个技能的基础冷却均为 5 秒，基础开启/施放消耗均为 15 MP。石化和雷链的特殊参数在对应 GA 默认值中，其余主要参数在 DA 中。

## 原型与目标通路

`UBatch6FieldPrototype` 在既有配方上增加跟随开关、波前和减速参数。`UBatch6FieldAbility` 沿用 `UPureMassAbility` 的网络目标任务及服务器运行时；普通 Actor 和 Mass 实体共用 `PureMassCombat` 查询、可见性判断及目标解析。

Actor 伤害继续通过现有 GAS 伤害 GE，Mass 伤害继续通过 `UPureMassSubsystem::ApplyDamage`。客户端不决定伤害和控制结果。表现沿已有 Runtime Actor 复制，分叉雷链另复制有界的线段列表。

Actor 石化使用原生 Infinite GE 的运行期 Spec 授予 Stun 标签，组件维护期限、材质和动画恢复；GE 被对象池清除时同步恢复。组件按对象池租约隔离旧生命。Mass 石化保存在 Fragment，停止移动/攻击，并将石化标志复制到 Crowd；Crowd 使用石质材质的独立渲染批次。目前 Mass 石化显示固定姿态，不保证保留命中瞬间的精确动画帧。

减速与地形减速取最强值，独立期限；强减速先到期后，较弱但仍有效的减速继续。Actor 空闲组件停 Tick，Mass 不创建逐怪 Actor。

## 资产和菜单

图标已实际查看项目 `Skill_Icons/Textures/128x128` 的 50 种图案：护环 02、石化 44、火球 03、暴风雪 17、雷链 11、震波 08。导出预览位于 `Saved/GAAuthoring/CompositionBatch6/icons/contact-sheet.png`。

制作脚本先检查原生类与外部资产，再创建独立 DA、GA 和特效。仅追加/更新本批技能标识的目录行。接入主 `DA_AbilityInfo`、Mass 目录和 Batch5 目录，Spell 原型页使用可滚动列表展示图标。旧目录行与装备状态必须保持。

## 验证入口

- `Scripts/GA/Batch6/verify_native.ps1`：构建并运行本批原生自动化用例，使用独立运行目录。
- `Scripts/GA/Batch6/verify_assets.py`：编辑器中独立读回资产、配方和目录。
- PIE 验收须实际学习、装备、按键施放；覆盖 Actor 和 Mass，检查耗蓝、冷却、伤害数字、控制到期、取消和死亡清理。
- 联机须由远程客户端输入，再检查服务器结算和另一端表现。脚本直接在服务器调用 RPC 不构成远程输入验收。

编译、静态读回和 NullRHI 自动测试不能证明特效视觉尺寸、菜单排版或双客户端表现。最终验证结果随本轮证据更新。

## 本轮结果（2026-10-31）

- 完整 Editor 构建成功。原生用例 9/9 通过（4 项测试使用无模型的临时角色，带无骨骼插槽环境警告；0 失败）。报告：`Saved/GAAuthoring/CompositionBatch6/3e1aaa52333e41eaa97634547ac76af1/native/index.json`。
- 独立资产读回通过：6 GA、8 配方节点、精确父类/图标/标签/伤害 GE 和三份目录；主/Batch5/Mass 元数据分别为 41/41/32 行，保留旧行。报告运行：`0759e21ed9244032b1396f8b6b38fa01`。
- 原 `BP_Player` 双客户端 Actor/GAS 验收通过：六个技能，87 项断言。包括真实远程 Enhanced Input、单次扣费、护环持续扣费和关闭、Actor 伤害与复制、石化材质/动画恢复、减速恢复、四目标真实雷链分叉及预算。报告：`Saved/GAAuthoring/CompositionBatch6/a8629679a716482faa43362a0d4f3a53/actor-pie.json`。
- 原玩家的两端 Spell 菜单六项选择/描述验证通过。发现并修复原菜单 `WrapTextAt=2` 导致的竖排缩字；只为该菜单复制独立字体样式表，不修改原共享表。最终截图已实际查看。
- 暴风雪使用原冰霜 Niagara，并增加半透明范围圆盘及有界实例化落雪；无碰撞、无阴影，结束清理。圆盘的几何半径与配置相同，不代表已经完成所有坡面和遮挡的艺术验收。
- QA 地图为 `/Game/CompositionBatch6/QA/L_Batch6Test`，使用原玩家/控制器及独立 Actor QA GameMode。原地图生成器没有修改。结束后恢复了原 `L_RoguelikeRun` 编辑器地图和后台限速设置。

本轮未覆盖：真实存档重载/无缝换图、完整对象池第二次生命 PIE、打包版本及用户手动操作验收。石化的租约隔离和清 GE 恢复已有原生回归，但不能代替完整池化流程测试。Mass 的后续完善与正式验收按用户要求延后。

![原玩家技能菜单](images/composition-batch6/menu.png)

![冰霜区域与落雪原型](images/composition-batch6/blizzard.png)

![四目标雷链与伤害反馈](images/composition-batch6/chain.png)
