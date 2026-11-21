# 技能组装：直接 PIE 带图操作

直接 PIE 会生成默认法师，但没有经过存档职业选择，`PlayerState.Profession` 为 `Unselected`。此前工作台先检查职业，导致即使选中法师 GA，也被拒绝并没有生成图。这条路径已修复：开发工作台核验真实默认法师的 PlayerState、ASC、Avatar 和战斗类型后允许测试，不改变职业或存档。显式选择的近战职业仍保持原限制。

以下图片为修复后的真实 PIE 界面截图。完整正常地图入口也已验证：[L_RoguelikeRun 直接 PIE 截图](images/runtime-workshop/00-real-game-map-direct-pie.png)。

## 1. 开始测试，显示基础图

直接 PIE → 点击属性按钮 → 第四页“技能组装” → 下拉选择 `Abilities.Fire.FireBolt · GA_FireBolt_C` → 点击“开始测试”，再点击“适应窗口”。

应该出现“服务器已接受 · 版本1 · 5节点 / 4连线”。本例选择原生火球版本；Actor/Mixed 等其他实际类版本的图可能不同。

![开始测试并适应窗口后的完整基础图](images/runtime-workshop/01-started-base-graph.png)

## 2. 选择投递节点，修改传播方式

点击第三个节点 `Delivery.ProjectileAwait` 的**节点本体**，再拖动面板右侧纵向滚动条查看下方表单。将策略下拉框选择为 `Propagation.Sequential`，填写：

| 界面字段 | 示例值 |
|---|---|
| 总目标数（含首击） | 3 |
| 命中后等待（秒） | 0.3 |
| 下一目标半径（cm） | 400 |
| 每跳伤害保留比例 | 0.8 |

此时图仍是版本1，参数尚未提交。“预览画布编辑”应可点击。鼠标停在画布上时滚轮负责缩放；查看下面表单可拖动整个面板右侧滚动条。

![选中投递节点并填写传播参数](images/runtime-workshop/02-selected-delivery-node.png)

## 3. 预览草稿，再应用

点击“预览画布编辑”。图应显示紫色背景和“草稿 · 尚未应用 · 版本2”；第三个节点已显示候选 `Propagation.Sequential`。核对后点击“应用草稿”。“放弃草稿”则保留原技能。

![紫色草稿与应用草稿按钮](images/runtime-workshop/03-propagation-draft.png)

## 4. 核对已接受图，返回游戏试放

应用后图应恢复蓝色，显示“服务器已接受 · 版本2”，第三个节点变为 `Propagation.Sequential`。本例替换投递策略，节点和边总数保持5和4，是正常结果。

拖动滚动条回顶部，点击“隐藏面板 · 按 T 施法”，鼠标瞄准敌人并按 **T**。火球命中后等待0.3秒，从命中位置向400cm内下一目标发出新的火球，本例最多包含首击在内的3个目标，每跳伤害保留80%。该测试组合使用临时 T 技能，原左键技能仍保持原样。

![应用后服务端接受版本2的新投递策略](images/runtime-workshop/04-applied-propagation.png)

再次打开属性第四页可继续修改、撤销或重置。“隐藏面板”保留临时技能；“关闭测试”及窗口“关闭”清理它。

## 节点、端口、连线分别怎样操作

- 点击节点本体：修改该节点参数，或替换它实际支持的策略。本例的 `Propagation.Sequential` 从投递节点本体选择。
- 点击输出端口：选择此端口支持的并行新增片段。可用模板取决于真实载荷类型。
- 点击已有连线：选择 `Insert.Registered`，在这一条边上串接支持的片段，例如石化；先应用后，新节点还能被继续选择和修改。

输出端口的实际选择界面：

![点击输出端口显示其片段操作](images/runtime-workshop/02-selected-output-port.png)

本次验证包含68项相关自动化、正常游戏地图直接 PIE 的5项入口检查、单机103项和Listen远端111项真实界面/施法检查。测试过程中职业始终为 `Unselected`，没有先设成 Mage；详细证据见[验证记录](runtime-graph-workshop-validation.md)。最终手感和视觉仍由用户 PIE 验收。
