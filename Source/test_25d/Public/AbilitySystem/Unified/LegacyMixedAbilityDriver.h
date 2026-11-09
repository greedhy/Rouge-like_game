/** Graph strategy / compatibility host -> invocation driver -> existing damage/query services.
 * Mutable clocks, targets and deduplication are per invocation; the host owns replicated/GC resources. */
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/LegacyAbilityDriverTypes.h"
class APureMassRuntime;
class UAbilityPrototype;
class UExtensionNode;
class UGameplayAbility;

class TEST_25D_API FMixedAbilityDriver
{
public:
    explicit FMixedAbilityDriver(APureMassRuntime* Host);
    /** A missing presentation host is terminal; no raw actor pointer survives world teardown. */
    bool IsFinished() const;
    /** 委托 PureMassCombat::Visible，从指定起点检查 Actor/实体遮挡并忽略本执行器。 */
    bool Visible(FVector Origin, const FPureMassCombatTarget& Candidate) const;
    /** 服务器验证来源 Avatar/世界与目标；Actor 应用 GE，Mass ApplyDamage；随后追加 Batch6 减速、命中表现与 OnHit 子节点。 */
    bool Hit(FPureMassCombatTarget Candidate, float Scale = 1.f);
    /** Publish the backend event and request child admission; graph calls use the scheduler, compatibility calls spawn a standalone driver. */
    bool Emit(FName Event, UAbilityPrototype* Child, FVector Position, FPureMassCombatTarget Candidate);
    /** 范围查询后过滤已命中、不可解析及不可见目标，选择距离最近的混合目标。 */
    FPureMassCombatTarget Nearest(FVector Origin, float Radius) const;
    /** 范围分支入口：按扇形、环形、直线/高度或波前筛选，调用 Hit；波形记录已命中目标避免重复。 */
    void Pulse();
    /** Tick 弹体分支：推进追踪/穿透/返航，统一扫掠 Actor、实体及墙体，结算命中并处理结束。 */
    void Projectile(float Delta);
    /** Tick 连锁分支：结算当前跳及衰减，更新链起点，选择下一未命中目标。 */
    void Chain();
    /** Tick 环绕分支：维护轻量弹体状态、发射间隔和追踪扫掠；这些弹体不是每颗一个敌人 Actor。 */
    void Orbit(float Delta);
    /** 幂等结束：正常结束生成 OnExpire，取消标记共享 Context；销毁地表、清 OrbitBolts、更新复制并延迟销毁。 */
    void Finish(bool bCancelled);
    /** Initialize authority behavior. */
    void Start();
    /** Advance the original backend clock and behavior. */
    void Advance(float Delta);

private:
    friend class APureMassRuntime;
    TWeakObjectPtr<APureMassRuntime> Presentation;
    FLegacyAbilityChildAdmission ChildAdmission;
    bool bStarted = false;
    /** Route admitted children through the graph; standalone compatibility calls keep their original spawn path. */
    bool RequestChild(FName Event, UAbilityPrototype* Child, FVector Position, FVector ChildDirection,
                      FPureMassCombatTarget Candidate, int32 ChildDepth, bool bSingle);
    bool bReturning = false;
    FVector Direction = FVector::ForwardVector;
    FVector ChainOrigin = FVector::ZeroVector;
    FPureMassCombatTarget Target;
    int32 Depth = 0, Emissions = 0;
    TSet<FPureMassCombatTarget> HitTargets;
    float Age = 0.f, PeriodClock = 0.f, Travel = 0.f, LaunchClock = 0.f;
    float PreviousWaveRadius = 0.f;
};
