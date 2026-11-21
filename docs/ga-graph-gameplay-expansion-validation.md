# 技能图玩法扩展验证记录

本轮直接在本地 `main` 修改，未提交或推送。未改正式 GA 授予、费用资产、掉落或存档；6788个既有 `.uasset` 的前后 SHA256 全部相同，见 `Saved/GAAuthoring/GraphExpansion/20261122/content-integrity.json`。

## 构建与自动化

完整 Development Editor 构建成功，日志 `Saved/Logs/graph-expansion-delivery-build.log`。环境沿用此前约定禁用导入包中不兼容的 UnLua/LuaProtobuf/LuaRapidjson/LuaSocket/UnLuaTestSuite，项目插件配置文件未改。

全量与双端PIE验证版本 SHA256：`b9c21eb943719ffb77fa68d28d73716e9188c87cef081bd711ca83e80779de17`。实际编辑器 PID、启动时间和磁盘模块记录在 `Saved/GAAuthoring/GraphExpansion/20261122/loaded-delivery-build.json`，已核对该进程的实际模块路径。

| 验证 | 结果 | 报告 |
|---|---|---|
| 原版缺失行为 red | 3项按预期失败：并发字段、原位参数模板、规则注册 | `Saved/Automation/UnifiedGA/20261121T164540-d083a35f/index.json` |
| 连续编辑 red | 真实替换后参数编辑遭拒，已增加有序工作台合同 | `Saved/Automation/UnifiedGA/20261121T174025-3f009ff7/index.json` |
| 全量 `test_25d`，NullRHI | 1049执行，1048通过、1既有资产基线失败；无未运行项 | `Saved/Automation/UnifiedGA/20261121T180904-ad539259/index.json` |
| 新增专项和原生/FIFO分裂，真实RHI | 18/18通过；输入指纹未变 | `Saved/Automation/UnifiedGA/20261121T181109-bb05cfd0/index.json` |

新增12项包含能力审计、真实同帧兄弟弹体、4并发位下9颗投递排队、取消全部兄弟/精确lease、参数编译与原图隔离、顺序编辑重放与普通来源冲突、三来源下严格保护不被中间编辑抹除、混合Actor/Mass最近/最远/低血、真实击杀/未击杀→查询→石化、概率0/1和实际注册类。原分裂6项继续覆盖旧FIFO、原生准备帧、混合目标、来源退休、目标旧生命和接触回执等原合同。

全量唯一失败为已记录的 `test_25d.UnifiedGA.Catalog.AllConfigurationAssets`：9个旧 Stander 时间轴资产没有 authoring montage 引用。本轮保留该失败及原资产，未降低校验标准。完整报告的通过数包含 succeededWithWarnings；不能只取 succeeded，也不能将本次报告描述为全量全绿。

两次正式测试运行都附有 `Saved/GAAuthoring/UnifiedGA/<同runId>/validation.json`，核验 Source、Config、项目/插件DLL、规则与所选Content输入前后指纹相同。全量运行的 success=false 正确保留上述资产失败。

独立只读审查确认一处普通来源保护漏洞，修复已通过RED→GREEN及上述最终全量回归；复现报告 `20261121T180213-5ee9ae80`。无遗留Critical/Important或延后Minor；审查结果见 `Saved/GAAuthoring/GraphExpansion/20261122/review-result.json`。

最后只为一个namespace补充闭合注释，移除此注释后整文件逐字节相同，其余所有Source/Config字节与上述全量运行相同，证据 `final-format-equivalence.json`。24个任务C++文件格式校验通过；再次构建成功（`Saved/Logs/graph-expansion-format-build.log`），最终DLL SHA256为 `4d62d069a674c072089ceaff73a355557cbec746a53802de61c3e54683b9d5a5`，并再次通过真实RHI18/18（`Saved/Automation/UnifiedGA/20261121T181948-a43cb469/index.json`，当前源码/配置/DLL/资产指纹未变）。下列PIE报告保留实际执行时的B9版本指纹，不改写历史证据。

## 真实第四页与施法

脚本 `Scripts/GA/GraphExpansion/verify_expansion_pie.py` 复用项目现有原生探针：原属性按钮、第四页 Slate 按钮、真实画布节点/输出口、真实 UMG 模板和数值控件、预览/应用 RPC、原 T/鼠标目标/动画事件。没有直接 ApplyEdit、伪造命中、注入 TargetData/Notify，职业始终 Unselected。

| 场景 | 结果 | 实际报告 |
|---|---|---|
| Standalone，直接PIE默认法师 | 128项检查通过；权威/本地弹体峰值2 | `Saved/GAAuthoring/GraphExpansion/RealCasting/9b1711812a1a4134b0abee13b5804fb2/report.json` |
| 两人Listen Server，远端客户端 | 136项检查通过；服务器/远端弹体峰值2 | `Saved/GAAuthoring/GraphExpansion/RealCasting/8b52719847664bcb9687103d16c4cf01/report.json` |

两场均完成11次真实界面编辑：替换为并发Forked→总数6→同轴再次改5→延迟0.2→最近→未击杀条件→查询→最低生命比例→目标石化→概率条件→范围石化。每个草稿提交前已接受图不变，客户端/服务器接受同一hash和revision；原节点身份/连线保持；真实根施法三次接触各不同原生命，扣费一次，后继规则有实际观察事件，三个真实敌人受伤和石化，远端属性/状态复制一致。

测试结束撤销最后编辑、重置原图、精确关闭确认、回收临时Spec和贡献、恢复原授予/资源，销毁仅本测试创建的敌人和AI控制器；远端宿主会话不变。编辑器已结束PIE，恢复 `L_RoguelikeRun`、原后台节流和实际备份的窗口尺寸；未保存地图/资产为空。备份和独立读回见 `pie-session-backup.json`、`pie-restored.json`、`current-state.json`。

首轮脚本将过长链路接至 Query.Completed 后超出原火球图深度8，预览正确拒绝；随后保留预算改用传播Completed的独立概率分支。第二轮脚本误把Area.Petrify的片段ID当作节点TypeId，已按真实 `Query.PetrifyArea → Status.PetrifyBatch` 修正断言；两轮错误均正常关闭预览、清理夹具，保留原报告，未作为通过证据。

## 兼容决定及证据边界

按用户约定直接在本地main开发，不提交/推送；原配置保持串行，工作台新Forked显式并发；首版选敌限定为原默认/最近/最远/最低生命比例；工作台顺序编辑显式授权且保留普通来源保护；原图深度预算不扩张。代价是旧配置须手动开启并发、更多规则须后续按同一路线扩展、过长串接会明确被拒绝。真实测试提供有界证据，不等于穷举所有GC/取消交错或解释既有Stander资产缺失。

## 实际界面与用户验收

以下截图来自上述单机通过场景，未编辑图像。

![包含传播、未击杀条件、选敌和概率分支的实际接受图](images/graph-expansion/01-complete-concurrent-rule-graph.png)

![同一Forked节点的实际生效参数](images/graph-expansion/02-in-place-fork-parameters.png)

用户待PIE验收：在正式房间敌群中调整分支数、等待和选敌策略，确认弹体表现、手感和技能搭配。此轮真实界面场景使用Actor敌人；Mass的选敌/条件/伤害后状态由实际Mass自动化验证，未据此宣称Mass房间逐组合双端视觉验收。未验证独立多进程/打包客户端或任意无限派生组合。
