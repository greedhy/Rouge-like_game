// 玩家输入入口：鼠标扫掠产生混合目标，按键转发能力系统；实体承伤回调将数字批量发送给所属客户端显示。
#pragma once

#include "CoreMinimal.h"
#include "Player/MyPlayerController.h"
#include "PureMassPlayerController.generated.h"

struct FInputActionValue;

/** Cosmetic damage payload, independent of entity lifetime and replication snapshots. */
USTRUCT()
struct FPureMassDamageNumber
{
    GENERATED_BODY()
    UPROPERTY()
    int32 EntityId = INDEX_NONE;
    UPROPERTY()
    FVector_NetQuantize Position = FVector::ZeroVector;
    UPROPERTY()
    float Damage = 0.f;
    UPROPERTY()
    bool bBlocked = false;
    UPROPERTY()
    bool bCritical = false;
};

/** New controller for copied Mass maps; no Actor proxy is needed for cursor targeting. */
UCLASS()
class TEST_25D_API APureMassPlayerController : public AMyPlayerController
{
    GENERATED_BODY()
public:
    // 加载实体伤害数字所需的默认组件蓝图。
    APureMassPlayerController();
    // 本地控制器每帧刷新鼠标目标。
    virtual void PlayerTick(float DeltaTime) override;
    // 结束生命周期时清理本对象拥有的资源，再调用父类收尾。
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    // 服务端按实体、格挡与暴击合并伤害数字并启动批量发送计时器。
    void QueueMassDamageNumber(int32 EntityId, FVector Position, float Damage, bool bBlocked, bool bCritical);
    UPROPERTY(EditDefaultsOnly, Category = "Pure Mass|Feedback")
    TSubclassOf<UDamageTextComponent> MassDamageTextComponentClass;
    UPROPERTY(EditDefaultsOnly, Category = "Pure Mass|Input")
    TObjectPtr<UMyInputConfig> MassInputConfig;
    UPROPERTY(EditDefaultsOnly, Category = "Pure Mass|Input")
    TObjectPtr<UInputAction> MassMoveAction;
    UPROPERTY(EditDefaultsOnly, Category = "Pure Mass|Input")
    TObjectPtr<UInputAction> MassShiftAction;
    // 读取当前鼠标命中的实体标识。
    UFUNCTION(BlueprintPure, Category = "Pure Mass")
    int32 GetHoveredEntity() const
    {
        return HoveredEntity;
    }
    // 读取当前鼠标命中的有效角色。
    UFUNCTION(BlueprintPure, Category = "Pure Mass")
    AActor* GetHoveredCombatActor() const
    {
        return HoveredActor.Get();
    }

protected:
    // 绑定移动动作以及技能按下、持续和松开输入。
    virtual void SetupInputComponent() override;

private:
    friend class FPlayerBasicInputTest;
    // 向所属客户端发送积累的伤害数字后清空批次。
    void FlushMassDamageNumbers();
    // 向所属客户端批量传输伤害表现数据。
    UFUNCTION(Client, Unreliable)
    void ClientShowMassDamageNumbers(const TArray<FPureMassDamageNumber>& Numbers);
    TArray<FPureMassDamageNumber> PendingDamageNumbers;
    FTimerHandle DamageNumberTimer;
    // 从当前受控角色取得项目能力系统。
    UMyAbilitySystemComponent* MassASC() const;
    // 把二维移动输入转换为控制器朝向下的世界移动。
    void MoveMass(const FInputActionValue& Value);
    // 优先处理背包与拾取，再把技能按下事件交给能力系统。
    void Pressed(FGameplayTag Tag);
    // 始终转发松开事件，保证引导技能能在界面打开后结束。
    void Released(FGameplayTag Tag);
    // 未被阻止时向能力系统转发持续按住事件。
    void Held(FGameplayTag Tag);
    // 将鼠标反投影为射线，经统一扫掠更新角色与实体悬停目标。
    void UpdateCursor();
    // 结合背包界面状态与能力标签判定输入是否被阻止。
    bool IsBlocked(FGameplayTag Tag) const;
    int32 HoveredEntity = 0;
    TWeakObjectPtr<AActor> HoveredActor;
};
