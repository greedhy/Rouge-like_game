/**
 * 扩展行为执行器：Execute/Emit -> Spawn -> FinishSpawning/BeginPlay；后续引擎 Tick 驱动周期、弹体、转移和结束。Hit 分流 Actor GE 与 Mass 伤害；Stack/Displace 将状态交给世界子系统。Node/端点/结束状态复制给客户端，Context 不复制。Finish 清理地表并延迟销毁；EndPlay 为地表清理兜底。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PureMass/PureMassCombatTarget.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "ExtensionRuntime.generated.h"

UCLASS()
class TEST_25D_API AExtensionRuntime : public AActor
{
    GENERATED_BODY()
public:
    /** 构造复制 Actor、移动复制及 Niagara 子组件；在 PostUpdateWork Tick 推进，Visual 随 Actor 销毁。 */
    AExtensionRuntime();
    /** 服务器验证施法者、整图与坐标，限制地表数和共享生成预算；延迟生成期间写入 Context/目标/事件中心后 FinishSpawning。 */
    static AExtensionRuntime* Spawn(UExtensionCastContext* Context, UExtensionNode* Node, FVector Position,
                                    FVector Direction, FPureMassCombatTarget Target = {}, int32 Depth = 0,
                                    FVector Center = FVector::ZeroVector, bool bConfirmedHit = false);
    /** 初始化本地表现；服务器按 Behavior 执行 Burst、转交 Stack、发起 Chain、解析 Attach、请求 Displace 或创建 Surface。 */
    virtual void BeginPlay() override;
    /** 客户端仅更新连线参数；服务器验证 Context，按剩余寿命推进跟随/转移/扫掠/周期与结束；与 GA 激活不处于同一同步栈。 */
    virtual void Tick(float Delta) override;
    /** 引擎退出/销毁入口：服务器兜底销毁所属地表，再调用父类；组件由 Actor 生命周期回收。 */
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    /** 注册 Node、BeamStart/End 与 bFinished；子图 Context 和预算仅保存在服务器。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(ReplicatedUsing = RefreshVisual, BlueprintReadOnly)
    TObjectPtr<UExtensionNode> Node;
    UPROPERTY()
    TObjectPtr<UExtensionCastContext> Context;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FVector BeamEnd;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FVector BeamStart;
    UPROPERTY(ReplicatedUsing = RefreshVisual, BlueprintReadOnly)
    bool bFinished = false;
    /** 校验存活目标后分流 Actor/Mass 伤害，再多播表现并执行 HitChild；命中前保存的位置用于致死后子节点快照。 */
    bool Hit(FPureMassCombatTarget Candidate, float Scale = 1.f);
    /** 幂等结束；仅正常结束且施法者有效时生成 CompleteChild，销毁地表，更新表现和复制状态，再保留 0.35 秒销毁窗口。 */
    void Finish(bool Cancelled = false);
    /** 只读返回当前混合目标快照；调用者仍需 Resolve 验证它是否存活。 */
    FPureMassCombatTarget GetTarget() const
    {
        return Target;
    }

private:
    UPROPERTY()
    TObjectPtr<class UNiagaraComponent> Visual;
    UPROPERTY()
    TObjectPtr<class ATerrainSurface> Surface;
    FPureMassCombatTarget Target;
    TSet<FPureMassCombatTarget> HitTargets;
    TArray<FGuid> Moves;
    FVector Direction, Center, LastPosition;
    int32 Depth = 0, Transfers = 0, Hits = 0;
    float Age = 0, Clock = 0, Travel = 0;
    bool bPresented = false;
    bool bConfirmedHit = false;
    /** 行为连接入口：共享 Context，以 Depth+1 生成子节点，传递命中位置/中心及服务端确认命中快照；不再次 Commit。 */
    bool Emit(UExtensionNode* Child, FVector Position, FPureMassCombatTarget Candidate, FVector EventCenter,
              bool bFromConfirmedHit = false);
    /** 初次 BeginPlay/后续 Tick 调用：结算一跳、记录去重目标和衰减，复制两端位置，再选可见的最近未命中目标。 */
    void ChainStep();
    UFUNCTION()
    /** BeginPlay、Finish 及复制通知共用表现入口：循环粒子或一次结束表现；专用服务器跳过，不执行伤害。 */
    void RefreshVisual();
    UFUNCTION(NetMulticast, Unreliable)
    /** 不可靠多播命中表现入口：由服务器 Hit 发起，客户端只播放 Niagara，不据此结算伤害。 */
    void Impact(FVector Position);
};
