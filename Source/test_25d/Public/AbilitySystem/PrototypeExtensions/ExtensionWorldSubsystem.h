/**
 * 持久效果宿主：UWorld 按 Game/PIE 生命周期创建与销毁；Runtime BeginPlay -> AddStack/RequestDisplacement。世界 Tick 管理叠层/Actor 位移，Mass 处理器 -> MoveMass 管理实体位移。子系统自身不复制，叠层显示通过 Presentation Actor 复制，Actor 位移经 CharacterMovement；Deinitialize 清理根运动源及表现。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "PureMass/PureMassCombatTarget.h"
#include "ExtensionWorldSubsystem.generated.h"

USTRUCT()
struct FExtensionStackState
{
    GENERATED_BODY()
    UPROPERTY()
    TObjectPtr<UExtensionCastContext> Context;
    UPROPERTY()
    TObjectPtr<UExtensionNode> Node;
    FPureMassCombatTarget Target;
    FVector Position, Direction;
    int32 Depth = 0, Layers = 0, VisualId = 0;
    float Remaining = 0;
    bool bPending = false;
};

USTRUCT()
struct FExtensionMoveState
{
    GENERATED_BODY()
    UPROPERTY()
    TObjectPtr<UExtensionCastContext> Context;
    UPROPERTY()
    TObjectPtr<UExtensionNode> Node;
    FPureMassCombatTarget Target;
    FGuid Token;
    FVector Destination;
    float Remaining = 0;
    uint16 RootMotionId = 0;
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
    bool AddStack(UExtensionCastContext* Context, UExtensionNode* Node, FPureMassCombatTarget Target, FVector Position,
                  FVector Direction, int32 Depth, bool bConfirmedHit = false);
    FGuid RequestDisplacement(UExtensionCastContext* Context, UExtensionNode* Node, FPureMassCombatTarget Target,
                              FVector Center, FVector Direction);
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
    static bool CasterAlive(const UPrototypeCastContext* Context);
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
    int32 NextVisualId = 1;
};
