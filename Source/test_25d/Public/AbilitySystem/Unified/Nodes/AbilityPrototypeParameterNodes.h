// 调用链：FineCompiler -> Scope.Prototype 冻结参数/预留逻辑预算 -> 独立查询、效果和时序节点。
// 参数帧只属于服务器的一次根执行；队列/节点保活，结束后随图回收，不持有 Actor 或运行资源。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityPrototypeParameterNodes.generated.h"

class UAbilityPrototype;
class FAbilityNodeRegistry;

/** 手工原子图可跳过被拒绝分支；旧实际GA编译必须保留旧运行时获取失败导致整图失败的合同。 */
UENUM()
enum class EAbilityPrototypeLogicalRejectionPolicy : uint8
{
    SkipBranch,
    FailExecution
};

/** 同一逻辑片段的唯一数值来源；新的子片段创建新帧，周期和并发后继只读共享原帧。 */
UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeParameterFrame : public UAbilityExecutionParameters
{
    GENERATED_BODY()
public:
    /** 从图私有参数再冻结本次值，并绑定根/来源；失败不返回可发布的半初始化帧。 */
    static UAbilityPrototypeParameterFrame* Create(UObject* Outer, const UAbilityPrototype& Frozen, int32 Level,
                                                   FName Profile, UAbilityPrototype* StableReference, FGuid Root,
                                                   const FAbilitySourceToken& Source, FString& Error,
                                                   int32 LogicalDepth = 0, bool bSingleProjectile = false,
                                                   const FAbilityNodeInput* EntryInput = nullptr);
    /** 调用方不得修改返回参数；运行时也不从原 DA 回读数值。 */
    const UAbilityPrototype& GetParameters() const;
    float GetDamage() const;
    int32 GetLevel() const;
    FName GetProfile() const;
    int32 GetLogicalDepth() const;
    bool IsSingleProjectile() const;
    UAbilityPrototype* GetStableReference() const;
    /** 只用于旧 authored.Completed；恢复原调用值而非最终位置，保留当前帧身份，不重新捕获目标。 */
    bool RestoreEntryInput(const FAbilityNodeInput& Current, FAbilityNodeInput& Output, FString& Error) const;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototype> Parameters;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototype> StableReference;
    int32 CapturedLevel = 1;
    float CapturedDamage = 0.f;
    FName CapturedProfile;
    int32 CapturedLogicalDepth = 0;
    bool bCapturedSingleProjectile = false;
    // 对象引用仍由活动节点/队列的对应输入保活；此处只保存只读原调用值，不取得场景资源所有权。
    FAbilityNodeInput CapturedEntryInput;
    bool bHasEntryInput = false;
};

/** 仅管理参数与一次逻辑生成准入，不包含碰撞、周期伤害或旧 Driver。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeScopeNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Instanced)
    TObjectPtr<UAbilityPrototype> Parameters;
    UPROPERTY(EditAnywhere)
    TObjectPtr<UAbilityPrototype> StableReference;
    UPROPERTY(EditAnywhere)
    FName Profile;
    UPROPERTY(EditAnywhere)
    int32 LogicalDepth = 0;
    UPROPERTY(EditAnywhere)
    bool bSingleProjectile = false;
    UPROPERTY(EditAnywhere)
    EAbilityPrototypeLogicalRejectionPolicy LogicalRejectionPolicy =
        EAbilityPrototypeLogicalRejectionPolicy::SkipBranch;
    /** 校验显式兼容族、原深度与唯一参数，不因合法原 DA 的连接被图替换而伪造子资产。 */
    virtual bool Validate(FString& Error) const override;
    /** 原型根才可申请兼容 helper 合同；普通图仍使用原有有界预算。 */
    virtual bool SupportsPrototypeCompatibilityRoot(FName RequestedProfile, FString& Error) const override;
    virtual bool SupportsOriginalNode(FName OriginalType, FName OriginalProfile, FString& Error) const override;
    virtual bool SupportsOriginalLifetime(FName OriginalType, FName OriginalProfile, FName LifetimeType,
                                          FString& Error) const override;
    virtual int32 MaximumOriginalLifetimeNodes() const override;
    /** 仅编译副本可修改；所有数值消费者在执行时从这份参数建立帧。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
    /** 单轴赋值只写本Scope实际消费的字段；种类、profile和孩子身份保持原值。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

/** 没有影子几何字段；类型/过滤沿用本片段 frame 的原行为 profile。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeQueryNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

/** 没有第二份伤害/等级/伤害类型配置；共享批动作从 frame 取得唯一数值。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeDamageNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 显式传播 payload 模式只消费累计倍率一次；独立子 Scope 已重置为自身固定伤害。 */
    UPROPERTY(EditAnywhere)
    bool bConsumeInputScales = false;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeScopeNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 每次原逻辑生成只预留一次；显式失败策略复原旧GA获取失败，手工Skip策略只关闭该分支。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityPrototypeParameterNodes
{
    /** 参数、查询与效果共用的只读身份检查；不从其它根或已失效来源借用帧。 */
    TEST_25D_API const UAbilityPrototypeParameterFrame* ResolveFrame(const UAbilityGraphInstance& Owner,
                                                                     const FAbilityNodeInput& Input, FString& Error);
    /** 注册独立 Scope 和复用共享 batch 执行器的细查询/效果端口。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeParameterNodes
