// 调用链：服务器配方 -> 批查询/同根原生命账本 -> 不可变目标集合 -> 共享伤害动作 -> 有消费者的命中分支。
// 集合不复制到客户端、不持有目标强引用；图取消关闭发布，已结算属性仍由原 Actor/Mass 通路同步。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityTargetSet.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilityBatchCombatNodes.generated.h"

class FAbilityNodeRegistry;

/** 仅保存历史事实，不延长目标 Actor/实体生命。 */
struct FAbilityBatchHitRecord
{
    FAbilityTargetToken Target;
    FAbilityDamageResult Result;
};

/** 动作差异只影响真实结果的位置/拒绝门；所有模式继续复用 AbilityDamageService。 */
UENUM(BlueprintType)
enum class EAbilityDamageActionProfile : uint8
{
    Unified,
    LegacyActor,
    LegacyMixed
};

/** 空集合也是一次正常查询完成；不把 MaxTargets 当可静默截断的命中前缀。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityQueryBatchNodeConfig : public UAbilityQueryNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityQueryLifePolicy LifePolicy = EAbilityQueryLifePolicy::FreshQuery;
    /** 验证批集合上限和显式生命策略；继承 Query 参数以共用现有数值修正入口。 */
    virtual bool Validate(FString& Error) const override;
};

/** 整批先完成根伤害，再请求真实命中后继；内部目标循环不占单目标节点调用预算。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityDamageBatchNodeConfig : public UAbilityDamageNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityDamageActionProfile Profile = EAbilityDamageActionProfile::Unified;
    /** 校验伤害继承合同及已登记结果 profile，不接受未知枚举作为隐式兼容模式。 */
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityQueryBatchNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 权威端冻结完整集合，保持候选顺序；即使没有敌人也发布一个空集合完成事件。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityDamageBatchNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 每个原生命在应用前复核；收集整批结果后才请求 OnHit，源失效/取消立即停止后续。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 封口待续回调；图取消负责丢弃 receipt，已落地状态不在此回滚。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 每次真实接触的图 body 完成后继续；无消费者时同步整批快路径不制造逐目标事件。 */
    void ContinueBatch();
    /** 批末才排队原 OnHit，不等待各个独立子原型的整个寿命。 */
    void FinishBatch(UAbilityGraphInstance& Owner);
    FAbilityNodeInput HitPayload(const FAbilityBatchHitRecord& Hit) const;
    UPROPERTY(Transient)
    FDamageEffectParams PreparedParams;
    TSharedPtr<const FAbilityTargetSetSnapshot> Targets;
    TArray<FAbilityBatchHitRecord> Results;
    EAbilityDamageActionProfile ActionProfile = EAbilityDamageActionProfile::Unified;
    int32 TargetIndex = 0;
    bool bExtensionPayload = false;
    bool bHasHook = false;
    bool bContactSequence = false;
};

namespace AbilityBatchCombatNodes
{
    /** 注册 TargetSet typed 端口和真实执行类；不覆盖已存在的相同 TypeId。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityBatchCombatNodes
