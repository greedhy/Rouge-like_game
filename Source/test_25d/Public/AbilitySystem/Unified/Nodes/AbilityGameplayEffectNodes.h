// 调用链：属性监听/受击前端或统一图 -> 共享 GE 动作 -> 原 ASC Spec/属性；所属句柄由调用者精确撤销。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "GameplayEffectTypes.h"
#include "AbilityGameplayEffectNodes.generated.h"

class FAbilityNodeRegistry;
class UGameplayEffect;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityGameplayEffectNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<UGameplayEffect> EffectClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float EffectLevel = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<FGameplayTag, float> SetByCallerMagnitudes;
    /** 默认作用于来源自身；显式目标分支只接受当前活 Actor 令牌。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bApplyToLiveActor = false;
    virtual bool Validate(FString& Error) const override;
    virtual bool MatchesType(FName TypeId) const override;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityOwnedEffectNodeConfig : public UAbilityGameplayEffectNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DurationSeconds = 1.f;
    virtual bool Validate(FString& Error) const override;
    virtual bool MatchesType(FName TypeId) const override;
};

struct TEST_25D_API FAbilityGameplayEffectApplication
{
    bool bApplied = false;
    FActiveGameplayEffectHandle Handle;
    TWeakObjectPtr<UAbilitySystemComponent> TargetASC;
};

namespace AbilityGameplayEffectActions
{
    /** 保留原 Spec 等级和任意 SetByCaller；没有费用提交，不将属性事件解释为敌对伤害。 */
    TEST_25D_API FAbilityGameplayEffectApplication Apply(const FAbilitySourceToken& Source,
                                                         const UAbilityGameplayEffectNodeConfig& Settings,
                                                         const TOptional<FAbilityTargetToken>& Target = {});
    /** 原 Attributes 监听：配置列表全初始化，只有精确匹配项写入事件值；空列表模式只填事件Tag。 */
    TEST_25D_API bool BuildEventMagnitudes(FGameplayTag EventTag, float Magnitude,
                                           const TArray<FGameplayTag>& InitializedTags, bool bInitializeListedTags,
                                           TMap<FGameplayTag, float>& Out);
    /** 清理仅针对这次返回的GE句柄，不按GE类移除其他能力的状态。 */
    TEST_25D_API void Remove(FAbilityGameplayEffectApplication& Applied);
} // namespace AbilityGameplayEffectActions

UCLASS(Transient)
class TEST_25D_API UAbilityGameplayEffectNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityOwnedEffectNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float Delta) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    FAbilityGameplayEffectApplication Applied;
    float Remaining = 0.f;
};
namespace AbilityGameplayEffectNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
