# 元素技能候选与节点设计

## 资料和候选

Larian 的技能制作说明将投递形态、目标条件、基础伤害和地表动作分开配置。地表转换接口还定义了毒与油的引燃、水的导电与冻结，以及地表来源和持续时间。这里借鉴的是这些玩法关系。技能名称、实时节拍、指向方式、作用范围和素材均按本项目设计。[技能制作](https://docs.larian.com/Skill_creation) · [地表转换](https://docs.larian.com/Osiris/API/TransformSurfaceAtPosition)

| 原机制 | 本项目候选 GA | 适用原因 |
| --- | --- | --- |
| 定点创建毒地 | **沼蚀毒床** | 项目已有 Toxic/Frost/Lava/Vines 地表和 Actor／Mass 接触处理。持续地形增加了站位决策。 |
| 火引燃毒地 | **熔沼引燃** | 把布置与转化分成两次施法。首版查询施法者自己的临时毒地，成功时继承原地块尺寸和剩余寿命。 |
| 扩展区域的冰冷命中 | **霜锋潮** | 用游戏已有的扫过式波前代替回合内瞬时结算，可用距离或速度调出不同节奏。每个原生命最多受击一次。 |
| 水促进导电 | **潮印放电** | 先按状态过滤、再按距离排名，放电仅作用于配方认可的目标。这给上批雨幕提供了有条件的后续用法。 |

更大范围的地表清除、地表转云和受控位移适合作为后续候选。清除要定义地图地表和多玩家地块权限，地表转云还需要空中层与接触规则。Larian 将吹散地表和状态列为单独的投递类型，也把地表与云视作不同层。[技能制作](https://docs.larian.com/Skill_creation) · [地表与云转换](https://docs.larian.com/Osiris/API/TransformSurfaceAtPosition)

## 节点拆分

| GA | 默认图及类型化边 | 可独立修改或替换 |
| --- | --- | --- |
| 沼蚀毒床 | `Ground Origin → World.ElementSurface`，位置通过 `Spawned` 发给后继。地表出生后由原接触系统结算。 | 落点、地表类型、半宽、寿命、出生后追加节点。 |
| 熔沼引燃 | `Ground Origin → World.IgniteSurface`，成功时 `Transformed: Origin` 为原地块中心。 | 热源位置、寻找范围、转化成功后追加节点。转化成功和图终止各有独立端口。 |
| 霜锋潮 | `Ground Origin → Spatial.Wavefront.TargetFound → Effect.Damage → Presentation.ElementImpact`。另用 `Started: Origin → Presentation.ElementPulse` 播放起手。 | 波前最大半径、速度、寿命、命中上限、伤害值、伤害元素、命中效果分支及独立表现。 |
| 潮印放电 | `Ground Origin → Query.SelectTargets.TargetFound → Effect.Damage → Presentation.ElementImpact`。 | 筛选状态、搜索距离、优先级、人数上限、伤害值、伤害元素和命中效果分支。 |

地形节点只接位置。地块的实际接触、周期伤害和移动影响仍由原地形系统结算。伤害节点接经校验的活目标，保留 `DamageResolved` 供后续的状态控制和反馈。地块跨技能选择存续，到期或来源生命失效时回收。Actor 和 Mass 由同一地块提供几何和类别，两套承伤路径沿用项目原规则。

开发图可在业务 `Origin` 输出上追加 `World.ElementSurface` 或 `World.IgniteSurface`。例如在「炽焰涌爆」的 `Query.Shape.Completed` 追加引燃，火伤后还会搜索该落点附近的自有毒地。「沼蚀毒床」则可从 `World.ElementSurface.Spawned` 接位置分支，只有实际创建地表后才运行。引燃后续从 `Transformed` 接出，空放只有 `Completed`。这两种世界动作由服务器执行，每位施法者最多持有 4 块临时地表。引燃在水平 3.6 米和上下 1.5 米内选择最近的一块，保留原地块的归属、位置、尺寸和截止时间。

地表类别、波前速度与上限、目标筛选各有独立配置。波前按此次施法保存的身份去重，达到上限后完成。选敌节点先过滤状态，再按策略排序和截取，所以近处的干燥目标不会占用名额。表现支路只消费位置或已结算命中。

## 实施与验收

- [x] 从生产目录和真实 T 键复现缺口，初次测试确认目录、毒地连招、冰浪和放电用例能捕获缺失接入。
- [x] 实现并登记有限地块创建和转化，校验来源、同一地块只转化一次和剩余寿命。
- [x] 复用波前、过滤查询和伤害，独立登记表现。以真实 T 试装和 Mass 波前上限用例验证。
- [x] 接入第四页、可信片段和卡片。Development Editor 构建成功，全项目 1074 项中 1073 项通过。唯一失败为已知的 9 个 Stander 时间轴缺少蒙太奇引用。

报告位于 `Saved/GAAuthoring/ElementalTerrainWave/Verification/index.json`。实际特效和双端表现仍待 PIE 验收。
