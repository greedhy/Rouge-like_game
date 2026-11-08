#pragma once
#include "CoreMinimal.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "GameplayEffect.h"
#include "GruxEnemyCharacter.generated.h"

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
    FActiveGameplayEffectHandle ApplyRoarArmor(float Armor, float Duration);
    void RemoveRoarArmor(FActiveGameplayEffectHandle Expected);
    UFUNCTION(BlueprintPure, Category = "Grux")
    bool HasRoarArmor() const;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

protected:
    virtual void InvalidateEnemyAttack() override;

private:
    FActiveGameplayEffectHandle RoarArmor;
};
