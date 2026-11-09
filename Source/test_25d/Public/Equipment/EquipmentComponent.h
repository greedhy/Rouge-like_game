/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 主链路：UI RequestEquip/RequestUnequip读取实例ID及背包/装备Revision -> 所属组件可靠Server RPC -> TryEquip/TryUnequip服务器复核 -> 准备GE/能力 -> 联合提交 -> ClientResult及状态复制。
 * 同步时机：服务器RPC完成后ForceNetUpdate请求尽快更新；Slots/Revision/bNeedsReconcile为普通复制属性，没有OwnerOnly条件。实际接收者仍受宿主PlayerState的网络相关性约束。
 * ClientResult仅返回本次成功与否，不携带完整装备状态；不可认为收到成功回执时背包与装备复制已经同时到达。客户端OnRep_State刷新外观和UI，服务器提交也显式调用它服务监听服务器本地显示。
 * 生命周期：由PlayerState构造并持有；运行GE句柄和武器能力句柄仅在服务器维护。Avatar/ASC变化时ReconcileEffects重建归属，EndPlay推进WeaponGeneration、撤销本组件武器能力并清理装备GE；不清空整个ASC。
 */
// 调用链：界面请求 → 服务器装备或卸装 → 背包实例校验、效果及武器能力准备 → 联合提交 → 复制与持久化通知。
#pragma once
#include "CoreMinimal.h"
#include "Equipment/EquipmentRollTypes.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayAbilitySpec.h"
#include "EquipmentComponent.generated.h"

class AStanderPlayer;

class UAbilitySystemComponent;
class UBackpackComponent;
struct FReplicatedInventorySlot;
struct FActiveGameplayEffect;
struct FBackpackItemDefEntry;

USTRUCT(BlueprintType)
struct FEquipmentSlot
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FGameplayTag SlotTag;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FPrimaryAssetId ItemDefinitionId;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FGuid ItemInstanceId;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FEquipmentRollData RollData;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEquipmentChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEquipmentRequestResult, bool, bSucceeded);

UCLASS(ClassGroup = (Equipment), meta = (BlueprintSpawnableComponent))
class TEST_25D_API UEquipmentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // 启用装备组件复制，由玩家状态持有装备业务状态。
    UEquipmentComponent();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FGameplayTagContainer AvailableSlots;
    UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly)
    TArray<FEquipmentSlot> Slots;
    UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly)
    int32 Revision = 0;
    UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly)
    bool bNeedsReconcile = false;
    UPROPERTY(BlueprintAssignable)
    FEquipmentChanged OnEquipmentChanged;
    UPROPERTY(BlueprintAssignable)
    FEquipmentRequestResult OnRequestResult;
    // 从本地背包读取实例及双版本并向服务器发起装备请求。
    UFUNCTION(BlueprintCallable, Category = "Equipment")
    void RequestEquip(int32 BackpackSlot, FGameplayTag SlotTag);
    // 从本地装备槽读取实例及双版本并向服务器发起卸装请求。
    UFUNCTION(BlueprintCallable, Category = "Equipment")
    void RequestUnequip(FGameplayTag SlotTag);
    // 接收带实例及版本的可靠服务器装备请求。
    UFUNCTION(BlueprintCallable, Server, Reliable)
    void ServerEquip(int32 BackpackSlot, FGameplayTag SlotTag, FGuid ExpectedInstance, int32 BackpackRevision,
                     int32 EquipmentRevision);
    // 接收带实例及版本的可靠服务器卸装请求。
    UFUNCTION(BlueprintCallable, Server, Reliable)
    void ServerUnequip(FGameplayTag SlotTag, FGuid ExpectedInstance, int32 BackpackRevision, int32 EquipmentRevision);
    // 将装备事务结果可靠回传给所属客户端。
    UFUNCTION(Client, Reliable)
    void ClientResult(bool bSucceeded);
    // 校验背包实例后准备武器能力和新效果，将旧装备返还原背包格并统一提交。
    bool TryEquip(int32 BackpackSlot, FGameplayTag SlotTag, FGuid ExpectedInstance, int32 BackpackRevision,
                  int32 EquipmentRevision);
    // 预留背包空格并准备武器切换，移除实例效果后返还原物品。
    bool TryUnequip(FGameplayTag SlotTag, FGuid ExpectedInstance, int32 BackpackRevision, int32 EquipmentRevision);
    /** Called only after actor info and base attribute initialization. Safe to repeat. */
    // 在角色初始化后补建缺失效果和武器能力，保留有效效果以避免重复加成。
    bool ReconcileEffects();
    // 判断效果初始化记录是否对应当前角色。
    bool IsReadyForCurrentAvatar() const;
    // 校验槽位、实例唯一性、装备效果及随机词条和武器配置。
    bool ValidateRecords(const TArray<FEquipmentSlot>& Records) const;
    // 校验背包与装备跨集合实例唯一性，联合恢复记录并按角色就绪状态重建效果。
    bool RestoreCombined(const TArray<FReplicatedInventorySlot>& Items, int32 Capacity, int32 BagRevision,
                         const TArray<FEquipmentSlot>& Records, int32 SavedRevision);
    // 返回当前装备记录对应的武器配置。
    UFUNCTION(BlueprintPure)
    class UWeaponSetConfig* GetWeaponSet() const;
    // 返回武器世代号，供调用方识别过期武器上下文。
    uint32 GetWeaponGeneration() const
    {
        return WeaponGeneration;
    }
    // 按授予句柄判断能力是否属于本组件武器组。
    bool IsWeaponAbility(FGameplayAbilitySpecHandle Handle) const;
    // 读取能力当前输入槽；被武器挤出时回退到保存的原槽位。
    FGameplayTag GetSavedInputSlot(const FGameplayAbilitySpec& Spec) const;
    // 约束武器能力可用输入槽，并保护当前武器配置占用的槽位。
    bool CanAssignWeaponSlot(FGameplayAbilitySpecHandle Handle, FGameplayTag Slot) const;
    // 定位玩家状态当前角色并应用装备武器外观。
    void RefreshWeaponVisuals();
    // 记录武器能力挤出的普通能力输入槽，供卸装后恢复。
    void RememberWeaponSlotDisplacement(FGameplayAbilitySpecHandle Incoming, const FGameplayAbilitySpec& Displaced);
    // 检查指定实例是否已占用装备槽。
    bool ContainsInstance(const FGuid& Id) const;
    // 登记组件业务状态的网络复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    // 注册时补齐标准装备槽，兼容旧蓝图默认配置。
    virtual void OnRegister() override;
    // 服务器结束组件生命周期时撤销武器能力及本组件装备效果。
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    // 装备复制更新后刷新武器外观和装备界面。
    UFUNCTION()
    void OnRep_State();
    // 验证服务器权限、双版本、事务重入及当前角色存活状态。
    bool CanMutate(int32 BackpackRevision, int32 EquipmentRevision) const;
    // 从所属玩家状态取得背包组件。
    UBackpackComponent* Backpack() const;
    // 从所属玩家状态取得共享能力系统组件。
    UAbilitySystemComponent* ASC() const;
    // 通过背包共用目录查询物品定义。
    const FBackpackItemDefEntry* Definition(const FPrimaryAssetId& Id) const;
    // 构造装备效果规格并注入词条总值，只接受实际生效且未受抑制的效果。
    FActiveGameplayEffectHandle Apply(const FBackpackItemDefEntry& Def, const FEquipmentRollData& Roll);
    // 按实例句柄检查当前能力系统上的装备效果是否仍有效。
    bool HasEffect(const FGuid& Id) const;
    // 统一提交装备版本、背包复制和资源修正，再通知界面及持久化监听者。
    void Commit();
    // 按最终生命和法力上限裁剪当前资源，不因上限增加补满。
    void ClampResources();
    // 解绑移除通知并按本组件保存的句柄清除效果。
    void ClearEffects();
    TWeakObjectPtr<UAbilitySystemComponent> EffectASC;
    TMap<FGuid, FActiveGameplayEffectHandle> Handles;
    bool bMutating = false;
    TWeakObjectPtr<UAbilitySystemComponent> WeaponASC;
    TArray<FGameplayAbilitySpecHandle> WeaponHandles;
    TMap<FGameplayAbilitySpecHandle, FGameplayTag> DisplacedSlots;
    TWeakObjectPtr<UWeaponSetConfig> AppliedWeaponSet;
    FGuid AppliedWeaponInstance;
    uint32 WeaponGeneration = 0;
    // 验证武器组唯一性、可取消性和能力标签，预授予新能力供事务提交。
    bool PrepareWeaponSet(const TArray<FEquipmentSlot>& Records, TArray<FGameplayAbilitySpecHandle>& Prepared);
    // 撤销旧武器能力、恢复被挤出能力，再为新武器分配输入槽并刷新外观。
    void CommitWeaponSet(const TArray<FEquipmentSlot>& Records, const TArray<FGameplayAbilitySpecHandle>& Prepared,
                         bool bIncludeDefault = true);
    // 撤销尚未提交的临时武器能力。
    void DiscardWeaponSet(const TArray<FGameplayAbilitySpecHandle>& Prepared);
    // 从装备记录中查找带武器能力组的定义。
    UWeaponSetConfig* WeaponSetFor(const TArray<FEquipmentSlot>& Records, bool bIncludeDefault = true) const;
    AStanderPlayer* FindStanderAvatar() const;

    FDelegateHandle RemovedDelegate;
    // 订阅装备效果移除事件，用于检测外部移除造成的状态缺失。
    void BindEffects();
    // 忽略事务内部移除；外部移除本组件效果时标记需要对账。
    void OnEffectRemoved(const FActiveGameplayEffect& Effect);
    TWeakObjectPtr<AActor> ReadyAvatar;
};
