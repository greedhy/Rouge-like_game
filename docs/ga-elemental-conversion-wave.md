# 元素组合第九批设计

沿用第四页的本局免费试装、鼠标落点和 T 键。四种技能分别扩展地表、冻结、定向位移和以带状态的目标为多段爆裂中心。Larian 的制作工具把扇形、落点、状态条件和地表清除分成不同动作。这一批借鉴其中的组合关系，使用本项目自己的技能、实时规则和素材。[技能制作](https://docs.larian.com/Skill_creation) · [移除地表层](https://docs.larian.com/Osiris/API/RemoveSurfaceLayer) · [地表转化](https://docs.larian.com/Osiris/API/TransformSurface)

| 本轮移植 | 适配方式 | 可替换节点或参数 |
| --- | --- | --- |
| 涡流涤地 | 在落点处理施法者持有的临时地表，复用原租约 | 寻找半径、地表种类、处理数量、移除成功位置与后继 |
| 狭线裂霜 | 按鼠标方向选直线上的冻结目标，实际扣血后再消费同一原生命的状态 | 起点、几何形状、射程、宽度、目标数、状态条件、伤害元素、消耗状态与命中表现 |
| 风压散流 | 按鼠标方向做扇形筛选，使用已有带碰撞约束的位移动作 | 角度、距离、目标数、状态条件、位移距离和表现 |
| 灼印迸火 | 先等待，再筛选带灼热的敌人；每个标记提供一个范围爆裂中心 | 引信、筛选与排序、数量、目标转落点、每次爆裂的几何、伤害与独立表现 |

地图永久地表、云层与地表互转属于地图层能力。它们需要独立的空中接触、层权限和导航更新合同。本批的地表动作沿用现有临时地表租约。

## 图连接

| 操作 | 真实连接 |
| --- | --- |
| 涡流涤地 | `Origin → World.DispelSurface`，每次实际移除发送 `Removed: Origin`，可接范围效果和表现；`Completed` 表示本次查询收尾。 |
| 狭线裂霜 | `Spatial.Anchor → Query.Shape → Condition.ElementalStatus → Effect.Damage → Condition.DamageApplied → Target.ResolveDamageTarget → Status.ConsumeExposure`。实际扣血、身份复核和匹配状态三者都成立时才消耗。 |
| 风压散流 | `Spatial.Anchor → Query.Shape → Condition.ElementalStatus → Movement.TargetDisplace`。位移可接完成位置或拒绝位置；被免疫和受场景阻挡的目标不影响其它目标的处理。 |
| 灼印迸火 | `Control.DelayOrigin → Query.SelectTargets → Spatial.TargetOrigin.Located → Query.Shape → Effect.Damage`。目标经过服务器原生命验证后成为局部爆裂中心，每个中心独立选敌。 |

`Set.QueryGeometry` 可在通用 `Query.Shape` 中切换圆形、扇形、圆环与直线。直线开放长度和完整宽度，扇形开放半径与完整开角，圆环开放内外半径。形状节点不决定状态类型或伤害。`Spatial.TargetOrigin`、`Status.ConsumeExposure` 可追加在 `LiveTarget` 上，`World.DispelSurface` 可追加在 `Origin` 上。

例如可在裂霜的「已扣血并复核活目标」位置接另一个状态效果。改装「冻僵碎裂」同时更新伤害前筛选和伤害后消耗。若另把物理伤害转为能改变该状态的元素，消耗节点读取的是结算后的状态，只有仍匹配才清除。引燃和清除均只处理本次生命拥有的临时地表。

## 验收

- [x] 验证实际目录、9 张新卡与四项技能的 T 键施法。第四页共有 48 个技能、67 张卡。
- [x] 验证 Actor 与 Mass 的正伤害消费、零伤害保持、旧生命复核和定向位移。
- [x] 状态、位置适配与地表模块进入可信图编辑，验证圆形转直线确实改变实战目标集合，并验证越权地表保护。
- [x] Development Editor 构建成功。1081 项全量自动化中 1080 项通过，唯一失败仍为 9 个既有 Stander 时间轴缺少蒙太奇引用。

报告位于 `Saved/GAAuthoring/ElementalConversionWave/Accepted/index.json`。实际特效和双端表现仍待 PIE 验收。
