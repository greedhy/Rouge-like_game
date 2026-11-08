// 主要调用链：公共预警 GA → 召唤配置快照 → 接触窗口调用角色 Summon；施法与爆发直接复用公共范围攻击。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Ability/TelegraphedEnemyAbility.h"
#include "ShamanEnemyAbility.generated.h"
class UShamanSummonConfig;

UCLASS()
class TEST_25D_API UShamanCastAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UShamanCastAbility();
};
UCLASS()
class TEST_25D_API UShamanBurstAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UShamanBurstAbility();
};
UCLASS()
class TEST_25D_API UShamanSummonAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UShamanSummonAbility();
    // 复制本次召唤配置，避免预警期间配置变化影响结果。
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    // 验证召唤配置与现存随从数量，再检查公共攻击条件。
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* Source = nullptr,
                                    const FGameplayTagContainer* Target = nullptr,
                                    FGameplayTagContainer* Relevant = nullptr) const override;

protected:
    // 确认攻击序号和租约有效后显示命中，并让角色使用快照召唤随从。
    virtual void ResolveHit() override;
    UPROPERTY(Transient)
    TObjectPtr<UShamanSummonConfig> SummonSnapshot;
};
