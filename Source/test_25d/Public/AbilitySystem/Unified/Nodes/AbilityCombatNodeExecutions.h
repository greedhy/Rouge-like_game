// 调用链：Schema2 查询 -> TargetFound 活目标端口 -> Damage 节点 -> 原 Actor/Mass 伤害服务。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityTargetQueryService.h"
#include "AbilityCombatNodeExecutions.generated.h"

class FAbilityNodeRegistry;

/** 明确的权限转换：历史快照只允许重新验证原生命，不重新捕获相同 Actor 的新租约。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityResolveImpactNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(Transient)
class TEST_25D_API UAbilityResolveImpactNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityQueryNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAbilityQueryRequest Query;
    /** 输入位置/方向来自前一节点；资产保留几何尺寸与目标过滤。 */
    virtual bool Validate(FString& Error) const override;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityDamageNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Damage = 25.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamagePerLevel = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag DamageType;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityQueryNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityDamageNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityCombatNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
