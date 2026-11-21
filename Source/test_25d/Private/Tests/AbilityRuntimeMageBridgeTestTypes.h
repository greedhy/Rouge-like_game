// 调用链：自动化真实 ASC -> 生产 FireBolt Notify 桥；仅提供稳定身份和资产无关发射配置。
#pragma once
#include "AbilityTraditionalTestAbility.h"
#include "AbilityGraphTestAbility.h"
#include "AbilitySystem/Ability/MyArcaneShardAbility.h"
#include "AbilitySystem/Ability/MyBeamSpell.h"
#include "Actor/PointCollection.h"
#include "AbilityRuntimeMageBridgeTestTypes.generated.h"

UCLASS(Transient, NotBlueprintable)
class UAbilityRuntimeFireBoltBridgeTest : public UAbilityTraditionalFireBoltTest
{
    GENERATED_BODY()
public:
    UAbilityRuntimeFireBoltBridgeTest()
    {
        FGameplayTagContainer Identities;
        // 编辑器先构造测试CDO，之后AssetManager才注册原生标签；CDO不参与目录。
        // 实际测试授予时的新实例会重新解析，缺失身份仍由真实配方/激活测试拒绝。
        Identities.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Fire.FireBolt"), false));
        Identities.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Prototype.Burst")));
        SetAssetTags(Identities);
        CostGameplayEffectClass = UAbilityGraphTestCost::StaticClass();
    }
};

/** 仅补足可播放素材依赖，目标/按键/Notify/费用继续运行生产函数。 */
UCLASS(Transient, NotBlueprintable)
class UAbilityRuntimeShardBridgeTest : public UMyArcaneShardAbility
{
    GENERATED_BODY()
public:
    UAbilityRuntimeShardBridgeTest()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
        SetAssetTags(
            FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Arcane.ArcaneShards"), false)));
        ShardPointCollectionClass = APointCollection::StaticClass();
        CostGameplayEffectClass = UAbilityGraphTestCost::StaticClass();
    }
};

UCLASS(Transient, NotBlueprintable)
class UAbilityRuntimeBeamBridgeTest : public UMyBeamSpell
{
    GENERATED_BODY()
public:
    UAbilityRuntimeBeamBridgeTest()
    {
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
        SetAssetTags(
            FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Thunder.Electrocute"), false)));
    }
    void ConfigureMontage(UAnimMontage* Montage)
    {
        ElectrocuteMontage = Montage;
    }
};
