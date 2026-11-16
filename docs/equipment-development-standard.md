# 装备模块开发规范

日期：2026-10-30。适用项目：test_25d，UE 5.8、GAS、服务器权威联机。

更新：2026-11-16 增加[固定暗金扩展](2026-11-16-unique-equipment-guide.md)。仅 `UUniqueEquipmentGameplayEffect` 可使用受限 Channel2 MultiplyCompound；普通/随机装备继续遵循下文原规则。暗金的限定技能规则由已有共享动作消费，不能借此放开任意执行/触发组件。下文“首期”范围作为历史规范保留，新能力与实际验证以扩展指南为准。

本文是基于现有代码的可行性分析与后续开发约定，不表示装备模块已实现，也不授权实现所有扩展项。未明确的产品规则采用下述建议基线，实施前按当前任务范围确认。

## 1. 可行性结论与方案选择

“装备时给玩家添加一个 Infinite GE，卸下时移除”适合常驻属性加成和常驻 Gameplay Tag。一个 GE 可以包含多条 Modifier，不需要每条属性单独创建 GE。

Infinite 只代表不自动到期，不代表自动跨地图保存，也不保证死亡时自动移除。模块必须管理装备所有权、效果句柄、重建与卸除。

| 方案 | 取舍 |
| --- | --- |
| 直接复用 Potion 的 UseEffect/使用函数 | 改动少，但会消耗物品且没有装备实例与效果回收管理，不采用 |
| 复用物品、拾取、背包，新增装备组件管理 GE | 推荐；保持现有数据通路，明确装备独立生命周期 |
| 新建完整物品与装备框架 | 重复现有能力，迁移成本高，当前无必要 |

首期建议仅支持固定属性、不可堆叠装备，每件装备一个非周期 Infinite GE。攻击触发、主动技能、召唤、套装、随机词条和耐久不是本次默认范围。复杂效果将来可由装备拥有的 GA/事件逻辑实现，不能把任意行为强塞进一个 GE；届时必须同时管理授予的能力句柄及取消策略。

## 2. 已核对的项目基础

- `Source/test_25d/Public/Backpack/ItemDefinition.h`：已有目录 `UBackpackItemDefinitions` / `FBackpackItemDefEntry`，以及旧 `UItemDefinition`；共享名称、图标、拾取、堆叠和 UseEffect 等概念。新增装备优先进入目录，不再建立另一份物品身份系统。
- `Private/Backpack/BackpackComponent.cpp`（以下源码短路径均相对 `Source/test_25d`）：`TryUseItemForPawn` 在服务器创建 Spec、施加 GE，随后 `UseItemAt` 扣数量。返回句柄没有作为长期装备状态保存。
- `Public/Backpack/InventoryModel.h`、`Public/Backpack/ReplicatedInventory.h`：槽位主要保存定义 ID 与数量；FastArray 复制基础已存在，但没有装备实例身份。
- `Private/Player/MyPlayerState.cpp`：ASC 与背包位于 PlayerState，ASC 使用 Mixed 复制；已有背包快照导入导出及 PlayerState 状态复制接入。
- `Private/Character/PlayerCharacter.cpp`：ASC ActorInfo 使用 PlayerState 作为 Owner、角色作为 Avatar。装备不能假定 ASC 跟随 Pawn 一起销毁。
- `Private/AbilitySystem/MyAttributeSet.cpp`：已有 Health/Mana 限制与部分最大值变化处理，但不能据此认定卸装后最大值下降一定会修正当前资源。

以上为源码检查结论；未通过编辑器审计所有 Potion 蓝图资产，未运行新增装备测试或 PIE。

## 3. 模块职责与数据

建议新增 `UEquipmentComponent`，挂在现有 PlayerState 上并复用同一个 ASC。背包负责物品存放，装备组件负责槽位约束、装备事务及效果所有权；UI 只提交请求和显示结果。

物品目录新增独立装备配置（以下名称为建议，尚不存在）：

- `bEquippable`、允许的槽位 Tag、`EquipEffect`、效果等级或服务器认可的参数。
- 保留消耗品 `UseEffect`，不得直接将现有 Potion GE 的持续策略改为 Infinite。
- 首期装备 `bStackable=false`、`MaxStack=1`；普通“使用并消耗”入口拒绝装备。
- 定义 ID 标识类型；`ItemInstanceId` 标识某一件实体。两件同类型装备不能仅靠定义 ID 区分。

实例 ID 必须贯穿背包、装备槽、掉落、快照和恢复；复制新物品才生成新 ID，移动原物品保留原 ID。首期即使没有随机词条，也应保证同一实例在玩家所有物品位置中只出现一次。

装备槽状态至少包含 SlotTag、ItemInstanceId、ItemDefinitionId；服务器运行时另存该实例对应的 ASC 弱引用与 GE Handle。效果句柄不可作为网络身份或存档数据，客户端不得用服务器句柄卸除效果。

建议装备后从背包移入装备槽，卸下时移回背包，不消耗、不复制物品。满背包时卸装失败；换装应支持原装备放回新装备腾出的背包位置。槽位清单、双手占位和职业限制尚未指定，不在本文虚构既定规则。

## 4. GE 资产约束

1. 常驻属性 GE 使用 Infinite、Period=0，首期 StackingType=None，使每件装备有独立 Active GE。
2. 不以 Instant GE 修改基础属性后再手工减回数值；装备卸除通过精确句柄移除 Modifier，避免与升级、Buff 混算。
3. 不按 GE 类或宽泛 Tag 批量清除，避免卸下一枚戒指时删除另一枚或药剂效果。SourceObject 可辅助追踪来源，但不是实例身份或独立堆叠的替代品。
4. 禁止常驻装备 GE 在应用时触发不可逆奖励、治疗、伤害或移除无关效果。首期限制为可回滚的属性/Tag 效果，避免换装失败后无法恢复副作用。
5. 应用后同时检查成功结果、有效句柄以及对应 Active GE 的存在；禁止成功显示已装备但没有效果。允许条件抑制时必须另有明确规则和 UI 状态，首期不引入。
6. GE 等级、SetByCaller 数值及词条只能来自服务器校验后的定义/实例。不得修改共享 GE CDO 来表达单件装备数值。
7. 数值策划必须注明加法、乘法、覆盖规则，并测试多来源组合；百分比叠加不能仅凭 UI 文案推断。

## 5. 服务器事务与同步

客户端通过有所有权的 PlayerController/组件 RPC 发起装备、卸装和换装请求。服务器重新验证玩家身份、当前 Avatar、槽位、实例位置、请求版本、状态限制、容量和资产配置；不信任客户端提交的 GE 类、属性值或目标 ASC。

事务流程：

1. 校验并预留目标槽位和物品空间，记录原状态，防止重复请求与回调重入。
2. 准备新 Spec，执行受控的旧效果移除与新效果应用。过程中不发布持久化快照或完成事件。
3. 新效果成功后提交背包与装备状态，更新版本，再广播一次完成事件。
4. 任一步失败，移除已应用的新效果，恢复旧物品位置和旧效果。回滚若重新应用旧 GE，也必须检查成功；失败需显式报告并进入待修复状态，不能假装事务成功。

GAS 调用可能同步触发属性回调，并不提供跨背包和 GE 的通用数据库事务。实施时需保护重入、延迟业务通知，并验证换装过程中最大值修正不会意外扣血；不能只用“先移除后添加”宣称原子性。

同一个实例重复装备应幂等或返回明确错误，绝不重复加成。非法/过期请求拒绝后返回最新状态。装在装备槽的物品不能通过背包删除、出售或掉落入口绕过卸装清理。

复制装备业务状态，不要求远端客户端遍历完整 Active GE。Mixed 模式下不要假定所有观察者都有相同效果详情；本人 UI 使用权威装备数据和复制属性，公开外观按需单独同步。Listen Server 本地显示也要刷新，不能只依赖 OnRep。

## 6. 属性、死亡与恢复

服务器主动穿戴、卸下或替换装备时，分别记录操作前的生命和法力比例，按提交时 GAS 算出的最终上限结算一次。直接上限 Modifier 和主属性产生的派生上限适用同一规则。最终上限相同的资源无需写入，旧上限为零时将原当前值限制在新范围内。装备效果对账仍按最终上限裁剪当前资源，旅行资源在装备效果恢复后写入。

建议死亡/倒地保留装备和常驻属性，阻止死亡状态下换装；若以后要求死亡脱落或暂停效果，需明确修改生命周期契约。不要将装备 GE 放进通用 Debuff 清理范围，也不要死亡时无差别移除全部 Infinite GE。

- 同一 ASC 更换 Avatar：保留仍有效的效果，仅对缺失效果补建，不因重复 InitAbilityActorInfo 再次叠加。
- 新建 ASC：完成基础属性初始化、ActorInfo 绑定及物品恢复后，依据装备记录重建 GE。
- 保存定义 ID、实例 ID、槽位及服务器认可的实例参数，不保存 Handle、ASC 指针或已经包含装备加成的最终属性作为基础值。
- 扩展现有快照、PlayerState 复制及实际旅行/重连恢复调用链，不能仅添加导出字段就宣称完成。背包和装备快照要共享一致版本，避免重复恢复同一物品。
- 老快照无装备字段时默认空装备；未知定义、重复实例、非法槽位必须有明确拒绝或隔离记录，禁止静默复制、丢弃物品。
- 外部效果移除要与装备状态对账。允许的停用不应被立即重建循环抵消；非预期缺失记录诊断并按受控流程修复。

## 7. 测试与交付门槛

| 层级 | 必须覆盖 |
| --- | --- |
| 数据与逻辑自动化 | 非法槽位、实例重复、满背包卸装、换装空间复用、请求幂等、快照旧版本 |
| GAS 自动化 | 装备加成、卸下恢复、两个同类 GE 独立移除、药剂共存、应用被拒绝、失败回滚、换装保持生命/法力比例 |
| 生命周期自动化 | 重复初始化、新旧 ASC、倒地复活、旅行/重连重建、装备 GE 非预期移除 |
| 联机 PIE 用户验收 | 至少服务器与远程客户端；验证请求权限、UI/属性同步、快速重复换装、掉线恢复、延迟条件；发布专服则追加 Dedicated Server |

最低演示用例：基础护甲 10，两件装备分别 +5 与 +7，装备后 22，卸下第一件后 17，全卸后 10；药剂效果独立保留。测试再重复换装和恢复，最终状态不得出现物品复制或额外 GE。

交付依次报告代码逻辑、资产配置与 UI 表现、自动化结果、用户 PIE 结果；明确区分已验证、未验证、已知限制。当前文档仅完成源码分析与规范编写，所有装备实现与运行验收待后续任务。

## 8. 参考

- [Epic：Gameplay Effects](https://dev.epicgames.com/documentation/unreal-engine/gameplay-effects-for-the-gameplay-ability-system-in-unreal-engine)：持续策略、Modifier 和堆叠机制。
- [Epic：UGameplayEffect](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/UGameplayEffect)：资产属性参考。具体 API 以项目 UE 5.8 安装源码为准。

## 2026-10-30 计算通道约束

真实玩家 GE_SecondaryAttributes 使用 Channel0 Override；同通道装备加法会被覆盖。装备 Modifier 统一使用 Channel1 AddBase，保持 Infinite、Period=0、Stacking=None。UE5.8 的 GameplayAbilitiesDeveloperSettings 通过 OverrideConfigSection 读取 DefaultGame.ini 的 AbilitySystemGlobals 段，在此启用通道并设置 BaseAttributes/EquipmentBonuses 别名。运行校验拒绝 Channel0 装备。所有新增装备需与真实初始化 GE 组合验证属性、主属性重算、独立卸除及资源上限；Active GE 存在不能替代数值断言。表格修改方式不变。
