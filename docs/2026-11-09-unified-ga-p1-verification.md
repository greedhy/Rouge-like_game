# 统一 GA 环境 P1 实施与验证记录

## 顺序与发布边界

已先审核设计，再把现有代码通过 [PR #1](https://github.com/greedhy/Rouge-like_game/pull/1) 合入 GitHub main，远端合并 SHA 为 `f21c74e4769a30410f73fb81469e6b60565df3fd`。本轮新框架随后在 `codex/unified-ga-environment` 开发，没有直接追加到 main。

继续采用代码/配置发布边界：本地已有 Content 修改、素材包、LFS 规则、图片以及四个新样例 uasset 未上传 GitHub；样例制作脚本随源码保留。本机资产测试依赖本地素材集，代码仓库单独克隆不等于拥有完整测试素材。

首包源码提交为 `c44edd3ca9ab65459ca0d724d741de4f65be0674`。提交后，已针对正常 Unity 编译分组再次完整构建并重跑全部 300 项，仍为 0 失败/未执行；日志 `Saved/Logs/Build-UnifiedGA-committed.log`，最终报告 `Saved/Automation/UnifiedGA/20261108T163221-32d1fc40/index.json`。该开发分支没有推送或合并到 main。

## 本轮实现

- 统一目录登记 656 项：302 个 GA 蓝图、51 个生产原生 GA 类、303 个配置；5 个测试类明确排除，Unknown 和引用错误均为 0。包含直接 C++ 授予、间接蓝图继承、角色上的 SwordConfig/CombatConfig/MonsterConfig 绑定。
- V1 图配置、稳定节点身份、类型注册、端口/DAG/预算校验；资产与执行实例分离。
- 来源/目标生命令牌绑定 World、RunId、ASC Avatar、Actor 池 Lease 或真实 Mass EntityHandle；每次异步执行重验。
- 有界执行实例、单次事件去重、取消/失败诊断、GAS 任务所有权及结束清理。
- Delay 与 Petrify 两个节点、共享石化服务、真实 LocalPredicted 目标请求入口。图与费用/控制只由服务器执行，状态表现沿用现有复制通路。
- 独立 `GA_DelayedPetrify` 样例：0.25 秒延迟、1 秒单目标石化、10 Mana、2 秒冷却。没有自动授予、占槽或替换原范围石化。
- 幂等制作/严格读回脚本、故意错误配置的语义测试、手动触发的独立 PIE 探针。

本轮同时修复两个由测试/复核发现的真实边界：世界已经 Cleanup、UObject 尚未 GC 时拒绝旧令牌；等待客户端目标期间固定激活时来源，拒绝持久 ASC 换 Avatar 后的旧请求。后者先用真实共享 PlayerState ASC 复现了错误扣蓝和石化，再验证修复。

新增文件改变 Unity 编译分组后暴露的旧测试夹具歧义，只通过限定 `DungeonPlayerRunTests::FFixture`、`ExtensionTests::FFixture` 和 `TerrainTests::FWorld` 修正，未改变对应游戏行为。

## 新鲜验证证据

| 检查 | 结果 | 证据 |
|---|---|---|
| 最终完整 Editor 构建 | Succeeded | `Saved/Logs/Build-UnifiedGA-8.log` |
| UnifiedGA 定向自动化 | 37 项全部 Success | `Saved/Automation/UnifiedGA/20261108T162016-934d65ce/index.json` |
| 全项目 test_25d 回归 | 300 项通过，0 失败/未执行/运行中；160 无警告、140 带警告 | `Saved/Automation/UnifiedGA/20261108T162444-a086fa5f/index.json` |
| 预期测试覆盖 | 源码发现的 300 项全部执行 | `Saved/GAAuthoring/UnifiedGA/20261108T162444-a086fa5f/validation.json` |
| 零匹配防误报 | UE 执行 0 项，包装脚本正确退出 1 | `Saved/GAAuthoring/UnifiedGA/20261109T000219-b36fae2d/validation.json` |
| 已知旧测试/报告解析 | PetrifyLeaseIsolation 通过 | `Saved/Automation/UnifiedGA/20261109T000219-f3ce9209/index.json` |
| 样例独立进程读回 | 原生图校验、引用、Mana 费用及冷却标签通过；没有创建/保存 | `Saved/GAAuthoring/UnifiedGA/sample-20261108T162640840623Z.json` |
| 错误资产验证 | 错入口、缺费用、缺冷却标签、动态 Registry 行均拒绝；恢复后通过 | `Saved/GAAuthoring/UnifiedGA/sample-validation-tests.json` |
| 最终独立只读审计 | 656 项，0 差异，进程退出 0 | `Saved/GAAuthoring/UnifiedGA/20261108/catalog-final/audit-report.json`、`diff.json` |
| 独立代码复核 | 发现的 4 项问题均已处理，定向复审通过 | `Saved/GAAuthoring/UnifiedGA/20261108/p1-final-review.md` |

上述自动化实际检查费用/冷却一次、Actor/Mass 生效和解除、取消、坏目标数据、遮挡/射程/阵营、池回收再借出、来源替换（等待前后）、世界退出、配置快照、DAG 多次调用、队列预算及终态回调重入。原石化通过实际已有 GA 蓝图激活检查当前策划半径、费用、持续、冷却和空放规则。

首次运行的失败报告保留，未用旧报告替代。测试夹具修正包括显式注册未 BeginPlay 世界的 AttributeSet、使用服务器收到的预测激活请求而非远程激活转发，以及按小帧推进引擎时间。一次并发 commandlet 的 SOMOLMCP 端口冲突已记录，最终验证全部串行重跑并以退出 0 为准。

## 未完成边界与下一阶段

矩阵当前为 653 Inventoried、3 Adapting、0 Verified：新样例 GA、图和原生入口处于待验收阶段。目录完整登记不表示所有旧 GA 已使用统一执行器；现有原型、第五批、时间轴、敌人、武器和装备入口尚未完成迁移。

未执行实际单机 PIE 探针、双端网络传输或视觉质量验收。测试中的远程控制器模拟服务器接收路径，不证明真实客户端预测/网络/远端表现。手动步骤见 [制作指南](ga-unified-authoring-guide.md#样例资产与验收)。

P2 下一阶段计划见 [18 种 Kind 兼容迁移](superpowers/plans/2026-11-09-unified-ga-kind-migration.md)。仍需按具体兼容金样推进，并解决无限 Orbit 与 V1 总预算的差异；P3–P5 的跨施法、动画/传统 GA、装备派生入口也尚未实现。本记录只证明 P0 和 P1 的代码/资产/自动化阶段，不关闭整体统一目标。
