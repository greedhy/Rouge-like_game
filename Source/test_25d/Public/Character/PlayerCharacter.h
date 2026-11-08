// 主要链路：服务器占有角色后绑定玩家状态中的能力系统并恢复进度；客户端在玩家状态复制到达后绑定界面，存档从同一能力与属性通路导出。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/BaseCharacter.h"
#include "Interface/PlayerInterface.h"
#include "PlayerCharacter.generated.h"

class USpringArmComponent;

class UCameraComponent;
class UNiagaraComponent;
class UBackpackComponent;
class ULoadScreenSaveGame;

UCLASS()
class TEST_25D_API APlayerCharacter : public ABaseCharacter, public IPlayerInterface
{
    GENERATED_BODY()
public:
    // 创建俯视相机和升级特效，并配置平面移动。
    APlayerCharacter();
    // 在蓝图默认值应用后启用胶囊重叠，使交互范围独立于外观物理资产。
    virtual void PostInitializeComponents() override;

    // 清理延迟死亡计时器后执行父类结束流程。
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    // 服务器占有后恢复能力与进度、对账装备效果，再恢复世界状态。
    virtual void PossessedBy(AController* NewController) override;
    // 客户端收到玩家状态后补建能力系统与界面绑定。
    virtual void OnRep_PlayerState() override;

    //player interface
    // 累加经验并通过玩家状态通知订阅者。
    virtual void AddToXP_Implementation(int32 InXP) override;
    // 触发全端升级粒子表现。
    virtual void LevelUp_Implementation() override;
    // 读取累计经验。
    virtual int32 GetXP_Implementation() const override;
    // 通过等级数据表将经验转换为等级。
    virtual int32 FindLevelForXP_Implementation(int32 InXP) const override;
    // 查表取得指定等级的属性点奖励。
    virtual int32 GetAttributePointsReward_Implementation(int32 Level) const override;
    // 查表取得指定等级的技能点奖励。
    virtual int32 GetSpellPointsReward_Implementation(int32 Level) const override;
    // 累加玩家等级，并刷新当前等级可学习的能力状态。
    virtual void AddToPlayerLevel_Implementation(int32 InPlayerLevel) override;
    // 累加属性点并通知订阅者。
    virtual void AddToAttributePoints_Implementation(int32 InAttributePoints) override;
    // 累加技能点并通知订阅者。
    virtual void AddToSpellPoints_Implementation(int32 InSpellPoints) override;
    // 读取可用属性点。
    virtual int32 GetAttributePoints_Implementation() const override;
    // 读取可用技能点。
    virtual int32 GetSpellPoints_Implementation() const override;
    // 创建或显示技能范围指示器，使用可选贴花材质。
    virtual void ShowMagicCircle_Implementation(UMaterialInterface* DecalMaterial) override;
    // 移除技能范围指示器。
    virtual void HideMagicCircle_Implementation() override;
    // 仅服务器写入检查点进度，并交给游戏模式持久化。
    virtual void SaveProgress_Implementation(const FName& CheckpointTag) override;
    // 保存基础属性与可保存技能；排除装备授予能力，保留装备接管前的技能槽位。
    void WriteProgressToSaveData(ULoadScreenSaveGame* SaveData, const FName& CheckpointTag);

    UFUNCTION(BlueprintPure, Category = "Backpack")
    // 取得玩家状态拥有的背包组件。
    UBackpackComponent* GetBackpackComponent() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UNiagaraComponent> LevelUpNiagaraComponent;

    // combat interface
    // 读取玩家状态等级。
    virtual int32 GetPlayerLevel_Implementation();
    // 执行基础死亡表现，延迟通知游戏模式并分离相机。
    virtual void Die(const FVector& DeathImpulse) override;

    UPROPERTY(EditDefaultsOnly)
    float DeathTime = 5.f;

    FTimerHandle DeathTimer;
    // Overrides of replicated notify functions from ABaseCharacter - do not redeclare UFUNCTION() here
    // 同步眩晕输入封锁标签与特效，并调用基类刷新速度。
    virtual void OnRep_Stunned() override;

    // 依据复制燃烧状态启停燃烧特效。
    virtual void OnRep_Burned() override;
    // 优先读取旅行进度，否则读取存档；恢复技能与基础属性，再对账装备并恢复旅行资源。
    void LoadProgress();

protected:
    // 以玩家状态为所有者、角色为化身绑定能力系统，刷新装备外观并初始化界面。
    virtual void InitAbilityActorInfo() override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USpringArmComponent> CameraBoom;

    UFUNCTION(NetMulticast, Reliable)
    // 令升级粒子朝向摄像机后重新激活。
    void MulticastLevelUpParticles() const;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> TopDownCameraComponent;
};
