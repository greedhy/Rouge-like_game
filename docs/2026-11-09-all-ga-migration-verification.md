# 全量 GA 迁移与验证记录

本记录对应用户要求：迁移现有所有 GA，完成自动化后合并本地 `main`，后续沿同一可扩展路线开发。允许将原蓝图行为改为 C++ 并保留原效果、资产身份与配置；没有本轮远端推送授权。

实现提交 `ed93126e411dc32b2e1769ab01836d110bb5d877` 已在合并前 385 项全回归通过后快进合入本地 `main`。合并后使用正常 Unity 分组重新构建，并再次通过全部 385 项。本轮未 push；以下收口记录继续在 main 维护。

## 实现范围

共同入口为 `UMyGameplayAbility` 的激活会话，行为通过 Schema 2 注册策略或同一共享动作执行。Actor/Mixed 前四批与第五批旧 DA 由兼容编译器冻结并生成图；第六批、传统法术、Stander/剑盾/武器、怪物专属攻击、属性监听与受击 GE 均有明确实际路由。选择器和基类记录管理职责，不伪称攻击节点。

原型 Runtime Actor 保留复制表现与兼容接口，实际行为移至 Driver；新开发不继续复制旧 Kind 分支。来源、目标和异步回调绑定世界、ASC Avatar、RunId 与池租约/实体句柄，取消只清理本次资源。

本轮同时修复了实测或审查确认的问题：伤害观察者回收目标后误击新生命；同步取消/重激活后旧回调清理新会话；并行位移抢占与错误恢复；眩晕撤销预警后遗留移动锁；独立怪物节点的显示范围与判定面不一致；蓝图换父类时继承默认值覆盖原实例/网络策略。

## 资产与原行为保留

本轮必要写入严格限制在以下六个原 GA，不改原路径、身份或无关素材：

| 原资产 | 处理 |
|---|---|
| `Enemy/Ability/GA_HitReact` | 原生受击前端，保留原 GE、蒙太奇与 BlendOut 清理时点、预测键路径 |
| `Enemy/Ability/GA_SummonAbility` | 显式配置原蒙太奇、地面效果、Notify 和召唤间隔 |
| `Player/Ability/Arcane/ArcaneChard/GA_ArcaneShards` | 原生碎片前端，保留原点集合、数量、间隔、费用/冷却时点 |
| `Player/Ability/Passive_Startup/GA_ListenForEvent` | 原生属性监听，保留可达事件分支、五个数值标签、零填充、等级 1 |
| `Player/Ability/Passive_Startup/GA_Listen_New` | 原生 XP 监听，保留精确事件匹配、等级 0、原实例与网络策略 |
| `Player/Ability/Thunder/GA_Electrocute` | 显式配置原蒙太奇、周期及最短持续时间 |

上述相对目录均位于 `/Game/BluePrints/AbilitySystem`。原 K2 图保留供审计，已迁移的原生 Activate 不再执行旧 K2 激活链。召唤/近战无额外 Commit、Melee 击退概率分支的额外命中、Arcane 首片后费用与尾延迟冷却、Electrocute 原周期/结束命中等历史差异都显式保留。

资产每次保存前备份，校验原图语义、变量及受保护 CDO 配置。编译可能重建 PinId 和数组顺序，语义摘要忽略这些临时身份，仍比较节点身份、连线目标、类型和默认值。保存后另起进程读回，不使用同进程 ReloadPackages 证明落盘。

## 验证范围

最终报告与 [迁移矩阵](ga-unified-migration-matrix.json) 按源码/资产指纹对应。中途失败报告用于诊断，不替代最终树验证。

| 检查 | 最终结果 | 本地证据 |
|---|---|---|
| main 已提交树完整 Editor 构建 | Succeeded，正常 Unity 分组 | `Saved/Logs/Build-UnifiedGA-main-committed.log` |
| main 全项目 test_25d 回归 | 385 项 Success；0 failed/notRun/inProcess；166 无警告、219 带警告 | `Saved/Automation/UnifiedGA/20261108T184219-4efd824a/index.json` |
| 被测输入绑定 | 1174 个文件前后 SHA256 一致，含 Source/Config/DLL/605 个目录资产 | `Saved/GAAuthoring/UnifiedGA/20261108T184219-4efd824a/validation.json` |
| 全部 GA 蓝图 | 302 项内存编译成功，原图语义与磁盘包不变 | `Saved/GAAuthoring/UnifiedGA/BlueprintValidation/20261108T182834Z-f79f2d32/index.json` |
| 全部配置 | 303 项原生校验或明确运行引用合同通过 | `Saved/GAAuthoring/UnifiedGA/ConfigurationValidation/20261108T184247Z-45C4908540BD0A382507D4A6685F03F4/configuration-report.json` |
| 最终目录与独立读回 | 659 项、123 个旧配置/profile 编译；6 个已保存 GA 来源核验通过；无未接入动作 | `Saved/GAAuthoring/UnifiedGA/CatalogMigration/20261108T185937Z-37270169/catalog-report.json` |
| 迁移矩阵收口 | 302 个 GA 蓝图、54 个原生类、303 个配置，共 659 行 Verified；整表 schema 通过，逐行保留实际目标/生命周期/证据及待验收边界 | `Saved/GAAuthoring/UnifiedGA/FinalMatrix/20261108T185948Z-3bd077be/finalization-report.json` |
| 迁移审计脚本自测 | finalizer 14 项、迁移器 31 项、目录审计 9 项全部通过 | `Scripts/GA/Unified/test_migration_finalizer.py`、`test_catalog_migration.py`、`audit_catalog.py --self-test` |

配置验证中，8 个 MonsterCombat/StoneGolem 资产没有统一 Validate 函数，报告明确记录真实引用/有限数值合同，不声称调用不存在的校验器；3 个时间轴按真实关联蒙太奇校验。旧字段的配置校验保持已有规则，没有借迁移扩大数值范围。

保存链来自 `CatalogMigration/20261108T182312Z-1cc4375c` 中确实已保存的四项，以及 `20261108T182645Z-9db92af8` 的两项。第一轮未保存的失败项不计作证据；最终另一个 UE 进程逐项核对原始备份、原图/属性与当前磁盘哈希。报告和备份均位于本机 Saved；没有把素材包或这些临时文件上传远端。

自动化代表共享策略、生命周期、实际伤害/状态、资产配置和接入回归，不表示每个技能的全部动画/特效已逐个视觉验收。每行标明具体用例与适用范围。

矩阵 `Verified` 按上述范围定义。原路径内迁移的 `replacementPath` 为 null；实现路由单列于证据，不能把内部路由名称当作 UE 资产路径。旧 Melee/Nova/Shards 等实际前端仍为 Actor-only，不能把节点可选的 Mass 模式误写为旧技能既有支持。最终文档/矩阵整理不改变被测 Source、Config、DLL 和资产；1174 个被测输入继续与最终回归报告一致。

## 验收边界与后续入口

用户 PIE 视觉验收及真实双端联机仍单列 Pending。重点检查原有技能输入/装备切换、修改时间轴后的动画时长、怪物预警与接触点、取消后的移动恢复、对象池第二次生命，以及监听/受击在远端的可见状态。

启动日志仍有既有素材与插件警告，包括 StructUtils 依赖/弃用、旧 Rig 导入和旧标签。`GA_ListenForEvent` 的构造期提前加载来自旧 `ExtensionIntegration`；已核对 UE 5.8 保留未注册标签名字，并在独立读回检查实际字段，不把这些警告当作标签已丢失，也不宣称日志无警告。

后续只按 [统一制作指南](ga-unified-authoring-guide.md)、[框架与调用链](ga-framework-reference.md)、[源码地图](ga-composition-code-map.md) 和仓库 `skills/build-ga` 开发。安装版 skill 同步保留其他脚本/引用；项目 `AGENTS.md` 明确本地 main 开发约定。原始用户 Content 调参、资源包、图片及 LFS 规则保持原有未提交状态。
