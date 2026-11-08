# 伤害数字优化：多目标分布验证（2026-11-03）

## 结果

相同总输入速率 500 次/秒，分别均匀轮转命中 1、10、50 个目标。每个目标自身每十次命中一次暴击，避免目标序号与暴击样式绑定。每种条件三轮，固定种子打乱每轮六组顺序；每组 3 秒预热、5 秒引擎 CSV、3 秒排空。全部条件保持 50 个无模型、无碰撞、关闭移动的临时 Character，位置在玩家附近；只改变参与显示的目标数。

| 目标数 | 不合并 FrameTime P95 ms | 合并 FrameTime P95 ms | 请求合并率 | 丢弃 |
|---|---:|---:|---:|---:|
| 1 | 21.570 | 10.653 | 96.3% | 0 |
| 10 | 20.954 | 13.675 | 71.3% | 0 |
| 50 | 19.425 | 21.058 | 6.1% | 0 |

每个条件共 12000 条输入。容量 1024、预留 0、正常动画、池化开启；18 组结束均验证 active=0、pending=0。未用丢弃数字获得此表收益。

密集重复命中时合并有效；50 目标时，每个目标约每 100 ms 才命中一次，已接近合并窗口，能合并的请求很少。本次 50 目标 P95 反而更高，不能承诺合并普遍提高性能，也不能据此单次实验认定回退的精确原因。应继续按目标命中密度调参；当前不擅自修改默认策略。

## 可复现入口与证据

- Scripts/Performance/profile_damage_number_distribution.py：单人 L_MonsterBaseline PIE，固定种子 20261103。
- Scripts/Performance/summarize_damage_number_distribution.py：读取 latest-distribution.txt，汇总每组 CSV。
- 原始目录：`Saved/Performance/DamageNumbers/d3d1bf1ddc104bfb83f66c7d2740650d`。
- 三轮帧合并后的 P95，不是三轮 P95 的均值；没有置信区间。

## 验证边界

以上是本地显示请求注入，不是完整战斗。未包括真实技能、RPC 和伤害结算的成本。Editor/PIE 的 Slate 和线程指标包含编辑器工作，不等同打包游戏。池容量留存、输入至显示延迟、丢包和长时间运行仍需专门采样。

## 真实技能功能验证

Scripts/Performance/verify_damage_number_combat.py 复用现有六技能 Actor/GAS 双端输入验收脚本，在远端 EnhancedInput 注入施法，检查真实服务器伤害、耗蓝、控制恢复、分裂闪电多目标与客户端表现；仅增加伤害数字统计。六技能为 StarFlameHalo、PetrifyingGaze、SolarFireball、GlacialStorm、FissionChain、EarthShockwave。

本次 89 项断言全部通过。远端客户端 actor_inputs=12、requests=12、created=4、reused=8、released=12，active/pending/dropped/pending_dropped 全部为 0；房主本地显示统计全部为 0。正常技能节奏下没有合并，不能声称本轮真实技能获得压力场景中的帧率收益。此处无 CSV 对照，只证明现有技能链路与数字池可一起工作，截图观感仍由用户验收。

证据：Saved/GAAuthoring/CompositionBatch6/019733a135cc471a85f86a76683b045c/damage-combat.json。测试对象只在 PIE 创建，已结束 PIE 并恢复 L_RoguelikeRun。

## Development 前置检查与下一步

`Build.bat test_25d Win64 Development -Project=... -WaitMutex -NoHotReloadFromIDE` 返回 0，140 个构建动作完成，104.43 秒，输出 Binaries/Win64/test_25d.exe。日志 Saved/Performance/DamageNumbers/game-build.log。现有弃用 API 编译警告未在本轮改动。

这仅证明非编辑器 Game 目标可以编译链接；本轮没有执行 Cook/Stage 或打包运行，不能将该 EXE 视为完整可分发游戏。下一步先提供受命令行显式开关控制的 Development 原生基准入口，替代依赖编辑器 Python 的显示注入与调度，再 Cook/Stage 指定基准关卡进行同条件对照。需同时记录实际输入率、目标密度、显示延迟、进程内存与 CSV，而不是沿用 PIE 数字写成打包收益。

恢复证据 Saved/Performance/DamageNumbers/distribution-restored.json：L_RoguelikeRun，dirty maps/assets 均为空；Pool=1、Capacity=256、MergeSeconds=0.1、Reserved=8、Diagnostic=0、后台限速恢复 true。本轮只添加测试脚本和文档，没有改变运行时策略或项目资产。

独立复核未发现影响本次结论的重要问题。复现时请新开 PIE：真实技能脚本目前检查累计计数，本次初值均为零；若未来复用旧会话，应改为按世界计算前后差值，避免旧请求干扰。
