// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/MMC/MMC_MaxMana.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Interface/CombatInterface.h"
UMMC_MaxMana::UMMC_MaxMana()
{
    IntelligenceDef.AttributeToCapture = UMyAttributeSet::GetIntelligenceAttribute();
    IntelligenceDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
    IntelligenceDef.bSnapshot = false;
    RelevantAttributesToCapture.Add(IntelligenceDef);
}

float UMMC_MaxMana::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
    //gather tag from source and target
    const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
    const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();
    FAggregatorEvaluateParameters EvaluationParameters;
    EvaluationParameters.SourceTags = SourceTags;
    EvaluationParameters.TargetTags = TargetTags;

    float Intelligence = 0.f;
    GetCapturedAttributeMagnitude(IntelligenceDef, Spec, EvaluationParameters, Intelligence);
    Intelligence = FMath::Max(Intelligence, 0.f);

    int32 PlayerLevel = 1;

    if (Spec.GetContext().GetSourceObject()->Implements<UCombatInterface>())
    {
        PlayerLevel = ICombatInterface::Execute_GetPlayerLevel(Spec.GetContext().GetSourceObject());
    }

    return 50 + Intelligence * 10 + PlayerLevel * 10.f;
}
