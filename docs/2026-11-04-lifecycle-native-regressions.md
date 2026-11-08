# 原生生命周期回归与修复

## 范围

按用户要求实现四类测试：真实范围伤害GE临时委托、UI控制器订阅及GC、玩家死亡定时器提前销毁、怪物两次生命与旧外部回调。怪物遵循 monster-development-standard.md，保留已有Actor/GAS及对象池。无资产修改，无Git提交。

## 失败证据与修复

- RadialSubscriptionScope：真实GE_Damage执行三次，每次伤害实际减少生命值；旧实现每次留下捕获局部变量引用的委托。测试清除残留后再迭代，不故意广播失效引用。修复用作用域退出解绑本次句柄。
- UIBindings：旧Overlay/Spell控制器8项绑定身份/解绑断言失败。持久回调改为AddWeakLambda，新增显式解绑接口，在重复绑定、替换依赖及BeginDestroy时调用；替换参数清除类型缓存。新增动态委托接收者，验证重复Bind只通知一次、旧ASC不再通知、真实GC后旧控制器失效且广播不再通知。GC行为主要以Overlay代表验证，Spell验证绑定及解绑。
- DeathTimerEarlyDestroy：初次夹具只DispatchBeginPlay，未设置Actor初始化状态，不能用该失败认定业务缺陷。已补完整组件初始化并断言IsActorInitialized及Destroy后BeginPlay结束，再对原实现单独复测，仍仅在定时器残留断言失败（DeathRed/index.json）。修复EndPlay显式ClearTimer，回调改BindWeakLambda；最终测试推进世界时间超过原30秒期限。
- PoolOldCallback：第一条生命开始攻击并预约不属于Actor绑定的外部计时器，死亡、休眠、复用并开始新攻击后推进计时器。旧回调实际执行，但IsAttackCurrent拒绝旧Lease/Serial，旧FinishAttack不结束新攻击。生产怪物代码无需修改。最初测试只Tick一次未激活pending timer，已修正多帧推进，不能把这个测试驱动错误视为业务缺陷。

## 入口与证据

新增LifecycleDeathTests.cpp、LifecycleUITests.cpp和LifecycleUIProbe.h；范围伤害复用PureMassMixedTargetTests.cpp的真实GAS夹具；怪物测试加入DungeonEnemyPoolTests.cpp。命令前缀：test_25d.Lifecycle；同时回归test_25d.Roguelike.Pool。

旧实现报告Saved/Lifecycle/Red/index.json；正确死亡夹具的旧实现报告Saved/Lifecycle/DeathRed/index.json；最终报告Saved/Lifecycle/Final/index.json。不要用进程退出码替代Automation JSON状态，当前环境失败测试进程也可能返回0。

## 边界

测试在隔离UnrealEditor-Cmd进程、NullRHI下执行，证明相关原生生命周期行为，不代表视觉或网络旅行验收。旧回调使用真实TimerManager及生产IsAttackCurrent/FinishAttack，仍不能证明所有GA外部效果都正确校验租约。

未覆盖：目标在范围伤害同步调用中被销毁的特殊路径、所有UI类别（AttributeMenu还有同类裸捕获）、消息DataTable缺失、完整双客户端旅行/断线、全部蓝图潜在动作。本轮不把这些项标记安全。原生测试是定向故障复现，不是通用分配器追踪系统。

## 最终结果

Editor Development编译成功。最终Automation JSON确认10/10 Success：四项Lifecycle与六项Roguelike.Pool。源码修复后没有修改测试预期来掩盖业务失败；两处夹具修正已在上文说明。编辑器重新启动到L_RoguelikeRun。
