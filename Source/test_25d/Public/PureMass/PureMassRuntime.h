/** GA / graph strategy -> Spawn or SpawnDriven -> mixed driver; graph or compatibility Tick advances behavior. Actor owns replicated endpoints, Surface and Batch6 presentation; driver owns clocks, targeting and child admission. */
// 技能运行入口：施法生成行为节点，逐帧分发弹道与范围行为，经统一目标查询分流到角色效果或实体伤害；结束时清理附属表现。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "PureMass/PureMassCombatTarget.h"
#include "AbilitySystem/Unified/LegacyAbilityDriverTypes.h"
#include "PureMassRuntime.generated.h"

class FMixedAbilityDriver;
class UNiagaraComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UGameplayAbility;

USTRUCT(BlueprintType)
struct FPureMassOrbitBolt
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FVector Position = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    bool bLaunched = false;
    UPROPERTY(BlueprintReadOnly)
    float Age = 0.f;
};

/** One effect actor per active behavior node, never an actor per enemy. */
UCLASS()
class TEST_25D_API APureMassRuntime : public AActor
{
    GENERATED_BODY()
public:
    // 构建可复制的技能行为载体及默认关闭的粒子组件。
    /** 构造复制 Actor、移动复制和 Niagara；表现组件初始关闭，等待有效配置。 */
    APureMassRuntime();
    // 服务端校验施法上下文与生成预算，延迟创建并填充行为节点。
    /** 服务器验证 Context/配置/预算，延迟生成并写入方向、目标与深度后完成生成；失败返回空指针。 */
    static APureMassRuntime* Spawn(UPrototypeCastContext* Context, UAbilityPrototype* Config, FVector Position,
                                   FVector Direction, FPureMassCombatTarget Target = {}, int32 Depth = 0,
                                   bool bSingle = false);
    // 在已提交攻击后更新施法者环绕技能的目标。
    /** 成功提交攻击后的入口：将混合目标通知施法者当前环绕节点，允许 Actor 和 Mass 目标。 */
    static void NotifyCommittedAttack(AActor* Caster, UGameplayAbility* Ability, FPureMassCombatTarget Target);
    // 仅接受有效且严格早于墙体的实体命中时间。
    /** 只接受有限且位于线段内、严格早于墙体的实体碰撞时间；相同时间由墙体阻挡。 */
    static bool EntityBeforeWall(float EntityTime, bool bWall, float WallTime)
    {
        return FMath::IsFinite(EntityTime) && EntityTime >= 0.f && EntityTime <= 1.f &&
               (!bWall || EntityTime < WallTime);
    }
    // 开始时初始化表现，并按原型分发散射、瞬发范围或地表创建。
    /** 初始化表现；服务器执行初始散射、瞬发范围或地表生成，后续持续行为交 Tick。 */
    virtual void BeginPlay() override;
    // 先同步本地表现，再由服务端推进寿命、耗蓝和各类行为周期。
    /** 本地更新粒子/Batch6 表现；权威端校验施法者、取消、寿命和扣蓝，再按 Kind 推进周期行为。 */
    virtual void Tick(float Delta) override;
    // 结束生命周期时清理本对象拥有的资源，再调用父类收尾。
    /** 引擎退出兜底清理所属 Surface，再由父类回收 Actor 及其组件。 */
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    // 登记行为配置、目标端点、波半径和结束状态等复制字段。
    /** 注册配置、光束端点、波半径、环绕状态、结束标记和单弹体状态；伤害 Context 不复制。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    // 服务端取消整次施法上下文并结束当前节点。
    UFUNCTION(BlueprintCallable)
    /** 外部/GA 的服务器取消入口：标记整次施法 Context 并结束本节点。 */
    void CancelCast();
    // 读取当前行为节点是否已结束。
    /** 只读当前节点是否结束，不代表同 Context 的所有子节点均已销毁。 */
    bool IsFinished() const
    {
        return bFinished;
    }
    UPROPERTY(ReplicatedUsing = RefreshVisual, BlueprintReadOnly)
    TObjectPtr<UAbilityPrototype> Prototype;
    UPROPERTY()
    TObjectPtr<UPrototypeCastContext> Context;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FVector BeamEnd = FVector::ZeroVector;
    UPROPERTY(Replicated)
    float WaveRadius = 0.f;
    UPROPERTY(Replicated, BlueprintReadOnly)
    TArray<FPureMassOrbitBolt> OrbitBolts;

    /** Graph-owned invocation: installs child admission before BeginPlay and disables autonomous gameplay ticking. */
    static APureMassRuntime* SpawnDriven(UPrototypeCastContext* InContext, UAbilityPrototype* Config, FVector Position,
                                         FVector InDirection, FPureMassCombatTarget InTarget, int32 InDepth,
                                         bool bInSingle, FLegacyAbilityChildAdmission Admission,
                                         UAbilityPrototype* PresentationConfig = nullptr);
    /** The graph advances the extracted strategy while the actor only updates replicated presentation. */
    TSharedPtr<FMixedAbilityDriver> GetDriver() const
    {
        return Driver;
    }

private:
    friend class FMixedAbilityDriver;
    TSharedPtr<FMixedAbilityDriver> Driver;
    bool bExternallyDriven = false;
    /** Immutable execution snapshot; only the original asset reference is replicated for presentation. */
    UPROPERTY()
    TObjectPtr<UAbilityPrototype> ExecutionPrototype;
    static APureMassRuntime* SpawnInternal(UPrototypeCastContext* InContext, UAbilityPrototype* Config,
                                           FVector Position, FVector InDirection, FPureMassCombatTarget InTarget,
                                           int32 InDepth, bool bInSingle, FLegacyAbilityChildAdmission Admission,
                                           bool bDriven, UAbilityPrototype* PresentationConfig);
    UPROPERTY()
    TObjectPtr<UNiagaraComponent> Visual;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Batch6Area;
    UPROPERTY()
    TObjectPtr<UInstancedStaticMeshComponent> Batch6Snow;
    UPROPERTY()
    TArray<TObjectPtr<UNiagaraComponent>> BoltVisuals;
    UPROPERTY()
    TObjectPtr<ATerrainSurface> Surface;
    UPROPERTY(ReplicatedUsing = RefreshVisual)
    bool bFinished = false;
    UPROPERTY(Replicated)
    bool bSingle = false;
    bool bPresented = false;
    // 初始播放与复制通知共同更新循环特效和结束表现。
    UFUNCTION()
    /** BeginPlay/Finish/RepNotify 共用：更新循环和一次结束表现；专用服务器跳过；初始化光束端点后才激活 Niagara。 */
    void RefreshVisual();
    // 按范围配置创建贴地圆盘与雪花实例，仅用于表现。
    /** 配置驱动的地面圆盘/雪花组件创建与位置更新，仅用于表现；组件由 Runtime 持有，不参与命中。 */
    void UpdateBatch6Presentation();
    UFUNCTION(NetMulticast, Unreliable)
    /** 服务器 Hit 发起的不可靠多播，仅通知客户端一次性命中特效。 */
    void Impact(FVector Position);
    // 通过统一遮挡查询判断目标是否可见。
    /** 委托 PureMassCombat::Visible，从指定起点检查 Actor/实体遮挡并忽略本执行器。 */
    bool Visible(FVector Origin, const FPureMassCombatTarget& Candidate) const;
    // 命中后按角色或实体分流伤害，追加减速、命中特效和子节点。
    /** 服务器验证来源 Avatar/世界与目标；Actor 应用 GE，Mass ApplyDamage；随后追加 Batch6 减速、命中表现与 OnHit 子节点。 */
    bool Hit(FPureMassCombatTarget Candidate, float Scale = 1.f);
    // 沿共享施法上下文生成下一层子节点并沿用递归预算。
    /** 服务器沿同一 Context 以 Depth+1 生成子节点，继承生成预算和取消状态，不再次扣根技能费用。 */
    bool Emit(UAbilityPrototype* Child, FVector Position, FPureMassCombatTarget Candidate);
    // 按扇形、环形、直线或波前规则过滤候选并逐一命中。
    /** 范围分支入口：按扇形、环形、直线/高度或波前筛选，调用 Hit；波形记录已命中目标避免重复。 */
    void Pulse();
    // 推进弹道并扫掠命中，处理穿透、追踪、回返及墙体终止。
    /** Tick 弹体分支：推进追踪/穿透/返航，统一扫掠 Actor、实体及墙体，结算命中并处理结束。 */
    void Projectile(float Delta);
    // 执行一跳连锁伤害，递减倍率并选择下一目标。
    /** Tick 连锁分支：结算当前跳及衰减，更新链起点，选择下一未命中目标。 */
    void Chain();
    // 生成环绕弹体，按发射间隔追踪目标并扫掠结算。
    /** Tick 环绕分支：维护轻量弹体状态、发射间隔和追踪扫掠；这些弹体不是每颗一个敌人 Actor。 */
    void Orbit(float Delta);
    // 从范围候选中挑选未命中过且可见的最近目标。
    /** 范围查询后过滤已命中、不可解析及不可见目标，选择距离最近的混合目标。 */
    FPureMassCombatTarget Nearest(FVector Origin, float Radius) const;
    // 幂等结束节点；正常到期触发子节点，取消则标记共享上下文并清理表现。
    /** 幂等结束：正常结束生成 OnExpire，取消标记共享 Context；销毁地表、清 OrbitBolts、更新复制并延迟销毁。 */
    void Finish(bool bCancelled);
};
