/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 数据链路：服务器BackpackComponent修改槽位并标脏 -> NetDeltaSerialize/FastArrayDeltaSerialize -> 组件复制回调 -> InventoryModel应用槽位。
 * 本结构不是UObject/单例：作为组件的值成员存活；ItemInstanceId与RollData随槽位复制。修改Items不会自动替业务代码调用MarkItemDirty；删除/缩容需标记数组变化。
 * 复制数据与客户端MVVM对象分离，网络上传输槽位业务状态，不传输UInventoryModel或Widget指针。
 */
// 复制协议在 ReplicatedInventory.h 内实现；此翻译单元仅包含定义，不承担额外运行逻辑。
#include "Backpack/ReplicatedInventory.h"
