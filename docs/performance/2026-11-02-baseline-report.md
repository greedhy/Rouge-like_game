# 首份怪物性能基准与瓶颈报告（2026-11-02）

## 结论

本次 200 实体分散初始布局：Mass 模拟平均 1.040 ms/帧，P95 1.157 ms；整帧 P95 16.014 ms、P99 16.955 ms，1.36% 帧超过 16.667 ms。不能据此宣称完整游戏稳定 60 FPS。

最优先调查的是编辑器/Slate UI 开销：引擎 CSV 的 Exclusive/GameThread/UI 平均 7.799 ms，是本次最大已识别 GameThread 分项。它包含编辑器环境，不能直接归因于游戏 HUD。下一步先用独立打包进程复测，再决定是否优化游戏 UI。

## 环境与负载

- CPU：AMD Ryzen 9 9950X3D；GPU：NVIDIA GeForce RTX 5090（机器另有集显和虚拟显示适配器）。UE 5.8 Editor Development，Windows，Standalone 网络模式的 PIE 新窗口，单玩家。
- 地图：`/Game/Performance/L_MonsterBaseline`，独立复制已有 QA 地图，移除预置 Mass 生成区域，使用已有 PureMass GameMode。原玩法地图未改。
- 测试类型：`/Game/Performance/DA_BenchmarkGoblin`，只将攻击伤害改为 0，源类型仍为 5。玩家静止在中央导航房间，怪物持续选敌、移动、避让和攻击，不因玩家死亡降低负载。
- 每组预热 5 秒，CSV 采集 15 秒，等待写盘 3 秒。0/100/200 个实体；初始间距 120 cm，密集组 70 cm。投射到已有导航后生成。无玩家施法、无掉落或死亡压力。
- 关闭本次 PIE 背景节流，`t.MaxFPS=0`、`r.VSync=0`。未锁固定时间步。继承编辑器画质和窗口大小；本轮未完整记录画质/窗口配置，跨机器或跨会话对比前必须补齐。
- 每组仅一次，固定顺序。密集组与分散组会向同一个目标聚集，不代表整个采样期间持续保持不同密度。不能从密集组更快得出因果结论。
- Python 只编排阶段，CSV 来自原生引擎计时；Slate 回调和编辑器、Trace 都有观测开销。

## 实测整帧（ms）

| 初始场景 | 帧数 | 平均 | P95 | P99 | 超过16.667ms |
|---|---:|---:|---:|---:|---:|
| 空场 | 1220 | 12.302 | 14.125 | 14.944 | 0.08% |
| 分散100 | 1132 | 13.249 | 15.234 | 16.025 | 0.27% |
| 分散200 | 1099 | 13.643 | 16.014 | 16.955 | 1.36% |
| 密集200 | 1147 | 13.076 | 14.672 | 15.393 | 0.09% |

## 自定义埋点（分散200，ms/帧）

| 分项 | 平均 | P95 |
|---|---:|---:|
| MassBegin | 0.0117 | 0.0168 |
| TerrainQuery | 0.0060 | 0.0073 |
| SpatialPublish | 0.0245 | 0.0321 |
| MonsterUpdate | 1.0082 | 1.1217 |
| MonsterMove | 0.3644 | 0.4069 |
| NavigationRequest | 0.0391 | 0.0561 |
| MassFinish | 0.0035 | 0.0041 |
| MassSim | 1.0397 | 1.1566 |
| SnapshotCopy | 0.0062 | 0.0072 |
| InstanceReconcile | 0.0209 | 0.0241 |
| InstanceTransforms | 0.0147 | 0.0202 |
| CrowdTick | 0.0450 | 0.0578 |
| NetSnapshotPublish | 0.0014 | 0.0111 |

以上是 inclusive scope，每帧累计所有调用。例如 MonsterMove 已包含在 MonsterUpdate 和 MassSim 中，不能把表格逐行求和。CrowdTick 包含快照与实例处理。

- Mass 内部：移动/世界碰撞约 0.364 ms，值得进一步拆分 sweep、贴地和邻居避让。剩余 MonsterUpdate 时间还包含选敌、邻居计算等；目前未独立计时，不能断言邻居查询是最大热点。
- 寻路平均约 0.039 ms，当前每帧最多8请求，尚未记录排队或路径陈旧程度；低 CPU 成本不等于导航响应质量已经达标。
- CrowdTick 平均约 0.045 ms，当前规模下其 CPU 提交并非首要热点。这不是骨骼蒙皮/GPU成本。
- GPUTime 平均约 2.029 ms，仅为引擎 CSV 报告值；没有 GPU pass 分解及完整渲染验证，不能据此承诺更大规模。

## 0.1秒与60 FPS

PureMassProcessor 每帧运行；Crowd Tick 每帧更新显示位置。SnapshotInterval=0.1秒控制网络快照，NetUpdateFrequency=10。

分散200样本共有1099帧，MassSim在1099帧均有记录；采样帧时间总和约14.994秒，对应约73.3次/秒。NetSnapshotPublish在141帧记录，对应约9.4次/秒（间隔按帧量化）。这说明本机模拟没有被限制为10Hz。

本轮不改网络频率。客户端插值平滑度、网络接收频率、丢包与RTT都未测试。若未来规定固定60Hz模拟，需要独立设计固定步长、积累器和追帧上限，不能只设置0.016秒Timer。

## 复现

1. 编译 test_25dEditor Win64 Development；本次完整构建成功。
2. 编辑器执行 `Scripts/Performance/prepare_baseline.py`；要求无未保存地图/资产。
3. 单玩家 Standalone 模式 PIE，执行 `Scripts/Performance/run_baseline.py`。脚本显式启用 `CsvCategory MonsterPerf enable`，因为动态模块类别可能晚于启动参数处理注册。
4. 等待 `Saved/Performance/latest-run.txt` 指向的 run.json，必须检查 passed=true、实际实体数和移动数量，不能只看 MCP 回执。
5. CSV在 Saved/Profiling/CSV；用 `python Scripts/Performance/summarize_baseline.py <按场景顺序的四个CSV> --out <输出JSON>` 汇总。解析器拒绝缺失自定义埋点的数据。
6. 停止PIE，执行 finish_baseline.py 恢复原地图并关闭记录。背景节流由运行脚本恢复。

## 证据与验证边界

- 成功运行：`Saved\Performance\ce8df26eb45e4ad5b5802b92c0f760df/run.json`。结束时实际实体0/100/200/200；运动实体0/66/151/148。
- 汇总数据：[JSON](2026-11-02-baseline-summary.json)。原始 CSV 对应文件名见 JSON；CPU Trace：`Saved/Performance/baseline.utrace`。原始大文件留在 Saved，不纳入源码。
- 初次试跑在入口导航投影失败，已停止并作废；第二次移至中央房间后四场景成功。没有将失败样本混入表格。
- 没有进行Actor同条件对照、多人、持续施法、GC/加载压力或打包验证，也没有量化优化收益；本次只建立当前基线。
- 下一轮：固定窗口/画质、每组重复至少3次并轮换顺序，打包Development复测UI热点；然后增加移动/导航延迟与邻居候选数计数，最后增加真实技能压力。先明确瓶颈再改算法。
