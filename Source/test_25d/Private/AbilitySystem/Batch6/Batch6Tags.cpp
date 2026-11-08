/**
 * 模块加载时注册第六批技能/冷却原生 GameplayTag，供资产和 GAS 查找；此文件没有逐次施法函数或运行对象，无需实例生命周期管理。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "NativeGameplayTags.h"

// Register before saved GA defaults are deserialized, as with the existing prototype batches.
#define BATCH6_TAG(Name)                                                                                               \
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Ability##Name, "Abilities.Batch6." #Name);                                           \
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Cooldown##Name, "Cooldown.Batch6." #Name);
BATCH6_TAG(StarFlameHalo)
BATCH6_TAG(PetrifyingGaze)
BATCH6_TAG(SolarFireball)
BATCH6_TAG(GlacialStorm)
BATCH6_TAG(FissionChain)
BATCH6_TAG(EarthShockwave)
#undef BATCH6_TAG
