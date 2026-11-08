# 伤害数字性能：第一阶段基准

日期：2026-11-02。当前完成原实现诊断基准，尚未实施池化、Actor 合并显示或原生耗时埋点，不宣称优化收益。

## 已确认的路径

- MyPlayerController::ShowDamageNumber 每次请求 NewObject、RegisterComponent，再调用 SetDamageText。
- PureMassPlayerController 已有服务端 100 ms 合并和显示数量上限，但显示端仍创建组件。
- BP_DamageTextComponent 调用 WBP_DamageText 更新，Delay 1 秒后 DestroyComponent。复用时必须处理结束回调及动画重置。

## 可复现方式

在 `/Game/Performance/L_MonsterBaseline` 启动单玩家 Standalone PIE，执行 `Scripts/Performance/run_damage_numbers.py`。脚本调用当前本地控制器的 Actor 数字 RPC，仅触发表现，不改变血量。三组请求速率 0/100/500 每秒，每组重复三轮；每轮 3 秒预热、5 秒采样、2 秒排空。数字集中在玩家位置，属于同位置大量透明 UI 的压力场景，不代表正常分散战斗。

脚本临时关闭后台限速、t.MaxFPS 和 VSync，结束或捕获异常时恢复。`summarize_damage_numbers.py` 生成汇总。数字存在性在预热阶段断言，排空阶段检查仍注册组件数量。

## 实测结果

| 请求/秒 | PIE 回调 P50 ms | P95 ms | P99 ms | 派发均值 μs/请求 | 三轮总请求 | 各轮排空后注册数字 |
|---|---:|---:|---:|---:|---:|---|
| 0 | 15 | 16 | 16 | — | 0 | 0 / 0 / 0 |
| 100 | 15 | 16 | 16 | 253.87 | 2400 | 0 / 0 / 0 |
| 500 | 16 | 31 | 32 | 95.62 | 12000 | 0 / 0 / 0 |

原始证据：`Saved/Performance/DamageNumbers/e110bee4d3a24be5a56a4bc634d563d5/baseline.json` 和 `summary.json`。较早目录含失败的 Python API 探测，不纳入结果。

## 解释与限制

这些帧间隔来自 Slate Python 回调，不是打包游戏帧时间；派发耗时包含 Python/reflection/RPC/组件初始化，不能称为 NewObject 独占耗时。三个重复运行的样本合并计算百分位。高负载出现回调长尾；单次派发平均成本反而降低，不能据此声称创建更快，可能涉及缓存、摊销和测量框架影响。

尚无原生 CPU scope、GPU 时间、内存、网络带宽或优化后数据。不能将本表写成“优化后提升 X%”。排空为 0 只说明本次检查未发现注册数字残留，不等价于证明没有 UObject/内存泄漏。

## 下一阶段

1. 在创建、注册、显示、归还路径添加原生 Insights/CSV scope 和数量计数，区分各项开销。
2. 实现稳定本地 owner 的有界池；让受池管理对象的结束事件归还，复用前清理动画和显示状态；世界结束时真实销毁。
3. 保留旧路径开关，先相同数量测试纯池化，再单独测试合并与数量预算，避免把减少显示量的收益算到池化上。
4. 验证暴击/格挡样式、目标死亡、连续复用、满池和联机归属，再做匹配基准。
