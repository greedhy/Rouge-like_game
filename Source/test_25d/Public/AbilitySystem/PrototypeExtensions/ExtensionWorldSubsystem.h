/**
 * 持久效果宿主：UWorld 按 Game/PIE 生命周期创建与销毁；Runtime BeginPlay -> AddStack/RequestDisplacement。世界 Tick 管理叠层/Actor 位移，Mass 处理器 -> MoveMass 管理实体位移。子系统自身不复制，叠层显示通过 Presentation Actor 复制，Actor 位移经 CharacterMovement；Deinitialize 清理根运动源及表现。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "PureMass/PureMassCombatTarget.h"
#include "AbilitySystem/Unified/LegacyAbilityDriverTypes.h"
#include "ExtensionWorldSubsystem.generated.h"

struct FRootMotionSource;

/** 与旧扩展节点共享同一个世界叠层宿主；不含 CompleteChild 或任何固定 payload。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FExtensionStackSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName EffectId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Threshold = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Lifetime = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DetonationDelay = .4f;

    /** 独立校验叠层数值，不要求创建无实际作用的子节点资产。 */
    bool Validate(FString& Error) const;
};

enum class EExtensionStackReleaseReason : uint8
{
    Replaced,
    Triggered,
    Expired,
    Cancelled,
    SourceInvalid,
    TargetLost,
    WorldEnded
};

/** 服务器历史阈值事实。原目标生命可已死亡，只能以位置继续范围 payload，不能据此重授权状态。 */
struct TEST_25D_API FExtensionStackTrigger
{
    FGuid Lease;
    FAbilitySourceToken Source;
    FAbilityTargetToken TargetLife;
    FVector Position = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    UPrototypeCastContext* Context = nullptr;
    UObject* RetainedSource = nullptr;
    int32 Depth = 0;
};

/** 请求只在服务器内部构建；World 接受后须保活 Context/RetainedSource，替换时释放原等待者。 */
struct TEST_25D_API FExtensionStackRequest
{
    UPrototypeCastContext* Context = nullptr;
    UObject* RetainedSource = nullptr;
    FExtensionStackSettings Settings;
    FAbilityTargetToken TargetLife;
    FVector Position = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    int32 Depth = 0;
    bool bConfirmedHit = false;
    TFunction<void(const FExtensionStackTrigger&)> OnThreshold;
    TFunction<void(EExtensionStackReleaseReason)> OnReleased;
};

USTRUCT()
struct FExtensionStackState
{
    GENERATED_BODY()
    UPROPERTY()
    TObjectPtr<UPrototypeCastContext> Context;
    UPROPERTY()
    TObjectPtr<UObject> RetainedSource;
    UPROPERTY()
    FExtensionStackSettings Settings;
    FAbilityTargetToken TargetLife;
    FGuid Lease;
    FVector Position, Direction;
    int32 Depth = 0, Layers = 0, VisualId = 0;
    float Remaining = 0;
    bool bPending = false;
    // 非复制的调用出口；仅在服务器保存。记录移除或替换时必须通知旧执行释放等待状态。
    TFunction<void(const FExtensionStackTrigger&)> OnThreshold;
    TFunction<void(EExtensionStackReleaseReason)> OnReleased;
    uint64 CancellationVersion = 0;
};

USTRUCT()
struct FExtensionMoveState
{
    GENERATED_BODY()
    UPROPERTY()
    TObjectPtr<UPrototypeCastContext> Context;
    UPROPERTY()
    TObjectPtr<UExtensionNode> Node;
    FPureMassCombatTarget Target;
    FGuid Token;
    FVector Destination;
    float Remaining = 0;
    uint16 RootMotionId = 0;
    /** Exact applied resource, not a reusable uint16 ID: cleanup must never mark a later source with the same ID. */
    TSharedPtr<FRootMotionSource> OwnedRootMotion;
};

UCLASS()
class TEST_25D_API UExtensionWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 引擎子系统创建筛选：只支持 Game/PIE，避免编辑器预览世界维护战斗状态。 */
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    /** 为 Tickable 子系统提供性能统计标识，不推进游戏逻辑。 */
    virtual TStatId GetStatId() const override;
    /** 服务器逐帧过期清理；先移出待引爆记录再生成子节点以避免重入失效；清理根运动/替换令牌，并更新复制表现快照。 */
    virtual void Tick(float Delta) override;
    /** 世界退出：移除所有记录的 Actor RootMotionSource，清空叠层/移动/令牌并销毁 Presentation，最后调用父类。 */
    virtual void Deinitialize() override;
    /** Explicitly cancel persistent effects after their initiating GA has ended. Authority only. */
    UFUNCTION(BlueprintCallable)
    /** 服务器主动清理入口：标记此施法者的持久上下文取消，结束其 Runtime，再 Tick(0) 立即处理无效记录。 */
    void CancelCasterEffects(AActor* Caster);
    bool AddStack(UPrototypeCastContext* Context, UExtensionNode* Node, FPureMassCombatTarget Target, FVector Position,
                  FVector Direction, int32 Depth, bool bConfirmedHit = false,
                  FLegacyAbilityChildAdmission ChildAdmission = {}, TFunction<void()> ReleasedCallback = {});
    /** 共享服务器叠层 API；返回本次接管的准确租约，失败不改变既有层数/等待者。 */
    FGuid AddStackShared(const FExtensionStackRequest& Request, FString& Error);
    /** 仅取消当前仍由该租约拥有的记录；旧施法清理不能取消接管后的新记录。 */
    bool CancelStack(FGuid Lease);
    FGuid RequestDisplacement(UPrototypeCastContext* Context, UExtensionNode* Node, FPureMassCombatTarget Target,
                              FVector Center, FVector Direction);
    /** 查询/取消准确位移命令，不能按目标或可复用 RootMotion ID 回收其他施法的资源。 */
    bool IsDisplacementActive(FGuid Token) const;
    bool CancelDisplacement(FGuid Token);
    /** Mass 处理器逐实体调用：插值到终点并再次约束碰撞，更新 Remaining；清空旧路径并短暂占用移动，避免同帧导航覆盖。 */
    void MoveMass(struct FPureMassFragment& Fragment, float Delta);
    /** Runtime 查询旧位移令牌是否已被替换或失效，用于阻止旧单目标节点继续完成子图。 */
    bool WasMoveReplaced(FGuid Token) const
    {
        return Replaced.Contains(Token);
    }
    /** 只读返回当前叠层记录数量，不是所有目标层数的总和。 */
    int32 StackCount() const
    {
        return Stacks.Num();
    }
    /** 按施法者、EffectId、混合目标查当前层数；没有匹配记录返回零。 */
    int32 LayersFor(AActor* Caster, FName EffectId, FPureMassCombatTarget Target) const;
    /** Runtime/子系统共用守卫：检查 Context 未取消、弱引用有效、Actor 未销毁且战斗接口未死亡。 */
    static bool CasterAlive(UPrototypeCastContext* Context);
    /** Actor/Mass 位移共用几何约束：胶囊扫掠、落地坡度/高度和可用导航投影；失败保留起点，不穿墙或越悬崖。 */
    static bool ConstrainMove(UWorld* World, AActor* Ignore, FVector Start, FVector Desired, float Radius,
                              float HalfHeight, FVector& Result);

private:
    UPROPERTY()
    TArray<FExtensionStackState> Stacks;
    UPROPERTY()
    TArray<FExtensionMoveState> Moves;
    UPROPERTY()
    TObjectPtr<class AExtensionPresentation> Presentation;
    TMap<FGuid, float> Replaced;
    /** 取消先封口：已经移出数组、尚待回调的同施法者记录也会看到新版本，不能再发布阈值。 */
    TMap<TWeakObjectPtr<AActor>, uint64> CancellationVersions;
    int32 NextVisualId = 1;
    bool bShuttingDown = false;
};
