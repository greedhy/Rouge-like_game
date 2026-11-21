// 调用链：批效果完成/Orbit真实伤害结果 -> 独立表现适配 -> 同帧准确宿主 -> Niagara；不执行伤害或子GA。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeContactNodes.h"
#include "AbilityPrototypePresentationNodes.generated.h"

class FAbilityNodeRegistry;

/** 不持有第二份资源/数值，中心与scale/ground规则来自本次Actor参数帧和准确宿主。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypePulseNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypePulseNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 整批GE后的独立中心表现；默认只接原Field/Trap Pulse，Burst初始化表现不得重复。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 只将严格验证过的本根Orbit发射凭据转换成其唯一原参数帧，再复用已有Impact动作。 */
UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeOrbitImpactNodeExecution : public UAbilityPrototypeImpactNodeExecution
{
    GENERATED_BODY()
public:
    /** 不放宽全局ResolveFrame；其它PreparedInventory不能借这个表现入口认领原宿主。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityPrototypePresentationNodes
{
    /** Pulse为Origin→Origin；OrbitImpact保留DamageResult，并与运动/数值完全分离。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypePresentationNodes
