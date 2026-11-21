# 肉鸽战斗房随机精英强化

## 同类游戏参考

| 游戏 | 可借鉴的机制 | 在本项目的用法 |
| --- | --- | --- |
| 暗黑破坏神 IV | 精英词缀可以改变战场，例如筑墙。暴雪也在精英系统更新中强调词缀辨识与随从互动。 | 给每种词缀独立标记。后续可做有持续时间的阻隔，以及能被优先击破的支援精英。 [2.0 更新日志](https://news.blizzard.com/en-gb/article/24140808/diablo-iv-patch-notes-2-0) · [精英敌人更新](https://news.blizzard.com/en-gb/article/24243142/sanctuary-ignites-with-itemization-38-systems-changes) |
| 死亡细胞 | 精英保留原招式，再获得随机精英能力，例如闪电墙、分身、护盾柱。 | 保留现有怪物 AI 与攻击预警。后续由怪物类型限制增补的 GA 池。 [Update 11](https://dead-cells.com/patchnotes/11) · [Update 14](https://dead-cells.com/patchnotes/14) |
| 以撒的结合 | 冠军用颜色区分，有死亡爆炸、治疗同伴等变体。 | 词缀用颜色和文字共同识别。后续的死亡爆炸提前显示实际红圈。 [社区 Wiki 冠军汇总](https://bindingofisaacrebirth.wiki.gg/wiki/Champions) |
| Last Epoch | 半血狂怒、邻近盟友免伤、半血分身。雷电词缀限定在雷系敌人。 | 本批实装半血狂暴。后续按怪物类型筛选主动词缀，并用房间难度限制词缀组合。 [官方怪物词缀](https://forum.lastepoch.com/t/the-convergence-update-beta-0-9-patch-notes/51975) · [官方冠军词缀](https://forum.lastepoch.com/t/last-epoch-tombs-of-the-erased-patch-notes/75247) |
| 恶意不息 | 终局瘟疫影响区域，清理部分敌人后仍需击败该区域的瘟疫首领。 | 借鉴区域威胁和分段清场。精英作为房间必杀目标，随现有波次推进。 [The Breach 官方更新日志](https://forum.norestforthewicked.com/t/patch-notes-the-breach/15046) |
| 挺进地牢 | 诅咒值影响 Jammed 出现概率，变体以深红色和黑烟识别。 | 让第二波精英概率可配置。后续可接入局内风险等级。 [社区 Wiki 诅咒](https://enterthegungeon.wiki.gg/wiki/Curse) · [Jammed](https://enterthegungeon.wiki.gg/wiki/Jam) |

## 本批实现

普通战斗房首波保证一只 Actor 精英，第二波默认有 35% 概率再出现一只。第二波沿用必杀账本，在首波所有必杀目标死亡后开始。每次以精英替换本波一个普通 Actor 名额，Mass 近战数量保持原值。专用精英房的固定精英也获得一个词缀。Boss 房、休息房、商人房、宝箱房与起始房保持现有规则。

楼层种子、局号、房间索引和波次决定抽样结果。同一局重复计算得到同一结果，使用同一张地图开启新局也会重新抽取。精英使用 Profile 的现有 EliteClass 和精英掉落规则。普通战斗房的精英基础生命默认 180，专用精英房沿用 EliteHealth。每只精英按权重抽一个词缀。

| 词缀 | 效果 | 头顶标记 |
| --- | --- | --- |
| 坚韧 | 出生时当前及最大生命乘以 1.5 | 金色 FORTIFIED |
| 狂暴 | 首次降到半血时提高移动速度 35%，持续至死亡。眩晕、受击、地形定身及 Slow 继续按现有移动规则生效 | 红色 FRENZY，触发后 ENRAGED |
| 再生 | 最近一次受伤满 3 秒后开始恢复，每秒恢复最大生命的 3%，再次受伤重置等待，满血时停止 | 绿色 REGENERATING |

### 开发顺序

1. 增加 Profile 权重、概率和普通战斗房精英生命配置，测试固定种子、边界概率、房间过滤与配置错误。
2. 将词缀写入现有服务器生成请求。测试两波预占名额、精英击杀门槛和普通 Mass 数量。
3. 在 Actor/GAS 上实现强化和词缀显示。测试属性、半血与眩晕叠加规则、再生计时、死亡和销毁清理。词缀精英走现有非池化死亡路径。
4. 编译项目并运行 Roguelike 原生回归。使用真实地牢数据资产验证配置默认值。双客户端 PIE 验收词缀显示、攻击原动画和第二波视觉节奏。

后续扩展候选包括护盾柱、可见延迟的死亡爆炸、冲锋、分身和精英光环。接入前分别确定可用怪物、冷却与并发上限，按现有敌人 GA 预警、房间边界和生命周期规则实现。

## 配置与验证

在编辑器打开 `/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay`，展开 `Elite Affixes`。可调整开关、第二波额外精英的概率、普通战斗房精英的基础生命和词缀权重。第二波概率为 0 时每个普通战斗房只有首波精英，设为 1 可稳定验证连续两波的精英。权重用于相对抽样，删除一个词缀条目便可将其移出池。

- UE5.8 Development Editor 构建成功。`test_25d.Roguelike` 共运行 48 项，全部通过，其中 18 项带警告。新增的三个用例分别覆盖真实 Profile 抽样、导演的分波生成队列和 Actor 强化生命周期，均无警告。
- 在 `L_RoguelikeRun` 的双玩家 PIE 中临时把第二波概率设为 100%。两波各出现一只 Actor 精英和 25 只 Mass 哥布林。单独留下精英时房间保持当前波次，击杀后推进下一波或清场。两端均收到相同词缀并在死亡后移除标记。另用带渲染的 PIE 截图核对了首波再生精英的头顶显示。
