# 怪物眩晕表现（2026-11-16）

眩晕继续由现有 `Debuff.Stun` / Mass `StunRemaining` 控制。星星只是状态表现，不新增 GA、不另设表现倒计时，也不修改弹反的眩晕时长和世界慢动作参数。

## 当前动画

- 哥布林、食尸鬼、恶魔、萨满的动画蓝图继承 `ABP_Enemy`，其中已有 `Stun` 状态。PIE 已观察哥布林、食尸鬼在眩晕时切换姿态，恢复后回到待机。
- 石头人、蘑菇和 Grux 的独立动画蓝图目前没有对应的专用眩晕状态；眩晕阻止移动和攻击，动画继续按原蓝图播放。PIE 已观察石头人、Grux 保持待机。
- Mass 眩晕取消攻击并回到 Idle；本次没有增添动画银行条目。
- 本次没有修改动画蓝图、动画片段或蒙太奇。

## 星星资源与尺寸

基线检查发现 16 个 EnemyCharacter 蓝图中有 8 个扩展怪物没有绑定 `NS_Stars`。旧版统一的相对高度 `Z=50` 也不适合不同体型。

当前 16 个蓝图（含基础类、旧版和扩展版）以及 2 个 PureMass 类型统一使用：

`/Game/BluePrints/Characters/Effects/NS_Stars_EnemyStun`

它复制自 `/Game/Assets/Effects/Stun/NS_Stars`，保留星形、颜色和原有循环，发射器改为 **Local Space**。共享源资源保持不变。画面检查发现原始世界空间粒子会飘散，因此副本在本地空间缩小轨道，保持星形精灵可读。

Actor 和 Mass 共用 `UDebuffNiagaraComponent::MakeStunTransform`：

- 体型系数 `S = Clamp(世界胶囊半径 / 34, 0.65, 2.0)`。
- 特效中心为胶囊中心向上 `世界胶囊半高 + 12 × S` 厘米。
- 特效世界缩放为 `0.15 × S`，适配小怪、精英和 Boss。
- Actor 每次进入眩晕重新计算，随根组件移动；Mass 随 Crowd 的显示位置插值更新。

新增怪物沿用组件并绑定这个副本即可。调整其他粒子模板时应重新检查轨道大小；这个缩放基准针对该 `NS_Stars` 副本。

## 生命周期与复制

Actor：`ASC 标签变化 → DebuffNiagaraComponent → 头顶变换 → Activate / DeactivateImmediate`。

- BeginPlay 或延迟注册 ASC 时读取当前标签计数，兼容先收到状态后建立组件的时序。
- 多个眩晕效果重叠时，最后一个标签消失才关闭星星；不重复创建组件。
- 眩晕结束、死亡立即清除循环粒子；EndPlay 解除 ASC、死亡和延迟注册委托。
- 对象池仍走现有 GE 清理和 Niagara 停用流程；下一生命不继承星星。
- 燃烧等其他 Debuff 保留原来的渐退方式。

Mass：片段发布 `bStunned`，Crowd FastArray 传输到客户端；仅在眩晕时建立本地 Niagara 组件。恢复、死亡、实体移除、批次切换和世界退出均销毁它。没有为 Mass 实体创建额外 Actor。

持续时间跟随真实状态。世界减速时，GE、Mass 状态和粒子均受世界时间影响，不会因独立实时时钟提前熄灭。

## 制作与验收

脚本位于 `Scripts/GA/StunStars/`；基线、逐次备份、保存哈希、编译日志、测试与画面证据位于 `Saved/GAAuthoring/StunStars/20261116/`。

- `author.py` 仅配置固定清单中的 16 个蓝图、2 个 Mass 类型并保存专用副本，校验共享源未改变。
- `verify_assets.py` 在不同 Unreal 进程读回绑定、眩晕标签及实际保存哈希。
- `test_25d.StunPresentation` 覆盖已有标签的初始同步、重叠、到期、死亡、EndPlay、两次池生命、大小与 Mass 快照往返。
- 真实渲染运行额外检查 Niagara 激活/停用；NullRHI 运行不声称验证画面。
- 临时 PIE 场景验证哥布林、食尸鬼、石头人、Grux：2 秒和 5 秒眩晕仍显示，8 秒恢复后清除；检查后恢复原地图和视角，不保存预览场景。

远端多人画面和 Mass 实际画面尚未做人工验收。Mass 绑定、状态复制字段与现有弹反/实体控制回归有自动化覆盖。用户在正式地图中的最终视觉验收仍独立记录。

最终完整构建：`Saved/GAAuthoring/StunStars/20261116/build-final-03.log`。
相关自动化 27/27：`Saved/Automation/UnifiedGA/20261116T131630-4e518109/index.json`，对应验证器确认源文件和 DLL 输入哈希前后一致。
D3D12 表现测试 3/3：`Saved/GAAuthoring/StunStars/20261116/render-tests-final/index.json`。
独立进程资产读回：`Saved/GAAuthoring/StunStars/20261116/asset-verification.json`；读取旧 Grux 骨架产生内存脏标记但没有保存它，原始 NS_Stars 哈希不变。
