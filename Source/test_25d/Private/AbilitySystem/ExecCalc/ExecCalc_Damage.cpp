// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/ExecCalc/ExecCalc_Damage.h"
#include "AbilitySystemComponent.h"
#include "MyAbilityTypes.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Interface/CombatInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"

struct MyDamageStatics
{

    DECLARE_ATTRIBUTE_CAPTUREDEF(Armor);
    DECLARE_ATTRIBUTE_CAPTUREDEF(BlockChance);
    DECLARE_ATTRIBUTE_CAPTUREDEF(ArmorPene);
    DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalHitChance);

    DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalHitDamage);

    DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalHitResistence);

    DECLARE_ATTRIBUTE_CAPTUREDEF(FireResistance);
    DECLARE_ATTRIBUTE_CAPTUREDEF(ThunderResistance);
    DECLARE_ATTRIBUTE_CAPTUREDEF(WaterResistance);
    DECLARE_ATTRIBUTE_CAPTUREDEF(MagicResistance);
    DECLARE_ATTRIBUTE_CAPTUREDEF(PhysicsResistance);
    DECLARE_ATTRIBUTE_CAPTUREDEF(PureResistance);

    TMap<FGameplayTag, FGameplayEffectAttributeCaptureDefinition> TagsToCaptureDefs;
    MyDamageStatics()
    {
        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, Armor, Target, false);
        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, BlockChance, Target, false);

        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, ArmorPene, Source, false);

        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, CriticalHitChance, Source, false);

        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, CriticalHitDamage, Source, false);

        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, CriticalHitResistence, Target, false);

        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, FireResistance, Target, false);
        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, ThunderResistance, Target, false);
        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, WaterResistance, Target, false);
        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, MagicResistance, Target, false);
        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, PhysicsResistance, Target, false);
        DEFINE_ATTRIBUTE_CAPTUREDEF(UMyAttributeSet, PureResistance, Target, false);
        const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();

        TagsToCaptureDefs.Add(GameplayTags.Attributes_Secondary_Armor, ArmorDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Secondary_BlockChance, BlockChanceDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Secondary_ArmorPene, ArmorPeneDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Secondary_CriticalHitChance, CriticalHitChanceDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Secondary_CriticalHitDamage, CriticalHitDamageDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Secondary_CriticalHitResistence, CriticalHitResistenceDef);

        TagsToCaptureDefs.Add(GameplayTags.Attributes_Resistance_Pure, PureResistanceDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Resistance_Fire, FireResistanceDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Resistance_Water, WaterResistanceDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Resistance_Thunder, ThunderResistanceDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Resistance_Magic, MagicResistanceDef);
        TagsToCaptureDefs.Add(GameplayTags.Attributes_Resistance_Physics, PhysicsResistanceDef);
    }
};
static const MyDamageStatics& DamageStatics()
{
    static MyDamageStatics DamageStatic;

    return DamageStatic;
}

UExecCalc_Damage::UExecCalc_Damage()
{

    RelevantAttributesToCapture.Add(DamageStatics().ArmorDef);
    RelevantAttributesToCapture.Add(DamageStatics().BlockChanceDef);
    RelevantAttributesToCapture.Add(DamageStatics().ArmorPeneDef);

    RelevantAttributesToCapture.Add(DamageStatics().CriticalHitChanceDef);

    RelevantAttributesToCapture.Add(DamageStatics().CriticalHitDamageDef);
    RelevantAttributesToCapture.Add(DamageStatics().CriticalHitResistenceDef);

    RelevantAttributesToCapture.Add(DamageStatics().FireResistanceDef);
    RelevantAttributesToCapture.Add(DamageStatics().ThunderResistanceDef);
    RelevantAttributesToCapture.Add(DamageStatics().WaterResistanceDef);
    RelevantAttributesToCapture.Add(DamageStatics().MagicResistanceDef);
    RelevantAttributesToCapture.Add(DamageStatics().PhysicsResistanceDef);
    RelevantAttributesToCapture.Add(DamageStatics().PureResistanceDef);
}
void UExecCalc_Damage::DetermineDebuff(
    const FGameplayEffectCustomExecutionParameters& ExecutionParams, const FGameplayEffectSpec& Spec,
    FAggregatorEvaluateParameters EvaluationParameters,
    const TMap<FGameplayTag, FGameplayEffectAttributeCaptureDefinition>& InTagsToDefs) const
{
    const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();

    for (TTuple<FGameplayTag, FGameplayTag> Pair : GameplayTags.DamageTypesToDebuffs)
    {
        const FGameplayTag& DamageType = Pair.Key;
        const FGameplayTag& DebuffType = Pair.Value;
        const float TypeDamage = Spec.GetSetByCallerMagnitude(DamageType, false, -1.f);
        if (TypeDamage > -.5f) // .5 padding for floating point [im]precision
        {
            // Determine if there was a successful debuff
            const float SourceDebuffChance = Spec.GetSetByCallerMagnitude(GameplayTags.Debuff_Chance, false, -1.f);

            float TargetDebuffResistance = 0.f;

            const FGameplayTag& ResistanceTag = GameplayTags.DamageTypesToResistance[DamageType];
            ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(InTagsToDefs[ResistanceTag],
                                                                       EvaluationParameters, TargetDebuffResistance);
            TargetDebuffResistance = FMath::Max<float>(TargetDebuffResistance, 0.f);
            const float EffectiveDebuffChance = SourceDebuffChance * (100 - TargetDebuffResistance) / 100.f;
            const bool bDebuff = FMath::RandRange(1, 100) < EffectiveDebuffChance;
            if (bDebuff)
            {
                FGameplayEffectContextHandle ContextHandle = Spec.GetContext();

                UMyAbilitySystemLibrary::SetIsSuccessfulDebuff(ContextHandle, true);
                UMyAbilitySystemLibrary::SetDamageType(ContextHandle, DamageType);

                const float DebuffDamage = Spec.GetSetByCallerMagnitude(GameplayTags.Debuff_Damage, false, -1.f);
                const float DebuffDuration = Spec.GetSetByCallerMagnitude(GameplayTags.Debuff_Duration, false, -1.f);
                const float DebuffFrequency = Spec.GetSetByCallerMagnitude(GameplayTags.Debuff_Frequency, false, -1.f);

                UMyAbilitySystemLibrary::SetDebuffDamage(ContextHandle, DebuffDamage);
                UMyAbilitySystemLibrary::SetDebuffDuration(ContextHandle, DebuffDuration);
                UMyAbilitySystemLibrary::SetDebuffFrequency(ContextHandle, DebuffFrequency);
            }
        }
    }
}
void UExecCalc_Damage::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
                                              FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{

    const UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
    const UAbilitySystemComponent* TargetSC = ExecutionParams.GetTargetAbilitySystemComponent();

    AActor* SourceAvatar = SourceASC ? SourceASC->GetAvatarActor() : nullptr;
    AActor* TargetAvatar = TargetSC ? TargetSC->GetAvatarActor() : nullptr;

    int32 SourcePlayerLevel = 1;

    if (SourceAvatar->Implements<UCombatInterface>())
    {
        SourcePlayerLevel = ICombatInterface::Execute_GetPlayerLevel(SourceAvatar);
    }
    int32 TargetPlayerLevel = 1;

    if (TargetAvatar->Implements<UCombatInterface>())
    {
        TargetPlayerLevel = ICombatInterface::Execute_GetPlayerLevel(TargetAvatar);
    }

    const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
    FGameplayEffectContextHandle EffectContextHandle = Spec.GetContext();

    const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
    const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

    FAggregatorEvaluateParameters EvaluateParameters;
    EvaluateParameters.SourceTags = SourceTags;
    EvaluateParameters.TargetTags = TargetTags;

    //get damage set by caller
    DetermineDebuff(ExecutionParams, Spec, EvaluateParameters, MyDamageStatics().TagsToCaptureDefs);

    float Damage = 0;

    FGameplayTag Damage_Pure;

    const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();

    //添加标签和属性快照对应的数据
    bool bPure = false;

    for (const auto& DamageTypeTag : FMyGameplayTags::Get().DamageTypesToResistance)
    {
        const FGameplayTag DamageType = DamageTypeTag.Key;
        const FGameplayTag ResistanceType = DamageTypeTag.Value;

        const FGameplayEffectAttributeCaptureDefinition CaptureDef =
            MyDamageStatics().TagsToCaptureDefs[ResistanceType];
        float DamageTypeValue = Spec.GetSetByCallerMagnitude(DamageType, false, 0);
        if (DamageTypeValue <= 0.f)
            continue;
        if (UMyAbilitySystemLibrary::IsRadialDamage(EffectContextHandle))
        {
            FDelegateHandle RadialHandle;
            TWeakObjectPtr<AActor> RadialTarget = TargetAvatar;
            ON_SCOPE_EXIT
            {
                if (auto* ValidTarget = RadialTarget.Get())
                    if (auto* Combat = Cast<ICombatInterface>(ValidTarget))
                        Combat->GetOnDamageSignature().Remove(RadialHandle);
            };
            if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(TargetAvatar))
            {
                RadialHandle = CombatInterface->GetOnDamageSignature().AddLambda(
                    [&DamageTypeValue](float DamageAmount)
                    {
                        DamageTypeValue = DamageAmount;
                    });
            }
            UGameplayStatics::ApplyRadialDamageWithFalloff(
                TargetAvatar, DamageTypeValue, 0.f, UMyAbilitySystemLibrary::GetRadialDamageOrigin(EffectContextHandle),
                UMyAbilitySystemLibrary::GetRadialDamageInnerRadius(EffectContextHandle),
                UMyAbilitySystemLibrary::GetRadialDamageOuterRadius(EffectContextHandle), 1.f,
                UDamageType::StaticClass(), TArray<AActor*>(), SourceAvatar, nullptr);
        }

        if (DamageType.GetTagName() != FName("Damage_Pure"))
        {
            if (DamageTypeValue)
            {
                float Resistance = 0.f;
                ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(CaptureDef, EvaluateParameters, Resistance);
                Damage += DamageTypeValue * (1 - Resistance * 0.01f);
            }
        }
        else
        {
            Damage += DamageTypeValue;
        }
    }

    //capture block chance ,block does half damage

    float TargetBlockChance = 0.f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().BlockChanceDef, EvaluateParameters,
                                                               TargetBlockChance);
    TargetBlockChance = FMath::Max(0.f, TargetBlockChance);

    const bool bBlocked = (TargetSC && TargetSC->HasMatchingGameplayTag(USwordGuardEffect::StateTag())) ||
                          FMath::RandRange(0, 100) < TargetBlockChance;
    if (bBlocked)
        Damage *= 0.5f;

    UMyAbilitySystemLibrary::SetIsBlockedHit(EffectContextHandle, bBlocked);

    //ArmorPene ignore percentage of armor,
    float TargetArmor = 0.f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().ArmorDef, EvaluateParameters,
                                                               TargetArmor);
    TargetArmor = FMath::Max(0.f, TargetArmor);

    UCharacterClassInfo* SourceCharacterClassInfo = UMyAbilitySystemLibrary::GetCharacterClassInfo(SourceAvatar);
    FRealCurve* ArmorPeneCurve = SourceCharacterClassInfo->DamageCalConst->FindCurve(FName("ArmorPene"), FString());
    const float ArmorPeneCoefficient = ArmorPeneCurve->Eval(SourcePlayerLevel);

    float SourceArmorPene = 0.f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().ArmorPeneDef, EvaluateParameters,
                                                               SourceArmorPene);
    SourceArmorPene = FMath::Max(0.f, SourceArmorPene);

    FRealCurve* EffectiveArmorCurve =
        SourceCharacterClassInfo->DamageCalConst->FindCurve(FName("EffectiveArmor"), FString());
    const float EffectiveArmorCoefficient = EffectiveArmorCurve->Eval(TargetPlayerLevel);

    //critical hit and resilience

    float CriticalHitChance = 0.f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().CriticalHitChanceDef, EvaluateParameters,
                                                               CriticalHitChance);
    CriticalHitChance = FMath::Max(0.f, CriticalHitChance);

    float CriticalHitResistence = 0.f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().CriticalHitResistenceDef,
                                                               EvaluateParameters, CriticalHitResistence);
    CriticalHitResistence = FMath::Max(0.f, CriticalHitResistence);

    float CriticalHitDamage = 0.f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().CriticalHitDamageDef, EvaluateParameters,
                                                               CriticalHitDamage);
    CriticalHitDamage = FMath::Max(0.f, CriticalHitDamage);

    FRealCurve* CriticalHitResistanceCurve =
        SourceCharacterClassInfo->DamageCalConst->FindCurve(FName("CriticalHitResistance"), FString());
    const float CriticalHitResistanceCoefficient = EffectiveArmorCurve->Eval(TargetPlayerLevel);

    float EffectiveCriticalHitChance = CriticalHitChance - CriticalHitResistence * CriticalHitResistanceCoefficient;
    EffectiveCriticalHitChance = FMath::Max(0.f, EffectiveCriticalHitChance);

    const bool bCriticalHit = FMath::RandRange(0, 100) < EffectiveCriticalHitChance;
    if (bCriticalHit)
    {
        Damage *= (1 + CriticalHitDamage * 0.01f);
    }
    UMyAbilitySystemLibrary::SetIsCriticalHit(EffectContextHandle, bCriticalHit);

    const float EffectiveArmor = TargetArmor * (100 - SourceArmorPene) * 0.01f;
    Damage *= (100 - EffectiveArmor * EffectiveArmorCoefficient) * 0.01f;

    FGameplayModifierEvaluatedData EvaluatedData(UMyAttributeSet::GetIncomingDamageAttribute(),
                                                 EGameplayModOp::Additive, Damage);
    OutExecutionOutput.AddOutputModifier(EvaluatedData);
}
