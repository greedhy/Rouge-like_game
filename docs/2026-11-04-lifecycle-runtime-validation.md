# 生命周期运行追踪：第一阶段

新增 Scripts/Lifecycle/trace_pie.py：30秒、有界JSONL观测；每约100ms枚举PIE世界的Actor、ASC、DamageTextComponent、DamageNumberPool，保存类型、世界、角色、隐藏/Tick状态、可读取的池生命编号和数字池计数。跨帧仅保留字符串/标量，不保留被测UObject强引用。记录 observed/changed/no_longer_observed。

这是状态采样器，不是析构钩子或完整内存诊断器。对象路径不是跨重建的唯一实例身份；短于采样间隔的生命可能漏检；no_longer_observed不代表内存已经回收。不能拿此机制证明没有UAF。

## 本次真实测试

修正Python接口名称后，记录275次采样、1141个事件；结束PIE后remaining=0。首次运行接口错误，失败证据保留，不计入成功。

原生 test_25d.Roguelike.Pool 六项通过：BlackboardLeaseIsolation、CapacityAndQuarantine、DeathDoesNotDestroy、GasResetAndReuse、PerformanceReport、RealBlueprintRagdollRestore。覆盖多轮复用、GAS清理、黑板生命隔离、同房间禁复用、真实蓝图布娃娃恢复等既有断言。

verify_damage_number_aggregation.py 七项通过：目标销毁后保留数字、分组总额、样式分离、归还、预留及复用后的颜色恢复。结束后恢复L_RoguelikeRun。

证据目录：`C:\Users\greedy\Documents\Unreal Projects\test_25d\Saved\Lifecycle\e02fedb1de894ec5aac7d02db35abfcc`。

## 尚未完成的验证

范围伤害栈引用委托、旧UI订阅、死亡裸this定时器还没有加入原生故障测试；不得标为已运行证实或已修复。本阶段没有改动业务实现。异步乱序、网络旅行、GC强制回收、Mass过期句柄和所有GA外部生命周期仍待专门测试。

后续需补Development原生事件钩子，以弱对象索引/序列号和LeaseId形成身份，追踪委托绑定/解绑、计时器预约/取消/执行。在隔离进程测试实际范围伤害调用前后订阅是否残留；清理临时订阅后退出，避免在主编辑器主动写失效栈内存。UI测试应销毁旧控制器并GC后广播，死亡测试应提前销毁角色后推进定时器。测试应先在旧实现暴露失败，再验证修复。
