# 精英守护光环与中文存档界面

1. 在现有 Actor 精英词缀池增加守护光环，600 单位内同房间的其他 Actor 敌人在生命扣减前减伤 25%。精英自身、Mass 敌人和玩家不受增益。复用现有精英模型、原攻击预警和死亡路径。服务端按当前距离和存活状态判定，无周期计时或遗留效果。
2. 让强化精英使用其现有 HealthBar 组件同时绘制生命和中文说明。说明随词缀及狂暴状态同步，普通敌人沿用自己的血条。显示使用引擎 Roboto 与 Droid Sans Fallback 组合字体。
3. 存档输入框和名字展示切换至支持中文的组合字体，保留 Unicode 存档格式与固定槽 ID。损坏或类型错误的槽显示中文状态，并保留显式删除路径。新增测试覆盖 Unicode 文件往返、损坏档隔离、光环距离及生命周期。
4. 编译并运行原生回归。在编辑器检查中文血条和存档页面。Windows 输入法组合、打包版和确认未复现的原始崩溃栈分别记录验收结果。

## 验证结果

- UE5.8 Development Editor 编译成功。Roguelike 和 Rooms 共 58 项通过，18 项带警告。守护词缀测试覆盖有效范围、跨房间、来源自身和死亡清理。中文存档测试通过 GameMode 写入并重载独立的临时槽，核对 ViewModel 名称原样保留，并验证不兼容文件可标记和删除。
- 双玩家 Listen Server PIE 强制生成守护精英，双方得到一致的中文词缀，两波均在击杀精英后推进，死亡后清除血条。Standalone PIE 使用中文样本检查存档输入框实际字形。
- Windows 输入法候选框和组合输入仍需人工验证。尚未构建打包版，也未定位用户原始崩溃的调用栈。本次修复覆盖进入存档页面时解引用空读档结果的问题。

存档标题从 `FText` 转为 `FString` 后交给 Unreal 的 SaveGame 序列化。中文覆盖由控件的组合字体和打包时的 ICU 数据决定。 [UE 字符编码](https://dev.epicgames.com/documentation/unreal-engine/character-encoding-in-unreal-engine) · [UE 字体本地化](https://dev.epicgames.com/documentation/unreal-engine/asset-localization-in-unreal-engine)
