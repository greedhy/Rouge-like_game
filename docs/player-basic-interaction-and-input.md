# 玩家基础交互与输入约定

2026-11-04。适用于原Player、Stander及其普通/生成房间/Mass控制器。

## 基础功能复用

Stander继承现有PlayerCharacter、PlayerState ASC、背包、装备、升级、存档以及房间倒地/复活流程。模型、动画和武器技能可以不同；拾取、交互和基础输入不应依赖模型是否具有Physics Asset。

玩家在PostInitializeComponents启用胶囊重叠。保留原角色的模型碰撞与敌人碰撞规则，不修改源模型资源。WorldPickupComponent继续通过现有服务器重叠登记和拾取请求把物品放入PlayerState背包；装备仍保留原实例及词条。

本次已复现的故障：Stander没有Physics Asset，原共享基类又关闭胶囊重叠，导致没有可登记的拾取重叠。隔离PIE中，同一武器、同一玩家、同一拾取RPC，开启胶囊前失败，开启后成功。

## 输入规则

- WASD控制常规移动。左键点击、长按和松开均不触发朝鼠标移动或导航寻路。GA自己的突进和跳跃位移保持原技能行为。
- 鼠标仍用于瞄准、目标选择及UI。LMB槽技能直接用左键释放，无需Shift，也不要求光标悬停敌人。RMB与数字槽保持原绑定。
- 普通与Mass控制器都向GAS发送按下/持续/释放事件；开关技能继续使用按下事件，持续技能继续接收释放事件。
- 打开背包或输入封锁会阻止新的移动和施法；已经开始的技能仍会收到松键事件，避免持续技能卡住。背包键仍可关闭背包。
- E拾取与既有背包快捷键不变。新角色必须复用这些输入资产和服务器请求，不从资源包示例复制独立拾取逻辑。

## 本轮验证

根因证据：`Saved/GAAuthoring/StanderWeapons/pickup-root-cause.json`。

新增`test_25d.Player.BasicInput.KeyboardOnlyAndDirectLMB`覆盖两种控制器空地左键激活、键盘移动、左键无移动、UI/控制封锁、释放与开关技能，以及原Player/Stander运行初始化后的胶囊重叠。`Scripts/GA/StanderWeapons/verify_basic_interaction.py`验证真实Stander拾取进入背包、背包UI及输入目录。

最终构建通过。`Saved/GAAuthoring/StanderBasicInput/NativeFinal/index.json`：34项通过（22项无警告、12项带警告），0失败，涵盖新增输入用例、Stander、装备背包和房间玩家生命周期。

`pie-interaction.json`：真实Stander胶囊重叠、拾取RPC、武器进入背包及背包UI开关通过，测试后恢复背包快照。

`pie-enhanced-input.json`：在隔离自动化存档的实际L_RoguelikeRun试炼注入Enhanced Input动作，未按Shift的LMB正常播放攻击蒙太奇且位移0 cm，键盘动作移动约124 cm。最初临时场景缺少房间就绪状态、输入被封锁，不能用于此项验收，已改为实际试炼复验通过。

自动化PIE结束后恢复原地图；人工操作手感及多人双端交互仍由后续PIE验收。本轮没有把所有基础系统逐一重新实现；它们继续沿用共享玩家基类和已有数据通路。
