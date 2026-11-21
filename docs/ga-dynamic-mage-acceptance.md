# 动态法师技能：PIE验收操作

本轮入口是开发测试面板与原技能输入。正式符文掉落、三选一和局内存档不在本轮范围；不需要为每种组合新增GA类或保存组合资产。自动化、实际游戏与联机证据见[交付记录](ga-dynamic-mage-delivery-evidence.md)，最终动画、粒子可读性和操作手感仍由用户验收。

## 开始和退出

1. 在现有法师试玩入口进入已选择Mage职业的玩家。控制台输入 `RuntimeRecipe` 打开面板。
2. 选择实际基础技能及其类版本，点击开始预览。Actor与Mixed版本是独立目录行，优先选择当前游戏模式对应版本。
3. 选择实际节点、修改模板、有限参数以及适用的片段/端口，逐项提交，确认版本递增且没有编译错误。数字修改后检查提交值保持；多个Scope时按原片段名和“分发容器/单颗弹体”说明选择。
4. 点击隐藏面板后，用原鼠标目标和 `T` 施法。渠道技能按原语义松开，开关技能按原语义再次按键。Orbit还需要原进攻技能的攻击信号，可使用原左键技能。
5. 再次输入 `RuntimeRecipe` 重显同一面板，可逐项移除修改。关闭会等待准确回执，再归还本预览的临时技能和资源；原技能槽、等级、装备与存档不因预览改变。

提交被拒绝时，上一份有效配方继续使用。应依据错误修正节点、端口、输入类型或参数，不修改共享DA来掩盖错误。

## 建议先验的三个组合

### 火球逐跳与状态

选择原 `GA_FireBolt`，在 `Delivery.ProjectileAwait` 使用 `Propagation.Sequential`，例如总目标数3、命中后等待0.5秒、下一目标半径400cm、每跳伤害保留比例0.8。再在同一投递节点的真实 `Hit` 端口追加 `Target.Petrify`，持续1.5秒。

对三个分开的敌人施法：第一颗真实命中后才出现下一颗；下一颗从前一命中位置出发，并继续等待实际命中。检查三次表现、伤害衰减、石化位置、一次原施法费用，以及撞墙或没有下一目标时的收尾。A被击杀后仍可继续选B；旧目标池复用的新生命不能被旧命中当作原目标。

### 三支追踪或穿透弹体

选择Mixed `GA_Prototype_Multishot`。在分发容器Scope设置Count3、Spread0。追踪方案还需在 `Distribution.PrototypeMultishot` 显式设置 `Set.PelletTargetPolicy=1`，再在单颗 `Delivery.PrototypeFlight` 设置Homing运动；该目标策略保留本次已选中的原生命。对单颗飞行的 `Contact` 追加 `Target.Petrify`。

穿透方案保持默认目标分发策略，设置单颗飞行的Pierce接触策略、ImpactLimit3和DamageFalloff0.5，再追加同一状态。对排成一线的敌人，应瞄向远端目标，让原三维射线穿过前面的敌人；地面和墙体仍会真实阻挡。分别检查三颗独立弹体、真实接触次数、状态、衰减和取消后的资源。

### Emitter替换为短光束

选择Mixed `GA_Prototype_Emitter`，在实际Emitter Scope设置Count3、Period0.1秒。对 `Control.PrototypeEmitter.Launch` 使用 `Replace.RegisteredBranch`，片段选 `Prototype.Beam`，持续0.3秒、周期0.1秒。光束射程和资源来自可信Beam原资产。

应出现三次独立短光束和原光束表现，旧投射物分支被替换。检查发射原点、朝向、命中、混合与取消；提前关闭预览不能继续发射或留下活动宿主。

## 其余代表场景

| 场景 | 主要编辑点 | 人工检查重点 |
|---|---|---|
| Field | 实际Scope的Radius/Duration；FieldCadence的Period接Area.Petrify | 范围与表现一致，范围外不伤，周期和正常结束正确 |
| Trap / Delay | 实际ArmDelay；Action或Expired接Area.Petrify | 布防/等待与效果独立，取消不补发正常到期效果 |
| Attach | 附着Scope周期、持续和转移；Period接Target.Petrify | 跟随原生命，转移及失效清理正确 |
| Stack | 选择实际叠层孩子Scope；活目标State.PrototypeStack的Threshold接Area.Petrify | 阈值前不触发，两次施法正确共享叠层，正常/致死分支不重复 |
| Move | 实际位移参数；Completed接Area.Petrify | 真实水平移动、阻挡与取消结果、完成位置和状态一致 |
| Wave | 实际半径、前沿速度和持续；TargetFound接Target.Petrify | 空间推进、一次接触和旧生命重验正确 |
| Shards | Effect.ShardDamageBatch替换为状态 | 点阵顺序与原Notify、费用保持，替换后没有隐藏原伤害 |
| Channel | Effect.ElectrocutePeriod替换或追加状态 | 原费用持续，纯效果不在64次后截断；松键、来源/目标失效收尾 |
| Orbit | 接触后的独立状态；原左键攻击触发 | 原库存、补充、费用和攻击信号保持；停用后准确清理 |

## 在途修改与双人检查

同一预览先施放逐跳伤害火球，在其尚未完成时，通过面板把载荷改成状态；旧Root仍按旧图完成，新施法使用新版本。新状态链在飞行中移除修改，不能取消已经接受的在途版本；下一次施法恢复原单发。检查版本/摘要、真实伤害、旧Burn自然到期、状态和费用，不能靠手动清GE来得到结果。

Listen Server由远端玩家操作，分别观察服务器结算、客户端投射物/范围/HP/状态与另一玩家配方隔离。涉及多个撤回时，应等待每笔新版本回执后再提交下一笔。世界时间类技能继续保留现有单机限制。

验收反馈建议写明：基础类版本、节点/模板、参数、目标位置、操作顺序、期望和实际表现。这样可以区分配置、原前端时点、动作服务、表现同步与对象生命周期问题。
