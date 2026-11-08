# 核心调用链、同步与生命周期阅读入口

2026-11-06。本轮在30个cpp/h内增加说明，不改变运行逻辑。详细契约写在文件顶部，关键同步/清理步骤写在对应函数前。

| 阅读入口（Source/test_25d下cpp及对应h） | 重点说明 |
|---|---|
| Private/Player/MyPlayerState.cpp | 默认子组件所有权、ASC Owner/Avatar、Mixed复制、旅行快照转移 |
| Private/AbilitySystem/MyAbilitySystemComponent.cpp | 输入→GAS、服务端换槽、Spec复制与UI通知的区别、异步引用就绪 |
| Private/Equipment/EquipmentComponent.cpp | 请求→校验→事务→结果RPC/属性复制，双版本、GE与武器句柄回收 |
| Private/Backpack/BackpackComponent.cpp、Public/Backpack/ReplicatedInventory.h | 模型/VM创建与持有、FastArray标脏与接收后重建 |
| Private/Online/RoomSubsystem.cpp | GameInstance作用域、OnlineSession异步流程、超时和全局委托解绑 |
| Private/PureMass/PureMassSubsystem.cpp、PureMassCrowd.cpp | 权威实体与客户端快照边界、发布间隔、差量复制、插值及世界拆除 |
| Private/Roguelike/DungeonEnemyPool.cpp | 死亡→等待→休眠→复用；普通池账本与Actor寿命的区别 |
| Private/UI/Widget/DamageNumberPool.cpp、DamageTextComponent.cpp | 本地世界池、组件创建注册、弱/强引用、动画结束回收与真正销毁 |
| Private/AbilitySystem/Ability/StanderWeaponActionAbility.cpp、Private/Actor/StanderWeaponProjectile.cpp | GA对象与一次激活的不同寿命、取消清理、飞行表现复制及旧武器代次失效 |
| Private/MyAssetManager.cpp、MyGameplayTags.cpp | 引擎持有的AssetManager、静态标签值实例、初始化顺序与跨地图边界 |

## 阅读时的边界

- Server/Client RPC用于调用，Replicated属性用于状态传递；成功回执不等于相关组件状态已经同时到达。
- ForceNetUpdate请求尽早更新，不是立即送达确认。NetUpdateFrequency也不是每客户端的实际接收频率保证。
- 当前装备和背包字段没有OwnerOnly条件，不应把它们描述成仅本人可见；最终接收范围还受宿主网络相关性约束。
- WorldSubsystem、GameInstanceSubsystem、引擎管理器及静态C++值实例生命周期不同，不能统称为“全局单例”。
- UObject的Outer、GC强引用、Actor的Owner、网络所有权各有用途；注释按实际持有关系说明，不把它们混为一谈。
- 本轮是代码语义文档补充，不代表新增联机功能或完成多人运行验收。

## 验证

本轮基线、变更文件名单与token检查位于 `Saved/CppReadability/2026-11-06-network-lifecycle/`。忽略注释与空白后对比全部419个文件，确认无代码token变化；30个目标文件遵循项目clang-format配置。未为纯注释变更新增行为测试。
