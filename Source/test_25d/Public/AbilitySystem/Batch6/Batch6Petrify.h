#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "GameplayEffectTypes.h"
#include "Batch6Petrify.generated.h"
class UMaterialInterface;
struct FActiveGameplayEffect;
UCLASS()
class TEST_25D_API UBatch6PetrifyEffect : public UGameplayEffect
{ GENERATED_BODY() public: UBatch6PetrifyEffect(); };
UCLASS(ClassGroup=(Abilities), meta=(BlueprintSpawnableComponent))
class TEST_25D_API UBatch6PetrifyComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UBatch6PetrifyComponent();
 bool Apply(float Duration, UMaterialInterface* Material);
 virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
 static bool IsLeaseValid(int32 Expected, int32 Current) { return Expected >= 0 && Expected == Current; }
private:
 UPROPERTY(ReplicatedUsing=OnRep_State) bool bPetrified = false;
 UPROPERTY(ReplicatedUsing=OnRep_State) TObjectPtr<UMaterialInterface> StoneMaterial;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> AppliedMaterial;
 UFUNCTION() void OnRep_State();
 void Clear();
 void OnControlRemoved(const FActiveGameplayEffect& Effect);
 bool bVisualApplied = false;
 bool bWasPaused = false;
 UPROPERTY(ReplicatedUsing=OnRep_State) int32 Lease = 0;
 double Expires = 0;
 FActiveGameplayEffectHandle ControlHandle;
};
UCLASS()
class TEST_25D_API UBatch6PetrifyAbility : public UPrototypeAbility
{
 GENERATED_BODY()
public:
 UBatch6PetrifyAbility();
 virtual FString GetDescription(int32 Level) override;
 virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation) const override;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Petrify") float Radius = 650;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Petrify") float Duration = 3;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Petrify") TObjectPtr<UMaterialInterface> StoneMaterial;
 virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, const FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Data) override;
};








