#pragma once
#include "AbilitySystem/Ability/TelegraphedEnemyAbility.h"
#include "StoneGolemAbility.generated.h"
UCLASS(Abstract)
class TEST_25D_API UStoneGolemAttackAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
};
UCLASS()
class TEST_25D_API UStoneGolemHeavyHammer : public UStoneGolemAttackAbility
{
    GENERATED_BODY()
public:
    UStoneGolemHeavyHammer();
};
UCLASS()
class TEST_25D_API UStoneGolemQuake : public UStoneGolemAttackAbility
{
    GENERATED_BODY()
public:
    UStoneGolemQuake();
};
UCLASS()
class TEST_25D_API UStoneGolemShieldBash : public UStoneGolemAttackAbility
{
    GENERATED_BODY()
public:
    UStoneGolemShieldBash();
};

UCLASS()
class TEST_25D_API UStoneGolemSelectAttack : public UTelegraphedSelectAttack
{
    GENERATED_BODY()
};
