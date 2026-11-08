# 活跃伤害数字：UI 开销拆分、合并与预算

## 结果与证据

最终版本在 L_MonsterBaseline 单人 PIE 进行五组、各三轮对照。每秒 500 条 Actor 命中显示请求，同一目标，90% 普通、10% 暴击；每轮 3 秒预热、5 秒 CSV 采样、3 秒排空。每组累计 12000 条输入，统计量为三轮采样帧合并结果。池化始终开启，纯对照容量为 1024，避免隐式丢弃。

| 策略 | FrameTime 均值 ms | FrameTime P95 ms | GameThreadTime 均值 ms | Slate DrawPrePass 均值 ms | GPUTime 均值 ms | SlateUI DrawCall 均值 |
|---|---:|---:|---:|---:|---:|---:|
| 正常动画、全部显示 | 16.615 | 20.420 | 16.607 | 3.323 | 1.763 | 1322.8 |
| 停止动画、静态显示 | 15.342 | 17.763 | 15.337 | 3.024 | 1.741 | 1324.8 |
| 停止动画、隐藏数字 | 8.524 | 9.524 | 7.870 | 1.819 | 1.761 | 336.8 |
| 100 ms 合并 | 8.534 | 9.705 | 7.733 | 1.848 | 1.757 | 373.6 |
| 不合并、最多 128 个活跃数字 | 8.895 | 10.677 | 8.771 | 1.995 | 1.740 | 587.0 |

合并组：12000 输入转为 449 条显示，11551 次并入已有分组，0 丢弃。容量 128 组：12000 输入中丢弃 8928 条显示（伤害不受影响）。隐藏组只用于消融诊断，不能作为可交付的视觉优化方案。

推断：当前压力场景中，活跃屏幕空间 Widget 参与的布局/显示路径是主要可观察成本；停止动画也有一些收益，但远小于减少可见数字数量。GPUTime 在各组接近，没有证据表明该场景主要受 GPU 限制。合并组 FrameTime P95 下降约 52.5%，这是改变显示策略后的特定场景收益，不是纯池化收益。

原始记录：`Saved/Performance/DamageNumbers/1698627132cd439a8a81083ed2048c98/ui.json`、`ui-summary.json`。每组 CSV 路径记录在 ui.json 中。前一个运行目录 `9cf66899aa9c4fb9909c802271fa2142` 是样式修复前实验，不能替代本表。

## 实现与调参

- `DamageNumbers.MergeSeconds`：默认 0.1，范围 0–0.5；0 关闭 Actor 显示合并。只在本地客户端延迟显示，不延迟伤害计算，也不减少既有 Actor RPC 带宽。
- 分组键：本地玩家、目标弱引用身份、组件类、格挡标记、暴击标记。目标销毁后保留最后收到的位置和伤害，仍然显示；不同目标及样式不相加。
- 合并窗口从首条待显示请求启动，批次内等待不超过配置窗口加调度误差。累加采用有限值检查和浮点上限保护。
- Pending 队列最多 256 个分组/世界，超额丢弃显示并计入 `pending_dropped`。重要反馈在 flush 时优先分配活跃槽位，但不保证满 pending 队列时仍能入队。
- `DamageNumbers.Capacity`：仍默认每个本地玩家 256 个活跃数字。
- `DamageNumbers.Reserved`：默认 8，最多占容量的 25%，普通数字不能使用这些预留槽位；暴击或格挡可以使用，不抢占已经播放的数字。
- Mass 已有服务器 100 ms 合并，继续直接调用 Show，不重复等待。
- `DamageNumbers.Diagnostic`：0 正常、1 停止动画但显示、2 停止动画并隐藏；仅供测试，正常游戏保持 0。

新增原生 Insights scope：DamageNumbers_QueueActor、DamageNumbers_FlushActor。报告增加 actor_inputs、merged、pending_dropped、pending；原 requests 表示送入显示入口的次数，启用合并后不能再将它当作原始命中请求量。

测试发现并修复了上阶段池化未覆盖的样式残留：原蓝图用实例变量保存 Block/Critical 中间值和 Out Color。复用前恢复蓝图声明的数字、布尔、枚举与颜色标量默认值，保留 Widget 树和动画对象；不修改蓝图资产。

## 验证及复现

1. Editor Development 编译成功。
2. verify_damage_number_aggregation.py 检查普通 10+20=30、独立暴击 7、格挡 5、另一个销毁目标 40；7 项断言覆盖总额、样式、合并次数、归还、预留和暴击槽位复用后的普通颜色。
3. verify_damage_number_network.py 的 8 项断言全部通过，覆盖远端/房主 Mass 致死反馈和客户端复用，以及真实 GAS 伤害产生的两次 Actor RPC 仅在远端攻击者客户端合并。初始 Python 编辑器执行上下文直接调用不能作为该 RPC 链路的可靠验证；测试改用 DamageNumberTestLibrary 在正常世界定时器中执行两次 GE，客户端记录 actor_inputs=2、merged=1。测试入口仅允许编辑器中的服务器 PIE，非编辑器构建返回 false。
4. 最终性能脚本：profile_damage_number_ui.py；汇总：summarize_damage_number_ui.py。脚本恢复原 CVar 和后台限速设置。旧 compare_damage_numbers.py 已显式关闭合并、预留和诊断，避免污染池化对照。

## 局限与后续

这是 Editor/PIE 的引擎 CSV，不是上一阶段 Python 回调间隔，两者不应直接比较。SlateUI DrawCall 包含本次编辑器/PIE 窗口的 Slate 工作，不能全部归因于游戏伤害数字；线程指标也不等同于该功能独占时间。RenderThreadTime 在本环境中接近零，未用它推导渲染线程收益。实验顺序固定，没有报告置信区间。

单目标密集命中对合并最有利；不同目标很多时合并率会下降，不能将这些结果外推为整款游戏稳定达到某帧率。下一步应做多目标分布、正常技能战斗与打包 Development 复测，并测量内存和端到端显示延迟。长时间运行、网络丢包和用户观感验收仍待完成。
