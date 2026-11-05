#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyAbilityTypes.h"
#include "Player/StanderWeaponActionConfig.h"
#include "StanderWeaponProjectile.generated.h"
class AStanderPlayer;
class UNiagaraComponent;
class ADungeonRunDirector;
UCLASS()
class TEST_25D_API AStanderWeaponProjectile : public AActor
{
 GENERATED_BODY()
public:
 AStanderWeaponProjectile();
 void Initialize(AStanderPlayer* Source,const FStanderWeaponActionEvent& Event,const FDamageEffectParams& Params,FVector Direction);
 virtual void Tick(float Delta) override;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
 TWeakObjectPtr<AStanderPlayer> Caster;
 UPROPERTY() FDamageEffectParams DamageParams;
 UPROPERTY() FStanderWeaponActionEvent Action;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> FlightVisual;
 UPROPERTY(ReplicatedUsing=OnRep_FlightEffect) TObjectPtr<UNiagaraSystem> FlightEffect;
 UFUNCTION() void OnRep_FlightEffect();
 FVector Forward=FVector::ForwardVector;
 uint32 WeaponGeneration=0;
 TWeakObjectPtr<ADungeonRunDirector> OriginDirector;
 FGuid DungeonGeneration;
 FGuid RunId;
 int32 OriginRoom=INDEX_NONE;
 bool bHasDungeon=false;
 bool AllowsLocation(const FVector& Location) const;
};
