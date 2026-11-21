# 直接 PIE 技能组装修复计划

用户路径：不经过存档选职业，直接 PIE → 属性第四页 → 选择法师 GA → 开始测试。已在实际 `L_RoguelikeRun` 复现：默认 `BP_DungeonPlayer` 是 Elementalist，PlayerState.Profession 仍为 Unselected；服务端先拒绝职业，未生成图。

范围：开发工作台在未选职业时识别属于同一 PlayerState/ASC 的真实法师玩家 Avatar，不写职业或存档，不放开显式近战职业。状态错误移至开始按钮下方。沿原图与 T 输入施法，不修改 GA 资产。

- [x] 实际游戏路径确认红色失败证据，记录角色类型、职业和服务端回执。
- [x] 新增未选职业法师的 Begin/草稿/应用/关闭回归，保留近战和错误 Avatar 的拒绝覆盖。
- [x] 最小修正服务端资格校验及错误位置，完整构建。
- [x] 单机与远端真实组装/施法测试不再预先强设 Mage，验证职业始终未选择。
- [x] 实际截图展示开始后基础图、选端口/节点、草稿与应用；更新操作文档并恢复编辑器状态。

最终68项（21普通通过+47带警告通过），正常地图直接PIE5项，单机103项、远端111项（均无Mage预设）通过。详见 `docs/runtime-graph-workshop-validation.md`，截图逐步说明见 `docs/runtime-graph-workshop-visual-walkthrough.md`；原地图、后台节流和窗口尺寸已恢复。只读审查未发现需修改项，用户PIE验收待完成。
