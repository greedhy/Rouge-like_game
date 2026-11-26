// 调用链：地面输入 -> 有限引信 -> 状态筛选 -> 活目标转落点 -> 多个有限局部爆裂。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"
#include "WorkshopEmberAbilities.generated.h"

class FAbilityNodeRegistry;

/** 只接受活目标。适配器仅导出可复用的位置，不持有目标权限。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityTargetOriginConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    /** 需要避开锚点自身的传播才记录弱身份，默认仅导出位置。 */
    UPROPERTY(EditAnywhere, Category = "Query")
    bool bRememberExcludedTarget = false;

    virtual bool SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Port, FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityTargetOriginExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 复核当前原生命，使用其服务器位置，不缓存 Actor 或 Mass 实体。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 按次冻结筛选、爆裂和表现，各分支从一个验证后的带状态目标出发。 */
UCLASS()
class TEST_25D_API UWorkshopEmberDetonation : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopEmberDetonation();
};

namespace WorkshopEmberAbilities
{
    /** 登记可移植到其它范围与选敌技能的活目标转位置适配器。 */
    TEST_25D_API void RegisterNodes(FAbilityNodeRegistry& Registry);
} // namespace WorkshopEmberAbilities
