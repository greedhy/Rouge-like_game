#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DamageNumberPool.generated.h"
class UDamageTextComponent;
class APlayerController;
USTRUCT()
struct FPendingDamageNumber
{
 GENERATED_BODY()
 TWeakObjectPtr<APlayerController> Player;
 TWeakObjectPtr<AActor> Target;
 UPROPERTY() TSubclassOf<UDamageTextComponent> Class;
 FVector Position=FVector::ZeroVector;
 float Damage=0;
 bool Blocked=false,Critical=false;
};
UCLASS()
class TEST_25D_API UDamageNumberPool : public UWorldSubsystem
{
 GENERATED_BODY()
public:
 void QueueActor(APlayerController* Player,TSubclassOf<UDamageTextComponent> Class,AActor* Target,float Damage,bool Blocked,bool Critical);
 void Show(APlayerController* Player,TSubclassOf<UDamageTextComponent> Class,FVector Position,float Damage,bool Blocked,bool Critical);
 void Release(UDamageTextComponent* Component);
 virtual void Deinitialize() override;
 UFUNCTION(BlueprintPure) FString GetReport() const;
 UFUNCTION(BlueprintCallable) void ResetCounters();
private:
 void FlushActorNumbers();
 UPROPERTY(Transient) TArray<FPendingDamageNumber> Pending;
 FTimerHandle FlushTimer;
 int64 ActorInputs=0,Merged=0,PendingDropped=0;
 UPROPERTY(Transient) TArray<TObjectPtr<UDamageTextComponent>> Components;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Anchors;
 int64 Requests=0,Created=0,Reused=0,Released=0,Dropped=0;
 double ShowMs=0,CreateMs=0,RegisterMs=0;
 bool bClosing=false;
};
