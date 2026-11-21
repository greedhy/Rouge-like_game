# 动态法师配方的真实面板验收

本页记录游戏内面板的验证方法。命令桥调用过配方组件、无渲染控件测试通过、真实 UMG 面板施法通过是三种不同证据。修正 QA 数值焦点写入顺序并加载新 DLL 后，基础面板、下列 14 个组合和在途版本均完成新的 Standalone PIE 实测，双人 Listen 远端基础面板也已通过；其余远端场景按各自报告登记，不能用其它测试代替。

旧报告 `Saved/GAAuthoring/DynamicMage/RealCasting/8e449c78dd664ee6a8a24f73ff23d477/report.json` 当时的 37 项检查通过，清理成功，但仅保留为交互/施法路径记录：后续确认先对 Spin 取焦点再 SetValue 会在失焦时提交旧文本。新脚本因此增加 Apply 后下一帧数值仍等请求的断言；正确数值验收采用下述新批次，不沿用这份旧记录。

新的基础报告为 `Saved/GAAuthoring/DynamicMage/RealCasting/3171ca423bb74e948fb23a43603780ae/report.json`：38 项检查通过，Apply 点击及焦点提交后实际状态时长仍为请求的 `1.5`，准确临时授予回收和清理均成功。新的组合总报告为 `Saved/GAAuthoring/DynamicMage/UMGScenes/c657575f02e94822aeeaf697718c4f9a/report.json`：`finished=true`、`passed=true`，14 行各自 `passed=true`、`cleanup_succeeded=true`。每行包含独立原始报告路径；正确数值、三个独立实际飞行调用、三个真实冻结 Beam 宿主及原输入都以这一批新报告为准。

在途版本报告 `Saved/GAAuthoring/DynamicMage/RealCasting/d8d94c29dacc48f39c3aae6affc74370/report.json` 的 55 项检查及清理通过：真实 UMG 替换和两次 Remove 均发生在第三次物理命中前，旧链保留已冻结版本，第三次施法恢复实际默认，准确 Spec 全集和焦点复原。双人远端基础面板报告 `Saved/GAAuthoring/DynamicMage/RealCasting/751745c94dc440d4a317438c29f0b410/report.json` 为 `mode=ListenServerRemote`，43 项检查和清理通过；不以 Standalone 结果代替这份实际远端 RPC/输入/状态/焦点证据。

最终DLL双人远端 UMG 版本报告 `Saved/GAAuthoring/DynamicMage/RealCasting/2e12c7e91e5d41cf98acd145d3bf2ecc/report.json` 的 68 项检查和清理全部通过。实际两笔 Remove 分别等待双端版本确认，撤回仍发生于第三次物理命中前；下一次默认施法前双方旧状态自然恢复，默认施法没有继承旧状态。命令桥版本对照 `Saved/GAAuthoring/DynamicMage/RealCasting/a4ca7c8d18bf4d94861c7d966ff7b426/report.json` 的 47 项检查及清理也通过，但不作为 UMG 操作证据。

## 入口和资源归属

`MyPlayerController.RuntimeRecipe` 创建本玩家的原生 `AbilityRuntimeRecipePanel`。面板从真实目录选择 DefinitionId，只向所属 PlayerState 的开发组件提交可信 ID 和有限数值。服务器负责版本/基础摘要校验、私有图编译、临时 Spec 和所属租约。客户端不上传图、配置对象或类路径。

`RuntimeRecipe.Probe UMG.*` 仅在开发构建中存在。探针在下一原生 CoreTicker 执行，避开 Python 编辑器调用保护；查找准确本地控制器在屏的真实面板，再操作它的 WidgetTree 和原按钮委托。它不是 OS 鼠标自动化，也不绕过面板直接调用 ApplyEdit。

| 探针动作 | 实际入口与边界 |
|---|---|
| `UMG.Open` | 原控制器打开或重显面板；不创建替身面板 |
| `UMG.Select RecipeDefinition <ID>` | 仅选择本地可信目录中的可选行 |
| `UMG.Select RecipeNode <index>` | 选择当前真实基础图的显示节点 |
| `UMG.Select RecipeModifier/RecipeFragment/RecipePort <option>` | 仅选择控件当前已经具有的准确选项 |
| `UMG.Value RecipeValue/RecipeRadius/RecipeInterval/RecipeMaxDepth/RecipeBranches <value>` | 有限值且在真实 SpinBox 边界内；服务器仍独立验证 |
| `UMG.Click RecipeBegin/RecipeApply/RecipeRemove/RecipeHide/RecipeClose` | 对真实按钮设置所属用户焦点并广播其原 OnClicked 委托 |
| `Press/Release` | 原控制器 T 输入；TargetData、动画 Notify 与结算仍来自原 GA |
| `PressPrimary/ReleasePrimary` | 原控制器左键输入，用于验证 Orbit 与原进攻技能联动；不直接广播攻击信号 |

探针只临时保存弱世界/控制器和有界文本。数值输入先让真实中性按钮结束旧文本编辑，再 SetValue 并进入新数值控件；点击后下一帧再次核对实际值。面板隐藏后保留原临时授予；关闭时仅通过匹配的真实关闭回执归还预览资源。`GetLastCloseRequestId` 仅回读本面板发出的准确身份。`HasOwnerViewportFocus` 仅检查所属 Slate 用户的真实游戏视口焦点路径；不转移焦点，也不以直接 T 输入成功推测焦点正确。纯表现宿主的类型从只读 `GetPresentationKind` 取得冻结快照值，未就绪返回 `-1`；不能通过旧 `Prototype` 字段猜测新 Beam。

## 基础面板场景

在本任务的空 `L_Batch6Test` Standalone 或双人 Listen PIE 中执行 `Scripts/GA/DynamicMage/verify_umg_interaction_pie.py`。脚本从本地玩家和准确复制身份配对的权威玩家读取状态，完成：

1. 真实面板选择 FireBolt，点击原 Begin，核对两端恰好多一个临时 Spec。
2. 选择独立 Await 的真实 Hit 端口追加石化，点击原 Apply 并等双方接受同一版本。
3. 检查控件实际获得焦点；点击 Hide 后，确认面板被隐藏且真实视口取得焦点。
4. 使用原 T、鼠标目标、动画和 Notify；观察真实血量、双方目标状态及原费用回执。
5. 重显同一个面板，点击原 Remove，确认恢复基础配方但准确临时 Spec 保留。
6. 点击原 Close，等待本面板准确 GUID 的成功回执；核对双方全部 Spec 句柄集合与预览前完全一致，视口焦点恢复。
7. 归还本任务设置的属性 base/职业，销毁准确归属的测试敌人、AI 控制器和表现宿主；不写资产或存档。

## 跨轴组合场景

`verify_umg_combinations_pie.py` 共用上述控件/清理驱动。每次运行只设置一个 `DYNAMIC_MAGE_UMG_SCENE`，产生独立报告；不预先烘焙组合 GA。所有编辑逐项等待服务器接受，失败立即报告，不跳过不支持项。

`verify_umg_inflight_versions_pie.py` 独立覆盖配方版本：同一面板先提交逐跳；首个实际命中后通过原控件替换伤害为石化；下一次石化逐跳首击后通过两个原 Remove 按钮撤回全部自有规则。旧箭保留原版本，后续第三次真实施法恢复单发默认。每个根核对全部观察的版本/摘要、原费用和异常终结，最后验证同一临时 Spec 被准确回收。上述 Standalone 报告已实际通过，远端版本另列实际运行证据。

远端撤回必须逐笔等待所属客户端与服务器的同一版本、摘要确认，第一笔 Remove 被确认后才能提交第二笔；本地同步成功不能代替这个事务约束。两份版本脚本通过 ASC 的 Owned/Granted Tag 查询自然等待旧 Burn 结束，不能用只查询效果 AssetTags 的接口判断 DOT 已消失，也不移除旧 GE。UMG 版本脚本还等待准确目标的双方石化状态自然恢复，再开始下一次原输入，避免将上一轮延迟复制的状态当作默认技能的新效果。上述修订以最新远端报告为证，先前失败及其完整清理记录不登记为通过。

| 场景值 | 实际面板编辑与观察目标 |
|---|---|
| `Sequential` | 原 FireBolt Await 替换逐跳，真实 Hit 追加石化；至少三个不同目标获得状态 |
| `ScatterHoming` | 实际 Multishot 的数量/散射、独立飞行追踪和接触状态组合 |
| `ScatterPiercing` | 散射数量、穿透接触上限/倍率和接触状态；至少两个目标获得状态 |
| `EmitterBeam` | 实际 Emitter 数量/周期，替换原 Launch 子分支为可信 Beam；观察三个不同真实 Beam 宿主及原伤害 |
| `Field` | 实际范围/有限寿命与 Period 区域状态分离 |
| `Trap` | 实际布防时间和 Action 区域状态分离 |
| `Delay` | 实际延迟与正常 Expired 区域状态分离 |
| `Attach` | 实际周期/寿命/转移次数与逐次目标状态分离 |
| `Stack` | 实际阈值/延迟/层寿命；两次原输入施法，保留自然冷却和共享层记录 |
| `Move` | 实际目标位移后 Completed 区域状态；记录真实目标位置改变 |
| `Wave` | 实际范围/速度/寿命与波前 TargetFound 状态分离 |
| `Shards` | 实际点阵 GA 的独立批伤害替换为批石化；要求没有隐藏旧伤害 |
| `Channel` | 原 Electrocute 周期伤害替换为批石化，保留原松键和最终阶段 |
| `Orbit` | 原 Orbit 的实际接触伤害后追加石化；通过原左键进攻触发库存发射 |

上述 14 行均在 `c657575f02e94822aeeaf697718c4f9a` 批次实际通过。Standalone 数值/状态/输入通过仍不代表双人复制或粒子艺术质量已验收。ScatterHoming 显式使用 `PelletTargetPolicy=OriginalSelectedLife`，不改变未编辑时不继承目标的旧默认；Piercing 沿真实三维射线瞄远端目标，保留地面碰撞；Stack 参数选择实际状态节点最近的唯一上游 Scope，避免修改它的投射物父片段；Move 夹具保留原 CharacterMovement 根运动管线，不能禁用移动后再声称验证了位移。

各场景逐帧复制公开观察值，避免 Wave 等高频事件把初次费用挤出 128 条显示环形缓冲。缓存是测试侧纯值，不改变生产观察预算。详细的取消、池新生命、Actor/Mass 交错、撞墙/超时和共享预算仍以对应行为自动化用例为证；此脚本不能替代它们。

示例运行方式（由负责编辑器的任务在下一次正确 DLL 的 PIE 中执行）：

```python
from pathlib import Path
import unreal

DYNAMIC_MAGE_UMG_SCENE = "EmitterBeam"
exec((Path(unreal.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_umg_combinations_pie.py").read_text(encoding="utf-8"))
```

报告位于 `Saved/GAAuthoring/DynamicMage/RealCasting/<run>/report.json`。单个场景必须同时满足 `passed=true` 和 `cleanup_succeeded=true` 才能登记通过；脚本中的场景目录、构建成功、AST 检查均不代表场景已通过。粒子艺术质量、动画可读性和最终手感继续由用户 PIE 验收。

最终DLL另通过Standalone基础面板 `858e54ba995647ccb847ef20ab6790f6`（38项）与远端基础面板 `01ea123988f241968b8ab698c234a535`（43项），均准确清理。前文14组合批次及其独立Standalone版本场景仍保留090852报告边界，不冒充最终DLL的重复执行。原地图/节流已恢复，无PIE和未保存资产；人工视觉验收仍Pending。完整结果见[交付证据](ga-dynamic-mage-delivery-evidence.md)。
