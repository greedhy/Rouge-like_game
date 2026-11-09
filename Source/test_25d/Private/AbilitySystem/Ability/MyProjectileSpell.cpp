// 调用链：蓝图SpawnProjectile/轨道弹前端 -> 本次会话验证 -> 共享延迟构造/发射动作 -> 原投射物伤害。
#include "AbilitySystem/Ability/MyProjectileSpell.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "Interface/CombatInterface.h"

bool UMyProjectileSpell::CaptureProjectileSource(FAbilitySourceToken& Source) const
{
    const auto Session = GetExecutionSession();
    FString Error;
    return IsActive() && Session && Session->IsSourceCurrent() &&
           AbilityExecutionIdentity::CaptureSource(GetAvatarActorFromActorInfo(), Source, Error);
}

AMyProjectile* UMyProjectileSpell::CreateProjectileDeferred(TSubclassOf<AMyProjectile> Class,
                                                            const FTransform& Transform)
{
    FAbilitySourceToken Source;
    if (!CaptureProjectileSource(Source))
    {
        return nullptr;
    }
    return AbilityTraditionalActions::CreateProjectileDeferred(Source, MakeDamageEffectParamsFromClassDefaults(), Class,
                                                               Transform);
}

void UMyProjectileSpell::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                         const FGameplayAbilityActorInfo* ActorInfo,
                                         const FGameplayAbilityActivationInfo ActivationInfo,
                                         const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UMyProjectileSpell::SpawnProjectile(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag,
                                         const FName SocketName, const bool bOverridePitch, const float PitchOverride)
{
    FAbilitySourceToken Source;
    if (!CaptureProjectileSource(Source) || !Source.Avatar->Implements<UCombatInterface>())
    {
        return;
    }
    const FVector Origin = ICombatInterface::Execute_GetCombatSocketLocation(Source.Avatar.Get(), SocketTag);
    FAbilityActorProjectileSettings Settings;
    Settings.ProjectileClass = ProjectileClass;
    // 旧单发入口在保存Transform后才覆写局部Pitch，因此保留其实际方向；图配置可显式覆写Pitch。
    AbilityTraditionalActions::SpawnProjectiles(Source, MakeDamageEffectParamsFromClassDefaults(), Settings,
                                                GetAbilityLevel(), Origin, ProjectileTargetLocation);
}
