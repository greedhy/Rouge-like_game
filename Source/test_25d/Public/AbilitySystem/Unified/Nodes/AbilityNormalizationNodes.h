// 调用链：旧端口的细节点绑定 -> 明确权限降级/有序完成事件 -> 原 authored 后继。
// 桥接节点不查询、不伤害，不把历史结果升级为新活目标。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityNormalizationNodes.generated.h"
class FAbilityNodeRegistry;

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityImpactFactNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityOrderedCompletionNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeCompletedInputNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

/** 预算端点不拥有payload业务；Request拒绝只跳过原可选孩子，不把helper启动判为失败。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityOriginalPortNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY()
    EAbilityPortPayload Payload = EAbilityPortPayload::Origin;
    UPROPERTY()
    bool bRequestChild = true;
    virtual bool Validate(FString& Error) const override;
    virtual bool MatchesType(FName TypeId) const override;
};
UCLASS(Transient)
class TEST_25D_API UAbilityOriginalPortNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 只转发已验证输入；原端口发布种类由私有配置显式确定。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeCompletedInputNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 原Legacy.Completed返回原调用输入；正常OnExpire保留终点，二者不共用位置语义。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityImpactFactNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** DamageResult 明确降级为旧 ConfirmedImpact，保持事实位置及原生命，不生成新权限。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityOrderedCompletionNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 原正常结束先 OnExpire 后 Completed；两条外部边不能合成顺序不明的一个出口。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityNormalizationNodes
{
    TEST_25D_API FName OriginalPortType(EAbilityPortPayload Payload);
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityNormalizationNodes
