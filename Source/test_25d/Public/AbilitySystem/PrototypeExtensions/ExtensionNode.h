/**
 * 扩展数据图：Behavior 决定执行分支，HitChild/PeriodChild/CompleteChild 描述连接；禁止混入父类旧 OnHit/OnPeriod/OnExpire。GA 激活及 Runtime::Spawn -> ValidateGraph -> 文件内 Validate 递归。DA 是共享配置；Context 则记录单次施法预算、地表数及已引爆效果。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "ExtensionNode.generated.h"

UENUM(BlueprintType)
enum class EExtensionKind : uint8
{
    Attach,
    Stack,
    Displace,
    Projectile,
    Chain,
    Burst,
    Delay,
    Surface
};

/** Opt-in graph: old configuration assets are never interpreted as extension nodes. */
UCLASS(BlueprintType)
class TEST_25D_API UExtensionNode : public UAbilityPrototype
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EExtensionKind Behavior = EExtensionKind::Burst;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UExtensionNode> HitChild;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UExtensionNode> PeriodChild;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UExtensionNode> CompleteChild;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName EffectId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 MaxTransfers = 2;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float TransferRadius = 350.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 StackThreshold = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float StackLifetime = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float DetonationDelay = .4f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float MoveDistance = 180.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float SafeDistance = 70.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bPull = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bSingleTarget = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<TSubclassOf<AActor>> ImmuneActorClasses;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FName> ImmuneActorTags;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<FName, float> ActorTagMovementScale;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<TObjectPtr<class UPureMassMonsterType>> ImmuneMassTypes;
    UFUNCTION(BlueprintPure)
    /** 新建路径集合，从深度零执行 Validate；失败阻止激活/生成，不修改配置资产。 */
    bool ValidateGraph() const;
};

UCLASS()
class TEST_25D_API UExtensionCastContext : public UPrototypeCastContext
{
    GENERATED_BODY()
    // 兼容已存在的反射类身份；公共施法状态已移到 UPrototypeCastContext，不能形成第二份计数器。
};
