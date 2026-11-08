# 可选玩家与近战角色素材可行性

检查日期：2026-10-31。范围为本地资产与玩家接入代码审计；不创建角色、不改技能或生成配置、不打断正在运行的PIE。

## 结论

用户所指人物包在当前工程的实际目录为 `/Game/RPGHeroSquad`。现有素材足以制作首版剑盾近战玩家。优先TinyHero/TinyHeroGirl，共用一套近战技能与动画，先作为两种外观；不同技能职业需要另外设计。

包内8个模型是RPGHero、TinyHero、TinyHeroGirl、AnimalHero（模型名Dog）四种形象各自的PBR/Polyart材质风格版本，不是八套独立职业。共3套Skeleton、61段AnimSequence、8个示例Blueprint；未发现包内AnimBP、BlendSpace或Montage。

## 实际资产读回

| 形象 | 动画数 | 已有主要动作 | 适合范围 |
|---|---:|---|---|
| TinyHero / TinyHeroGirl | 共用38段 | Attack01/02/03、Attack04及Start/Spinning片段、防御/格挡受击、战斗四向移动、冲刺、跳跃、受击、死亡/起身 | 首选剑盾玩家，可设计三段普攻、防御和旋转斩 |
| RPGHero | 12段 | 两段普通攻击、待机、走跑冲刺（IP/RM）、受击、死亡/恢复 | 简化近战；复杂连招、防御和闪避需补动作或重定向 |
| AnimalHero / Dog | 11段 | 两段攻击、防御、前进走跑冲刺、受击、眩晕、死亡/恢复 | 后续特殊外观近战角色，移动和战斗动作较少 |

TinyHero的Attack01/02/03各约0.533秒，Attack04约0.533秒，旋转片段约0.333秒，起手约0.200秒；这些是片段长度，不是已确认的命中时间。三套骨骼彼此独立，TinyHeroGirl与TinyHero共用TinyHero_Skeleton。

TinyHero已有Weapon_L、Weapon_R等插槽和配套静态剑盾模型。项目基类Weapon是SkeletalMeshComponent且默认附着WeaponHandSocket，不能只替换模型而忽略武器组件与插槽适配。

此外，`/Game/Assets/Characters/Aura/Animations/Motion/01_Sword_Shield` 有29段剑盾动画：三段攻击、格挡/弹反/格挡受击、翻滚/闪避、跳跃攻击以及8个Skill命名片段。已有 `ABP_Player_SwordShield`。这套资产使用SK_Aura骨骼，不能直接赋给TinyHero；可作为后续重定向候选，实际脚步、手握武器、比例和接触质量需预览。文件名中的Skill不代表已有对应GA。

## 接入缺口

1. 8个示例BP的父类均为Engine.SkeletalMeshActor，属于模型展示资产；没有项目玩家Character、输入、PlayerState ASC、房间倒地复活通路。应复用现有玩家体系，房间玩法保留DungeonPlayerCharacter能力，替换视觉/动画和技能配置。
2. 新建专属AnimBP、移动BlendSpace、攻击Montage与Slot。包内攻击动画没有现成Notify，需逐帧预览确定命中窗口、连段输入窗口、取消窗口及表现时序。
3. 复用PlayerState ASC、GAS伤害、输入和装备数值通路，补近战GA：朝向锁定、挥砍查询、每段去重、墙体遮挡、打断和收尾。静态剑盾外观不能自动等同于背包装备实例。
4. 可选角色还需角色身份/配置资产、选择UI、出生应用与保存恢复。源码中已有Elementalist/Warrior/Ranger枚举，但不能据此认定已有完整角色选择系统。
5. 普通移动先用InPlace动画与CharacterMovement。TinyHero包未发现明确命名的翻滚动画；若首版必须翻滚，需重定向Aura动作并验证，不能直接承诺动作已可用。

## 推荐首版范围（候选，尚未实施）

- TinyHero或TinyHeroGirl剑盾角色，另一形象可共用玩法作外观选项。
- Attack01/02/03作为三段普攻候选；Defend/DefendHit支持防御表现；Attack04系列作为旋转斩候选。
- 参数使用UE数据资产，配置模型、AnimBP、武器挂点、基础属性、初始GA、技能伤害/范围/时序。
- 先确定角色与技能设计，再预览真实武器动作校准命中，随后实现、自动化和用户PIE验收。不要预先将三段动画长度当作战斗节奏最终值。

## 证据与验证边界

审计脚本 `Scripts/GA/audit_player_candidate_assets.py`；读回 `Saved/PlayerAssetAudit/rpg-hero-inventory.json`。已实际加载包内骨骼/模型/动画、读取骨骼关系与时长、求值动画首帧、读取示例BP父类和玩家AnimBP骨骼；合计109条相关记录、最终0读取错误。首轮访问受保护Sockets属性失败，改为临时SkeletalMeshComponent读取公开socket/bone列表后成功。只加载和读取资产，无资产保存或关卡切换。

尚未验证：新模型在游戏镜头下的观感、逐帧攻击接触、根运动与碰撞表现、重定向质量、角色选择运行链路。素材可行性结论不代表新角色已完成或可直接加入正式名单。
