/** Graph strategy / compatibility host -> invocation driver -> existing damage/query services.
 * Mutable clocks, targets and deduplication are per invocation; the host owns replicated/GC resources. */
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/LegacyAbilityDriverTypes.h"
class APrototypeRuntime;
class UAbilityPrototype;
class UExtensionNode;
class UGameplayAbility;

class TEST_25D_API FActorAbilityDriver
{
public:
    explicit FActorAbilityDriver(APrototypeRuntime* Host);
    /** A missing presentation host is terminal; no raw actor pointer survives world teardown. */
    bool IsFinished() const;
    /** 范围行为的候选 Actor 查询，供 Pulse/Trap 等筛选敌方目标。 */
    void GatherTargets(TArray<AActor*>& Out) const;
    /** Beam 目标可见性检查，供服务器 Tick 决定是否继续引导。 */
    bool HasLineOfSight(AActor* Target) const;
    /** 服务器行为命中入口：构造节点伤害参数并调用 ApplyDamageEffect，随后 Emit(OnHit) 执行组合子节点。 */
    void Hit(AActor* Target, float Scale = 1.f);
    /** Burst/Field/Trap 的范围脉冲入口：查询候选并调用 Hit，驱动命中表现。 */
    void Pulse();
    /** Tick 的弹体分支：推进飞行、检测碰撞和行为结束，处理追踪、穿透及返航等节点差异。 */
    void TickProjectile(float Delta);
    /** Tick 的连锁分支：按当前链状态选择后续目标，更新连线端点与命中记录。 */
    void TickChain(float Delta);
    /** BeginPlay 的扇形分支：筛选夹角内目标并执行节点命中。 */
    void ConePulse();
    /** BeginPlay 的环形/直线分支：按配置形状与高度筛选目标后结算。 */
    void ShapePulse();
    /** Tick 的扩散波分支：推进半径并记录已命中目标，避免同一波反复命中。 */
    void TickWave(float Delta);
    /** Tick 的发射器分支：按周期选择目标并通过 OnPeriod 生成子节点。 */
    void TickEmitter(float Delta);
    /** 从指定起点检查目标可见性，供需要独立发射/链起点的行为调用。 */
    bool VisibleFrom(const FVector& Origin, AActor* Target) const;
    /** Publish the backend event and request child admission; graph calls use the scheduler, compatibility calls spawn a standalone driver. */
    bool Emit(FName Event, UAbilityPrototype* Child, AActor* Target, FVector Position);
    /** 正常到期/取消汇合点：幂等设置结束标志；正常路径触发 OnExpire，取消路径标记共享 Context；清理附属对象并保留短暂复制寿命。 */
    void Finish(bool bCancelled);
    /** ASC OffensiveAttackTargeted 委托入口：校验敌方目标与距离后记录 Orbit 的攻击目标。 */
    void OffensiveAttack(UGameplayAbility* Ability, AActor* Target);
    /** Tick 的环绕分支：清理失效引用、定期补充弹体、发射/返航并分配环绕相位；弹体伤害完成后回调 OrbitHit。 */
    void TickOrbit(float Delta);
    /** 环绕弹体 DamageApplied 委托入口：转发 OnHit 子图，并在目标已死亡时广播本地 OnKill。 */
    void OrbitHit(AActor* Target);
    /** Finish 与 EndPlay 共用清理：服务器解绑 ASC 监听并销毁地表/环绕弹体，随后清空持有引用。 */
    void CleanupOwnedActors();
    /** Initialize authority behavior. */
    void Start();
    /** Advance the original backend clock and behavior. */
    void Advance(float Delta);

private:
    /** Validate the original life before selected targets are used again. */
    bool IsLiveTarget(AActor* Target) const;
    friend class APrototypeRuntime;
    TWeakObjectPtr<APrototypeRuntime> Presentation;
    FLegacyAbilityChildAdmission ChildAdmission;
    bool bStarted = false;
    /** Route admitted children through the graph; standalone compatibility calls keep their original spawn path. */
    bool RequestChild(FName Event, UAbilityPrototype* Child, FVector Position, FVector ChildDirection,
                      FPureMassCombatTarget Candidate, int32 ChildDepth, bool bSingle);
    TWeakObjectPtr<AActor> AttackTarget;
    TWeakObjectPtr<AActor> LastChainTarget;
    TSet<TWeakObjectPtr<AActor>> HitTargets;
    FVector ChainOrigin = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    float Age = 0.f, PeriodClock = 0.f, ManaClock = 0.f, LaunchClock = 0.f, Distance = 0.f;
    int32 Depth = 0;
    int32 Emissions = 0;
    FDelegateHandle AttackDelegate;
};
