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
    LegacyStreaming,
    RegisteredSustained,
    PrototypeCompatibility
};

UENUM(BlueprintType)
enum class EAbilityPortPayload : uint8
{
    Origin,
    LiveTarget,
    ConfirmedImpact,
    DamageResult,
    TargetSet,
    /** 必须由已登记的库存消费者显式接收；普通位置节点不能无声丢弃库存归还责任。 */
    PreparedInventory
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
    /** 精确运行归属（例如一次开发预览）；不是观测订阅，也不随服务器瞬态图复制到客户端。 */
    FGuid OwnerLease;
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

/** 显式持续合同的速率与并发额度；累计次数可增长，活动资源与每帧事件仍有界。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilitySustainedBudget
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 InvocationStartsPerSecond = 256;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PhysicalStartsPerSecond = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxActivePhysical = 32;
};

/** 原型兼容的内部辅助工作额度；原Context的64次逻辑生成/深度3独立保留，普通图不使用此合同。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityPrototypeHelperBudget
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxRetainedInvocations = 256;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxActiveNodes = 128;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxEventsPerTick = 8192;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxActivePhysical = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PhysicalStartsPerSecond = 64;
};

/** 编译器可请求的已知数值轴；不是客户端提供的反射属性名。 */
enum class EAbilityConfigScalar : uint8
{
    Damage,
    Radius,
    Duration
};

/** 可信配方的单轴赋值协议；不是反射属性名，具体配置必须显式声明真实消费和边界。 */
UENUM(BlueprintType)
enum class EAbilityConfigParameter : uint8
{
    Count,
    Spread,
    Period,
    ArmDelay,
    Duration,
    Radius,
    InnerRadius,
    Width,
    HalfHeight,
    Range,
    CollisionRadius,
    Speed,
    HomingTurnRate,
    Motion,
    ImpactPolicy,
    ImpactLimit,
    DamageFalloff,
    TransferRadius,
    MaxTransfers,
    StackThreshold,
    StackLifetime,
    DetonationDelay,
    MoveDistance,
    SafeDistance,
    PelletTargetPolicy,
    PropagationMaxTargets,
    PropagationDelay,
    PropagationRadius,
    PropagationFalloff,
    ForkDepth,
    ForkBranches,
    ForkConcurrent,
    TargetSelection,
    ConditionOutcome,
    Chance
};

/** 节点私有配置由图资产实例化；运行时只读，不在这里保存定时器或状态。 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNodeConfig : public UObject
{
    GENERATED_BODY()
public:
    /** 只有真实宏参数准备策略能证明对应原节点；普通helper默认不能伪装原调用入口。 */
    virtual bool SupportsOriginalNode(FName OriginalType, FName OriginalProfile, FString& Error) const
    {
        Error = TEXT("This helper cannot represent an original invocation entry.");
        return false;
    }
    /** 原主寿命与新增helper资源分开证明；例如Surface使用clock，而不是Acquire资源调用。 */
    virtual bool SupportsOriginalLifetime(FName OriginalType, FName OriginalProfile, FName LifetimeType,
                                          FString& Error) const
    {
        Error = TEXT("This configuration has no original main-lifetime proof.");
        return false;
    }
    virtual int32 MaximumOriginalLifetimeNodes() const
    {
        return 0;
    }
    /** 仅已拆分的原型根可申请该合同；须核对显式profile、根深度和真实参数，不由字符串直接豁免。 */
    virtual bool SupportsPrototypeCompatibilityRoot(FName RequestedProfile, FString& Error) const
    {
        Error = TEXT("This configuration has no prototype-compatible root admission.");
        return false;
    }
    /** 仅编译器的私有副本可缩放；新配置显式实现自身参数合同，默认不声称支持任何轴。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
    {
        return false;
    }
    /** 编译器只在私有副本重放；未知或未消费轴拒绝，不能让UI提交无效果参数。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
    {
        Error = TEXT("This configuration does not consume the requested typed parameter.");
        return false;
    }
    /** 只有完成过持续资源/速率验证的配置可以声明合同；普通策略默认不能申请持续根。 */
    virtual bool SupportsSustainedRoot(FString& Error) const
    {
        Error = TEXT("This node configuration has no registered sustained-root contract.");
        return false;
    }
    /** 仅在编译私有图上证明登记策略和实际入边类型；目标状态自身的受控到期不等于新执行器调度。 */
    virtual bool SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming, FString& Error) const
    {
        Error = TEXT("This configuration has no registered synchronous repeated-payload contract.");
        return false;
    }
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
    /** 仅修改状态服务实际使用的秒数，非法缩放或绝对时长保留原值。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 显式证明同步单生命状态动作；重复端口仍须通过来源、目标生命及有限状态时长校验。 */
    virtual bool SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                            FString& Error) const override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Petrify", meta = (ClampMin = "0.001"))
    float DurationSeconds = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Petrify")
    TObjectPtr<UMaterialInterface> StoneMaterial;
};

UENUM()
enum class EAbilityOriginalNodeRole : uint8
{
    Helper,
    Entry,
    Lifetime,
    EntryAndLifetime
};

UENUM()
enum class EAbilityOriginalPublication : uint8
{
    Preserve,
    Publish,
    LegacyChildRequest
};

/** 原类型/配置族属于预算证明，不引用原共享Config对象；本次私有参数仍在实际Entry配置。 */
USTRUCT()
struct TEST_25D_API FAbilityOriginalNodeDefinition
{
    GENERATED_BODY()
    UPROPERTY()
    FName TypeId;
    UPROPERTY()
    FName LegacyProfile;
    UPROPERTY()
    bool bLegacyMacro = false;
};

/** 按原边声明顺序保留预算拓扑；不能用新增helper边假冒原调用关系。 */
USTRUCT()
struct TEST_25D_API FAbilityOriginalEdgeDefinition
{
    GENERATED_BODY()
    UPROPERTY()
    int32 FromIndex = INDEX_NONE;
    UPROPERTY()
    FName OutputPort;
    UPROPERTY()
    int32 ToIndex = INDEX_NONE;
};

/** Source Budget始终保存在Graph.Budget；本结构保留其原Schema/Profile/Policy解释，绝不改成helper预算。 */
USTRUCT()
struct TEST_25D_API FAbilityOriginalGraphProjection
{
    GENERATED_BODY()
    UPROPERTY()
    bool bEnabled = false;
    UPROPERTY()
    int32 SchemaVersion = 2;
    UPROPERTY()
    FName CompatibilityProfile;
    UPROPERTY()
    EAbilityGraphBudgetPolicy BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
    UPROPERTY()
    int32 EntryIndex = INDEX_NONE;
    UPROPERTY()
    TArray<FAbilityOriginalNodeDefinition> Definitions;
    UPROPERTY()
    TArray<FAbilityOriginalEdgeDefinition> Edges;
};

USTRUCT()
struct TEST_25D_API FAbilityOriginalOutputMapping
{
    GENERATED_BODY()
    UPROPERTY()
    FName OutputPort;
    UPROPERTY()
    FName OriginalPort;
    UPROPERTY()
    EAbilityOriginalPublication Publication = EAbilityOriginalPublication::Preserve;
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
    /** None为新helper/替代payload；原节点ordinal直接参与语义摘要，不用随机GUID决定预算身份。 */
    UPROPERTY()
    int32 OriginalIndex = INDEX_NONE;
    UPROPERTY()
    EAbilityOriginalNodeRole OriginalRole = EAbilityOriginalNodeRole::Helper;
    UPROPERTY()
    TArray<FAbilityOriginalOutputMapping> OriginalOutputs;
    /** 服务器编译器标记的新增有限片段；不得声称原Entry/寿命或取得辅助预算豁免。 */
    UPROPERTY()
    bool bRuntimeFinite = false;
    /** 仅编译器标记；必须从实际无限端口可达并逐入边重验纯动作，不能伪造原调用或有限域身份。 */
    UPROPERTY()
    bool bRuntimeRepeatedPayload = false;
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
