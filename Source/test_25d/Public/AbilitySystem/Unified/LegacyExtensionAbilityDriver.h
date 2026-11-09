/** Graph strategy / compatibility host -> invocation driver -> existing damage/query services.
 * Mutable clocks, targets and deduplication are per invocation; the host owns replicated/GC resources. */
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/LegacyAbilityDriverTypes.h"
class AExtensionRuntime;
class UAbilityPrototype;
class UExtensionNode;
class UGameplayAbility;

class TEST_25D_API FExtensionAbilityDriver : public TSharedFromThis<FExtensionAbilityDriver>
{
public:
    explicit FExtensionAbilityDriver(AExtensionRuntime* Host);
    /** A missing presentation host is terminal; no raw actor pointer survives world teardown. */
    bool IsFinished() const;
    /** Publish the backend event and request child admission; graph calls use the scheduler, compatibility calls spawn a standalone driver. */
    bool Emit(FName Event, UExtensionNode* Child, FVector P, FPureMassCombatTarget T, FVector EventCenter,
              bool ConfirmedHit = false);
    /** 校验存活目标后分流 Actor/Mass 伤害，再多播表现并执行 HitChild；命中前保存的位置用于致死后子节点快照。 */
    bool Hit(FPureMassCombatTarget T, float Scale = 1.f);
    /** 初次 BeginPlay/后续 Tick 调用：结算一跳、记录去重目标和衰减，复制两端位置，再选可见的最近未命中目标。 */
    void ChainStep();
    /** 幂等结束；仅正常结束且施法者有效时生成 CompleteChild，销毁地表，更新表现和复制状态，再保留 0.35 秒销毁窗口。 */
    void Finish(bool Cancelled = false);
    /** Initialize authority behavior. */
    void Start();
    /** Advance the original backend clock and behavior. */
    void Advance(float Delta);

private:
    friend class AExtensionRuntime;
    TWeakObjectPtr<AExtensionRuntime> Presentation;
    FLegacyAbilityChildAdmission ChildAdmission;
    bool bStarted = false;
    bool bAwaitingStack = false;
    /** World-owned stack releases this invocation without emitting completion a second time. */
    void ReleaseStack();
    /** Route admitted children through the graph; standalone compatibility calls keep their original spawn path. */
    bool RequestChild(FName Event, UExtensionNode* Child, FVector Position, FVector ChildDirection,
                      FPureMassCombatTarget Candidate, int32 ChildDepth, FVector EventCenter, bool bConfirmed);
    FPureMassCombatTarget Target;
    TSet<FPureMassCombatTarget> HitTargets;
    TArray<FGuid> Moves;
    FVector Direction, Center, LastPosition;
    int32 Depth = 0, Transfers = 0, Hits = 0;
    float Age = 0, Clock = 0, Travel = 0;
    bool bConfirmedHit = false;
};
