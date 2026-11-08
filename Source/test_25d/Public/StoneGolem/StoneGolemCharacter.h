#pragma once
#include "Character/TelegraphedEnemyCharacter.h"
#include "StoneGolemCharacter.generated.h"
UCLASS()
class TEST_25D_API AStoneGolemCharacter : public ATelegraphedEnemyCharacter
{
    GENERATED_BODY()
public:
    AStoneGolemCharacter();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StoneGolem")
    TObjectPtr<UStoneGolemConfig> MonsterConfig;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StoneGolem")
    TObjectPtr<UStaticMeshComponent> Hammer;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StoneGolem")
    TObjectPtr<UStaticMeshComponent> Shield;
    virtual UMonsterCombatConfig* GetCombatConfig() const override
    {
        return MonsterConfig;
    }

protected:
    virtual void ApplyConfig() override;
};
