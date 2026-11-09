// 接口链：出生/池唤醒 → GAS 敌人生命周期 → 死亡共享掉落与回收；身份由 GetLootRank 解析。

#pragma once

#include "CoreMinimal.h"
#include "test_25d/test_25d.h"
#include "Character/BaseCharacter.h"
#include "Components/WidgetComponent.h"
#include "Interface/HighLightInterface.h"
#include "Interface/IEnemyInterface.h"
#include "UI/WidgetController/OverlayWidgetController.h"
#include "Equipment/SharedLootSubsystem.h"

#include "EnemyCharacter.generated.h"

class UBehaviorTree;
class AMyAIController;
/**
 * 
 */
UCLASS()
class TEST_25D_API AEnemyCharacter : public ABaseCharacter, public IEnemyInterface, public IHighLightInterface
{
    GENERATED_BODY()
public:
    AEnemyCharacter();

    /** Highlight Interface */
    virtual void HighLightActor_Implementation() override;
    virtual void UnHighLightActor_Implementation() override;
    virtual void SetMoveToLocation_Implementation(FVector& OutDestination) override;
    //
    virtual void InitAbilityActorInfo() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    // Floor-owned pooling is opt-in; ordinary enemies retain their original death path.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dungeon Pool")
    void EnableDungeonPooling(float CorpseSeconds = 1.5f);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dungeon Pool")
    void SleepForDungeonPool(bool bKeepCorpse = false);
    bool CanFinishPoolReturn() const;
    bool HasPreparedPoolReturn() const
    {
        return bPoolResetDone;
    }
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dungeon Pool")
    bool WakeFromDungeonPool(const FTransform& Transform, FName Room);
    bool IsDungeonPoolSleeping() const
    {
        return DungeonPoolLife < 0;
    }
    /** Read-only diagnostic: zero = unmanaged, positive = live lease, negative = retired lease. */
    UFUNCTION(BlueprintPure, Category = "Dungeon Pool")
    int32 GetDungeonPoolLease() const
    {
        return DungeonPoolLife;
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Monsters")
    FName MonsterRoomId;

    // Runtime Actor Tags do not replicate. Clients need encounter membership for
    // target acquisition; the server still validates the active room and damage.
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dungeon")
    bool bDungeonEncounter = false;

    // APawn ownership changes to its controller on possession. Keep summon provenance separate.
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Summon")
    TWeakObjectPtr<AEnemyCharacter> SummoningSource;
    virtual void Die(const FVector& DeathImpulse) override;
    virtual void MulticastHandleDeath_Implementation(const FVector& DeathImpulse) override;
    virtual void PossessedBy(AController* NewController) override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Character Class Defaults")
    int32 Level = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Loot")
    EEnemyLootRank LootRank = EEnemyLootRank::Normal;
    /** 所有模式按共享类表和本次生命覆盖值解析掉落身份。 */
    UFUNCTION(BlueprintPure, Category = "Loot")
    EEnemyLootRank GetLootRank() const;
    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    bool bHitReacting = false;

    virtual void StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float LifeSpan = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget")
    TObjectPtr<UWidgetComponent> HealthBar;

    UPROPERTY(BlueprintAssignable)
    FOnAttributeChangedSignature OnHealthChanged;

    UPROPERTY(BlueprintAssignable)
    FOnAttributeChangedSignature OnMaxHealthChanged;

    //combat interface
    virtual int32 GetPlayerLevel_Implementation();

    virtual void InitlizeDefaultAttributes() const override;

    void HitReactTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

    UPROPERTY(BlueprintReadWrite, Category = "Combat")
    TObjectPtr<AActor> CombatTarget;

    /* IEnemyInterface敌人接口 */
    virtual AActor* GetCombatTarget_Implementation() const override;
    virtual void SetCombatTarget_Implementation(AActor* InCombatTarget) override;
    /* IEnemyInterface敌人接口 结束 */

    //ai

    UPROPERTY(EditAnywhere, Category = "AI")
    TObjectPtr<UBehaviorTree> BehaviorTree;

    UPROPERTY()
    TObjectPtr<AMyAIController> MyAIController;

    void SetLevel(int32 InLevel)
    {
        Level = InLevel;
    }

    UFUNCTION(BlueprintImplementableEvent)
    void SpawnLoot();

protected:
    virtual void GrantEnemyAbilities();
    // Immediately invalidate attack presentation; full GAS reset keeps its existing safe timing.
    virtual void InvalidateEnemyAttack()
    {
    }
    virtual void ReconcileEnemyPresentation()
    {
    }
    virtual bool DetachWeaponOnDeath() const override
    {
        return false;
    }

private:
    UFUNCTION()
    void OnRep_DungeonPoolLife();
    UPROPERTY(ReplicatedUsing = OnRep_DungeonPoolLife)
    int32 DungeonPoolLife = 0;
    UPROPERTY(ReplicatedUsing = OnRep_DungeonPoolLife)
    bool bDungeonPoolCorpse = false;
    UPROPERTY(Replicated)
    FVector DungeonPoolDeathImpulse = FVector::ZeroVector;
    FTransform PoolMeshTransform;
    FCollisionResponseContainer PoolMeshResponses;
    ECollisionEnabled::Type PoolMeshCollision = ECollisionEnabled::QueryOnly;
    ECollisionEnabled::Type PoolCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
    bool bPoolRagdollApplied = false;
    float PoolCorpseSeconds = 1.5f;
    double PoolCorpseUntil = 0;
    TMap<TWeakObjectPtr<UActorComponent>, bool> PoolComponentTicks;
    TMap<FGameplayAttribute, float> PoolAttributeBases;
    bool bPoolTickCaptured = false;
    bool bPoolActorTick = false;
    bool bPoolActorCollision = true;
    bool bPoolResetDone = false;
};
