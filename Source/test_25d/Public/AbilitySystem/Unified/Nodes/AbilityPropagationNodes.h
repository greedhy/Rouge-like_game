// 调用链：Origin输入 -> 首次投递 -> 真实命中 -> 等待/选敌 -> 下一投递；终态一次，图仍无环。
#pragma once
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityTargetSelectionService.h"
#include "AbilityPropagationNodes.generated.h"

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPropagationNodeConfig : public UAbilityDeliveryNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTargets = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float JumpDelay = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float JumpRadius = 350.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageFalloff = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityTargetSelectionPolicy TargetSelection = EAbilityTargetSelectionPolicy::Original;
    /** 私有编译副本的原位参数；未知轴交回投递基类，不改发布中的施法快照。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPropagationNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    bool Launch(FVector Origin, const TOptional<FAbilityTargetToken>& Target, FVector Direction, FString& Error);
    void Delivered(const FAbilityDeliveryResult& Result);
    void Complete();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPropagationNodeConfig> Settings;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityDeliveryInstance> Delivery;
    TArray<FAbilityTargetToken> Visited;
    FVector LastImpact = FVector::ZeroVector;
    FGuid RootCastId;
    float WaitRemaining = 0.f;
    int32 DeliveriesStarted = 0;
    bool bWaitingForJump = false;
};

namespace AbilityPropagationNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
