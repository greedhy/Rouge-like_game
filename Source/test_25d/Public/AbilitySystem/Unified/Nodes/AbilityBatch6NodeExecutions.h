// 调用链：Batch6 GA或任意Schema2图 -> 注册策略 -> 原伤害/石化服务 -> 类型化完成端口。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityBatch6NodeExecutions.generated.h"
class UNiagaraSystem;
class UMaterialInterface;
class FAbilityNodeRegistry;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityForkedChainNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTargets = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxDepth = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 BranchesPerTarget = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float JumpRadius = 400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageFalloff = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UNiagaraSystem> BeamSystem;
    /** 保留旧整数/跳跃距离夹取规则；拒绝非有限的伤害缩放。 */
    virtual bool Validate(FString& Error) const override;
};
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityAreaPetrifyNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 650.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UMaterialInterface> StoneMaterial;
    /** 参数必须有限；目标数量沿用旧范围查询，不引入每目标图调用上限。 */
    virtual bool Validate(FString& Error) const override;
};
UCLASS(Transient)
class TEST_25D_API UAbilityForkedChainNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 每调用独占BFS队列和去重集合，不激活完整GA，命中前重新验证生命身份。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};
UCLASS(Transient)
class TEST_25D_API UAbilityAreaPetrifyNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 原Gather/Visible/Enemy规则统一复用Status服务；状态由目标组件继续持有。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};
namespace AbilityBatch6Nodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
