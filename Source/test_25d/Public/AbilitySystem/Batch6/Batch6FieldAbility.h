#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "PureMass/PureMassAbility.h"
#include "Batch6FieldAbility.generated.h"
class UMaterialInterface;

/** Opt-in parameters consumed by the mixed-target runtime; existing assets are unchanged. */
UCLASS(BlueprintType)
class TEST_25D_API UBatch6FieldPrototype : public UAbilityPrototype
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bFollowCasterToggle = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bWaveFrontOnly = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUseAuthoredVFXScale = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") bool bShowAreaDisc = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") TObjectPtr<UMaterialInterface> AreaMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") bool bSnowfall = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.05", ClampMax="1")) float SlowMultiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.1", ClampMax="10")) float SlowDuration = 1.25f;
    static bool IsInWaveFront(float Distance, float Previous, float Current)
    { return FMath::IsFinite(Distance) && Distance >= 0.f && Distance <= Current && (Previous <= 0.f || Distance > Previous); }
};

UCLASS()
class TEST_25D_API UBatch6FieldAbility : public UPureMassAbility
{
    GENERATED_BODY()
public:
    virtual FString GetDescription(int32 Level) override
    {
        const auto* Config = Cast<UBatch6FieldPrototype>(Prototype);
        if (!Config || !Config->bFollowCasterToggle) return Super::GetDescription(Level);
        return FString::Printf(TEXT("<Title>%s</>\n<Small>Lv.%d | MP %.0f | CD %.1fs</>\n<Default>Toggle: %.0f MP/s; %.0f damage/%.1fs. Radius %.0f.</>"),
            *Config->DisplayName.ToString(), Level, Config->ManaCost, Config->Cooldown,
            Config->ManaPerSecond, Config->DamageAtLevel(Level), Config->Period, Config->Radius);
    }
    virtual bool IsInputToggleAbility() const override
    {
        const auto* Config = Cast<UBatch6FieldPrototype>(Prototype);
        return (Config && Config->Kind == EAbilityPrototypeKind::Field && Config->bFollowCasterToggle)
            || Super::IsInputToggleAbility();
    }
};
