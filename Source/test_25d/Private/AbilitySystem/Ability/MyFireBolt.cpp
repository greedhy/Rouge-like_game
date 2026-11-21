// 调用链：原蓝图目标/蒙太奇 -> SpawnProjectiles -> 共享Actor投射物动作 -> 原伤害和复制表现。

#include "AbilitySystem/Ability/MyFireBolt.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityNativeProjectileParameterNodes.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"

#include "Actor/MyProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"

UAbilityGraphDefinition* UMyFireBolt::BuildRuntimeBaseGraph(UObject* Outer, FString& Error) const
{
    Error.Reset();
    FAbilityActorProjectileSettings Settings;
    Settings.EquipmentAbilityTag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    Settings.ProjectileClass = ProjectileClass;
    Settings.Count = NumProjectiles;
    Settings.bLimitCountToLevel = true;
    Settings.Spread = ProjectileSpread;
    Settings.bConfigureHoming = true;
    Settings.bLaunchHoming = bLaunchHomingProjectiles;
    Settings.HomingAccelerationMin = HomingAccelerationMin;
    Settings.HomingAccelerationMax = HomingAccelerationMax;
    return AbilityNativeProjectileParameters::Build(Outer, Settings);
}

FString UMyFireBolt::GetDescription(int32 Level)
{
    const int32 ScaledDamage = Damage.GetValueAtLevel(Level);
    const float ManaCost = FMath::Abs(GetManaCost(Level));
    const float Cooldown = GetCooldown(Level);
    if (Level == 1)
    {
        return FString::Printf(TEXT(
                                   // Title
                                   "<Title>FIRE BOLT</>\n\n"

                                   // Level
                                   "<Small>Level: </><Level>%d</>\n"
                                   // ManaCost
                                   "<Small>ManaCost: </><ManaCost>%.1f</>\n"
                                   // Cooldown
                                   "<Small>Cooldown: </><Cooldown>%.1f</>\n\n"

                                   "<Default>Launches a bolt of fire, "
                                   "exploding on impact and dealing: </>"

                                   // Damage
                                   "<Damage>%d</><Default> fire damage with"
                                   " a chance to burn</>"),

                               // Values
                               Level, ManaCost, Cooldown, ScaledDamage);
    }
    else
    {
        return FString::Printf(TEXT(
                                   // Title
                                   "<Title>FIRE BOLT</>\n\n"

                                   // Level
                                   "<Small>Level: </><Level>%d</>\n"
                                   // ManaCost
                                   "<Small>ManaCost: </><ManaCost>%.1f</>\n"
                                   // Cooldown
                                   "<Small>Cooldown: </><Cooldown>%.1f</>\n\n"

                                   // Number of FireBolts
                                   "<Default>Launches %d bolts of fire, "
                                   "exploding on impact and dealing: </>"

                                   // Damage
                                   "<Damage>%d</><Default> fire damage with"
                                   " a chance to burn</>"),

                               // Values
                               Level, ManaCost, Cooldown, FMath::Min(Level, NumProjectiles), ScaledDamage);
    }
}

FString UMyFireBolt::GetNextLevelDescription(int32 Level)
{
    const int32 ScaledDamage = Damage.GetValueAtLevel(Level);
    const float ManaCost = FMath::Abs(GetManaCost(Level));
    const float Cooldown = GetCooldown(Level);
    return FString::Printf(TEXT(
                               // Title
                               "<Title>NEXT LEVEL: </>\n\n"

                               // Level
                               "<Small>Level: </><Level>%d</>\n"
                               // ManaCost
                               "<Small>ManaCost: </><ManaCost>%.1f</>\n"
                               // Cooldown
                               "<Small>Cooldown: </><Cooldown>%.1f</>\n\n"

                               // Number of FireBolts
                               "<Default>Launches %d bolts of fire, "
                               "exploding on impact and dealing: </>"

                               // Damage
                               "<Damage>%d</><Default> fire damage with"
                               " a chance to burn</>"),

                           // Values
                           Level, ManaCost, Cooldown, FMath::Min(Level, NumProjectiles), ScaledDamage);
}

void UMyFireBolt::SpawnProjectiles(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag,
                                   bool bOverridePitch, float PitchOverride, AActor* HomingTarget)
{
    if (!GetRuntimeRecipeError().IsEmpty() || (GetRuntimeRecipeSnapshot().Graph && HasStartedRuntimeRecipe()))
    {
        return;
    }
    FAbilitySourceToken Source;
    if (!CaptureProjectileSource(Source) || !Source.Avatar->Implements<UCombatInterface>())
    {
        return;
    }
    const auto Session = GetExecutionSession();
    if (GetRuntimeRecipeSnapshot().Graph &&
        (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo) ||
         GetExecutionSession() != Session || !CaptureProjectileSource(Source)))
    {
        // 原蓝图可能已经 Commit；公共会话返回同一结果。失败不能通过忽略返回值启动动态图。
        return;
    }
    ReportOffensiveAttack(HomingTarget);
    if (GetExecutionSession() != Session || !CaptureProjectileSource(Source))
    {
        return;
    }
    const FVector Origin = ICombatInterface::Execute_GetCombatSocketLocation(Source.Avatar.Get(), SocketTag);
    if (GetRuntimeRecipeSnapshot().Graph)
    {
        FAbilityNodeInput Input;
        Input.SourceToken = Source;
        Input.Origin = Origin;
        Input.EventCenter = ProjectileTargetLocation;
        FRotator Aim = (ProjectileTargetLocation - Origin).Rotation();
        if (bOverridePitch)
        {
            Aim.Pitch = PitchOverride;
        }
        Input.Direction = Aim.Vector();
        Input.Level = GetAbilityLevel();
        Input.DamageParams = MakeDamageEffectParamsFromClassDefaults();
        auto* Call = NewObject<UAbilityActorProjectileContext>(this);
        Call->bOverridePitch = bOverridePitch;
        Call->PitchOverride = PitchOverride;
        Input.Context = Call;
        FString Error;
        FAbilityTargetToken Target;
        if (IsValid(HomingTarget) && AbilityExecutionIdentity::CaptureTarget(
                                         Source, FPureMassCombatTarget::FromActor(HomingTarget), Target, Error))
        {
            Input.LiveTargetToken = Target;
        }
        // 原 FireBolt 前端可正常结束，真实飞行/延迟图继续归世界；显式取消仍关闭本次图。
        StartFrozenRuntimeRecipe(Input, EAbilityExecutionLifetime::IndependentCastEffect, Error);
        return;
    }
    FAbilityActorProjectileSettings Settings;
    Settings.EquipmentAbilityTag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    Settings.ProjectileClass = ProjectileClass;
    Settings.Count = NumProjectiles;
    Settings.bLimitCountToLevel = true;
    Settings.Spread = ProjectileSpread;
    Settings.bOverridePitch = bOverridePitch;
    Settings.PitchOverride = PitchOverride;
    Settings.bConfigureHoming = true;
    Settings.bLaunchHoming = bLaunchHomingProjectiles;
    Settings.HomingAccelerationMin = HomingAccelerationMin;
    Settings.HomingAccelerationMax = HomingAccelerationMax;
    AbilityTraditionalActions::SpawnProjectiles(Source, MakeDamageEffectParamsFromClassDefaults(), Settings,
                                                GetAbilityLevel(), Origin, ProjectileTargetLocation, HomingTarget);
}
