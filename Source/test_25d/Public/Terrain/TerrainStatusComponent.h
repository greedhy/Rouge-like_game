// 主要调用链：地块 RefreshContact → UpdateEffects → GE 与表现快照；TickComponent 淘汰过期接触；复制回调刷新本地表现；死亡/EndPlay 清理。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "Terrain/TerrainSurfaceConfig.h"
#include "TerrainStatusComponent.generated.h"

class ATerrainSurface;
class UAbilitySystemComponent;
class UNiagaraComponent;

USTRUCT()
struct FTerrainStatusPresentation
{
    GENERATED_BODY()
    UPROPERTY()
    uint8 ActiveMask = 0;
    UPROPERTY()
    float MovementMultiplier = 1.f;
    UPROPERTY()
    TArray<TObjectPtr<UNiagaraSystem>> Systems;
};

/** One status owner per creature: merges neighbouring tiles and owns GE cleanup. */
UCLASS(ClassGroup = (Terrain), meta = (BlueprintSpawnableComponent))
class TEST_25D_API UTerrainStatusComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // 启用组件复制及低频 Tick，并为四种地形状态分配表现槽位。
    UTerrainStatusComponent();
    // 死亡时清理；服务器推进接触、束缚和恢复计时并重算效果。
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* TickFunction) override;
    // 注册本类需要发送给客户端的复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    // 结束生命周期时释放本类持有的运行期资源，再调用父类清理。
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    // 在服务器登记或续期地块接触，立即重新汇总状态效果。
    void RefreshContact(ATerrainSurface* Surface);
    // 清空接触与计时，移除本组件持有的 GE 并恢复默认表现。
    UFUNCTION(BlueprintCallable, Category = "Terrain")
    void ClearTerrainEffects();
    // 读取表现位掩码，查询指定地形状态是否处于激活状态。
    UFUNCTION(BlueprintPure, Category = "Terrain")
    bool IsTerrainStatusActive(ETerrainSurfaceType Type) const;
    // 返回当前地形汇总后的移动速度倍率。
    UFUNCTION(BlueprintPure, Category = "Terrain")
    float GetTerrainMovementMultiplier() const
    {
        return Presentation.MovementMultiplier;
    }

private:
    struct FContact
    {
        TWeakObjectPtr<ATerrainSurface> Surface;
        TWeakObjectPtr<UTerrainSurfaceConfig> Config;
        float Remaining = 0.f;
    };
    TArray<FContact> Contacts;
    FActiveGameplayEffectHandle EffectHandles[4];
    TWeakObjectPtr<UAbilitySystemComponent> EffectASC;
    float RootRemaining = 0.f;
    float RecoveryRemaining = 0.f;
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraSystem> RootSystem;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UNiagaraComponent>> VisualComponents;
    UPROPERTY(ReplicatedUsing = OnRep_Presentation)
    FTerrainStatusPresentation Presentation;

    // 接收服务器表现快照后刷新速度及 Niagara 状态。
    UFUNCTION()
    void OnRep_Presentation();
    // 按类型选出最强接触，维护四类 GE 及束缚恢复窗口，再同步表现快照。
    void UpdateEffects();
    // 刷新角色速度，并按状态位创建、更新或停止脚底 Niagara 组件。
    void UpdatePresentation();
};
