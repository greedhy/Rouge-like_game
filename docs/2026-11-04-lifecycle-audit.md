# 生命周期管理分类与静态审计（2026-11-04）

本记录检查项目原生代码中的持有方式、清理函数、委托/定时器和对象池。是静态审计，未在本轮运行故障注入、GC压力测试或逐蓝图潜在动作检查，不能作为全项目不存在悬空引用的证明。搜索索引：Saved/lifecycle-index.txt。当前不修改业务实现。

## 按管理方式分类

| 管理方式 | 底层原理 | 项目实例 | 释放与边界 |
|---|---|---|---|
| World/Level 管理 Actor | SpawnActor 建立世界中的 Actor，Destroy 结束玩法生命周期，内存之后由 UObject GC 回收；指针非空不代表 Actor 可用 | 玩家、怪物、投射物、DungeonRunDirector | EndPlay 收尾；世界卸载也会结束 Actor。Owner 用于网络等语义，不保证 Owner 销毁时所有 Owner 指向它的其他 Actor 自动销毁 |
| ActorComponent | 默认子对象或 NewObject 创建，RegisterComponent 建立世界参与关系；注册、激活、可见性、存在性是不同状态 | Mesh、ASC、装备组件、伤害文字 | DestroyComponent 注销并走组件销毁；Actor 清理也影响所属组件；仅隐藏不释放资源 |
| GC 强引用 UObject | 可达根经反射属性遍历引用；可达对象中的 UPROPERTY UObject 指针/容器参与 GC | 数据资产、WidgetController、InventoryModel、背包 VM | 不再可达后才可 GC；NewObject 的 Outer 表示归属/命名上下文，不能单独当作任意对象的保活保证；TObjectPtr 也需要正确的 GC 可见持有位置 |
| UObject 弱引用 | TWeakObjectPtr 通过对象索引及序列信息验证，不延长对象生命 | 怪物池条目、待归还列表、目标缓存 | Get/IsValid 后使用；不会保证对象仍属于同一局战斗或同一次池租约 |
| World Subsystem | World 的子系统集合创建与管理实例，Initialize/Deinitialize 与拥有者配对 | DamageNumberPool、PureMassSubsystem、ExtensionWorldSubsystem、MonsterManagementSubsystem | 每个 World 独立，PIE 多世界也独立；清理时不能假设其他子系统仍可用 |
| GameInstance Subsystem | 跟随本进程 GameInstance，通常跨普通地图切换存在 | RoomSubsystem | Deinitialize 移除网络/旅行/地图委托、Ticker 和 Session 委托；不能把旧 World Actor 强行当作跨地图有效对象 |
| 手动对象池 | 对象保持存在，通过状态机暂停并复用；池化释放是逻辑释放 | DungeonEnemyPool、DamageNumberPool | 必须区分活跃、尸体/归还中、空闲、真正销毁；旧回调要校验租约 |
| 固定槽位缓存 | 按下标重复更新对象，需求增大时扩容，多余对象隐藏或释放引用 | ExtensionPresentation.Labels、Backpack.SlotVMsTyped | 不是通用 Acquire/Release 池；SlotVMPool 虽有声明，实际背包重用在 SlotVMsTyped |
| C++ 值/RAII/共享引用 | 栈/成员析构自动释放容器；TSharedPtr 最后一个引用释放非 UObject 资源 | 房间模型、空间索引、JSON报告、FStreamableHandle | 不能用 TSharedPtr 接管 UObject；共享引用环可能泄漏。取消异步操作与释放 Handle 是不同动作 |
| Mass Entity Handle | 实体句柄由 EntityManager 验证，数据按 archetype/chunk 存储 | PureMassSubsystem.Entities | CreateEntity/DestroyEntity，处理期间使用 Defer；不是项目级 Actor 对象池。长期缓存 Fragment 地址在结构变化后可能失效 |
| GAS 句柄/任务 | ASC 拥有能力与效果；GE/Ability 句柄表示逻辑资源，Task 随能力结束或取消收尾 | ASC、GA、AbilityTask、地形效果 | Actor 仍有效不代表 Ability/Effect/Target 仍有效；必须释放效果、结束任务和撤销事件等待 |
| 网络副本 | 服务端与客户端拥有不同地址的实例，通过属性/RPC复制状态，不复制内存指针 | PlayerState ASC、怪物池生命状态、房间联机 | 客户端引用解析可能滞后；服务器销毁与客户端观察到销毁有时差；客户端表现需要容忍目标消失 |

## 已定位风险

### 高优先级：范围伤害委托捕获栈引用

Source/test_25d/Private/AbilitySystem/ExecCalc/ExecCalc_Damage.cpp:211 的 AddLambda 捕获 [&DamageTypeValue]。DamageTypeValue 是循环内局部变量；绑定保存在目标的伤害委托中，ApplyRadialDamageWithFalloff 结束后没有 Remove。首次同步广播可能正常，但之后广播会写入已经结束生命周期的局部变量地址，并且反复计算会累计订阅。这是代码层面的悬空引用缺陷，尚未运行复现。

建议：保存 FDelegateHandle，并在同步范围伤害调用结束时用作用域清理解绑；同时确认广播只需要覆盖这次同步调用。仅改为按值捕获无法正确回传衰减后的伤害。

### 高优先级：UI 订阅方可能短于发布方

OverlayWidgetController.cpp:36/45 等和 SpellMenuWidgetController.cpp:36/61 使用 AddLambda([this])，当前对应目录未找到成对解绑逻辑。ASC/PlayerState 在 UI 重建、某些旅行情形下可能比旧 WidgetController 活得更久。原生 Lambda 的裸 this 不会自动参与 GC 有效性检查，因此存在失效回调风险；重复绑定还可能造成重复 UI 通知。需运行重建/旅行验证，不能仅靠当前未崩溃排除。

建议：AddUObject、AddWeakLambda 或显式保存句柄并在解除依赖时解绑；重复绑定前先清理旧依赖。弱绑定避免访问已回收实例，但仍需处理仍存活的旧 UI 订阅。

### 中高优先级：死亡延迟回调

PlayerCharacter.cpp:90 的 BindLambda([this]) 在定时器到期时使用角色。当前 PlayerCharacter/BaseCharacter 检索未见该 DeathTimer 的 EndPlay 清理。若在同一个仍活跃 World 中提前销毁角色，裸 Lambda 不受 UObject 委托的弱绑定保护。ClearAllTimersForObject 也不能笼统保证清除匿名裸 Lambda。

建议：改用弱对象绑定并在 EndPlay 显式 ClearTimer(DeathTimer)；测试死亡等待期间重生、Destroy、旅行及连续 Die。

### 中优先级：空指针与生命周期问题分开

OverlayWidgetController.cpp 的消息回调直接解引用 GetDataTableRowByTag 返回值。缺少 DataTable/行时可能为空，与 GC 无关。需要缺失配置测试与空值判断。

## 对象池的实际清理边界

怪物池是 Director 内的弱引用账本。Acquire 限制同类、Idle、LastRoom 不等于当前房间；没有空闲则 Spawn，配额满时可生成非池化 Actor。死亡后先清理，再等待尸体阶段结束才标为可用。Sleep 取消 GA、清除能力/GE/Cue、松散标签、Actor定时器和潜在动作；Wake 恢复属性、重新授予能力并重置 AI。EndPlay 销毁池内 Actor/Controller。

这降低状态污染，但 ClearAllTimersForObject/RemoveActionsForObject 只针对对应对象，不自动清除技能实体、外部对象上的全部回调。旧投射物/持续效果可能仍持有有效的来源 Actor；需要生命序号检查而非只有弱引用。现有生命序号/攻击失效机制不能直接推定覆盖了所有 GA 路径。

伤害数字池是 WorldSubsystem，UPROPERTY 数组持有组件，Anchor 属于本地世界。蓝图结束时 DestroyComponent 被重写为 Release，隐藏、停动画、关 Tick；只有真正清理 World 时解除池化并销毁。当前不抢占尚活跃槽位，减少旧延迟回调误归还新数字的风险。仍应测试重复归还、手工提前归还、Anchor提前销毁及换图。容量下降不立即释放已保留的全部闲置对象，属于保留策略，不应直接判为泄漏。

## 运行时如何测试

建议增加仅 Development/Automation 启用的生命周期审计记录器，记录 InstanceId、WorldId、网络角色、类型、Spawn/Acquire/Return/EndPlay/Deinitialize 事件、LeaseId、时间和调用原因。记录器使用弱引用或纯 ID，不以强引用保活被测对象，否则会掩盖 GC 缺陷。对象地址可辅助日志，但不能作为稳定身份。

| 场景 | 操作 | 需要断言 |
|---|---|---|
| 真正销毁 | Spawn → Destroy → 等待帧/测试GC | EndPlay仅一次，弱引用失效，旧回调无业务副作用；不要求 Destroy 当帧内存释放 |
| 怪物两次生命 | 受击/状态/攻击前摇 → 死亡 → 归还 → 下一房间复用 | Lease改变，满血、标签/AI正确，旧伤害/死亡奖励不作用于新生命 |
| UI重建 | 同一ASC重建/关闭HUD多次，释放旧控制器引用并GC，广播属性 | 仅新UI响应，旧订阅不访问失效对象 |
| 范围伤害重复广播 | 执行范围伤害后再次触发目标伤害事件 | 临时委托数量恢复基线，没有写入旧栈变量 |
| 延迟回调 | 预约任务后提前销毁/取消/换图 | 不运行已失效业务，句柄和订阅归零 |
| 异步加载 | 请求A后切换B、销毁发起者，改变完成顺序 | 旧请求无法覆盖新状态；弱引用和Generation都匹配 |
| 数字池压力 | 高低负载循环、不同样式、重复Release、结束PIE | Active/Pending归零，Retained符合上限/高水位策略，样式无污染 |
| 网络 | 双客户端、断线、重连、旅行、延迟下目标销毁 | 服务端结算一次，客户端无失效目标解引用，不跨World持有旧实例 |
| Mass | 删除后访问旧Handle，处理期间延迟删除，重复清房间 | IsEntityValid拒绝旧实体，缓存Views/Grid移除，不跨结构变更保存Fragment地址 |

检测组合：现有 Automation 用例、带渲染 PIE 联机、Unreal Insights 生命周期事件/内存轨迹和可用的原生内存检测构建。计数只能检测泄漏趋势，不能证明没有悬空指针；弱引用有效也不能证明池租约正确。测试GC仅在隔离测试中使用，不作为正式游戏每帧策略。

已有基础：DungeonEnemyPoolTests.cpp、RandomEquipmentLifecycleTests.cpp，以及 Scripts/Performance/verify_damage_number_pool.py、verify_damage_number_aggregation.py、verify_damage_number_network.py。它们不等同全部对象覆盖，本轮没有重新执行。

## 本轮覆盖及剩余工作

已检视：原生生命周期搜索结果、怪物/数字池、UI订阅、死亡定时器、范围伤害委托、房间Subsystem清理、Mass实体清理及固定槽位缓存。尚需逐资产审计蓝图事件、Delay/Timeline、Niagara完成回调，以及逐GA外部效果与来源租约；尚未增加审计记录器或运行故障注入。建议先修复明确的栈引用问题，再补UI/定时器回归，最后扩展全场景实例追踪。不能把本轮静态检查称为全项目生命周期安全验收。
