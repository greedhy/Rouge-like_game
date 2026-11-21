// 调用链：原新星前端 -> 查询/参数冻结 -> 可替换 payload -> 脚底 Cue；各片段仍由同一 GraphInstance 调度。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityNovaFragmentNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityNovaNodeConfig;
class UAbilityGraphDefinition;

/** 只消费查询已冻结的伤害参数和目标集合，不再次查询、扣费或应用装备规则。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNovaPayloadConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplier = 1.f;
    /** 固定乘数只作用于本片段主伤害，不改变根参数或其它调用的配置。 */
    virtual bool Validate(FString& Error) const override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

/** 表现读取同一查询输出的半径；稳定 Cue 身份属于图配置，临时参数不复制到客户端。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNovaCueConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag CueTag;
};

UCLASS(Transient)
class TEST_25D_API UAbilityNovaQueryExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 固定本次敌对目标生命、装备参数和原攻击观察时点；输出集合允许为空。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityNovaPayloadExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 原生新星的无击退、中心死亡冲量和真实 GE/Mass 结算，不隐含选择或表现。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityNovaCueExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 使用已冻结的实际几何和角色脚底位置执行原 Cue；取消后不补播。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityNovaFragments
{
    /** 将新星的实际选择、伤害和 Cue 拼为可独立替换的三个节点。 */
    TEST_25D_API UAbilityGraphDefinition* Build(UObject* Outer, const UAbilityNovaNodeConfig& Settings);
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityNovaFragments
