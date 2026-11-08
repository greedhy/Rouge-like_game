#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "MyArcaneNovaCue.generated.h"

class UNiagaraSystem;
class USoundBase;

/** One replicated cue produces the centre and ring locally on each observing client. */
UCLASS()
class TEST_25D_API UMyArcaneNovaCue : public UGameplayCueNotify_Static
{
    GENERATED_BODY()

public:
    virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Arcane Nova")
    TObjectPtr<UNiagaraSystem> BurstSystem;

    UPROPERTY(EditDefaultsOnly, Category = "Arcane Nova")
    TObjectPtr<USoundBase> BurstSound;
};
