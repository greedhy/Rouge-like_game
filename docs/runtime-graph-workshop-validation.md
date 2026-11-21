# 免费技能图工作台：验证记录

## 未选择职业直接 PIE 修复（2026-11-21）

真实 `L_RoguelikeRun` 中默认 `BP_DungeonPlayer` 的战斗类型为 Elementalist，职业仍为 Unselected。旧 CheckAuthority 在选择 GA 前拒绝此玩家，图为空。修复后仅对未选职业、属于同一 PlayerState/ASC 的真实默认法师 Avatar 提供开发预览，保留权威/Shipping 和显式近战职业门禁；没有写职业、存档或 GA 资产。错误和使用提示放到开始按钮附近。

| 验证 | 最新证据 | 结果 |
|---|---|---|
| 未选职业 Begin/草稿/应用/关闭、错误Avatar拒绝及原专项 | `Saved/Automation/UnifiedGA/20261121T012514-49ab34f7/index.json` | 68项通过（21普通、47带警告），0失败，0未执行 |
| 正常游戏地图直接 PIE 的原属性入口 | `Saved/GAAuthoring/WorkshopMageAccess/20261121/normal-entry-report.json` | 5项通过；真实5节点4连线；职业不变，原Spec集合和焦点恢复 |
| 单机完整节点/端口/边、草稿、原 T 施法 | `Saved/GAAuthoring/RuntimeGraphWorkshop/RealCasting/323981d626a34cfd8d9b6c1a05e6bafe/report.json` | 103项通过；原职业Unselected，未预设Mage，清理成功 |
| Listen远端完整同一路径及非所有者隔离 | `Saved/GAAuthoring/RuntimeGraphWorkshop/RealCasting/9dd47e2d9d7d497a81edc50897a57a38/report.json` | 111项通过；未预设Mage，清理成功 |

DLL SHA256为 `a21bd4c4d322cd9f0f555a43b8ba51697402cd356fd47058e1b3c809c71bc736`，测试进程113656。初始失败诊断、构建、哈希、恢复和只读审查记录在 `Saved/GAAuthoring/WorkshopMageAccess/20261121/`。此前QA预设Mage掩盖了入口问题；现在专项使用 `WORKSHOP_ALLOW_UNSELECTED=True`，禁止该预设并核对结束时职业仍未选择。截图和步骤见[带图说明](runtime-graph-workshop-visual-walkthrough.md)。本次仍是针对性回归，没有将历史全量测试称为本次重新执行。

## 属性窗口第四页接入（2026-11-21）

最新版本通过游戏内属性按钮进入“属性 / 装备 / 技能 / 技能组装”第四页。第四页直接嵌入完整工作台；控制台快捷入口复用这一实例。切页保留会话，隐藏试放解除父窗口输入注册，普通父窗口关闭等待准确回执，宿主移除精确放弃其租约。没有修改现有 GA 或 Content 资产。

| 验证层 | 本次报告 | 结果 |
|---|---|---|
| 第四页、工作台、键盘/鼠标基础输入、装备回归 | `Saved/Automation/UnifiedGA/20261121T005002-dde49424/index.json` | 66/66通过，0未执行 |
| Standalone 实际属性按钮/第四页、组装、原 T 施法、重开/撤销/父窗口关闭 | `Saved/GAAuthoring/RuntimeGraphWorkshop/RealCasting/bef883f434004dfcaeefe51b8e390f55/report.json` | 102项通过，清理成功 |
| Listen 远端实际同一路径、权威结算与非所有者隔离 | `Saved/GAAuthoring/RuntimeGraphWorkshop/RealCasting/dc43d2bb70a242c98dd2755ce0efac69/report.json` | 110项通过，清理成功 |
| Listen 远端原生 Tick 移除宿主 | `Saved/GAAuthoring/WorkshopFourthPage/20261121/remote-host-destruction.json` | 4项通过，会话/Spec集合清理，原职业恢复 |

最终DLL为 `73f4cd73f07b829ec20bcc3f23fbe7fc9694b94f8a5e2d78772e5aa4eb066298`，进程197736。实际模块路径、资产/输入哈希与截图在 `Saved/GAAuthoring/WorkshopFourthPage/20261121/`。本次没有重跑下方历史的1034项全量测试；下方旧资产基线和UnLua临时禁用边界仍适用。实际GUI验证使用空QA关卡和真实HUD/属性按钮、UMG委托、Slate页签及画布事件，不宣称OS层自动点击或用户视觉验收。

补查中，直接由Python移除远端宿主时未得到可靠RPC清理；改为原生Tick的 `UMG.Character.Remove` 后，原生产销毁路径验证通过。保留失败记录用于区分测试上下文与真实游戏行为。新增自动化夹具最初缺少正确的LocalPlayer Outer与世界控制器登记，修正后生命周期及最终66项全部通过。

日期：2026-11-21。当前测试版实施与自动化/实际游戏验证完成，最终界面手感和动画/粒子可读性待用户PIE验收。代码保留在本地main，没有暂存、提交或推送；没有保存Content资产。

## 最终同版本证据

最终DLL SHA256为 `7fb107823dadf37ba5b6c2efd914f0cc27ce7d6980341b35de5d41a422cbb77c`，真实PIE进程模块核验PID139704。完整汇总为 `Saved/GAAuthoring/RuntimeGraphWorkshop/20261121-implementation/final-validation-summary.json`。

| 验证层 | 报告 | 已观察结果 |
|---|---|---|
| 组装/旧面板/隔离专项 | `Saved/Automation/UnifiedGA/20261120T221219-4f716b4d/index.json` | 35/35通过，0未执行，输入清单未变 |
| 全量test_25d | `Saved/Automation/UnifiedGA/20261120T221324-935dc808/index.json` | 1034执行，1033通过，0未执行；唯一失败为既有Stander资产基线 |
| 实际RHI | `Saved/Automation/UnifiedGA/20261120T221455-f39ab1cb/index.json` | 14/14通过，0未执行，包含既有Niagara/Emitter表现专项 |
| Standalone真实界面与施法 | `Saved/GAAuthoring/RuntimeGraphWorkshop/RealCasting/cb2864b4499e4b8aaa54dd1b7d8cf15f/report.json` | 100项检查通过，finished/passed/cleanup均true |
| Listen远端真实界面与施法 | `Saved/GAAuthoring/RuntimeGraphWorkshop/RealCasting/90bde10039264b9c9710f7ebb4f0d5d7/report.json` | 108项检查通过，包含非所有者隔离，清理成功 |

全量没有宣称全绿：`test_25d.UnifiedGA.Catalog.AllConfigurationAssets`仍报告9个Stander时间轴缺少真实Montage引用，与任务前基线一致，未为此改写范围外资产。

最终1533个验证输入文件的成员和SHA均与当前文件一致；773个受保护资源包与本任务开始时一致。原 `L_RoguelikeRun`、后台节流true已恢复，编辑器检查`dirty=[]`、`maps=[]`、`pie=[]`，编辑器留在原地图供用户试玩。

验证使用UE5.8.1 Editor Development，临时禁用此前引入且与当前工具链不兼容的UnLua/LuaProtobuf/LuaRapidjson/LuaSocket/UnLuaTestSuite。未修改工程的插件启用配置，不能据此称完整UnLua环境已经验证。

## 实际验证了什么

真实UMG选择基础火球、点击实际Slate节点和输出口/连线，配置Sequential3目标/0.3秒/400cm/0.8衰减，预览保持当前图、应用后发布新图。准确命中连线插入单目标石化1.25秒，再点击新增节点Scale.Duration2，实际配置读回2.5秒。草稿只读，非法时长请求不污染已接受图。

隐藏后经原T输入、真实鼠标目标、原TargetData/动画Notify完成三次真实命中、伤害和石化，原费用仅一次；重显后撤销/重置并以准确请求回执关闭，恢复原Spec全集、属性/职业和自身夹具。远端检查客户端/服务器图与状态收敛，另一玩家未取得该配方或临时资源。

测试桥通过下一次原生Tick操作真实UMG及Slate指针事件，不直接调用编辑选择回调、TargetData、Notify或伤害。指针必须位于画布几何范围内，先适应窗口再选点；这不等同于OS鼠标/键盘自动化或人工美术质量认证。

单元层另验证精确边的前后兄弟次序与共享尾部、续接效果先于原后继、来源/目标原生命及GC、有限预算、草稿重放/过期回滚、同摘要不同GUID、后续改装选择新增节点、依赖父模块移除拒绝、正式同标签类隔离、外部来源和准确会话清理。新增单目标用例从真实FireBolt已接受图取得修改节点，并独立执行该状态节点验证Actor到期；完整前端施法以两份真实PIE报告为准。

## 保留的失败与修复历史

初始 `204857-5ef4914a`确实因服务器状态缺AcceptedGraph而失败。首次集成发现原生测试实例未继承测试CDO临时设置、抽象UObject夹具和旧面板绑定回归，均保留原报告，未修改为成功。真实单机进一步发现单目标Petrify没有Duration消费接口，批量状态测试不能代替；现已新增真实单目标Scale/Set支持及独立到期测试，并用最终DLL重新跑完整流程。

需求及正式元素球事务见[设计分析](2026-11-21-element-orbs-and-graph-workshop.md)，操作见[免费工作台指南](runtime-graph-workshop-guide.md)。

![实际草稿：7个节点、6条连线，单目标石化时长2.5秒](../Saved/GAAuthoring/RuntimeGraphWorkshop/20261121-implementation/workshop-duration-draft.png)
