// 调用链：伤害结算和图节点 -> 核对目标原生命 -> 读取、施加或消耗 Actor/Mass 元素状态。
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AbilityElementalExposure.generated.h"

struct FAbilitySourceToken;
struct FAbilityTargetToken;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class EAbilityElement : uint8
{
    Original,
    Fire,
    Water,
    Thunder,
    Arcane,
    Ice
};

UENUM(BlueprintType)
enum class EAbilityExposure : uint8
{
    None,
    Burning,
    Wet,
    Shocked,
    ArcaneMark,
    Steam,
    Conductive,
    Chilled,
    Frozen
};

/** 每个目标生命仅存一个槽位，使用权威世界游戏时间自动失效。 */
struct TEST_25D_API FAbilityElementalExposure
{
    EAbilityExposure Status = EAbilityExposure::None;
    double ExpiresAt = 0.;

    /** 已到期的状态等价于空槽，不需要为每个目标创建定时器。 */
    EAbilityExposure Get(double Now) const;
};

struct FAbilityElementalReaction
{
    EAbilityExposure Next = EAbilityExposure::None;
    float DamageMultiplier = 1.f;
    float Duration = 0.f;
    // 沿用当前绝对到期时间，连击不会不断刷新同一次冻结。
    bool bKeepCurrent = false;
};

namespace AbilityElements
{
    inline constexpr float ChillMoveRate = .65f;
    inline constexpr float ChillAttackRate = .65f;
    inline constexpr float ChillSeconds = 4.f;
    inline constexpr float FreezeSeconds = 1.25f;

    /** 精确映射元素伤害标签，物理和纯粹伤害返回 Original。 */
    TEST_25D_API EAbilityElement FromDamageTag(FGameplayTag Tag);
    /** 供类型化转换写入本地伤害副本；Original 保留来路的标签。 */
    TEST_25D_API FGameplayTag ToDamageTag(EAbilityElement Element);
    /** 有序匹配状态与入射元素。一次命中只选择一条规则，没有二次伤害调用。 */
    TEST_25D_API FAbilityElementalReaction Evaluate(EAbilityExposure Current, EAbilityElement Incoming);
    /** 通过原生命验证后读目标当前状态，给查询筛选与条件复用。 */
    TEST_25D_API EAbilityExposure Read(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target);
    /** 只施加元素反应状态，用同一反应表和生命控制路径，忽略其中的伤害倍率。 */
    TEST_25D_API bool Apply(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                            EAbilityElement Element, float DurationScale);
    /** 非伤害施加节点、Actor 和 Mass 共用有限时长与元素边界。 */
    TEST_25D_API bool CanApply(EAbilityElement Element, float DurationScale);
    /** 消耗原生命上当前匹配的元素状态，条件不再成立时不动其状态。 */
    TEST_25D_API bool Consume(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                              EAbilityExposure Expected);
    /** 共用项目已有冰霜地表素材，调用者在可见状态下通过 Niagara 组件持有它。 */
    TEST_25D_API UNiagaraSystem* GetFrostVisual();
} // namespace AbilityElements
