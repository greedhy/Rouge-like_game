#pragma once
#include "CoreMinimal.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "ShamanEnemyCharacter.generated.h"

UCLASS(BlueprintType)
class TEST_25D_API UShamanSummonConfig : public UStoneGolemAttackConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Summon")
    TSubclassOf<AEnemyCharacter> MinionClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Summon", meta = (ClampMin = "1", ClampMax = "5"))
    int32 MaxAlive = 5;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Summon", meta = (ClampMin = "1", ClampMax = "5"))
    int32 PerCast = 5;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Summon")
    float MinDistance = 75;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Summon")
    float MaxDistance = 300;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Summon")
    float Spread = 90;
    bool ValidateSummon() const;
};

UCLASS()
class TEST_25D_API AShamanEnemyCharacter : public ATelegraphedEnemyCharacter
{
    GENERATED_BODY()
public:
    virtual void Die(const FVector& Impulse) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintPure, Category = "Summon")
    int32 GetOwnedMinionCount() const;
    int32 Summon(const UShamanSummonConfig* Config, int32 ExpectedLease);

protected:
    virtual void InvalidateEnemyAttack() override;

private:
    void ClearSummons();
    UFUNCTION()
    void OnMinionRemoved(AActor* Actor);
    TMap<TWeakObjectPtr<AEnemyCharacter>, int32> Minions;
};
