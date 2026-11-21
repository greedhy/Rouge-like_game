// 调用链：共享近战扣血结果/敌人死亡 → BloodFX RPC → 世界血迹；复制流血状态 → 定时独立滴血。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/BloodFXSubsystem.h"
#include "BloodFXComponent.generated.h"

USTRUCT()
struct FBloodBleedingState
{
    GENERATED_BODY()
    UPROPERTY()
    bool bEnabled = false;
    UPROPERTY()
    int32 Life = 0;
};

/** 每个角色的视觉配置、生命去重和复制入口。实际伤害仍由 GAS 执行。 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class TEST_25D_API UBloodFXComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBloodFXComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blood")
    FBloodFXStyle Style;

    // Authority 接收已确认的扣血结果。Life 使用攻击前捕获的目标租约。
    void NotifyHit(FGuid AttackId, int32 Life, FVector Location, FVector Direction, bool bHeavy = false);
    // 原死亡入口每次生命调用，普通角色使用 0 租约。
    void NotifyDeath(int32 Life, FVector Location, FVector Direction, bool bLarge);
    // 每次新池生命恢复本地去重状态，旧 RPC 和旧滴血状态失败关闭。
    void ResetForLife(int32 Life);
    void StopForDeath();
    // 可供蓝图/未来 GE 适配调用，只设置视觉，最后一次关闭停止后续滴血。
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat|Blood")
    void SetBleeding(bool bEnabled);
    UFUNCTION(BlueprintPure, Category = "Combat|Blood")
    bool IsBleeding() const;
    UFUNCTION(BlueprintPure, Category = "Combat|Blood")
    FString GetBloodFXReport() const;
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    void BeginPlay() override;
    void EndPlay(EEndPlayReason::Type Reason) override;

private:
    friend class FBloodLifeTest;
    friend class FBloodStrikeTest;
    friend class FBloodWakeOrderTest;
    friend class FBloodBaseDeathTest;
    bool AcceptHit(FGuid AttackId, int32 Life);
    bool AcceptDeath(int32 Life);
    bool IsCurrentLife(int32 Life) const;
    int32 OwnerLife() const;
    float OwnerRadius() const;
    void RefreshBleeding();
    void Drip();
    UFUNCTION()
    void OnRep_Bleeding();
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastHit(FGuid AttackId, int32 Life, FVector_NetQuantize10 Location, FVector_NetQuantizeNormal Direction,
                      bool bHeavy);
    UFUNCTION(NetMulticast, Reliable)
    void MulticastDeath(int32 Life, FVector_NetQuantize10 Location, FVector_NetQuantizeNormal Direction, bool bLarge);
    UPROPERTY(ReplicatedUsing = OnRep_Bleeding)
    FBloodBleedingState Bleeding;
    TArray<FGuid> RecentHits;
    TOptional<int32> DeathLife;
    int32 LatestLife = 0;
    int32 AcceptedHits = 0;
    int32 AcceptedDeaths = 0;
    int32 DripSamples = 0;
    FTimerHandle DripTimer;
};
