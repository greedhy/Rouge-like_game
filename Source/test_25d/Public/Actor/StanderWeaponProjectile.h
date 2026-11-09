/**
 * 创建链：服务器StanderWeaponActionAbility::Execute生成弹体 -> Initialize保存伤害/方向与来源代次 -> 服务器Tick扫掠命中 -> 伤害结算或Destroy。
 * 同步：弹体Actor复制且启用移动复制；FlightEffect属性复制后OnRep_FlightEffect更新Niagara组件。服务器Initialize也直接调用OnRep以服务本地表现，不依赖服务器自动收到复制回调。
 * 生命周期：FlightVisual是弹体默认子组件，随Actor销毁，循环飞行特效不能作为无主世界特效单独生成。SetLifeSpan限制最大寿命；施法者、武器代次、RunId、地图Generation和房间失效时中止，防止换装或旅行后的旧弹体伤害。
 * 正常施法结束允许已发射弹体继续飞行；技能取消才主动销毁该次技能记录的弹体。Owner/弱引用及代次校验不能相互替代：引用有效不代表仍属于同一次武器/房间状态。
 */
// 主要调用链：动作 GA 的 Execute 生成 Actor -> Initialize -> 服务器 Tick -> PureMassCombat 扫掠 -> Actor GAS 或 Mass 伤害；OnRep_FlightEffect 同步表现。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyAbilityTypes.h"
#include "Player/StanderWeaponActionConfig.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "StanderWeaponProjectile.generated.h"
class AStanderPlayer;
class UNiagaraComponent;
class ADungeonRunDirector;
UCLASS()
class TEST_25D_API AStanderWeaponProjectile : public AActor
{
    GENERATED_BODY()
public:
    // 开启网络移动复制和 Tick，创建飞行特效组件。
    AStanderWeaponProjectile();
    // 服务器校验发射参数，记录施法者、武器代次和地牢归属，设置寿命与飞行特效。
    void Initialize(AStanderPlayer* Source, const FStanderWeaponActionEvent& Event, const FDamageEffectParams& Params,
                    FVector Direction);
    /** 共享动作在Initialize前写入原ASC/Avatar生命，普通结束不取消飞行，重绑定使旧弹体失效。 */
    void SetExecutionSourceToken(const FAbilitySourceToken& Source)
    {
        ExecutionSource = Source;
    }
    // 服务器验证来源仍有效，扫掠本帧路径并处理目标、墙体及房间边界命中。
    virtual void Tick(float Delta) override;
    // 注册飞行特效资源的网络复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

private:
    TOptional<FAbilitySourceToken> ExecutionSource;
    TWeakObjectPtr<AStanderPlayer> Caster;
    UPROPERTY()
    FDamageEffectParams DamageParams;
    UPROPERTY()
    FStanderWeaponActionEvent Action;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UNiagaraComponent> FlightVisual;
    UPROPERTY(ReplicatedUsing = OnRep_FlightEffect)
    TObjectPtr<UNiagaraSystem> FlightEffect;
    // 飞行特效资源复制到客户端后，更新Niagara组件并激活表现。
    UFUNCTION()
    void OnRep_FlightEffect();
    FVector Forward = FVector::ForwardVector;
    uint32 WeaponGeneration = 0;
    TWeakObjectPtr<ADungeonRunDirector> OriginDirector;
    FGuid DungeonGeneration;
    FGuid RunId;
    int32 OriginRoom = INDEX_NONE;
    bool bHasDungeon = false;
    // 检查位置仍属于生成时的地牢运行、地图代次和房间。
    bool AllowsLocation(const FVector& Location) const;
};
