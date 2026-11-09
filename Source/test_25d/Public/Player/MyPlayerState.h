/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 所有权与入口：构造函数CreateDefaultSubobject创建ASC、AttributeSet、Backpack和Equipment；组件随PlayerState存活，不随一次攻击创建。
 * 服务器PossessedBy初始化角色ActorInfo；客户端OnRep_PlayerState补建ActorInfo，Owner为PlayerState，Avatar为当前Pawn。
 * 同步：ASC启用Mixed效果复制；不能假设所有客户端都能遍历完整Active GE。成长字段通过DOREPLIFETIME复制，OnRep广播本地UI事件；服务器修改入口也负责本地通知。
 * 旅行：CopyProperties将背包装备快照和成长数据交给目标PlayerState，OverrideWith恢复旧状态；转移的是业务数据，不是旧世界中的GE句柄/组件指针。
 */
// 主要链路：玩家状态持有跨角色使用的能力系统、属性、背包和装备；属性变更广播驱动界面，联合快照用于旅行与恢复。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "Backpack/BackpackPersistence.h"

#include "Game/PlayerProfession.h"
#include "MyPlayerState.generated.h"

class ULevelUpInfo;
// Forward declarations
class UAbilitySystemComponent;
class UAttributeSet;
class UBackpackComponent;
class UEquipmentComponent;
class UBackpackItemDefinitions;
class ULoadScreenSaveGame;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlayerStatChanged, int32 /*StatValue*/)
    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLevelChanged, int32 /*StatValue*/, bool /*bLevelUp*/)

    /**
 * 
 */
    UCLASS()
class TEST_25D_API AMyPlayerState : public APlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    // Set on the server at login and carried across seamless travel.
    EPlayerProfession Profession = EPlayerProfession::Unselected;
    // 创建可复制能力系统、属性集、背包与装备组件。
    AMyPlayerState();
    // 注册本类需要复制的状态，同时保留父类复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    // 返回当前角色或玩家状态持有的能力系统。
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    // 客户端玩家状态初始化时建立背包本地状态。
    virtual void ClientInitialize(AController* C) override;
    // 返回共享属性集合。
    UAttributeSet* GetAttributeSet() const
    {
        return AttributeSet;
    }
    // 取得玩家状态拥有的背包组件。
    UBackpackComponent* GetBackpackComponent() const
    {
        return BackpackComponent;
    }
    UFUNCTION(BlueprintPure, Category = "Equipment")
    // 返回玩家状态拥有的装备组件。
    UEquipmentComponent* GetEquipmentComponent() const
    {
        return EquipmentComponent;
    }

    UPROPERTY(EditDefaultsOnly)
    TObjectPtr<ULevelUpInfo> LevelUpInfo;

    UPROPERTY(EditDefaultsOnly, Category = "Backpack")
    TObjectPtr<UBackpackItemDefinitions> BackpackItemDefinitions;

    FOnPlayerStatChanged OnXPChangedDelegate;
    FOnLevelChanged OnLevelChangedDelegate;
    FOnPlayerStatChanged OnAttributePointsChangedDelegate;
    FOnPlayerStatChanged OnSpellPointsChangedDelegate;

    // 读取玩家状态等级。
    FORCEINLINE int32 GetPlayerLevel() const
    {
        return Level;
    }
    // 读取累计经验。
    FORCEINLINE int32 GetXP() const
    {
        return XP;
    }
    // 读取可用属性点。
    FORCEINLINE int32 GetAttributePoints() const
    {
        return AttributePoints;
    }
    // 读取可用技能点。
    FORCEINLINE int32 GetSpellPoints() const
    {
        return SpellPoints;
    }

    // 累加经验并通过玩家状态通知订阅者。
    void AddToXP(int32 InXP);
    // 累加等级并广播升级通知。
    void AddToLevel(int32 InLevel);
    // 累加属性点并通知订阅者。
    void AddToAttributePoints(int32 InPoints);
    // 累加技能点并通知订阅者。
    void AddToSpellPoints(int32 InPoints);

    // 覆盖经验值并通知订阅者。
    void SetXP(int32 InXP);
    // 覆盖等级并广播非升级式刷新。
    void SetLevel(int32 InLevel);

    // 覆盖属性点并广播变化。
    void SetAttributePoints(int32 InPoints);
    // 覆盖技能点并广播变化。
    void SetSpellPoints(int32 InPoints);

    UFUNCTION(BlueprintPure, Category = "Backpack|Persistence")
    // 一并导出背包、装备、版本与物品实例词条，保持恢复数据对应。
    FBackpackSnapshot ExportBackpackSnapshot() const;
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Backpack|Persistence")
    // 服务器先校验容量、装备、实例唯一性与随机词条，再交给装备组件联合恢复。
    bool ImportBackpackSnapshot(const FBackpackSnapshot& Snapshot);
    // 记录背包持久化使用的玩家标识。
    void SetBackpackPersistencePlayerId(const FString& InPlayerId);
    // 读取背包持久化玩家标识。
    const FString& GetBackpackPersistencePlayerId() const
    {
        return BackpackPersistencePlayerId;
    }
    // 向新玩家状态转移背包装备快照、旅行资源与成长数据。
    virtual void CopyProperties(APlayerState* PlayerState) override;
    // 从旧玩家状态恢复持久化标识和背包装备联合快照。
    virtual void OverrideWith(APlayerState* PlayerState) override;
    // 读取旅行玩家槽位。
    int32 GetTravelPlayerSlot() const
    {
        return TravelPlayerSlot;
    }
    // 设置旅行玩家槽位。
    void SetTravelPlayerSlot(int32 Slot)
    {
        TravelPlayerSlot = Slot;
    }
    // 暂存跨地图进度及当前生命、法力。
    void SetTravelProgress(ULoadScreenSaveGame* Progress, float Health, float Mana);
    // 读取尚未消费的旅行进度。
    ULoadScreenSaveGame* GetTravelProgress() const
    {
        return TravelProgress;
    }
    // 读取旅行时保存的生命值。
    float GetTravelHealth() const
    {
        return TravelHealth;
    }
    // 读取旅行时保存的法力值。
    float GetTravelMana() const
    {
        return TravelMana;
    }
    // 清除已消费的旅行进度引用。
    void ClearTravelProgress()
    {
        TravelProgress = nullptr;
    }

protected:
    // 设置物品目录，服务器初始化背包。
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

    UPROPERTY()
    TObjectPtr<UAttributeSet> AttributeSet;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBackpackComponent> BackpackComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
    TObjectPtr<UEquipmentComponent> EquipmentComponent;

    UPROPERTY()
    FString BackpackPersistencePlayerId;

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Travel", meta = (AllowPrivateAccess = "true"))
    int32 TravelPlayerSlot = INDEX_NONE;
    UPROPERTY(Transient)
    TObjectPtr<ULoadScreenSaveGame> TravelProgress;
    UPROPERTY(Transient)
    float TravelHealth = 0;
    UPROPERTY(Transient)
    float TravelMana = 0;

    UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_Level)
    int32 Level = 1;

    UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_XP)
    int32 XP = 0;

    UFUNCTION()
    // 复制等级到达后通知本地界面。
    void OnRep_Level(int32 OldLevel);

    UFUNCTION()
    // 复制经验到达后通知本地界面。
    void OnRep_XP(int32 OldXP);

    UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_AttributePoints)
    int32 AttributePoints = 0;

    UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_SpellPoints)
    int32 SpellPoints = 0;

    UFUNCTION()
    // 复制属性点到达后通知本地界面。
    void OnRep_AttributePoints(int32 OldAttributePoints);

    UFUNCTION()
    // 复制技能点到达后通知本地界面。
    void OnRep_SpellPoints(int32 OldSpellPoints);
};
