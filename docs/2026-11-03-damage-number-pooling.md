# 伤害数字池化与原生耗时对照

## 实现

Actor 与 Mass 接入 UDamageNumberPool（WorldSubsystem）。每个本地玩家使用可见、无碰撞、非复制的锚点 Actor，避免数字随受击对象死亡而销毁。服务器的伤害计算、RPC 归属和 Mass 100 ms 合并保持原逻辑。

池按玩家和组件类复用。BP_DamageTextComponent 原有 Delay 1 秒结束时调用 DestroyComponent；受池管理的组件将该调用转为隐藏、停止动画、停用 Tick 并归还。复用前恢复默认组件变换、位置、Widget 透明度与渲染变换，再调用原蓝图更新文本和样式。活跃组件不提前回收，不把旧结束回调带入新一次显示。世界销毁时绕过归还，真正销毁组件和锚点。

`DamageNumbers.Pool 0/1` 切换创建销毁/复用（默认 1）。`DamageNumbers.Capacity` 默认 256，限制每个本地玩家同时活跃数字；满额丢弃新的表现请求，不改变伤害。池内存保留历史容量峰值，降低运行时容量不会立即缩减已有空闲槽位，关卡销毁统一释放。当前没有按暴击优先替换，也没有新增 Actor 合并窗口。

原生 Insights scope：DamageNumbers_Show、DamageNumbers_Create、DamageNumbers_Register、DamageNumbers_Release。GetReport 返回请求、创建、复用、归还、丢弃、活跃、保留数量，以及 Show/Create/Register 的累积毫秒；ResetCounters 不清空池。Show 包括池查找、创建/注册、蓝图显示等同步工作，不包括后续帧 Widget 动画和绘制，三个计时不可相加（包含关系）。计时自身也有开销。

## 对照实验

脚本 `Scripts/Performance/compare_damage_numbers.py`；L_MonsterBaseline 单人 PIE；同场景、同位置、同请求序列，每秒 100/500 次，各模式三轮。每轮 3 秒预热、5 秒采样、2 秒排空。原生统计涵盖该轮 8 秒请求阶段；回调百分位只取 5 秒采样段。临时容量 1024，所有组均无丢弃。池在组间保留，结果包含首次扩容和后续热池收益。

| 每秒请求 | 模式 | 三轮请求数 | 创建数 | 原生 Show 平均 μs/次 | PIE 回调 P95 ms |
|---|---|---:|---:|---:|---:|
| 100 | 创建/销毁 | 2400 | 2400 | 247.40 | 16 |
| 100 | 池化 | 2400 | 102 | 68.07 | 16 |
| 500 | 创建/销毁 | 12000 | 12000 | 102.06 | 31 |
| 500 | 池化 | 12000 | 415 | 22.98 | 32 |

500 次/秒的同步显示入口成本下降约 77.5%，创建次数下降约 96.5%。这是该场景中该入口的收益，**不是整体 CPU 或 FPS 提升**。回调 P95 未改善，下一步应测量活跃 Widget/Slate 更新、绘制和 GPU，而不是认为池化解决了帧时间问题。

相同模式都经过新管理器，基线也使用稳定锚点和相同容量策略；本表是管理器内创建/销毁与复用的比较，不直接与上一份原实现基准做百分比计算。PIE 回调时间不是引擎 FrameTime，未采集打包游戏、GPU 和精确内存数据；也未随机化实验顺序，不能推广为通用性能承诺。

原始数据：`Saved/Performance/DamageNumbers/00552dcc48c74e1eab5817df4a4cee49/comparison.json` 和 `summary.json`。

## 验证

- Editor Development 编译通过。
- 单人生命周期脚本：容量 2 时第三条丢弃；1 秒后归还；复用后文本变为 37；真实 Mass 致死伤害仍产生数字，最终全部归还。
- 双客户端脚本：远端伤害仅显示于远端；房主伤害仅显示于房主；目标死亡不影响数字结束；再次命中复用同一个客户端槽位。
- 已检查池化数字截图，保留原动画与暴击外观。测试结果分别保存为 Saved/Performance/DamageNumbers/lifecycle.json 和 network.json。
- 尚待正常游玩中的动画观感验收、网络丢包、长时间内存趋势与打包验证。本次不是伤害数字全部优化工作的终点。

脚本入口：verify_damage_number_pool.py、verify_damage_number_network.py。性能对照和生命周期脚本仅用于测试 PIE；结束测试后关闭 PIE，清除临时实体。实现未修改任何蓝图资产。

编译时发现工作区 EquipmentComponent.cpp 和 EquipmentPanel.cpp 的自身头文件不在第一行，只交换了前两条 include，未改变装备逻辑。
