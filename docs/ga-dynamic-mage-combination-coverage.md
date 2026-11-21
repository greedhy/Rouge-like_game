# 动态法师技能：最低组合验收集

本表对应[批准设计](superpowers/specs/2026-11-19-dynamic-mage-composition-design.md)的11类最低场景，区分公开配方解析、实际动作、真实面板和网络。实际原类默认复原另见[逐类清单](ga-dynamic-mage-inventory.json)，构建及报告版本见[交付证据](ga-dynamic-mage-delivery-evidence.md)。每种合法参数排列不在穷举承诺内；非法参数、错误端口和没有资格的派生应在提交前拒绝。

## 已取得的组合证据

| 组合 | 已验证行为与主要测试族 | 真实面板／游戏证据和边界 |
|---|---|---|
| 火焰箭逐跳 | NativePropagation、NativePreviewPhase：原TargetData/Notify、A命中后等待、从历史位置发B、等待B真实命中后再发C；原类、费用、衰减和状态保持 | UMG Sequential；另有远端逐跳报告。不是激活另一个完整GA |
| 传播边界与分叉 | NativeForkedPropagation、Delivery：致死后继续、FIFO、共根额度、阻挡/超时、重复回执、排队目标换生命与来源失效 | 专用BFS通过可信配方与原Notify验证；不把原Fission的同时Volley算作BFS，也不声称每个BFS负例都经过屏幕操作 |
| 散射＋追踪／穿透＋状态 | CrossAxis、TypedTemplate、NativeOverlay：数量/散布、单颗运动与接触、原生命目标策略、三颗carrier、每颗穿透衰减、状态与取消 | UMG ScatterHoming、ScatterPiercing；远端原生火球3发、碰撞半径64双端一致。追踪需显式Set.PelletTargetPolicy=1 |
| Emitter替换投递 | EmitterBranchReplacement：实际Launch口替换可信Prototype.Beam、3次真实短束、原资源/射程、无旧投射物双发、准确回收 | UMG EmitterBeam与真实RHI Niagara检查；有限3发不代表无限物理生成 |
| Field／Trap／Delay改载荷 | CrossAxis、Cadence、PrototypeSustained：原时钟、ArmDelay、区域状态、正常到期和取消、范围外不伤 | UMG Field、Trap、Delay；远端固定／跟随场半径450。Follow Contact伤害后状态追加在最终全量325周期、远端及单机补测分别通过 |
| Attach周期与转移 | StateRuntimeBranch、StateComposition、TypedState：周期状态、Actor→Mass转移、原生命、转移额度、失效及取消 | UMG Attach；不将代表资产的本地验证扩大为每个Attach资产的所有网络转移负例 |
| Stack阈值效果 | StateRuntimeBranch、StateComposition：跨施法共享所有者、阈值与延迟、致死历史位置、来源退休、无二次隐藏伤害 | UMG Stack使用实际孩子Scope及活目标TargetFound分支；原生命复用仍按原状态服务处理 |
| 位移完成后区域 | StateRuntimeBranch、Movement、TypedState：真实水平位移、准确Completed位置、墙/替换/RootMotion清理，取消与正常完成分开 | UMG Move；运动阻挡服务证据不冒充每个具体前端的完整阻挡后续规则 |
| Wave／Shards换状态 | WaveShardRuntimeBranch、WaveNode、Shards：原查询/点时钟、同step接触、效果回调后的几何重验、真实Notify、取消和池新生命 | UMG Wave、Shards；状态替换不自动扩大原资产的Actor/Mass支持声明 |
| Orbit／Channel与长期纯效果 | NativeElectrocute、NativeOrbit、PrototypeSustained：325周期、原周期费用、库存/攻击信号、纯状态替换和追加、退出准确归还；非法异步/物理后继拒绝 | UMG Channel、Orbit；保持原松键/开关/左键攻击语义。新增纯效果不因有限64调用额度截断原持续宿主 |
| 版本、隔离与撤回 | RuntimeRecipe、NativePreviewPhase、Lifecycle：事务回滚、旧Root冻结、新施法新图、撤回不取消已接受在途图、下一次默认、独立玩家和来源生命周期 | Standalone及远端真实UMG在途版本报告成功；每笔Remove等待双端revision/hash回执，旧Burn和状态自然结束，不手动清GE |

Standalone完整面板批次为 `Saved/GAAuthoring/DynamicMage/UMGScenes/c657575f02e94822aeeaf697718c4f9a/report.json`，14/14成功。该批逐项检查Apply后的实际数值，修正过的输入桥没有把请求值回写为1。它绑定090852二进制，不可冒充以后任意源码改动的重新验证。

Standalone在途版本 `d8d94c29dacc48f39c3aae6affc74370`、远端面板基础 `751745c94dc440d4a317438c29f0b410`、远端命令版本 `f5b05015c69b47bda06d2511c946477d`、远端面板版本 `5d53a85ac22b4675a885948ea4f74b11` 均通过并完成自身资源归还。个别早期失败保留在原报告中，不修改为成功。

## 后续开发如何延续

新组合沿“实际消费配置→共享动作→类型化端口→来源与目标原生命→生命周期／预算→旧对照与新行为”的单一路线。跨族追加先验证公开端口是否真正发布，不能仅依据声明名连接永不发生的事件。同步状态资格不能传播给新的异步时钟或物理宿主。

每个新规则须记录基础类版本、节点与端口、已提交参数、图版本／摘要、实际效果和清理结果。默认原类真实施法smoke、数值／时序对照、面板操作、真实网络和用户视觉是不同层次的证据；不互相替代。最终动画、粒子可读性和手感按[PIE验收步骤](ga-dynamic-mage-acceptance.md)由用户确认。

最终修订验证：全量 `100009-47fec444` 的1015项中1014通过（仅已知Stander引用基线失败），真实RHI `100137-3bd3175f` 5/5。最终DLL远端7/7、远端基础UMG43项、Standalone Follow32项与基础UMG38项成功并清理。受影响Follow同步解析资格已按最终DLL实测，未将此前14组合记录改写为新DLL重跑。最终1520个验证输入文件0变化，用户视觉待验。
