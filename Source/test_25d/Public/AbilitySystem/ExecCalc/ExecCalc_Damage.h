// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "ExecCalc_Damage.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UExecCalc_Damage : public UGameplayEffectExecutionCalculation
{
    GENERATED_BODY()
public:
    UExecCalc_Damage();
    virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
                                        FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
    void DetermineDebuff(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
                         const FGameplayEffectSpec& Spec, FAggregatorEvaluateParameters EvaluationParameters,
                         const TMap<FGameplayTag, FGameplayEffectAttributeCaptureDefinition>& InTagsToDefs) const;
};
