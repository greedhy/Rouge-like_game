// 调用链：运行时配方 -> 布防/波形/发射调度 -> 类型化端口 -> 独立查询、投递或效果。
// 配置不包含伤害；每次调用独享时钟与目标原生命，图负责同根预算和统一取消。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityTemporalNodes.generated.h"

class FAbilityNodeRegistry;

/** 几何和尾帧是旧 profile 的不同合同，不能以通用默认值合并。 */
UENUM(BlueprintType)
enum class EAbilityWaveQueryProfile : uint8
{
    ActorFront3D,
    MixedGrowing3D,
    Batch6Front3D,
    Batch6Growing3D
};

UENUM(BlueprintType)
enum class EAbilityEmitterProfile : uint8
{
    ActorCatchUp,
    MixedEight
};

/** 布防与总截止同时计时，避免 Delay 丢弃跨帧余量后延长原陷阱寿命。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityWaitTargetNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 350.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ArmDelay = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeActors = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeMass = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRequireVisibility = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bQueryOnStart = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeFinalBoundary = false;
    /** 校验有限半径、布防/总时长及目标表示，不隐式修正资产。 */
    virtual bool Validate(FString& Error) const override;
    /** 显式配置真实存在检测几何与原截止时钟，未知轴必须拒绝。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

/** 空间推进独立于 payload；已观察身份始终绑定第一次捕获的原生命。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityWavefrontNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Speed = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityWaveQueryProfile Profile = EAbilityWaveQueryProfile::ActorFront3D;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeActors = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeMass = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRequireVisibility = true;
    /** 校验显式 profile 与正有限半径/速度，配置不决定伤害或状态。 */
    virtual bool Validate(FString& Error) const override;
    /** 只配置空间推进实际消费的半径、速度和时长，不改profile或payload。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

/** 成功指子调用获同根预算准入；实际飞行和伤害由 Launch 后继决定。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityEmitterNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Count = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Period = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityEmitterProfile Profile = EAbilityEmitterProfile::ActorCatchUp;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeActors = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeMass = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRequireVisibility = true;
    /** 仅提供有限 Count/Duration；持续引导使用另行登记的持续合同。 */
    virtual bool Validate(FString& Error) const override;
    /** 只配置有限成功次数、周期、寿命与索敌半径，保持同根准入合同。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityWaitTargetNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 捕获本次位置及布防/截止策略，可显式选择入口时查询。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 布防后只发布位置 Triggered，范围与效果留给后继节点。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 释放本次时钟/生命缓存，不产生到期分支。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 只发布存在检测的位置信号；true 表示本次已经触发，不代表后继已经造成伤害。 */
    bool TryTrigger();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityWaitTargetNodeConfig> Settings;
    float Age = 0.f;
    TMap<FPureMassCombatTarget, FAbilityTargetToken> OriginalLives;
};

UCLASS(Transient)
class TEST_25D_API UAbilityWavefrontNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 初始化波形、已观察原生命与访问历史，不隐式伤害。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 按 profile 推进波形，每个符合几何的原生命最多发布一次。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 清理空间历史，已落地状态保持原服务所有权。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityWavefrontNodeConfig> Settings;
    float Age = 0.f;
    float Radius = 0.f;
    TMap<FPureMassCombatTarget, FAbilityTargetToken> OriginalLives;
    TArray<FAbilityTargetToken> Visited;
};

UCLASS(Transient)
class TEST_25D_API UAbilityEmitterNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 初始化有限成功次数与显式周期策略，不限定后继投递种类。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 仅 RequestChild 准入成功才计数，空闲/拒绝按 profile 消费积压。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 取消不补发子调用，不发布正常完成端口。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 消费一次周期后的投递尝试；回调返回 false 停止本帧追赶。 */
    bool EmitScheduled();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityEmitterNodeConfig> Settings;
    float Age = 0.f;
    float Clock = 0.f;
    int32 Successful = 0;
    FVector Direction = FVector::ForwardVector;
    TMap<FPureMassCombatTarget, FAbilityTargetToken> OriginalLives;
};

namespace AbilityTemporalNodes
{
    /** 注册三种异步策略及 Origin/LiveTarget 端口，不覆盖已有注册。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityTemporalNodes
