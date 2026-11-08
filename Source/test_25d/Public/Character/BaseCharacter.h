// 主要链路：角色基类统一战斗接口、地形状态和死亡表现；派生角色负责绑定能力系统，伤害通知与死亡广播供外部系统订阅。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Interface/CombatInterface.h"
#include "AbilitySystemComponent.h"
#include "NiagaraSystem.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"

#include "BaseCharacter.generated.h"

class UPassiveNiagaraComponent;
class UTerrainStatusComponent;
class UDebuffNiagaraComponent;
class UAbilitySystemComponent;
class UAttributeSet;
class UAnimMontage;
UCLASS(Abstract)
class TEST_25D_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface, public ICombatInterface
{
    GENERATED_BODY()

public:
    // Sets default values for this character's properties
    // 初始化碰撞、武器挂点、地形与状态特效组件。
    ABaseCharacter();
    // 返回当前角色或玩家状态持有的能力系统。
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    // 返回共享属性集合。
    UAttributeSet* GetAttributeSet() const
    {
        return AttributeSet;
    }
    // 每帧将特效挂点保持世界零旋转，避免随角色转向。
    virtual void Tick(float DeltaTime) override;
    // 调用父类伤害入口并广播实际返回伤害。
    virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator,
                             AActor* DamageCauser) override;

    //combat interface
    // 返回死亡状态，地下城倒地也映射到该状态。
    virtual bool IsDead_Implementation() const override;
    // 将当前角色作为战斗化身返回。
    virtual AActor* GetAvatar_Implementation() override;
    // 按武器、左右手或尾部标签取得攻击起点，未知标签返回零向量。
    virtual FVector GetCombatSocketLocation_Implementation(const FGameplayTag& MontageTag) override;
    // 清理地形效果并按策略分离武器，随后广播死亡表现。
    virtual void Die(const FVector& DeathImpulse) override;
    // 返回受击动画配置。
    virtual UAnimMontage* GetHitReactMontage_Implementation() override;
    // 返回受击血液特效配置。
    virtual UNiagaraSystem* GetBloodEffect_Implementation() override; //获取攻击特效
    // 按精确标签查找攻击动画，未命中返回空配置。
    virtual FTaggedMontage GetTaggedMontageByTag_Implementation(const FGameplayTag& MontageTag) override;
    // 读取当前召唤物计数。
    virtual int32 GetMinionCount_Implementation() override;
    // 累加召唤物计数。
    virtual void IncreaseMinionCount_Implementation(int32 Amount) override;
    // 返回职业配置。
    virtual ECharacterClass GetCharacterClass_Implementation() override;
    // 返回武器网格组件。
    virtual USkeletalMeshComponent* GetWeapon_Implementation() override;
    // 更新电击表现状态。
    virtual void SetIsBeingShocked_Implementation(bool bInShock) override;
    // 读取电击表现状态。
    virtual bool IsBeingShocked_Implementation() const override;

    // 提供能力系统注册完成的订阅入口。
    virtual FOnASCRegistered& GetOnASCRegisteredDelegate() override;
    // 提供角色死亡事件订阅入口。
    virtual FOnDeathSignature& GetOnDeathDelegate() override;
    // 提供伤害事件订阅入口。
    virtual FOnDamageSignature& GetOnDamageSignature() override;

    //

    FOnASCRegistered OnASCRegistered;
    FOnDeathSignature FOnDeath;
    FOnDamageSignature FOnDamage;
    UFUNCTION(NetMulticast, Reliable)
    // 同步死亡音效、布娃娃与溶解表现，最后广播死亡。
    virtual void MulticastHandleDeath(const FVector& DeathImpulse);

    UPROPERTY(EditAnywhere, Category = "Combat")
    TArray<FTaggedMontage> AttackMontage;

    // 返回全部攻击动画配置。
    virtual TArray<FTaggedMontage> GetAttackMontages_Implementation() override;

    UPROPERTY(ReplicatedUsing = OnRep_Stunned, BlueprintReadOnly)
    bool bIsStunned = false;

    UPROPERTY(ReplicatedUsing = OnRep_Burned, BlueprintReadOnly)
    bool bIsBurned = false;

    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bIsBeingShocked = false;

    UFUNCTION()
    // 复制眩晕状态后刷新移动速度。
    virtual void OnRep_Stunned();

    UFUNCTION()
    // 保留燃烧复制回调供派生类实现表现。
    virtual void OnRep_Burned();

    // 注册本类需要复制的状态，同时保留父类复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    float BaseWalkSpeed = 600.f;

    UFUNCTION(BlueprintCallable, Category = "Movement")
    // 合并死亡、眩晕、受击、藤蔓定身与减速；定身时立即停止移动。
    void RefreshMovementSpeed();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
    TObjectPtr<UTerrainStatusComponent> TerrainStatus;

    // 将眩晕标签计数转换为状态并重新计算移动速度。
    virtual void StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
    // 设置职业配置。
    void SetCharacterClass(ECharacterClass InClass)
    {
        CharacterClass = InClass;
    }

protected:
    // 提供死亡时是否分离武器的派生类策略。
    virtual bool DetachWeaponOnDeath() const
    {
        return true;
    }
    // Called when the game starts or when spawned
    // 进入游戏时调用父类初始化。
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    TObjectPtr<USkeletalMeshComponent> Weapon;

    UPROPERTY(EditAnywhere, Category = "Combat")
    FName CombatTipSocketName;

    UPROPERTY(EditAnywhere, Category = "Combat")
    FName LeftHandSocketName;

    UPROPERTY(EditAnywhere, Category = "Combat")
    FName RightHandSocketName;
    UPROPERTY(EditAnywhere, Category = "Combat")
    FName TailSocketName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    USoundBase* DeathSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    UNiagaraSystem* BloodEffect;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bDead = false;

    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

    UPROPERTY()
    TObjectPtr<UAttributeSet> AttributeSet;
    // 预留派生角色绑定能力系统的入口。
    virtual void InitAbilityActorInfo();

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Attributes")
    TSubclassOf<UGameplayEffect> DefaultPrimaryAttributes;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Attributes")
    TSubclassOf<UGameplayEffect> DefaultSecondaryAttributes;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Attributes")
    TSubclassOf<UGameplayEffect> DefaultVitalAttributes;

    // 使用自身作为来源创建效果规格，并施加到自己的能力系统。
    void ApplyEffectToSelf(TSubclassOf<UGameplayEffect> GameplayEffectClass, float Level) const;
    // 依次应用主属性、次属性和生命资源初始效果。
    virtual void InitlizeDefaultAttributes() const;
    // 仅服务器授予启动主动与被动能力。
    void AddCharacterAbilities();

    //dissolve effects
    // 为角色和武器创建动态材质，再交由蓝图时间线推进溶解。
    void Dissolve();

    UFUNCTION(BlueprintImplementableEvent)
    // 由蓝图驱动角色动态材质溶解时间线。
    void StartDissolveTimeline(UMaterialInstanceDynamic* DynamicMeterialInstance);

    UFUNCTION(BlueprintImplementableEvent)
    // 由蓝图驱动武器动态材质溶解时间线。
    void StartWeaponDissolveTimeline(UMaterialInstanceDynamic* DynamicMeterialInstance);

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UMaterialInstance> DissolveMeterialInstance;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UMaterialInstance> WeaponDissolveMeterialInstance;

    int32 MinionCount = 0; //被召唤物数量

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Class Defaults")
    ECharacterClass CharacterClass = ECharacterClass::Warrior;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UDebuffNiagaraComponent> BurnDebuffComponent;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UDebuffNiagaraComponent> StunDebuffComponent;

private:
    UPROPERTY(EditAnywhere, Category = "Abilities")
    TArray<TSubclassOf<UGameplayAbility>> StartupAbility;

    UPROPERTY(EditAnywhere, Category = "Abilities")
    TArray<TSubclassOf<UGameplayAbility>> StartupPassiveAbility;

    UPROPERTY(EditAnywhere, Category = "Combat")
    TObjectPtr<UAnimMontage> HitReactMontage;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPassiveNiagaraComponent> HaloOfProtectionNiagaraComponent;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPassiveNiagaraComponent> LifeSiphonNiagaraComponent;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPassiveNiagaraComponent> ManaSiphonNiagaraComponent;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> EffectAttachComponent;
};
