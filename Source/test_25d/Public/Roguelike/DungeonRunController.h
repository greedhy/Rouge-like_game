// 主要链路：地下城控制器读取局内导演状态以控制输入和界面；房间交互携带局标识交给服务器导演校验，退出通过房间子系统执行。
#pragma once
#include "CoreMinimal.h"
#include "Roguelike/DungeonGenerationController.h"
#include "Roguelike/DungeonRunTypes.h"
#include "DungeonRunController.generated.h"

class UDungeonRunWidget;
class UAbilitySystemComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;

UCLASS()
class TEST_25D_API ADungeonRunController : public ADungeonGenerationController
{
    GENERATED_BODY()
public:
    // 刷新交互映射与局流程行动门禁，并为本地玩家确保局内界面可见。
    virtual void PlayerTick(float DeltaTime) override;
    // 回收交互绑定、映射、界面和本控制器贡献的移动及能力门禁。
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    // 注册本类需要复制的状态，同时保留父类复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FDungeonPlayerRunView RunView;
    // 为房间交互附带当前局标识并发送服务器请求。
    UFUNCTION(BlueprintCallable)
    void RequestRoomInteraction(FName RoomId, EDungeonInteraction Action, int32 OfferIndex = -1);
    // 将交互交给局内导演进行权威校验与执行。
    UFUNCTION(Server, Reliable)
    void ServerRoomInteraction(FGuid RunId, FName RoomId, EDungeonInteraction Action, int32 OfferIndex);
    // 通知所属客户端退出地下城房间。
    UFUNCTION(Client, Reliable)
    void ClientReturnFromDungeon();
    // 根据当前房间角色发送宝箱、休息或胜利出口交互。
    UFUNCTION(BlueprintCallable)
    void InteractNearest();
    // 仅在胜利、结束或失败阶段允许本地离开房间。
    UFUNCTION(BlueprintCallable)
    void ReturnToMenu();

protected:
    // 保留父类输入绑定，为交互键创建动作、映射和可回收绑定。
    virtual void SetupInputComponent() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UDungeonRunWidget> RunWidget;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> InteractionAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> InteractionContext;
    TWeakObjectPtr<UEnhancedInputComponent> InteractionInput;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InteractionSubsystem;
    uint32 InteractionBindingHandle = 0;
    TWeakObjectPtr<UAbilitySystemComponent> BlockedASC;
    bool bHoldingRunMovement = false;
    // 迁移局流程持有的输入标签贡献，解除时只回收自身添加的计数。
    void UpdateActionBlock(bool bBlocked);
    // 本地玩家就绪或变化时迁移交互映射，以高于基础映射的优先级注册。
    void UpdateInteractionMapping();
};
