// 主要调用链：GA或战斗运行时 → 组装效果参数与自定义Context → 应用伤害GE → 属性集结算；UI通过库解析PlayerState和WidgetController。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "UI/HUD/MyHUD.h"
#include "UI/WidgetController/MyWidgetController.h"
#include "MyAbilitySystemLibrary.generated.h"

struct FDamageEffectParams;
class USpellMenuWidgetController;
class UAttributeMenuWidgetController;
enum class ECharacterClass : uint8;
class UAbilitySystemComponent;
class UOverlayWidgetController;
class ULootTiers;
class ULoadScreenSaveGame;

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyAbilitySystemLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /*
	 * Widget Controller
	 */

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|WidgetController",
              meta = (DefaultToSelf = "WorldContextObject"))
    // 获取本地首位玩家 HUD 与 PlayerState，汇总 ASC 和属性集依赖。
    static bool MakeWidgetControllerParams(const UObject* WorldContextObject, FWidgetControllerParams& OutWCParams,
                                           AMyHUD*& OutMyHUD);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|WidgetController",
              meta = (DefaultToSelf = "WorldContextObject"))
    static UOverlayWidgetController* GetOverlayWidgetController(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|WidgetController",
              meta = (DefaultToSelf = "WorldContextObject"))
    static UAttributeMenuWidgetController* GetAttributeMenuWidgetController(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|WidgetController",
              meta = (DefaultToSelf = "WorldContextObject"))
    static USpellMenuWidgetController* GetSpellMenuWidgetController(const UObject* WorldContextObject);

    /*
	 * Ability System Class Defaults
	 */

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|CharacterClassDefaults")
    // 按职业配置依次施加主属性、次属性和生命法力 GE。
    static void InitializeDefaultAttributes(const UObject* WorldContextObject, ECharacterClass CharacterClass,
                                            float Level, UAbilitySystemComponent* ASC);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|CharacterClassDefaults")
    static void InitializeDefaultAttributesFromSaveData(const UObject* WorldContextObject, UAbilitySystemComponent* ASC,
                                                        ULoadScreenSaveGame* SaveGame);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|CharacterClassDefaults")
    // 授予通用一级技能和按角色等级配置的职业技能。
    static void GiveStartupAbilities(const UObject* WorldContextObject, UAbilitySystemComponent* ASC,
                                     ECharacterClass CharacterClass);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|CharacterClassDefaults")
    static UCharacterClassInfo* GetCharacterClassInfo(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|CharacterClassDefaults")
    static UAbilityInfo* GetAbilityInfo(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|CharacterClassDefaults",
              meta = (DefaultToSelf = "WorldContextObject"))
    static ULootTiers* GetLootTiers(const UObject* WorldContextObject);

    /*
	 * Effect Context Getters
	 */
    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static bool IsCriticalHit(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static bool IsBlockedHit(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static bool IsSuccessfulDebuff(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static float GetDebuffDamage(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static float GetDebuffDuration(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static float GetDebuffFrequency(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static FGameplayTag GetDamageType(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static FVector GetDeathImpulse(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static FVector GetKnockbackForce(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static bool IsRadialDamage(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static float GetRadialDamageInnerRadius(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static float GetRadialDamageOuterRadius(const FGameplayEffectContextHandle& EffectContextHandle);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static FVector GetRadialDamageOrigin(const FGameplayEffectContextHandle& EffectContextHandle);

    /*
	 * Effect Context Setters
	 */

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetIsBlockedHit(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, bool bInIsBlockedHit);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetIsCriticalHit(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, bool bInIsCriticalHit);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetIsSuccessfulDebuff(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle,
                                      bool bInSuccessfulDebuff);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetDebuffDamage(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, float InDamage);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetDebuffDuration(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, float InDuration);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetDebuffFrequency(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, float InFrequency);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetDamageType(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle,
                              const FGameplayTag& InDamageType);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetDeathImpulse(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle,
                                const FVector& InImpulse);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetKnockbackForce(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle,
                                  const FVector& InForce);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetIsRadialDamage(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle,
                                  bool bInIsRadialDamage);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetRadialDamageInnerRadius(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle,
                                           float InInnerRadius);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetRadialDamageOuterRadius(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle,
                                           float InOuterRadius);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayEffects")
    static void SetRadialDamageOrigin(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle,
                                      const FVector& InOrigin);

    /*
	 * Gameplay Mechanics
	 */

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayMechanics")
    // 球形检索动态对象，去重收集实现战斗接口的存活 Actor。
    static void GetLivePlayersWithinRadius(const UObject* WorldContextObject, TArray<AActor*>& OutOverlappingActors,
                                           const TArray<AActor*>& ActorsToIgnore, float Radius,
                                           const FVector& SphereOrigin);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|GameplayMechanics")
    static void GetClosestTargets(int32 MaxTargets, const TArray<AActor*>& Actors, TArray<AActor*>& OutClosestTargets,
                                  const FVector& Origin);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayMechanics")
    // 依据首个 Actor 的 Player 或 Enemy 标签判断另一方是否异阵营。
    static bool IsNotFriend(AActor* FirstActor, AActor* SecondActor);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|DamageEffect")
    // 校验战斗许可，填写上下文与 SetByCaller 数据后向目标施加伤害 GE。
    static FGameplayEffectContextHandle ApplyDamageEffect(const FDamageEffectParams& DamageEffectParams);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayMechanics")
    static TArray<FRotator> EvenlySpacedRotators(const FVector& Forward, const FVector& Axis, float Spread,
                                                 int32 NumRotators);

    UFUNCTION(BlueprintPure, Category = "MyAbilitySystemLibrary|GameplayMechanics")
    static TArray<FVector> EvenlyRotatedVectors(const FVector& Forward, const FVector& Axis, float Spread,
                                                int32 NumVectors);

    static int32 GetXPRewardForClassAndLevel(const UObject* WorldContextObject, ECharacterClass CharacterClass,
                                             int32 CharacterLevel);

    /*
	 * Damage Effect Params
	 */

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|DamageEffect")
    static void SetIsRadialDamageEffectParam(UPARAM(ref) FDamageEffectParams& DamageEffectParams, bool bIsRadial,
                                             float InnerRadius, float OuterRadius, FVector Origin);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|DamageEffect")
    static void SetKnockbackDirection(UPARAM(ref) FDamageEffectParams& DamageEffectParams, FVector KnockbackDirection,
                                      float Magnitude = 0.f);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|DamageEffect")
    static void SetDeathImpulseDirection(UPARAM(ref) FDamageEffectParams& DamageEffectParams, FVector ImpulseDirection,
                                         float Magnitude = 0.f);

    UFUNCTION(BlueprintCallable, Category = "MyAbilitySystemLibrary|DamageEffect")
    static void SetTargetEffectParamsASC(UPARAM(ref) FDamageEffectParams& DamageEffectParams,
                                         UAbilitySystemComponent* InASC);
};
