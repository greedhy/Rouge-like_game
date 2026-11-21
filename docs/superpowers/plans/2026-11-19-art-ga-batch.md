# 美术驱动 GA 制作

本批组装三个法师技能和三个鬼剑士风格的近身技能。使用 Schema 2 注册策略、独立 GA/Graph/GE 与现有图标，按固定一级数值制作，作为可学习技能进入 Prototypes 页面。施法通过服务器完整 Commit 一次，独立图有界结束，来源失效则取消。

法师技能使用冰霜法球、火旋风和雷电法阵，参考 Warcraft III 的持续范围压制与暗黑类游戏的投射物及终结爆发。近身技能参考 DNF 鬼剑士的剑气、血气和鬼阵，制作短距离扇形二连斩、近身爆发加地面持续、固定灵魂阵加结束爆发。近身技能从角色面向或脚下出发，当前批次为法术/剑气动作，完整剑术蒙太奇连段留待专用动画前端接入。

步骤为资产类型与引用检查、配置及图校验、生成并编译资产、独立进程读回、真实激活与伤害/费用/结束检查、菜单与实机特效验证。MCP 优先读取现有编辑器，新标签在独立进程加载后再生成资产。

参考 [Warcraft III 大法师](https://classic.battle.net/war3/human/units/archmage.shtml)、[DNF 男鬼剑士](https://www.dfoneople.com/gameinfo/character/Slayer%28M%29)、[DNF 技能强化分类](https://www.dfoneople.com/gameinfo/guide/Start-Leveling%21/Enhancement-System-Skills)。
