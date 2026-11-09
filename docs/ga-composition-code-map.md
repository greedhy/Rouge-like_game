# 原型与组合 GA 源码阅读地图

当前开发入口为 [统一制作指南](ga-unified-authoring-guide.md)，架构、网络与生命周期合同见 [框架说明](ga-framework-reference.md)。箭头表示控制流；任务回调、引擎 Tick、复制通知常发生于不同帧，不能把整个流程当作一条同步 C++ 调用栈。

## 从一个技能追到实际伤害

先查看 GA 的父类和配置引用。共享 DA 是配置，会话属于一次激活，图实例属于一次执行。旧 DA 通过兼容编译器生成同一 Schema 2 图；子节点不再次激活完整 GA，也不默认重复扣根节点费用。

| 路径 | 文件位置（相对 Source/test_25d） | 关键顺序 |
|---|---|---|
| Actor 原型 | AbilitySystem/Prototypes、Unified/LegacyActorAbilityDriver | PrototypeAbility → 目标/费用 → LegacyGraphCompiler → GraphInstance → LegacyNodeExecution → Actor Driver |
| 混合原型 | PureMass、Unified/LegacyMixedAbilityDriver | PureMassAbility → 混合目标/费用 → 兼容图 → Mixed Driver |
| 第五批图 | AbilitySystem/PrototypeExtensions、Unified/LegacyExtensionAbilityDriver | ExtensionAbility → 兼容图 → Extension Driver → 原状态子系统 |
| 第六批分叉连锁 | AbilitySystem/Batch6、Unified/Nodes/AbilityBatch6NodeExecutions | Batch6ChainAbility → 目标/费用 → Delivery.ForkedChain → BFS/伤害/连线段 |
| 第六批范围 | AbilitySystem/Batch6/Batch6FieldAbility | PureMassAbility → 混合兼容图，保留波前/跟随/减速参数 |
| 第六批石化 | AbilitySystem/Batch6、Unified/Nodes/AbilityBatch6NodeExecutions | Batch6PetrifyAbility → Status.PetrifyArea → Actor 组件 / Mass 状态服务 |

上述表中的目录同时存在 Public 声明与 Private 实现。核心示例同步栈：

```text
UMyGameplayAbility::PreActivate：本次会话及来源生命
GA 目标确认 / 原费用时点
  UAbilityLegacyGraphCompiler::Compile 或指定 Schema 2 图
  UAbilityGraphInstance::StartWithInput：校验/冻结/实例化策略
    Legacy.Mixed.Burst：实际命中
      OnHit -> ConfirmedImpact（原目标生命与历史位置）
    FIFO 事件队列
      Target.ResolveImpact：验证原目标仍属于同一生命
        TargetFound -> LiveTarget
          Status.Petrify：原 Actor 组件 / Mass 状态服务
```

## 数据图边界

- 旧 Kind/Behavior 与子引用通过兼容编译器冻结并转为图节点/边；旧资产仍按原规则校验。新组合使用类型化端口，不硬填跨族旧指针。
- 原 Runtime Actor 保留表现、稳定资产复制与兼容接口；实际行为在 Driver，禁止再复制 Kind 执行循环。旧递归调用改为有界 FIFO，不保证全部同步观察者顺序不变。
- Batch6 分叉连锁有自己的广度优先预算，不是第五批 Chain 节点的同一实现。
- 混合目标 Hit 先 Resolve，再分别进入 Actor GE 或 PureMass 子系统伤害；Niagara 不决定命中。

## 网络与资源归属

目标任务通过 GAS SpecHandle/PredictionKey 关联请求；客户端鼠标数据不是服务器已经认可的命中。
PureMassTargetData 消费缓存前保留共享 Handle，并在 OnDestroy 移除自己的委托句柄。
传统 TargetDataUnderMouse 未提供同样的自定义 OnDestroy，不应在注释中声称它已经显式解绑。

Runtime 的 Context 通过 UPROPERTY 持有且留在服务器；配置、端点及结束标志等字段复制供客户端表现。
瞬时命中特效的不可靠多播不是伤害同步协议。有限 Runtime 可晚于 GA 结束；Beam/开关技能则保留取消关系。

叠层记录由 ExtensionWorldSubsystem 持有：GA 正常结束不删除叠层，子系统 Tick 过期或引爆，
CancelCasterEffects 可按来源主动清理，Deinitialize 清理世界退出资源。
Actor 位移由 RootMotionSource 交 CharacterMovement；Mass 位移由 ExtensionDisplacementProcessor 调用 MoveMass。
石化组件复制控制表现状态；减速组件的 Entries 不独立复制。两者均检查 Enemy 对象池 Lease，防止旧生命状态污染复用对象。

## 注释变更验证

`Saved/GACommentAudit/20261108/verification.json` 只证明当时纯注释整理。本轮包含行为迁移，不能沿用 token 比较作为迁移验收；请使用当前构建、全量自动化和独立资产读回证据。
