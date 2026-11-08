# 原型与组合 GA 源码阅读地图

本说明对应原型组合功能族，不宣称覆盖全部游戏代码。各文件内有函数职责、框架调用链和网络/生命周期说明。
箭头表示控制流；任务回调、引擎 Tick、复制通知常发生于不同帧，不能把整个流程当作一条同步 C++ 调用栈。

## 从一个技能追到实际伤害

先查看 GA 的父类和 Prototype 引用，再决定执行器。共享 DA 是配置，Context 是一次施法的运行状态；
子节点组合生成新的行为 Runtime，不等于再次激活一个 GA，也不再次扣根节点的费用。

| 路径 | 文件位置（相对 Source/test_25d） | 关键顺序 |
|---|---|---|
| Actor 原型 | AbilitySystem/Prototypes 对应 Public/Private | PrototypeAbility::ActivateAbility → 目标任务 → OnTargetData → Execute/CommitAbility → PrototypeRuntime::Spawn |
| 混合原型 | PureMass/PureMassAbility、PureMassRuntime | TargetReady → ValidateCombatTarget → ExecuteMass → Spawn → BeginPlay/Tick → Hit |
| 第五批图 | AbilitySystem/PrototypeExtensions | ExtensionAbility::Execute → ExtensionRuntime::Spawn → Behavior 分支 → HitChild/PeriodChild/CompleteChild |
| 第六批分叉连锁 | AbilitySystem/Batch6/Batch6ChainAbility | ReceiveTarget → Commit → Queue/Visited 广度优先结算 → 复制连线段 |
| 第六批范围 | AbilitySystem/Batch6/Batch6FieldAbility | PureMassAbility 执行；PureMassRuntime 消费波前、跟随、减速与范围表现参数 |
| 第六批石化 | AbilitySystem/Batch6/Batch6Petrify | ActivateAbility → 混合范围筛选 → Actor 组件 Apply / Mass ApplyPetrify |

上述表中的目录同时存在 Public 声明与 Private 实现。核心示例同步栈：

```text
UPrototypeAbility::Execute
  CommitAbility（GAS 内部调用费用/冷却回调）
  APrototypeRuntime::Spawn
    UAbilityPrototype::IsValidConfiguration
    UPrototypeCastContext::ReserveSpawn
    FinishSpawning
      BeginPlay（世界已进入播放时）

后续独立的引擎更新：
APrototypeRuntime::Tick
  行为分支，例如 TickProjectile / TickChain / Pulse
    Hit
      UMyAbilitySystemLibrary::ApplyDamageEffect
      Emit(OnHit)
        Spawn(同 Context, Depth + 1)
```

## 数据图边界

- 旧图使用 Kind 与 OnHit/OnPeriod/OnExpire；共享 Context 的生成预算限制深度与总节点数。
- 第五批使用 Behavior 与 HitChild/PeriodChild/CompleteChild；ValidateGraph 拒绝旧连接混入，并检查路径环。
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

本轮只补充/修正注释，不修改函数签名、反射声明或运行逻辑。
基线与检查结果位于 Saved/GACommentAudit，20261108/verification.json 比较忽略注释后的代码 token。
构建记录位于 Saved/Logs/Build-GAComments-20261108.log；构建结果以该次实际结果为准。
