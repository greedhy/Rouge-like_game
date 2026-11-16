---
name: build-unique-equipment
description: Use when creating or validating fixed unique equipment (暗金), skill or spell-family variants, melee body effects, percentage tradeoffs, or tag-conditioned sets (套装) in the test_25d UE5 GAS project.
---

# 制作暗金与技能联动套装

在项目根目录工作，先读 `AGENTS.md`、`docs/unique-equipment-production-workflow.md` 以及本批设计文档。普通加法/随机词缀装备走 `build-equipment`；这里的固定暗金不重抽RollData。仓库 `skills/build-unique-equipment` 是维护源，个人安装副本同步后验证一致。

## 制作合同

每件列出：稳定ID、名称/槽位/已目视图标、正负属性与曲线倍率、真实GA路径和精确AbilityTag、套装不同部件Tag与唯一奖励消费方、与现有装备的组合冲突、测试及投放范围。

按实际能力选择配置或扩展：查看 `UniqueEquipmentGameplayEffect.h/.cpp` 和对应解析器，不把描述、Tag或其他规则结构中的字段当成已实现行为。技能扩展另读GA统一指南；保持原前端费用/动画，共享动作只改本次快照，不嵌套激活完整GA。

## 配置要点

| 内容 | 位置/规则 |
|---|---|
| 元数据 | `/Game/Equipment/DT_Equipment`；固定ID、不可堆叠、Soul交互拾取、不走UseEffect |
| 数值 | `/Game/Equipment/Unique/CT_UniqueEquipmentStats`；属性及技能FScalableFloat均引用曲线 |
| GE | `/Game/Equipment/Unique/Effects/GE_<ID>`；父类`UUniqueEquipmentGameplayEffect`，Infinite/无周期/不堆叠 |
| 属性 | 专用暗金允许Channel2 MultiplyCompound [0.05,10]；2是+100%，0.7是−30%。普通/随机GE保留Channel1加法 |
| 套装 | 每种部件自己的TargetTags，规则要求全部精确Tag；套装倍率替代普通倍率，不重复相乘 |
| 技能 | 真实前端传 `EquipmentAbilityTag`，共享节点读取本次局部快照。范围或雷链可选精确技能或已登记系列 |
| 近战表现 | `MeleeRule.BodyEffect` 和持续曲线在每次接触开头播放，组件挂到施法者脚下并到期清理 |

乘法不能凭空生成0基础属性。主属性会影响派生属性；暴击/穿透倍率不是最终伤害倍率。属性在装备/重建Spec时求值，技能在每次动作按Spec等级求值；在途弹体保留快照。冲突元素/命中特效转换当前会拒绝近战动作，新配装应避免冲突或明确扩展策略。

## 制作与更新

1. 读取现有表/曲线/GE及工作区修改，选择真实可用的图标、技能和特效。新增反射字段先成功构建，不能用旧DLL写新字段；出现编辑器锁定时请用户保存关闭。
2. 使用 `Scripts/Equipment/author_unique_equipment.py` 和 `Scripts/Equipment/unique_equipment_catalog.json`。检查每件的技能Tag、套装标签、转换类型和特效是逐件配置；旧样例若硬编码单套装，先扩展作者与验证逻辑，不能只追加ITEMS。
3. 作者清单提供缺失项初始值；现有表格负责后续调参。备份旧包，合并缺失曲线/GE/表行，逐行逐键比较保留值。已有资产结构不符时按制作流程“已有GE结构迁移”处理，不通过重建覆盖策划数据。
4. 作者运行成功后幂等重跑；另起进程设置`UNIQUE_EQUIPMENT_VERIFY_ONLY=1`，验证磁盘SHA256、目录和实际值。检查新的report.json及成功标记，不以退出码或文件存在代替验收。

系列归属查 `EquipmentAbilityModifiers::GetSpellFamily`。范围法术验证至少两个真实成员及无关技能，雷链验证额外接敌、原费用及取消后清理。近战周身特效要实测挥空、多人时单次播放、到期、卸装后下一次不再出现，以及与可同时穿戴装备的组合。含新系列或新表现的实例见 `docs/2026-11-16-unique-equipment-spell-builds.md`。

## 必须证明的行为

- 真实玩家初始化GE与装备共同存在时，正负属性、资源上限、独立卸除和重复恢复正确；服务器按实例持有GE，旧药水与随机装备保留。
- 真实输出或扣血体现规则；新增技能身份至少有真实GA→共享动作的接线测试，不能只手工填写Tag后测辅助函数。
- 单件、齐套、拆任意件、重穿/读档、其它技能隔离、已有装备组合均按合同；已发射弹体不追溯变动，取消后不补伤害。
- 执行装备/背包/相关动作及全项目回归，检查非零测试数、失败、未运行与日志新鲜度。并行修改不覆盖；旧失败需用历史证据区分。

试玩命令实现位于 `Source/test_25d/Private/Equipment/UniqueEquipmentSamples.cpp`，检查其实际覆盖目录及生成数；正式掉落、地图修改和自动发包按用户范围执行。交付装备表、调参入口、实际报告及待PIE/双客户端画面项目。NullRHI或资产读回不等于视觉/联机通过。此skill不要求提交或推送。
