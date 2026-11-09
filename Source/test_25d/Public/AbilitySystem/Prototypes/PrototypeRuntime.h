/** GA / graph strategy -> Spawn or SpawnDriven -> driver Start; compatibility Tick or graph Advance -> driver behavior and child admission. This Actor owns only replication, presentation resources and compatibility delegates. */
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Unified/LegacyAbilityDriverTypes.h"
#include "PrototypeRuntime.generated.h"

class FActorAbilityDriver;

class UNiagaraComponent;
class AMyOrbitingFireBolt;
class UGameplayAbility;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPrototypeEvent, FName, Event, AActor*, Target, FVector, Location);

/** Replicated presentation with authority-only gameplay. Each cast owns an independent context. */
UCLASS()
class TEST_25D_API APrototypeRuntime : public AActor
{
    GENERATED_BODY()
public:
    /** 构造复制 Actor 与两个非自动激活 Niagara 组件；启用移动复制和 Tick，组件随 Actor 生命周期管理。 */
    APrototypeRuntime();
    static APrototypeRuntime* Spawn(UPrototypeCastContext* Context, UAbilityPrototype* Config, const FVector& Position,
                                    const FVector& Direction, AActor* Target, int32 Depth = 0, bool bSingle = false);
    /** FinishSpawning/复制生成后的引擎入口：先建立表现；服务器按 Kind 执行首发、创建地表或绑定 Orbit 攻击监听。 */
    virtual void BeginPlay() override;
    /** 引擎逐帧入口：本地更新表现；服务器处理取消、费用、寿命，再按 Kind 分派行为更新。后续 Tick 不在 ActivateAbility 的同步栈中。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 引擎销毁/世界退出兜底入口：清理所属对象与委托，再交父类释放组件。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 引擎复制注册入口：声明配置与表现状态字段；Context 和伤害执行保持服务器本地。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(ReplicatedUsing = OnRep_Prototype, BlueprintReadOnly)
    TObjectPtr<UAbilityPrototype> Prototype;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FVector BeamEnd;
    UPROPERTY(Replicated, BlueprintReadOnly)
    float WaveRadius = 0.f;
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bReturning = false;
    UPROPERTY(BlueprintAssignable)
    FPrototypeEvent BehaviorEvent;
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UPrototypeCastContext> Context;
    UFUNCTION(BlueprintPure)
    /** 查询仍有效的环绕弹体数量；只读，不创建或回收对象。 */
    int32 GetOrbitCount() const;
    UFUNCTION(BlueprintPure)
    /** 只读查询本 Runtime 结束标志；不是整张组合图全部节点结束的判断。 */
    bool IsFinished() const
    {
        return bFinished;
    }
    UFUNCTION(BlueprintCallable)
    /** GA 或外部的服务器取消入口：标记共享 Context，使同次施法其他 Runtime 在后续更新中停止。 */
    void CancelCast();
    /** 目标过滤公共入口：检查有效性、存活、阵营、战斗接口与 ASC；此执行器针对 Actor。 */
    static bool IsEnemy(AActor* Caster, AActor* Target);
    /** 根费用及持续费用入口：服务器校验余额后应用瞬时扣蓝 GE，返回应用成功状态。 */
    static bool SpendMana(AActor* Caster, float Amount);
    /** 根施法及子节点/表现共用地面投影；忽略角色，以可见性射线寻找地面，失败不返回伪造落点。 */
    static bool ProjectSurfaceToGround(AActor* Caster, const FVector& Position, FVector& OutPosition);
    /** 范围行为的候选 Actor 查询，供 Pulse/Trap 等筛选敌方目标。 */
    void GatherTargets(TArray<AActor*>& Out) const;

    /** Graph-owned invocation: installs child admission before BeginPlay and disables autonomous gameplay ticking. */
    static APrototypeRuntime* SpawnDriven(UPrototypeCastContext* InContext, UAbilityPrototype* Config,
                                          const FVector& Position, const FVector& InDirection, AActor* Target,
                                          int32 InDepth, bool bSingle, FLegacyAbilityChildAdmission Admission,
                                          UAbilityPrototype* PresentationConfig = nullptr);
    /** The graph advances the extracted strategy while the actor only updates replicated presentation. */
    TSharedPtr<FActorAbilityDriver> GetDriver() const
    {
        return Driver;
    }

private:
    friend class FActorAbilityDriver;
    TSharedPtr<FActorAbilityDriver> Driver;
    bool bExternallyDriven = false;
    /** Immutable execution snapshot; only the original asset reference is replicated for presentation. */
    UPROPERTY()
    TObjectPtr<UAbilityPrototype> ExecutionPrototype;
    static APrototypeRuntime* SpawnInternal(UPrototypeCastContext* InContext, UAbilityPrototype* Config,
                                            const FVector& Position, const FVector& InDirection, AActor* Target,
                                            int32 InDepth, bool bSingle, FLegacyAbilityChildAdmission Admission,
                                            bool bDriven, UAbilityPrototype* PresentationConfig);
    UPROPERTY()
    TObjectPtr<UNiagaraComponent> Visual;
    UPROPERTY()
    TObjectPtr<UNiagaraComponent> InnerVisual;
    UPROPERTY()
    TObjectPtr<ATerrainSurface> Surface;
    UPROPERTY()
    TArray<TObjectPtr<AMyOrbitingFireBolt>> Bolts;
    UPROPERTY(Replicated)
    bool bSingleProjectile = false;
    UPROPERTY(ReplicatedUsing = OnRep_Finished)
    bool bFinished = false;
    UPROPERTY(Replicated)
    bool bImpactOnFinish = false;
    bool bPresentationReady = false, bFinishPresented = false;
    UFUNCTION()
    /** 配置复制通知，同时由 BeginPlay 主动调用；负责本地 Niagara 初始化，不执行权威伤害。 */
    void OnRep_Prototype();
    UFUNCTION()
    /** 结束状态复制通知及服务器本地主动调用：关闭循环特效，并通过 bFinishPresented 防止重复结束表现。 */
    void OnRep_Finished();
    UFUNCTION(NetMulticast, Unreliable)
    /** 服务器发起的不可靠多播入口；实现函数仅播放瞬时命中特效，不结算伤害。 */
    void MulticastImpact(FVector Position);
    /** 行为事件连接器：服务器先广播本地 BehaviorEvent，再用同一 Context 和 Depth+1 生成 Child；该委托本身不是网络广播。 */
    bool Emit(FName Event, UAbilityPrototype* Child, AActor* Target, FVector Position);
    /** 服务器行为命中入口：构造节点伤害参数并调用 ApplyDamageEffect，随后 Emit(OnHit) 执行组合子节点。 */
    void Hit(AActor* Target, float Scale = 1.f);
    /** Burst/Field/Trap 的范围脉冲入口：查询候选并调用 Hit，驱动命中表现。 */
    void Pulse();
    /** 正常到期/取消汇合点：幂等设置结束标志；正常路径触发 OnExpire，取消路径标记共享 Context；清理附属对象并保留短暂复制寿命。 */
    void Finish(bool bCancelled);
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
    /** Tick 的环绕分支：清理失效引用、定期补充弹体、发射/返航并分配环绕相位；弹体伤害完成后回调 OrbitHit。 */
    void TickOrbit(float Delta);
    /** ASC OffensiveAttackTargeted 委托入口：校验敌方目标与距离后记录 Orbit 的攻击目标。 */
    void OffensiveAttack(UGameplayAbility* Ability, AActor* Target);
    /** 环绕弹体 DamageApplied 委托入口：转发 OnHit 子图，并在目标已死亡时广播本地 OnKill。 */
    void OrbitHit(AActor* Target);
    /** Beam 目标可见性检查，供服务器 Tick 决定是否继续引导。 */
    bool HasLineOfSight(AActor* Target) const;
    /** Finish 与 EndPlay 共用清理：服务器解绑 ASC 监听并销毁地表/环绕弹体，随后清空持有引用。 */
    void CleanupOwnedActors();
};
