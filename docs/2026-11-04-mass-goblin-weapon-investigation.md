# Mass近战哥布林武器排查：未完成视觉修复

后续状态：已按用户选定方案完成[制作阶段合并身体与长矛](2026-11-04-mass-merged-goblin.md)，六个 Mass 类型迁移到合并模型，双客户端截图已确认长矛正常可见。下文保留的是旧分体 GPU 附着路径的调查记录，该引擎附着问题本身未作修复。

已保存：DA_BenchmarkGoblin、DA_MassQAGoblin、DA_Batch6QAGoblin补齐既有SKM_MassGoblinSpear武器、插槽、偏移及动画时长配置，保留数值。资产备份与读回见Saved/GAAuthoring/MassGoblinDefaults/ed0698f959fe4dc9b8a45ac00990283f。

警告来自无模型的PlayerCharacter死亡测试夹具。新增LifecyclePlayerProbe在构造时使用无插槽挂接，仍继承真实Die/EndPlay；原生测试通过，本次日志无GetSocketInfoByName警告。

双客户端检查每端25个身体/武器、攻击预警、死亡移除和尸体回收全部通过，但截图仍缺少正常可见长矛。仅计数不足以验收。编辑器反复修改父组件属性后能显示长矛，包括BoundsScale保持1的对照；因此不能把原因确定为边界大小。候选武器边界覆盖、父边界放大及拓扑时MarkRenderStateDirty均未通过正常运行截图，已撤回生产渲染试验。

尚待完成：可靠的默认带武器渲染（当前资产仍是身体+独立长矛，不是合并网格），应进一步核对UE5.8 GPU附件父依赖/动画数据更新或制作合并骨骼网格。未声称已修复。所有诊断只在PIE运行，结束后不保留诊断参数。
