// 契约链：共享 GraphDefinition -> Validate -> 每次施法的 GraphInstance。
// 本文件只有可持久化配置和结束协议；计数器、目标生命身份及异步句柄属于执行实例。
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AbilityGraphTypes.generated.h"

class UMaterialInterface;

/** 结束原因不等同于端口；取消、源失效或预算耗尽不能伪装为正常完成。 */
UENUM(BlueprintType)
enum class EAbilityGraphEndReason : uint8
{
    Completed,
    Expired,
    Blocked,
    TargetLost,
    Cancelled,
    Failed,
    BudgetExceeded,
    SourceInvalid
};

/** 协议预留三种范围，V1 配置只接受 Cast；未实现的范围必须在执行前拒绝。 */
UENUM(BlueprintType)
enum class EAbilityGraphCancelScope : uint8
{
    Node,
    Subtree,
    Cast
};

/** 所属任务决定正常结束时移交或保留图；目标状态继续由原状态组件持有。 */
UENUM(BlueprintType)
enum class EAbilityExecutionLifetime : uint8
{
    BoundToAbility,
    IndependentCastEffect,
    TargetPersistentState
};

/** 兼容长驻行为允许累计事件增长，物理队列、活动节点和调用深度仍有硬上限。 */
UENUM(BlueprintType)
enum class EAbilityGraphBudgetPolicy : uint8
{
    Bounded,
    LegacyStreaming
};

UENUM(BlueprintType)
enum class EAbilityPortPayload : uint8
{
    Origin,
    LiveTarget,
    ConfirmedImpact,
    DamageResult
};

/** 注册表中的端口契约；位置快照不隐式升级为活目标能力。 */
struct TEST_25D_API FAbilityPortDescriptor
{
    FName Name;
    EAbilityPortPayload Payload = EAbilityPortPayload::Origin;
    bool bTerminal = false;
};

struct TEST_25D_API FAbilityExecutionOptions
{
    FName CompatibilityProfile;
    EAbilityExecutionLifetime Lifetime = EAbilityExecutionLifetime::BoundToAbility;
    EAbilityGraphBudgetPolicy BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
};

/** V1 安全上限；资产可收紧预算，不可扩大到超出已验证的执行范围。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityGraphBudget
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "1", ClampMax = "64"))
    int32 MaxInvocations = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "1", ClampMax = "256"))
    int32 MaxEvents = 256;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "1", ClampMax = "32"))
    int32 MaxActiveNodes = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "1", ClampMax = "8"))
    int32 MaxDepth = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "1", ClampMax = "64"))
    int32 MaxEventsPerTick = 64;
};

/** 节点私有配置由图资产实例化；运行时只读，不在这里保存定时器或状态。 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNodeConfig : public UObject
{
    GENERATED_BODY()
public:
    /** 配置自行校验数值；注册图无需了解每种节点的实现类。 */
    virtual bool Validate(FString& Error) const
    {
        Error.Reset();
        return true;
    }
    /** 同一配置类可服务多种注册策略；需要区分具体族的配置在此拒绝 TypeId 与实际参数不匹配。 */
    virtual bool MatchesType(FName TypeId) const
    {
        return true;
    }
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityDelayNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    virtual bool Validate(FString& Error) const override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delay", meta = (ClampMin = "0.001"))
    float DelaySeconds = 0.25f;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPetrifyNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    virtual bool Validate(FString& Error) const override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Petrify", meta = (ClampMin = "0.001"))
    float DurationSeconds = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Petrify")
    TObjectPtr<UMaterialInterface> StoneMaterial;
};

/** 稳定身份不依赖数组索引或枚举顺序；Config 的类型由注册表约束。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityGraphNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FGuid NodeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FName TypeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Graph")
    TObjectPtr<UAbilityNodeConfig> Config;
};

/** V1 每个节点只有一个执行输入；边携带原捕获目标，不重新选取或改变生命身份。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityGraphEdge
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FGuid From;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FGuid To;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FName OutputPort;
};
