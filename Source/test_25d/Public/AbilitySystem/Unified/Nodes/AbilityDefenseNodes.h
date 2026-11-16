// 调用链：Schema2 Defense.PerfectDodge -> 同一Pawn防御组件 -> 完美/受阻/完成类型化Origin端口。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Defense/PerfectDodgeComponent.h"
#include "AbilityDefenseNodes.generated.h"
class FAbilityNodeRegistry;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPerfectDodgeNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPerfectDodgeSettings Settings;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPerfectDodgeNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UPerfectDodgeComponent> Defense;
    FGuid Execution;
    bool bPerfectPublished = false;
};

namespace AbilityDefenseNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
