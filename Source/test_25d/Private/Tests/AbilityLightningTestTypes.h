// 调用链：自动化局部注册表 -> 已捕获TargetSet的子集/注入夹具 -> 生产Lightning载荷和Cue。
// 只供测试，不登记到生产内建目录，不代替任何真实命中或伤害动作。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityLightningTestTypes.generated.h"

UCLASS(EditInlineNew, DefaultToInstanced)
class UAbilityLightningSubsetTestConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 非负值仅用于证明：同源合法生命仍不等于本步已捕获的接触权限。 */
    UPROPERTY()
    int32 InjectedEntityId = INDEX_NONE;
};

UCLASS(Transient)
class UAbilityLightningSubsetTestExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 保留参数帧，只转发公开集合的第二个原生命；可选追加未被本步查询捕获的同源Mass生命。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};
