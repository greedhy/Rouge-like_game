// 调用链：旧配置编译为图 -> 注册工厂 -> 本调用执行对象 -> 提取后的 Driver -> 图子事件队列。
// 复制 Actor 仅提供既有表现与资源所有权，玩法推进由此策略驱动，不再次激活旧 GA。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/LegacyAbilityDriverTypes.h"
#include "AbilityLegacyNodeExecution.generated.h"

class UAbilityPrototype;
class UPrototypeCastContext;
class FAbilityNodeRegistry;
class APrototypeRuntime;
class APureMassRuntime;
class AExtensionRuntime;

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityLegacyNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 每个编译节点拥有独立参数快照；子引用只用于旧校验，实际连接由图边决定。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Compatibility")
    TObjectPtr<UAbilityPrototype> Parameters;
    /** 网络复制使用稳定资产引用；不能把上面的瞬态快照复制给客户端。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compatibility")
    TObjectPtr<UAbilityPrototype> PresentationParameters;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compatibility")
    FName Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compatibility")
    int32 LegacyDepth = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compatibility")
    bool bSingleProjectile = false;
    /** 保留旧深度 0..3、节点参数与 profile 的对应关系。 */
    virtual bool Validate(FString& Error) const override;
    virtual bool MatchesType(FName TypeId) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityLegacyNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    /** 先关闭图回调，再让 Driver 清理地表/环绕弹体等资源；正常完成不取消同次施法的其它节点。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 将旧事件转换为类型化载荷；致死目标只带确认命中快照，不冒充活目标。 */
    bool AdmitChild(const FLegacyAbilityChildRequest& Request);
    /** Driver 已完成时关闭本调用，图仍可继续运行其它已接纳的子节点。 */
    void CheckFinished();
    UPROPERTY(Transient)
    TObjectPtr<APrototypeRuntime> ActorHost;
    UPROPERTY(Transient)
    TObjectPtr<APureMassRuntime> MixedHost;
    UPROPERTY(Transient)
    TObjectPtr<AExtensionRuntime> ExtensionHost;
    UPROPERTY(Transient)
    TObjectPtr<UPrototypeCastContext> CastContext;
    bool bStarting = false;
};

namespace AbilityLegacyNodes
{
    /** 完整注册已提取的三个行为族；单个描述符决定配置和执行类，不修改图运行器分派。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
    /** 稳定字符串来自行为名而非枚举整数；第五批以 Behavior 决定类型。 */
    TEST_25D_API FName TypeId(const UAbilityPrototype& Parameters, FName Profile);
} // namespace AbilityLegacyNodes
