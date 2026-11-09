---
name: build-ga
description: Use when creating, modifying, integrating, or validating Unreal GAS GameplayAbilities (GA) in test_25d or a project with its MyGameplayAbility/MyAbilitySystemComponent architecture, including ability Blueprints, GameplayEffects, tags, input, skill menus, and save integration. 适用于按自然语言需求自动构建、调整和验证本项目游戏技能；不用于遗传算法或普通打包。
---

# 按需求构建 GameplayAbility

将用户的技能需求落实为可执行代码、UE 资产、项目接入和可核查的验证结果。支持新建、复用派生、参数调整、接入和修复。收到“做一个 GA”就推进实现；设计说明是中间产物。

本技能基于 test_25d 的 ArcaneNova 制作流程。工程根目录取当前 `.uproject`，不要取本技能安装目录；模块、引擎版本和可用工具以当前文件/连接读回为准。

## 1. 确定本次交付

先读工程内 `docs/ga-unified-authoring-guide.md` 和 `docs/ga-framework-reference.md`，再按 `docs/ga-production-and-runtime-guide.md` 的制作检查表与 [项目接入地图](references/project-map.md) 定位现有 GA，检查工作区已有变更。整理一份简短构建记录，写到 `Saved/GAAuthoring/<Ability>/<本次唯一标识>/`，包含：

- 操作：新建/修改/接入；内部英文标识、显示名、原型、目标 GA 路径。
- 行为：触发、选点、目标筛选、范围/射程、伤害类型与等级曲线、消耗/冷却、空放规则、持续/取消规则、网络策略。
- 接入：玩家/敌人/被动，初始授予或可学习，输入槽、技能树位置、存档、表现资源。
- 范围：本次修改的代码与资产、复用对象、明确假设和验收场景。

从现有实现推断非关键缺省值并简短告知。用户没要求初始装备时，玩家技能默认作为可学习技能登记；不自动占据按键。用户只给基础数值而未提升级时，先按固定值实现并说明。只有影响技能含义且现有上下文无法判断的选择才提问，并继续不依赖答案的工作。

## 2. 选择实现路径

按 [原型与生命周期](references/ability-patterns.md) 选择继承/蓝图/原生实现。

| 请求 | 最小变更路径 |
| --- | --- |
| 改耗蓝、冷却、伤害或表现引用 | 读真实 GA→GE/曲线/CDO；只改必要资产，按引用关系处理共享资源 |
| 同行为的新技能 | 复用注册策略，创建独立 Schema 2 图/参数和 GA 身份；原动画前端可共用实际动作服务 |
| 新行为 | 实现 UAbilityNodeConfig 校验和 UAbilityNodeExecution，登记真实执行类、类型化端口、目标与寿命；新增行为测试 |
| 投射物、近战、光束、召唤 | 查统一注册表中现有策略；动画/输入前端在真实 Notify 或 Timeline 时点执行，禁止嵌套激活完整旧 GA |
| 被动、光环、Buff | 采用持续生命周期及被动装备流程，设计效果句柄回收 |

能力只有一个实际伤害入口。新技能完整 Commit 一次，旧技能的分阶段费用等兼容合同以原资产和框架说明为准；不要借迁移改变费用或命中次数。服务器结算效果，成功/失败/取消均正确收尾。命中历史快照接活目标控制时必须经过 Target.ResolveImpact/Target.ResolveDamageTarget 复核原生命。按需求补行为测试；参数微调使用资产读回和相关运行检查。

## 3. 执行构建与资产接入

1. 完成必要 C++、原生标签、测试及描述。出现新反射类型/原生标签时，完整构建 Editor target 并重启加载，再创建引用它们的资产。
2. 按 [编辑器自动化](references/editor-automation.md) 绑定当前工程和编辑器实例；发现真实工具 schema，检查相关 CDO、蓝图执行流、技能目录和槽位。
3. 在本次目录保存共享资产快照、变更清单和读回基线。生成本能力专用、可重入的资产脚本，逐项创建/更新、编译和保存，再用独立进程读回；不要依赖同进程 ReloadPackages。原生 CDO 构造期不要加载会引出蓝图依赖的素材，引用由资产配置，避免早于原生标签注册。
4. 登记 GA/GE/Cue，按明确需求接入授予、输入、UI、存档。同路径或同身份已存在时核对再更新；不同对象冲突时保留并选择未占用位置或询问具体冲突。
5. 队列超时后用原 job_id 查询；未知结果先检查任务和资产。`console_exec` 回执只证明接收命令，完成以新鲜结果文件和独立读回为准。

优先使用已连接的 SOMOLMCP 工具；未暴露连接器时使用随附 `python scripts/mcp_call.py --help`。脚本路径相对于本技能，`--project` 显式传工程。脚本只负责可靠调用，GA 的创建与验证由上述流程执行。

没有编辑器/构建工具时，完成可执行源码与资产脚本，列明未执行阶段；恢复连接后继续。不要把脚手架或工具回执标为完整 GA。

## 4. 验证与交付

按 [验证与恢复](references/verification.md) 执行与变更匹配的资产检查、自动化测试和运行验证。用户要求联机时，将客户端发起、权威端单次结算和远端表现列为独立验收项。

最终给出技能行为、文件/资产路径、试玩方法、测试证据，以及剩余未验证项。新技能附简短设计/实现说明；仅改参数不强制新增长文档。保持工作区原有变更，不自动提交或推送。

## 敌人、装备与当前项目边界

- 制作敌人 GA 前阅读工程内 `docs/monster-development-standard.md` 和 `docs/monster-production-workflow.md`，检查最新 EnemyCharacter、对象池及授予入口。普通单体攻击有感叹号，范围/位移攻击有红色实际范围；锁定、命中和取消时序需与真实动画匹配。死亡/回收必须作废旧攻击回调，复用不能重复授予。
- 组合技能使用统一 Schema 2 的注册策略和端口。前四批/第五批旧 DA 通过兼容编译接入；跨族连接写入新图，不混填旧指针字段。目录登记、共同会话或继承同一父类均不能证明派生 GA 的专属动作已可组合，必须检查覆写和真实执行函数。不要遗漏根 GA、目录及实际输入接入。
- 按用户项目约定，后续开发在本地 main 进行；用户要求本地合并不等于授权远端 push。保留已有 Content 调参和未提交资源。
- 装备的常驻属性加成走既有 EquipmentComponent/Infinite GE，无需为每条属性创建 GA。涉及装备时使用工程内 `skills/build-equipment/SKILL.md` 和 `docs/equipment-production-workflow.md`；触发/主动装备能力尚不是现有数值装备通路的一部分。
- 验证分别记录代码、资产、实际属性/伤害、客户端请求和视觉验收。旧报告或收到命令回执不算本次通过。
- 仓库 `skills/build-ga` 为维护源；同步已安装副本时保留现有 references/scripts/agents，不用单个 SKILL.md 替换整个技能目录。更新后运行 quick_validate，并检查引用可达。

## 调用示例

`$build-ga 新建奥术脉冲 ArcanePulse：自身周围400范围，魔法伤害60，耗蓝20，冷却5秒，可在技能树学习，多人可用。沿用项目现有表现。`

`$build-ga 只把 GA_FireBolt 的耗蓝改为12，保留现有伤害、冷却、输入和技能树。`

`$build-ga 新建可装备的防护光环，提升自身护甲，卸下后移除增益，兼容保存和读取。`
