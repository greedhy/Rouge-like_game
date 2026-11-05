#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffect.h"
#include "PureMass/PureMassCombatTarget.h"
#include "Batch6Slow.generated.h"
UCLASS()
class TEST_25D_API UBatch6SlowEffect : public UGameplayEffect
{ GENERATED_BODY() public: UBatch6SlowEffect(); };
UCLASS()
class TEST_25D_API UBatch6SlowComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UBatch6SlowComponent();
 static bool Apply(UWorld* World,AActor* Caster,const FPureMassCombatTarget& Target,float Multiplier,float Duration);
 static float GetMultiplier(const AActor* Actor);
 virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 struct FSlow { float Multiplier; double Expires; };
 TArray<FSlow> Entries;
 int64 Lease = 0;
 FActiveGameplayEffectHandle Marker;
 void Clear();
};
