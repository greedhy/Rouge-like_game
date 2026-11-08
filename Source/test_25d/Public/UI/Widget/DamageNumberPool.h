/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 调用链：伤害反馈RPC到本地控制器 -> QueueActor合并或Show直接显示 -> UDamageTextComponent播放表现 -> DestroyComponent覆盖入口 -> Release复用。
 * 作用域与同步：UWorldSubsystem，每个世界一份，只为本地控制器显示；专用服务器不创建飘字表现。本池不复制，伤害值先由外部反馈通路送达，再由本地池显示，不能反向参与伤害结算。
 * 创建与持有：Show首次为玩家Spawn临时Anchor Actor，以Anchor为Outer NewObject文字组件并RegisterComponent；UPROPERTY数组保持组件与Anchor引用。待显示请求弱引用Player/Target，不延长战斗目标寿命。
 * 复用与退出：Release隐藏组件、停动画和Tick但保留对象；下次Show重置蓝图标量及渲染状态。Deinitialize先置bClosing并清定时器/队列，再解除组件回指、DestroyComponent、销毁Anchor并清空引用，避免销毁被再次转成入池。
 */
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DamageNumberPool.generated.h"
class UDamageTextComponent;
class APlayerController;
USTRUCT()
struct FPendingDamageNumber
{
    GENERATED_BODY()
    TWeakObjectPtr<APlayerController> Player;
    TWeakObjectPtr<AActor> Target;
    UPROPERTY()
    TSubclassOf<UDamageTextComponent> Class;
    FVector Position = FVector::ZeroVector;
    float Damage = 0;
    bool Blocked = false, Critical = false;
};
UCLASS()
class TEST_25D_API UDamageNumberPool : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    void QueueActor(APlayerController* Player, TSubclassOf<UDamageTextComponent> Class, AActor* Target, float Damage,
                    bool Blocked, bool Critical);
    void Show(APlayerController* Player, TSubclassOf<UDamageTextComponent> Class, FVector Position, float Damage,
              bool Blocked, bool Critical);
    void Release(UDamageTextComponent* Component);
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure)
    FString GetReport() const;
    UFUNCTION(BlueprintCallable)
    void ResetCounters();

private:
    void FlushActorNumbers();
    UPROPERTY(Transient)
    TArray<FPendingDamageNumber> Pending;
    FTimerHandle FlushTimer;
    int64 ActorInputs = 0, Merged = 0, PendingDropped = 0;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UDamageTextComponent>> Components;
    UPROPERTY(Transient)
    TArray<TObjectPtr<AActor>> Anchors;
    int64 Requests = 0, Created = 0, Reused = 0, Released = 0, Dropped = 0;
    double ShowMs = 0, CreateMs = 0, RegisterMs = 0;
    bool bClosing = false;
};
