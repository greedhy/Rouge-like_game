// 调用链：Schema2图 -> 注册Time.WorldSlow -> 同一世界时间租约；Stop只释放本节点请求。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilityWorldTimeNodes.generated.h"

class FAbilityNodeRegistry;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityWorldSlowNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAbilityWorldSlowSettings Settings;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityWorldSlowNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    TWeakObjectPtr<UAbilityWorldTimeSubsystem> Time;
    FGuid Lease;
};

namespace AbilityWorldTimeNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
