# UI、Widget 与 MVVM 架构盘点

盘点日期为 2026-11-13。以本地 `main` 当前工作树为基线，包括已存在的未提交资产改动。检查范围包含 `Source/test_25d`、项目配置、现有文档、UE 5.8 Asset Registry 的 `/Game` 内容，以及实际加载的 41 个 Widget Blueprint 和 25 个相关 Blueprint。原始图快照、Widget Tree、资产引用和默认类配置位于 `Saved/UIArchitecture/2026-11-13/inventory.json` 及同目录的逐资产 JSON。资产引用用于识别依赖和候选入口，运行时可达性需在对应地图验证。

## 迁移进度

首批存档界面现由 `ALoadScreenHUD::BeginPlay` 调用 `ULoadScreenWidget::InitializeSaveScreen`。槽位三态、按钮、确认框和返回主菜单由原生 Widget 处理。原生代码按设计器控件名接线，保留这些控件原有的蓝图属性类型。六个资产移除了已迁移的行为图。四个存档 Widget 的 7 条声明式 MVVM 绑定和 `WBP_LoadScreenWidget_Base` 中用于绑定的 `FindLoadScreenViewModel` 属性路径继续使用。下方盘点和逐资产统计保留迁移前基线。

游戏内属性、技能、背包与装备面板使用 `AMyPlayerController` 的输入路由。UMG 先处理鼠标交互，控制器消费剩余的左键和右键按下事件。键盘继续走游戏输入，即使焦点位于菜单子控件。属性和技能菜单通过 `UGameplayMenuWidget` 按显示状态注册，背包和装备随各自 Widget 注册。Overlay 中原有的四个输入模式节点已移除。存档、职业和房间仍使用自己的菜单输入模式。

人物窗口现由 `AMyHUD` 创建和持有。左上角提供属性、装备、技能三页，原属性和技能设计器资产嵌入原生页签容器，背包点击可装备物品时打开第二页。窗口统一管理输入注册和关闭，旧 Overlay 的独立开关分支已移除。装备效果使用实例保存词条或固定 GE，多个兼容槽位可在右侧切换对比。

## 创建与数据流

| 界面 | 创建入口与数据通路 | 当前实现 |
| --- | --- | --- |
| 标题与主菜单 | 默认地图 `/Game/levels/MainMenu` → `BP_MainMenuActor` → `WBP_Title`、`WBP_MainMenu` → 开始进入 `LoadMenu` | Actor 蓝图创建界面、设输入模式并负责场景表现。主菜单蓝图处理开始和退出。旧 Characters/Player 路径是重定向。 |
| 存档 | `BP_LoadScreenGameMode` → `BP_LoadScreenHud` → `ALoadScreenHUD::BeginPlay` → 创建 `UMVVM_LoadScreen` 和三个 `UMVVM_LoadSlot` → 创建 `WBP_LoadScreen` → 调蓝图初始化 → `LoadData` | C++ 负责存档、选槽和职业流程。蓝图负责初始化子视图、存档槽切换、按钮接线、确认弹窗与输入焦点。 |
| 职业、模式、房间和大厅 | `UMVVM_LoadScreen` → `ALoadScreenHUD` → `WBP_ModeAndRoom`，继承 `URoomMenuWidget` → `URoomSubsystem`。`RoomLobby` 直接创建原生 `URoomMenuWidget` | 房间 Widget 的布局、交互和页面状态在 C++。HUD 的房间 Widget 类可配置。 |
| 战斗 Overlay | 普通或 PureMass 玩家角色初始化本地 ASC 后 → `AMyHUD::InitOverlay` → 创建 `WBP_Overlay` → 注入 `UOverlayWidgetController` → Widget 订阅 → 广播首帧快照 → 加入视口 | WidgetController 从 ASC、属性集和 PlayerState 接收状态。蓝图向子 Widget 传递控制器和技能槽 Tag，订阅血蓝、经验、等级、技能图标和消息，处理菜单开关及输入模式。 |
| 属性菜单 | Overlay 按钮 → `WBP_AttributeMune` → `UMyAbilitySystemLibrary` 从 HUD 获取 `UAttributeMenuWidgetController` → 原生属性事件与升级入口 | 属性信息和升级在 C++。行绑定、属性标签、点数与按钮状态、关闭事件在蓝图。 |
| 技能菜单 | Overlay 按钮 → `WBP_SpellMune` → `USpellMenuWidgetController` → ASC 的花点、装配及客户端回调 | 选中、装配状态和业务请求在 C++。主动/被动树、槽位控件接线、按钮与动画仍有蓝图逻辑。主动树已有 `UPrototypeSpellTree` / `UPrototypeSpellEntry` 扩展。HUD 技能球另有图标刷新和冷却计时图。 |
| 背包与装备 | `AMyPlayerController::OpenBackpackUI` → `WBP_Backpack` → 手工注入 PlayerState 上 `UBackpackComponent` 的 `UMVVM_Backpack`，TileView 使用 `UMVVM_InventorySlot` → 同时创建 `UEquipmentPanel` | InventoryModel、VM、拖放手势和装备面板已有原生实现。背包蓝图保留 TileView 和 View Bindings，以及点击、关闭、使用、丢弃事件接线。装备请求沿服务器事务和背包/装备快照回传。 |
| 敌人血条 | `AEnemyCharacter` 的 `UWidgetComponent` → `WBP_EnemyHealthBar` → 注入敌人对象 → 血量/最大血量通知 | C++ 提供敌人属性通路。血条蓝图订阅状态，基类 `WBP_ProgressBar` 处理显隐、延迟和残血插值。读取怪物开发规范后再迁移这条链路。 |
| 伤害数字 | 玩家控制器/PureMass 反馈 → `UDamageNumberPool` → `BP_DamageTextComponent` → `WBP_DamageText` | C++ 负责排队、合并、池化。蓝图更新文字、格挡/暴击色与动画，经延迟进入 `DestroyComponent`，原生组件在池启用时将此转为释放。 |
| 装备拾取标签 | `AEquipmentSoulPickup` → `UWidgetComponent` → `UEquipmentSoulLabel` | 原生 Widget 显示世界空间装备名称。 |
| 副本面板 | `ADungeonRunController` → `UDungeonRunWidget` → `ADungeonRunDirector` 与控制器请求 | 原生 WidgetTree 负责副本状态、小地图、商店和离开操作。 |

`UMyUserWidget` 持有通用 `UObject` 控制器，`SetWidgetController` 触发 `WidgetControllerSet`。源码中还存在 `UAuraUserWidget`，本次扫描的 Widget 蓝图父链使用 `UMyUserWidget` 或各自专用基类。两个 HUD 基类为 `AMyHUD` 和 `ALoadScreenHUD`，另有 `AExtensionHUD : AMyHUD`，负责可选扩展目录与技能树。WidgetController 体系为 `UMyWidgetController` 及 Overlay、AttributeMenu、SpellMenu 三个子类。

### Overlay 配置分支

| HUD 蓝图 | 配置读回 |
| --- | --- |
| `BP_MyHUD` | 使用三种 `BP_*WidgetController` 基础配置。普通、MassDungeon 及 Actor QA GameMode 引用。 |
| `BP_MassHUD` | 使用三种 `BP_MassChild_*` 控制器。PureMass、DungeonGeneration、DungeonRun、SwordShield GameMode 引用。 |
| `BP_StanderHUD` | 使用 `BP_StanderOverlayController`、`BP_StanderSpellController`，属性复用 MassChild 配置。 |
| `BP_ExtensionHUD` | 使用三种 `BP_Extension_*` 控制器，继承对应 MassChild 配置。 |

四者的 Overlay 默认类都指向 `WBP_Overlay`。配置型控制器蓝图图为空，迁移时仍需保留各分支的类默认值，尤其是技能目录、属性信息和消息数据。三个独立的 `BP_Mass_*_widget_controller_class` 在资产注册表中未见外部引用。

## MVVM 组织

四个原生 VM 继承 `UMVVMViewModelBase`，字段和计算属性通过 FieldNotify 更新。检查到 7 个 Widget 含编辑器 MVVM View，共 23 条绑定记录，其中包含未被当前入口引用的 `WBP_Backpack1` 的 6 条。记录均为单向写入控件。

| Widget | VM 来源 | 绑定记录 |
| --- | --- | ---: |
| `WBP_LoadScreen` | 属性路径经 HUD 取 `BP_LoadScreenViewModel` | 1 |
| `WBP_LoadSlot_Vacant` | 手工注入 `BP_LoadSlotViewModel`，属性路径取主存档 VM | 1 |
| `WBP_LoadSlot_EnterName` | 同上 | 1 |
| `WBP_LoadSlot_Taken` | 同上 | 4 |
| `WBP_Backpack` | `UBackpackWidget::SetBackpackViewModel` 手工注入原生 `UMVVM_Backpack` | 6 |
| `WBP_Backpack1` | 同上 | 6 |
| `WBP_BackpackSlot` | 声明手工来源 `UMVVM_InventorySlot`，行对象从 TileView 传入 | 4 |

存档槽绑定包含玩家名、地图、等级和转换生成的文字。槽位三态由 `BP_LoadSlotWidgetSwitcher` 接收原生 VM 的索引广播切换。背包绑定覆盖容量、格子列表、使用/丢弃按钮文字和启用状态。槽位绑定覆盖图标 Brush/可见性、背景状态色与堆叠数。运行时实例由原生组件创建，资产 `BP_BackpackViewModel` 在本次读回中无外部引用。背包有原生 `NativeOnListItemObjectSet` 与蓝图 `OnListItemObjectSet`，迁移前应复测行复用时 FieldNotify 和图标更新的实际顺序。

## 迁移提案

建议按功能切片把 Widget 的行为迁到 C++，保留 Widget Blueprint Designer 中的 WidgetTree、动画、材质、字体和资源参数。使用专用原生父类和 `BindWidget` / `BindWidgetAnim` 接入已有控件。继续复用现有 WidgetController、ViewModel 和服务端入口。另一种做法是按功能同时把布局迁到原生 WidgetTree，房间和装备面板已有这种实现，但对依赖大量设计器控件、样式和动画的战斗 HUD，需要同步重建更多表现并扩大视觉回归范围。

| 批次建议 | 迁移范围 | 验收重点 |
| --- | --- | --- |
| 1. 存档界面 | `WBP_LoadScreen`、槽位 Switcher、三种子状态与确认框的初始化、订阅和按钮接线。继续使用存档 VM 和现有 View Bindings。主菜单开始、退出和输入接线可紧接着迁移。 | 空槽、输入和已有存档三态。职业选择、返回、读档、删除确认/取消、进入模式选择、重复打开和输入焦点。主菜单进入 LoadMenu 和退出。 |
| 2. Overlay 与技能/属性 | Overlay 菜单路由和子控件注入，再逐步迁属性行、血蓝/经验条、技能球和技能菜单。 | 先接视图再广播首帧，按 InputTag 刷新技能图标。冷却开始/结束、装配与加点、关闭恢复输入、角色/HUD 变更及多人换图。覆盖上述四种 HUD 配置。 |
| 3. 背包 | 迁移窗口与槽位的蓝图事件接线。沿用 TileView、现有 VM 和原生拖放。 | 列表行回收、重复打开、拖放取消、使用和丢弃。装备/卸装以及主机和客户端快照同步。 |
| 4. 敌人血条与飘字 | 各自独立迁移表现回调、动画/计时和生命周期清理。 | 每次生命重新初始化血条。飘字检查普通/暴击/格挡、重复入池、禁用池和世界退出。 |

迁移时需要明确每个事件的单一接线位置。Widget 创建、重建和关闭，以及 Controller 所依赖的 ASC/PlayerState 变更，都要验证订阅和解绑时机。蓝图默认类、控件名、WidgetTree、动画和 MVVM Property Path 都是迁移接口的一部分。换图回归应复用 `Scripts/Travel/verify_skill_bar_travel.py`，房间流程可从 `Scripts/Rooms/verify_pie_travel.py` 开始。源码已有 `test_25d.Lifecycle.UIBindings`、`test_25d.Equipment.UI` 与 `test_25d.Roguelike.UI` 测试入口。本次为静态盘点，尚未执行 PIE、自动化测试或迁移。

参考 [项目功能目录](project-feature-catalog.md)、[Aura 学习笔记](aura-learning-notes.md)、[换图后技能栏修复](2026-10-22-skill-bar-travel-fix.md)、[生命周期审计](2026-11-04-lifecycle-audit.md)、[装备模块开发规范](equipment-development-standard.md)和[怪物开发规范](monster-development-standard.md)。

## 逐资产清单

父类为 UE 读回的直接父类。图节点数统计资产自身的图，包括蓝图生成的默认节点。入向包引用数为项目内已登记的硬引用和软引用。

### Widget Blueprint（41）

| 包路径 | 直接父类 | 图节点数 | 入向引用数 | MVVM |
| --- | --- | ---: | ---: | --- |
| `/Game/BluePrints/MainMenu/WBP_Title` | `UserWidget` | 3 | 1 |  |
| `/Game/BluePrints/UI/AreYouSure/WBP_AreyouSure` | `MyUserWidget` | 22 | 1 |  |
| `/Game/BluePrints/UI/AttributeMenu/WBP_AttributeMune` | `MyUserWidget` | 39 | 1 |  |
| `/Game/BluePrints/UI/AttributeMenu/WBP_AttributePointRow` | `MyUserWidget` | 26 | 1 |  |
| `/Game/BluePrints/UI/AttributeMenu/WBP_FramedValue` | `MyUserWidget` | 18 | 3 |  |
| `/Game/BluePrints/UI/AttributeMenu/WBP_TextValueButtonRow` | `WBP_TextValueRow_C` | 26 | 1 |  |
| `/Game/BluePrints/UI/AttributeMenu/WBP_TextValueRow` | `MyUserWidget` | 28 | 2 |  |
| `/Game/BluePrints/UI/Backpack/WBP_Backpack` | `WBP_BackpackBase_C` | 34 | 4 | 有 |
| `/Game/BluePrints/UI/Backpack/WBP_Backpack1` | `WBP_BackpackBase_C` | 34 | 0 | 有 |
| `/Game/BluePrints/UI/Backpack/WBP_BackpackBase` | `BackpackWidget` | 3 | 2 |  |
| `/Game/BluePrints/UI/Backpack/WBP_BackpackSlot` | `BackpackSlot` | 17 | 2 | 有 |
| `/Game/BluePrints/UI/Button/WBP_Button` | `MyUserWidget` | 40 | 6 |  |
| `/Game/BluePrints/UI/Button/WBP_WideButton` | `MyUserWidget` | 40 | 7 |  |
| `/Game/BluePrints/UI/FloatingText/WBP_DamageText` | `UserWidget` | 33 | 1 |  |
| `/Game/BluePrints/UI/LoadMenu/BP_LoadSlotWidgetSwitcher` | `WBP_LoadScreenWidget_Base_C` | 36 | 1 |  |
| `/Game/BluePrints/UI/LoadMenu/WBP_LoadScreen` | `WBP_LoadScreenWidget_Base_C` | 69 | 1 | 有 |
| `/Game/BluePrints/UI/LoadMenu/WBP_LoadScreenWidget_Base` | `LoadScreenWidget` | 10 | 5 |  |
| `/Game/BluePrints/UI/LoadMenu/WBP_LoadSlot_EnterName` | `WBP_LoadScreenWidget_Base_C` | 12 | 1 | 有 |
| `/Game/BluePrints/UI/LoadMenu/WBP_LoadSlot_Taken` | `WBP_LoadScreenWidget_Base_C` | 14 | 1 | 有 |
| `/Game/BluePrints/UI/LoadMenu/WBP_LoadSlot_Vacant` | `WBP_LoadScreenWidget_Base_C` | 7 | 1 | 有 |
| `/Game/BluePrints/UI/MainMenu/WBP_MainMenu` | `UserWidget` | 14 | 1 |  |
| `/Game/BluePrints/UI/Overlay/SubWidget/WBP_EffectMessageWidget` | `MyUserWidget` | 17 | 2 |  |
| `/Game/BluePrints/UI/Overlay/SubWidget/WBP_HealthManaSpell` | `MyUserWidget` | 36 | 1 |  |
| `/Game/BluePrints/UI/Overlay/SubWidget/WBP_LevelUpMessageWIdget` | `MyUserWidget` | 3 | 0 |  |
| `/Game/BluePrints/UI/Overlay/WBP_Overlay` | `MyUserWidget` | 122 | 4 |  |
| `/Game/BluePrints/UI/ProgressBar/WBP_EnemyHealthBar` | `WBP_ProgressBar_C` | 25 | 9 |  |
| `/Game/BluePrints/UI/ProgressBar/WBP_GlobeProgressBarBase` | `MyUserWidget` | 71 | 2 |  |
| `/Game/BluePrints/UI/ProgressBar/WBP_HealthGlobe` | `WBP_GlobeProgressBarBase_C` | 27 | 1 |  |
| `/Game/BluePrints/UI/ProgressBar/WBP_ManaGlobe` | `WBP_GlobeProgressBarBase_C` | 27 | 1 |  |
| `/Game/BluePrints/UI/ProgressBar/WBP_ProgressBar` | `MyUserWidget` | 56 | 1 |  |
| `/Game/BluePrints/UI/ProgressBar/WBP_XP_Bar` | `MyUserWidget` | 13 | 1 |  |
| `/Game/BluePrints/UI/Rooms/WBP_ModeAndRoom` | `RoomMenuWidget` | 3 | 1 |  |
| `/Game/BluePrints/UI/SpellGlobe/WBP_EquipRowButton` | `MyUserWidget` | 60 | 1 |  |
| `/Game/BluePrints/UI/SpellGlobe/WBP_PictureFrame` | `MyUserWidget` | 37 | 1 |  |
| `/Game/BluePrints/UI/SpellGlobe/WBP_SpellGlobe` | `MyUserWidget` | 115 | 1 |  |
| `/Game/BluePrints/UI/SpellGlobe/WBP_ValueGlobe` | `MyUserWidget` | 77 | 1 |  |
| `/Game/BluePrints/UI/SpellMenu/WBP_EquippedSpellROw` | `MyUserWidget` | 24 | 1 |  |
| `/Game/BluePrints/UI/SpellMenu/WBP_My_SpellGLobeButton` | `MyUserWidget` | 94 | 2 |  |
| `/Game/BluePrints/UI/SpellMenu/WBP_OffensiveSpellTree` | `PrototypeSpellTree` | 40 | 1 |  |
| `/Game/BluePrints/UI/SpellMenu/WBP_PassiveSpellTree` | `MyUserWidget` | 34 | 1 |  |
| `/Game/BluePrints/UI/SpellMenu/WBP_SpellMune` | `MyUserWidget` | 99 | 1 |  |

### 相关 Blueprint（25）

| 包路径 | 直接父类 | 图节点数 | 入向引用数 | MVVM |
| --- | --- | ---: | ---: | --- |
| `/Game/BluePrints/Characters/Stander/BP_StanderHUD` | `MyHUD` | 4 | 1 |  |
| `/Game/BluePrints/Characters/Stander/BP_StanderOverlayController` | `BP_OverlayWidgetController_C` | 0 | 1 |  |
| `/Game/BluePrints/Characters/Stander/BP_StanderSpellController` | `BP_SpellMenuWidgetController_C` | 0 | 1 |  |
| `/Game/BluePrints/MainMenu/BP_MainMenuActor` | `Actor` | 17 | 2 |  |
| `/Game/BluePrints/UI/Backpack/WBP_Drag_Visual` | `DragDropOperation` | 0 | 0 |  |
| `/Game/BluePrints/UI/FloatingText/BP_DamageTextComponent` | `DamageTextComponent` | 9 | 4 |  |
| `/Game/BluePrints/UI/HUD/BP_LoadScreenHud` | `LoadScreenHUD` | 4 | 2 |  |
| `/Game/BluePrints/UI/HUD/BP_MyHUD` | `MyHUD` | 4 | 3 |  |
| `/Game/BluePrints/UI/ViewModel/BP_BackpackViewModel` | `MVVM_Backpack` | 0 | 0 |  |
| `/Game/BluePrints/UI/ViewModel/BP_LoadScreenViewModel` | `MVVM_LoadScreen` | 0 | 7 |  |
| `/Game/BluePrints/UI/ViewModel/BP_LoadSlotViewModel` | `MVVM_LoadSlot` | 0 | 5 |  |
| `/Game/BluePrints/UI/WidgetController/BP_AttributeMenuWidgetController` | `AttributeMenuWidgetController` | 0 | 3 |  |
| `/Game/BluePrints/UI/WidgetController/BP_OverlayWidgetController` | `OverlayWidgetController` | 0 | 9 |  |
| `/Game/BluePrints/UI/WidgetController/BP_SpellMenuWidgetController` | `SpellMenuWidgetController` | 0 | 6 |  |
| `/Game/PrototypeExtensions/Batch5/BP_ExtensionHUD` | `ExtensionHUD` | 4 | 1 |  |
| `/Game/PrototypeExtensions/Batch5/BP_Extension_attribute_menu_widget_controller_class` | `BP_MassChild_attribute_menu_widget_controller_class_C` | 0 | 1 |  |
| `/Game/PrototypeExtensions/Batch5/BP_Extension_overlay_widget_controller_class` | `BP_MassChild_overlay_widget_controller_class_C` | 0 | 1 |  |
| `/Game/PrototypeExtensions/Batch5/BP_Extension_spell_menu_widget_controller_class` | `BP_MassChild_spell_menu_widget_controller_class_C` | 0 | 1 |  |
| `/Game/PureMass/BP_MassChild_attribute_menu_widget_controller_class` | `BP_AttributeMenuWidgetController_C` | 0 | 3 |  |
| `/Game/PureMass/BP_MassChild_overlay_widget_controller_class` | `BP_OverlayWidgetController_C` | 0 | 2 |  |
| `/Game/PureMass/BP_MassChild_spell_menu_widget_controller_class` | `BP_SpellMenuWidgetController_C` | 0 | 2 |  |
| `/Game/PureMass/BP_MassHUD` | `MyHUD` | 4 | 4 |  |
| `/Game/PureMass/BP_Mass_attribute_menu_widget_controller_class` | `AttributeMenuWidgetController` | 0 | 0 |  |
| `/Game/PureMass/BP_Mass_overlay_widget_controller_class` | `OverlayWidgetController` | 0 | 0 |  |
| `/Game/PureMass/BP_Mass_spell_menu_widget_controller_class` | `SpellMenuWidgetController` | 0 | 0 |  |

`WBP_Drag_Visual` 的父类为 `UDragDropOperation`。它与 `WBP_Backpack1`、`WBP_LevelUpMessageWIdget` 在本次注册表读回中均无外部包引用。当前原生拖放逻辑动态创建 `UDragDropOperation` 和图标 `UImage`。
