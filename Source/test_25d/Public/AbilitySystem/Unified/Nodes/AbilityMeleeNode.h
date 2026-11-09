// 调用链：旧近战 GA 动画 Notify / 任意组合图 -> Melee.Contact -> 原 Actor GE 或 Mass 伤害 -> 命中快照。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityMeleeNode.generated.h"
class FAbilityNodeRegistry;

/** 一次瞬时接触查询。旧资产保留半径和双路径伤害，不以迁移之名重新平衡。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityMeleeNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 45.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeMass = false;
    /** 原 BP 在击退概率分支先 ApplyDamageEffect，再无条件 CauseDamage；默认保留该可观察行为。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bLegacyExtraDamageOnKnockback = true;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityMeleeNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 服务器在调用时捕获整批目标生命；每次效果和回调前再次校验，不跨池租约补伤害。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};
namespace AbilityMeleeNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
