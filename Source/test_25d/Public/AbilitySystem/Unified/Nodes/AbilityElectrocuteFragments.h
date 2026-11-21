// 调用链：原生雷链前端 -> 维持/目标宿主 -> 可替换周期与收束效果；不另行激活完整技能。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilityElectrocuteFragments.generated.h"

class UAbilityGraphDefinition;
class UAbilityElectrocuteNodeConfig;
class FAbilityNodeRegistry;

/** 单阶段只读快照：保存已准备参数及原生命集合；不拥有Actor、Timer或费用回调。 */
UCLASS(Transient)
class TEST_25D_API UAbilityElectrocutePhaseFrame : public UAbilityExecutionParameters
{
    GENERATED_BODY()
public:
    static UAbilityElectrocutePhaseFrame* Create(UObject* Outer, FGuid Root, const FAbilitySourceToken& Source,
                                                 const FDamageEffectParams& Damage,
                                                 const TArray<FAbilityTargetToken>& Targets, uint64 Sequence,
                                                 bool bFinal);
    const FDamageEffectParams& GetDamage() const;
    const TArray<FAbilityTargetToken>& GetTargets() const;
    uint64 GetSequence() const;
    bool IsFinal() const;

private:
    UPROPERTY(Transient)
    FDamageEffectParams Damage;
    TArray<FAbilityTargetToken> Targets;
    uint64 Sequence = 0;
    bool bFinal = false;
};

/** 原宿主拥有目标/表现/费用/Timer；细宿主只将两种实际效果发布给同一图的独立批节点。 */
UCLASS(Transient)
class TEST_25D_API UAbilityElectrocuteHostExecution : public UAbilityElectrocuteNodeExecution
{
    GENERATED_BODY()
public:
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** sink由同根资源索引取得精确宿主；共享旧目标迭代，每次GE后复核同步死亡/取消。 */
    void ExecutePeriodPayload(const UAbilityElectrocutePhaseFrame& Frame, float Level, float DamageScale);
    void ExecuteFinalPayload(const UAbilityElectrocutePhaseFrame& Frame, float DamageScale, float DebuffDamageScale);

protected:
    virtual void BeginPeriodEffects() override;
    virtual bool IsPeriodPending() const override;
    virtual void EmitFinalDamage(const FAbilityTargetToken& Life, FVector Location) override;
    virtual void FinishCompletion() override;

private:
    /** 阶段入口只排队；continuation结束时释放其索引，不延长独立OnHit后继的寿命。 */
    bool PublishPhase(bool bFinal);
    void PhaseCompleted(bool bFinal);
    void ReleasePhaseResource(UAbilityElectrocutePhaseFrame* Frame);
    UPROPERTY(Transient)
    TObjectPtr<UAbilityElectrocutePhaseFrame> PeriodFrame;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityElectrocutePhaseFrame> FinalFrame;
    TArray<FAbilityTargetToken> FinalTargets;
    bool bPeriodPayloadPending = false;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityElectrocutePayloadConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplier = 1.f;
    virtual bool Validate(FString& Error) const override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityElectrocutePeriodExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 一个批节点处理本期集合，不按目标增加策略调用/队列开销。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityElectrocuteFinalExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 完整Final参数保持来源ASC署名；完成后才允许宿主提交冷却。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityElectrocuteFragments
{
    /** 导出原费用和目标合同的细节点定义；周期/最终效果分开，原前端继续拥有费用接口。 */
    TEST_25D_API UAbilityGraphDefinition* Build(UObject* Outer, const UAbilityElectrocuteNodeConfig& Settings);
    /** 注册实际宿主与效果策略，不创建第二个执行器。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityElectrocuteFragments
