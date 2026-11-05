#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SwordShieldConfig.generated.h"
class UAnimMontage;
class UNiagaraSystem;

USTRUCT(BlueprintType)
struct FSwordStrike
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UAnimMontage> Montage;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float Damage=25;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float Radius=160;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float Angle=100;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float Contact=.22f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float PlayRate=1;
 bool Validate() const;
 bool Contains(FVector Origin,FVector Forward,FVector Target) const;
};
UCLASS(BlueprintType)
class TEST_25D_API USwordShieldConfig : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FSwordStrike> Combo;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FSwordStrike Spin;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UAnimMontage> GuardMontage;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float ComboReset=.9f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float GuardDuration=1;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float GuardCooldown=3;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float SpinCooldown=5;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float SpinMana=15;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UNiagaraSystem> HitEffect;
 bool Validate() const;
 static int32 ResolveComboIndex(int32 Next,double LastEnd,double Now,float Reset);
};
