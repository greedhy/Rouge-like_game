// 调用链：可信工作台片段 → 同步选敌/条件 → 类型化后继；不持有计时器、弹体或完整GA。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityTargetSelectionService.h"
#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilityGameplayRuleNodes.generated.h"
class FAbilityNodeRegistry;

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilitySelectTargetsConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 350.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTargets = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityTargetSelectionPolicy TargetSelection = EAbilityTargetSelectionPolicy::Nearest;
    /** 0任意，1仅Actor，2仅Mass，3低血，其余按元素状态筛选。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 TargetFilter = 0;
    /** 对空间锚点携带的弱身份做负向过滤，选择预算不会被锚点消耗。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bExcludeAnchorTarget = false;
    /** 拒绝无界半径、超过16的输出数量及未知策略；不产生运行时副作用。 */
    virtual bool Validate(FString& Error) const override;
    /** 仅消费数量、半径和选敌枚举三个类型化轴；写入后复用完整配置校验。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 只承诺Query.SelectTargets对Origin的有限同步合同，不提升其它输入权限。 */
    virtual bool SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Incoming, FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilitySelectTargetsExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 选择后每个实际应用前重验原生命和范围；前一后继的伤害/移动/取消先于下一目标。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityKilledGateConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bExpectedKilled = true;
    /** 仅接受ConditionOutcome的0/1，分别代表未击杀/击杀；其它轴交回基类拒绝。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 只有真实DamageResult输入可以作为伤害结果门控；不承诺LiveTarget输出。 */
    virtual bool SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Incoming, FString& Error) const override;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityChanceGateConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Chance = .5f;
    /** 概率必须是有限的[0,1]数值，错误在编译阶段返回。 */
    virtual bool Validate(FString& Error) const override;
    /** 修改独立配置的Chance轴，不在编译/预览时抽取随机结果。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 仅对Origin承诺同步纯门控，实际概率仍在权威执行时决定。 */
    virtual bool SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Incoming, FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityGameplayGateExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 服务器只发布Passed或Rejected之一；输出历史位置，不能将致死快照升级为活目标。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UENUM()
enum class EAbilityCombatRule : uint8
{
    HealthBelow,
    ElementalStatus,
    DamageApplied,
    EveryNthDamage,
    DamageTriggerLimit,
    RestoreMana,
    RestoreHealth
};

/** 有限图规则的配置。Value 的含义与范围由 Rule 固定，不能通过名称传任意属性。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityCombatRuleConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere)
    EAbilityCombatRule Rule = EAbilityCombatRule::HealthBelow;
    UPROPERTY(EditAnywhere)
    float Value = .35f;
    UPROPERTY(EditAnywhere)
    float Cooldown = 0.f;
    /** 校验本规则的单位、上下限与整数约束。 */
    virtual bool Validate(FString& Error) const override;
    /** 仅消费本规则对应的参数轴，冷却只对限频守卫有效。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float NewValue, FString& Error) override;
    /** 无副作用的判断可用在重复载荷域，资源与跨命中计数需要有限域。 */
    virtual bool SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Incoming, FString& Error) const override;
    /** 防止资产将不同输入合同的规则配置绑定到同一执行类的其它TypeId。 */
    virtual bool MatchesType(FName Type) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityCombatRuleExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 同步判断并发布原类型载荷，计数及恢复只消费实际正伤害结果。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityGameplayRuleNodes
{
    /** 将三个真实执行类与输入/终态端口登记到现有注册表，不创建第二套执行器。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
    /** 查找可信类型及其输入/参数，用于工作台菜单和服务端配置工厂。 */
    TEST_25D_API TOptional<EAbilityCombatRule> FindCombatRule(FName Type);
    TEST_25D_API EAbilityPortPayload GetCombatRuleInput(EAbilityCombatRule Rule);
    TEST_25D_API EAbilityConfigParameter GetCombatRuleParameter(EAbilityCombatRule Rule);
    /** 按权威 ASC 的实际缺口补充基础资源，走原属性写入及复制。 */
    TEST_25D_API bool RestoreResource(const FAbilitySourceToken& Source, bool bMana, float Amount);
} // namespace AbilityGameplayRuleNodes
