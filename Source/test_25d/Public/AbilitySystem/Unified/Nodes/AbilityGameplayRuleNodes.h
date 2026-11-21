// 调用链：可信工作台片段 → 同步选敌/条件 → 类型化后继；不持有计时器、弹体或完整GA。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityTargetSelectionService.h"
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

namespace AbilityGameplayRuleNodes
{
    /** 将三个真实执行类与输入/终态端口登记到现有注册表，不创建第二套执行器。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityGameplayRuleNodes
