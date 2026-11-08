// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/MMC/MMC_MaxHealth.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Interface/CombatInterface.h"
UMMC_MaxHealth::UMMC_MaxHealth()
{
    VigorDef.AttributeToCapture = UMyAttributeSet::GetVigorAttribute();
    VigorDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
    VigorDef.bSnapshot = false;
    RelevantAttributesToCapture.Add(VigorDef);
}

float UMMC_MaxHealth::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
    //gather tag from source and target
    const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
    const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();
    FAggregatorEvaluateParameters EvaluationParameters;
    EvaluationParameters.SourceTags = SourceTags;
    EvaluationParameters.TargetTags = TargetTags;

    float Vigor = 0.f;
    GetCapturedAttributeMagnitude(VigorDef, Spec, EvaluationParameters, Vigor);
    Vigor = FMath::Max(Vigor, 0.f);

    int32 PlayerLevel = 1;

    if (Spec.GetContext().GetSourceObject()->Implements<UCombatInterface>())
    {
        PlayerLevel = ICombatInterface::Execute_GetPlayerLevel(Spec.GetContext().GetSourceObject());
    }

    return 80.f + Vigor * 2.5f + PlayerLevel * 10.f;
}
