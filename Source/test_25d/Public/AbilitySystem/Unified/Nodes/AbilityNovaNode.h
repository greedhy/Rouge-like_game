// 调用链：ArcaneNova/组合图 -> 同一范围筛选 -> Shape.ArcaneNova -> 权威伤害与原 GameplayCue。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityNovaNode.generated.h"
class FAbilityNodeRegistry;

/** 原生前端的非复制观察桥接；节点先冻结整批目标生命，再通知原攻击订阅者。 */
UCLASS(Transient)
class TEST_25D_API UAbilityNovaNodeContext : public UObject
{
    GENERATED_BODY()
public:
    TFunction<void(AActor*)> BeforeActorDamage;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNovaNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 450.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag CueTag;
    /** Actor 原行为默认不扩展目标集合；新组合可以明确开启 Mass。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeMass = false;
    virtual bool Validate(FString& Error) const override;
};
UCLASS(Transient)
class TEST_25D_API UAbilityNovaNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};
namespace AbilityNovaNodes
{
    /** 保留原 Actor 中心距离、敌友、存活和 ASC 条件；不加入 LOS 或目标数量上限。 */
    TEST_25D_API void GatherActorTargets(AActor* Source, const FVector& Center, float Radius, TArray<AActor*>& Out);
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityNovaNodes
