#pragma once

// 调用链：Grux GA → 护甲 GE 或攻击阶段 → 复制通知 → 本地战斗表现组件。
#include "CoreMinimal.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "GameplayEffect.h"
#include "GruxEnemyCharacter.generated.h"

class UCombatPresentationComponent;

UCLASS()
class TEST_25D_API UGruxRoarArmorEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGruxRoarArmorEffect();
};

UCLASS()
class TEST_25D_API AGruxEnemyCharacter : public ATelegraphedEnemyCharacter
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grux|Presentation")
    TSubclassOf<UCombatPresentationComponent> CombatPresentationClass;

    virtual void PostInitializeComponents() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

    FActiveGameplayEffectHandle ApplyRoarArmor(float Armor, float Duration);
    void RemoveRoarArmor(FActiveGameplayEffectHandle Expected);
    UFUNCTION(BlueprintPure, Category = "Grux")
    bool HasRoarArmor() const;

protected:
    virtual void InvalidateEnemyAttack() override;
    virtual void ReconcileEnemyPresentation() override;
    virtual bool PlayAttackPresentation(const FStoneGolemPresentation& State) override;
    virtual void StopAttackPresentation() override;

private:
    void OnRoarArmorRemoved(const FGameplayEffectRemovalInfo& Info, FActiveGameplayEffectHandle Expected);
    void SetRoarPresentationLease(int32 Lease);
    UFUNCTION()
    void OnRep_RoarPresentation();

    UPROPERTY(Transient)
    TObjectPtr<UCombatPresentationComponent> CombatPresentation;

    UPROPERTY(ReplicatedUsing = OnRep_RoarPresentation)
    int32 RoarPresentationLease = INDEX_NONE;

    FActiveGameplayEffectHandle RoarArmor;
};
