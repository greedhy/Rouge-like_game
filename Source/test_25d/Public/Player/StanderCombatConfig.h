#pragma once
#include "Player/SwordShieldConfig.h"
#include "StanderCombatConfig.generated.h"

UCLASS(BlueprintType)
class TEST_25D_API UStanderCombatConfig : public USwordShieldConfig
{
 GENERATED_BODY()
public:
 // Local impulse: X follows locked attack direction, Y is right, Z is up. Death only.
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FVector> ComboDeathImpulses={FVector(900,0,250),FVector(1000,0,300),FVector(1200,0,450)};
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector CounterDeathImpulse=FVector(1100,0,300);
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector SpinDeathImpulse=FVector(1000,0,350);
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector DashDeathImpulse=FVector(1400,0,250);
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector LiftDeathImpulse=FVector(450,0,1600);
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector ShieldBashDeathImpulse=FVector(1600,0,200);
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FSwordStrike Lift;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FSwordStrike ShieldBash;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float LiftMana=18;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float LiftCooldown=6;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float ShieldBashMana=10;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float ShieldBashCooldown=4;
 static FVector WorldDeathImpulse(FVector LocalImpulse,FVector Forward);
 bool ValidateDeathImpulses() const;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FSwordStrike Counter;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FSwordStrike Dash;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float DashDistance=500;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float DashStart=.25f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float DashEnd=.65f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float DashCooldown=4;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float DashMana=12;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UNiagaraSystem> SwingEffect;
 bool ValidateStander() const;
 float DashFraction(float MontagePosition) const;
};
