# 元素法术与状态组合

**交付范围** 人物属性面板第四页的本局免费试装，复用 T 键、权威目标校验、一次 GAS 提交和原图执行器。新增四个技能，各自使用新的身份。

| 技能 | 默认配置 |
| --- | --- |
| 润泽雨幕 | 半径 3.2 米，立即与随后每 0.75 秒各扫描一次，共 3 次，每次最多 8 敌。应用水元素反应，零伤害。 |
| 炽焰涌爆 | 固定落点，0.45 秒后在半径 2.8 米内造成基础 36 点火伤。 |
| 雷鸣骤击 | 在落点半径 3 米内造成基础 28 点雷伤，潮湿或蒸汽触发既有导电。 |
| 寒晶爆发 | 在落点半径 2.5 米内造成基础 24 点冰伤，潮湿或冻僵触发既有冻结。 |

灵感来自元素相互转化。这批通过限时定点脉冲和目标状态串联，地形资产仍使用现有接触系统。[Larian 地表转换文档](https://docs.larian.game/Osiris/API/TransformSurfaceAtPosition)

- [x] 先跑真实目录和 T 键回归，新增用例确认能检测缺失的四个技能。覆盖雨幕零伤害、进入范围、冰冻、导电和点燃延迟。
- [x] 在 `AbilityElementalExposure` 共用单槽反应，用独立 `Status.ApplyExposure` 为当前 Actor/Mass 生命施加元素。复用既有状态时间和取消机制。
- [x] 用 `Control.Repeat/Control.DelayOrigin → Query.Shape → Effect.Damage 或 Status.ApplyExposure` 编成四个原生默认图。表现用同一次查询的落点和半径。
- [x] 登记状态元素、范围、频率、引信和命中后状态条件卡。开发图提供类型化参数与可信施加片段，并验证实际编辑项投影。
- [x] Development Editor 构建成功。全项目运行 1068 项，1067 项通过。唯一失败为已知的 `AllConfigurationAssets`，其中 9 个既有 Stander 时间轴缺少蒙太奇引用。新增元素、真实 T 键、图节点编辑和两项目录核对均通过。

最终报告位于 `Saved/GAAuthoring/ElementalWorkshopWave/Accepted/index.json`。实际特效与双端表现仍待 PIE 验收。
