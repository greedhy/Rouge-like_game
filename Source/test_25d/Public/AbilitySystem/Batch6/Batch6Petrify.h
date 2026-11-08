/**
 * 石化链：服务器 GA ActivateAbility/Commit -> 混合范围查询 -> Mass ApplyPetrify 或 Enemy 组件 Apply。Actor 用 Infinite Stun GE 控制，组件复制 bPetrified/材质/Lease，OnRep_State 管理本地动画暂停与材质。Tick/GE 移除回调/EndPlay -> Clear；缓存原材质只在匹配当前覆盖时恢复，避免覆盖其他系统修改。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "GameplayEffectTypes.h"
#include "Batch6Petrify.generated.h"
class UMaterialInterface;
struct FActiveGameplayEffect;
UCLASS()
class TEST_25D_API UBatch6PetrifyEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 构造 Infinite 控制 GE，实际 Stun Tag 在 Apply 的独立 Spec 中加入。 */
    UBatch6PetrifyEffect();
};
UCLASS(ClassGroup = (Abilities), meta = (BlueprintSpawnableComponent))
class TEST_25D_API UBatch6PetrifyComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 构造默认复制组件；初始不 Tick，应用状态后由 OnRep_State 开启。 */
    UBatch6PetrifyComponent();
    /** 服务器验证 Enemy/ASC/生命代次；首次施加控制 GE 并监听移除，取消能力和移动；延长到期时间，更新材质与复制。 */
    bool Apply(float Duration, UMaterialInterface* Material);
    /** 检查死亡/Lease 和服务器到期；服务器 Clear，客户端只重新协调表现，不能自行结算控制。 */
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    /** 组件退出时 Clear 控制与视觉覆盖，随后交父类清理。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 复制材质、Lease 和 bPetrified；原材质缓存/到期时间/GE 句柄不作为组件字段复制。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    /** 要求非负且相等的池生命代次，避免上次生命的石化作用于复用后的敌人。 */
    static bool IsLeaseValid(int32 Expected, int32 Current)
    {
        return Expected >= 0 && Expected == Current;
    }

private:
    UPROPERTY(ReplicatedUsing = OnRep_State)
    bool bPetrified = false;
    UPROPERTY(ReplicatedUsing = OnRep_State)
    TObjectPtr<UMaterialInterface> StoneMaterial;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> AppliedMaterial;
    UFUNCTION()
    /** 服务器主动调用与客户端复制回调：验证 Lease 后缓存/替换材质并暂停动画；解除时恢复本组件仍持有的覆盖。 */
    void OnRep_State();
    /** 先作废句柄并解绑委托，再移除权威 GE；清到期/石化标记，恢复本地表现并请求网络更新。 */
    void Clear();
    /** ASC GE 移除委托：只处理 ControlHandle，先作废句柄再 Clear，避免重复移除递归。 */
    void OnControlRemoved(const FActiveGameplayEffect& Effect);
    bool bVisualApplied = false;
    bool bWasPaused = false;
    UPROPERTY(ReplicatedUsing = OnRep_State)
    int32 Lease = 0;
    double Expires = 0;
    FActiveGameplayEffectHandle ControlHandle;
};
UCLASS()
class TEST_25D_API UBatch6PetrifyAbility : public UPrototypeAbility
{
    GENERATED_BODY()
public:
    /** 构造 ServerOnly、每 Actor 实例化 GA，控制效果由服务器执行。 */
    UBatch6PetrifyAbility();
    /** UI 使用当前半径、持续时间与 Prototype 的费用/冷却生成说明。 */
    virtual FString GetDescription(int32 Level) override;
    /** CommitAbility 回调，经稳定原生扣蓝 GE 扣除 Prototype.ManaCost。 */
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayAbilityActivationInfo Activation) const override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Petrify")
    float Radius = 650;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Petrify")
    float Duration = 3;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Petrify")
    TObjectPtr<UMaterialInterface> StoneMaterial;
    /** 服务器提交施法后查询可见范围目标：Mass 调 ApplyPetrify，Actor 按需注册组件并 Apply；完成后结束 GA，组件继续计时。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 const FGameplayAbilityActivationInfo Activation,
                                 const FGameplayEventData* Data) override;
};
