// 调用链：空间节点建立单步凭据 -> 批效果在每次实际动作前重新验证 -> 同步 GE 可改变后续候选。
// 凭据只记录已冻结的空间事实；独立准入账本不持有 Actor，随节点与在途快照一起回收。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityTargetSet.h"

class UAbilityGraphInstance;

/** 只登记已有真实对照的波形合同；普通 Query.Shape 不自动获得新的几何复核。 */
enum class EAbilitySelectionReceiptProfile : uint8
{
    ActorWave,
    MixedWave,
    MixedFrontWave
};

/**
 * 空间准入与数值效果分离：同一生命只属于一个已准入步骤，同步骤可供多个独立 payload 消费。
 * 只有空间/原生命通过后才登记，避免把被早先 GE 移开的候选错误标为永久已命中。
 */
class TEST_25D_API FAbilitySelectionAdmissionLedger
{
public:
    /** 由空间调用一次性绑定根和来源，不在后续消费时重新认领施法身份。 */
    FAbilitySelectionAdmissionLedger(FGuid InRoot, const FAbilitySourceToken& InSource);
    /** 当前图/来源必须与创建时完整相同，同 ASC 的另一次施法也不能借用。 */
    bool BelongsTo(FGuid ExpectedRoot, const FAbilitySourceToken& ExpectedSource) const;
    /** 后续空间步骤用身份去重；同根原生命账本另行拒绝池复用。 */
    bool Contains(const FPureMassCombatTarget& Identity) const;
    /** 同步接触后的状态后继可继续消费同一步已经成立的命中事实。 */
    bool AdmittedInStep(const FPureMassCombatTarget& Identity, FGuid Step) const;
    /** 首次准入记录步骤；重复当前步骤可消费，另一后续步骤与超限身份均失败关闭。 */
    bool Admit(const FPureMassCombatTarget& Identity, FGuid Step);

private:
    TMap<FPureMassCombatTarget, FGuid> AdmittedSteps;
    FGuid Root;
    FAbilitySourceToken Source;
};

/** 单步空间事实不再修改；弱宿主只用于原 LOS 排除，不取得表现资源所有权。 */
struct TEST_25D_API FAbilitySelectionReceipt
{
    EAbilitySelectionReceiptProfile Profile = EAbilitySelectionReceiptProfile::ActorWave;
    FVector Origin = FVector::ZeroVector;
    float MaximumRadius = 0.f;
    float PreviousRadius = 0.f;
    float CurrentRadius = 0.f;
    TWeakObjectPtr<AActor> IgnoredEffect;
    FGuid Step;
    /** 可变准入状态与上面的不可变几何值分开；每个 Wave 调用拥有独立账本。 */
    TSharedPtr<FAbilitySelectionAdmissionLedger> Admissions;
};

namespace AbilitySelectedTargetService
{
    /** GI输入入口可提前拒绝借用其它根的凭据；没有凭据的原集合不附加新限制。 */
    TEST_25D_API bool ValidateSelectionOwner(FGuid Root, const FAbilitySourceToken& Source,
                                             const FAbilityTargetSetSnapshot& Selection);
    /**
     * 始终验证原生命；凭据存在时按该步骤几何/LOS复核并登记实际准入，不重新捕获池生命。
     * 无凭据完全沿用原 ResolveTarget，不影响普通批查询、Native 或其它兼容 profile。
     */
    TEST_25D_API bool ResolveSelectedTarget(const UAbilityGraphInstance& Owner, const FAbilitySourceToken& Source,
                                            const FAbilityTargetSetSnapshot& Selection,
                                            const FAbilityTargetToken& Target, FVector& OutLocation);
} // namespace AbilitySelectedTargetService
