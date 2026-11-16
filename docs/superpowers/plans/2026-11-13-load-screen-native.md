# 存档界面 C++ 迁移计划

> 执行依据为 [UI 架构盘点](../../ui-architecture-inventory.md) 和用户已确认的迁移边界。当前项目在本地 `main` 执行。

目标是从 C++ 驱动存档界面的初始化、切换和按钮行为。现有 WidgetTree、样式、存档 VM 和声明式绑定继续作为资产配置使用。

- [x] 对实际蓝图建立迁移验证，记录 WidgetTree、MVVM 声明式绑定、按钮和返回地图配置。
- [x] 在 `ULoadScreenWidget` 迁移主视图、槽位切换器和槽位子视图的行为。在原生类中维护委托的绑定、解绑和确认弹窗生命周期。
- [x] 将 `ALoadScreenHUD` 的入口接到 C++ 初始化。保留存档 VM 调用和现有房间流程。
- [x] 使用 UE Editor API 删除已迁移的图。保留 MVVM 使用的属性路径、转换和设计器资产。
- [x] 编译 Editor，核对资产迁移前后差异，再验证空槽、输入、已占用、选择、删除确认/取消与返回流程。

Development Editor 构建通过。重新加载资产后确认 7 条存档 MVVM 绑定、设计器控件名称和类型、返回地图与基线一致。独立存档目录下的单实例 PIE 通过 17 项断言，包含重新打开存档地图并选择已有存档。验证报告位于 `Saved/UIArchitecture/LoadScreenMigration/`。
