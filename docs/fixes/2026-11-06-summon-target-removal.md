# 萨满召唤失败导致遭遇误判失败

## 证据与原因

用户在击杀怪物时观察到 `TargetRemovedWithoutKill:A33`。本次日志在
`2026.11.06-10.47.04:861` 记录 `BP_DemonWarrior_C` 出生碰撞失败，随后同一帧还有召唤重试，
下一帧报告房间失败。A33 是遭遇账本中的 Actor 编号；旧日志未记录编号到 Actor 的完整映射。

`ShamanEnemyAbility -> AShamanEnemyCharacter::Summon -> ADungeonRunDirector::SpawnSummonedEnemy`
原先在 `FinishSpawning` 前登记可选目标并绑定 `OnDestroyed`。UE 在 `FinishSpawning`
中的组件初始化阶段检查出生碰撞，无法调整时同步 `Destroy()`，因而触发
`OnActorDestroyed -> ResolveTarget(Unexpected)`，把正常的召唤位置失败写入遭遇错误。
后续重试也因账本已有错误而被拒绝。此路径与日志吻合；用户当时具体击杀的对象尚未确认。

## 修复

- 服务器先设置遭遇身份与召唤来源，再完成生成。只有仍有效、存活且属于当前房间的召唤物才登记目标与回调。
- 碰撞失败返回空指针；普通地图的萨满召唤也检查 FinishSpawning 后对象有效性，不计入成功数量或绑定死亡回调。
- 召唤物成功登记后的死亡、来源回收及异常销毁仍沿原账本规则处理；未将异常销毁自动算作击杀。
- 真正异常销毁额外记录目标编号、Actor、类名与房间，便于下一次定位。
- 本次不修改资产、伤害、预警、动画或复制协议；所有账本操作仍由服务器执行。

## 验证状态

新增 `test_25d.Shaman.SummonSpawnLifecycle`，覆盖真实出生碰撞拒绝、成功登记、死亡后销毁、
来源回收及异常销毁保护。该测试不启动 AI/BeginPlay；运行世界中萨满完整施法与击杀表现仍需 PIE 验收。

2026-11-06 用户关闭编辑器及调试器后，完整 Editor 构建与链接通过。
构建记录：`Saved/Logs/Build-SummonSpawnLifecycle.log`。

`test_25d.Shaman` 与 `test_25d.Roguelike.Model` 共 8 项测试全部通过（5 项无警告、3 项带警告，0 失败）。
报告：`Saved/Automation/SummonSpawnLifecycleFinal/index.json`；日志：
`Saved/Logs/Test-SummonSpawnLifecycleFinal.log`。
新用例实际记录出生碰撞失败，随后验证账本没有错误、没有遗留目标、可继续成功召唤。
用例最后故意销毁已登记的活目标，确认仍触发异常移除保护；该警告是预期结果。
原生空模型夹具另有既存 WeaponHandSocket 警告。

首轮单块阻挡被引擎自动调整出生位置绕开，测试未触发预期拒绝而失败；修订为两侧相反脱离方向的
阻挡结构后重跑通过。未为使测试通过而放宽断言。

待用户 PIE 验收：在原房间场景验证萨满附近阻挡、正常击杀召唤物、击杀萨满清理召唤物
均不使房间误判失败。本次自动化不等同于完整动画/AI 战斗试玩。
