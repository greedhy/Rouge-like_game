// 调用链：Scope 容器帧 -> 无伤害方向分布 -> Pellet Origin -> 同深度的独立 single Scope/Flight。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityPrototypeDistributionNodes.generated.h"
class FAbilityNodeRegistry;

/** 只控制已捕获原生命的显式路由；不查询目标，不从Actor/ID重新捕获下一生命。 */
UENUM()
enum class EAbilityPelletTargetPolicy : uint8
{
    None,
    OriginalSelectedLife
};

/** 数量/夹角仅来自同次原型帧，不复制第二份可编辑数值。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeMultishotNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 默认保留旧单弹不继承目标的合同；只有显式运行时策略才传递本次已选中的原生命。 */
    UPROPERTY(EditAnywhere)
    EAbilityPelletTargetPolicy PelletTargetPolicy = EAbilityPelletTargetPolicy::None;
    /** 参数协议只接受0/1整数；错误不修改原配置或其它轴。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeMultishotNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 容器只发射同深度单弹片段，不产生自身 OnHit/OnExpire 或额外物理租约。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityPrototypeDistributionNodes
{
    /** Origin -> Pellet:Origin 非终结、Completed:Origin 终结；子 Scope 自行获得真实逻辑准入。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeDistributionNodes
