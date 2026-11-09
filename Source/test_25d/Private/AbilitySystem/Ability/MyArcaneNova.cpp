// 调用链：GAS Commit -> Shape.ArcaneNova 捕获目标生命 -> 原目标报告 -> 共享伤害/Cue -> End。
#include "AbilitySystem/Ability/MyArcaneNova.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "UObject/StrongObjectPtr.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

UMyArcaneNova::UMyArcaneNova()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    Damage = FScalableFloat(45.f);
    DebuffChance = 0.f;
    KnockbackChance = 0.f;
    KnockbackForceMagnitude = 0.f;
    bIsRadialDamage = false;
}

bool UMyArcaneNova::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                       const FGameplayAbilityActorInfo* ActorInfo,
                                       const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
                                       FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid() || !ActorInfo->AvatarActor.IsValid() ||
        !DamageEffectClass || !DamageType.IsValid() || !CostGameplayEffectClass || !CooldownGameplayEffectClass ||
        !FMath::IsFinite(NovaRadius) || NovaRadius <= 0.f)
    {
        return false;
    }
    const AActor* Avatar = ActorInfo->AvatarActor.Get();
    if (!Avatar->Implements<UCombatInterface>() || ICombatInterface::Execute_IsDead(Avatar))
    {
        return false;
    }
    return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

float UMyArcaneNova::GetNovaDamage(int32 Level) const
{
    const int32 SafeLevel = FMath::Max(1, Level);
    return FMath::Max(0.f, Damage.GetValueAtLevel(SafeLevel) + BonusDamagePerLevel * (SafeLevel - 1));
}

void UMyArcaneNova::GatherNovaTargets(AActor* SourceAvatar, TArray<AActor*>& OutTargets) const
{
    AbilityNovaNodes::GatherActorTargets(SourceAvatar,
                                         IsValid(SourceAvatar) ? SourceAvatar->GetActorLocation() : FVector::ZeroVector,
                                         NovaRadius, OutTargets);
}

void UMyArcaneNova::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayAbilityActivationInfo ActivationInfo,
                                    const FGameplayEventData* TriggerEventData)
{
    const auto Session = GetExecutionSession();
    FAbilityNodeInput Input;
    FString Error;
    // 先锁定本次生命，提交仍由原 GA 单次执行；图中不再重复扣费或激活另一个 GA。
    if (!ActorInfo || !ActorInfo->IsNetAuthority() || !CanActivateAbility(Handle, ActorInfo) ||
        !AbilityExecutionIdentity::CaptureSource(ActorInfo->AvatarActor.Get(), Input.SourceToken, Error) ||
        !CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        if (GetExecutionSession() == Session)
        {
            EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        }
        return;
    }

    AActor* Avatar = nullptr;
    if (GetExecutionSession() != Session || (Session && !Session->IsSourceCurrent()))
    {
        if (IsActive() && GetExecutionSession() == Session)
        {
            EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        }
        return;
    }
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    Input.Origin = Avatar->GetActorLocation();
    Input.DamageParams = MakeDamageEffectParamsFromClassDefaults();
    Input.DamageParams.BaseDamage = GetNovaDamage(GetAbilityLevel());
    Input.Level = GetAbilityLevel();
    auto* Context = NewObject<UAbilityNovaNodeContext>(this);
    const TWeakObjectPtr<UMyArcaneNova> WeakThis(this);
    Context->BeforeActorDamage = [WeakThis, Session](AActor* Target)
    {
        auto* Self = WeakThis.Get();
        if (Self && Self->IsActive() && Self->GetExecutionSession() == Session &&
            (!Session || Session->IsSourceCurrent()))
        {
            Self->ReportOffensiveAttack(Target);
        }
    };
    Input.Context = Context;
    auto* Graph = NewObject<UAbilityGraphDefinition>(this);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Shape.ArcaneNova");
    auto* Config = NewObject<UAbilityNovaNodeConfig>(Graph);
    Config->Radius = NovaRadius;
    Config->CueTag = NovaCueTag;
    Node.Config = Config;
    Graph->Nodes.Add(Node);
    Graph->EntryNodeId = Node.NodeId;
    const TStrongObjectPtr<UAbilityGraphInstance> Run(NewObject<UAbilityGraphInstance>(this));
    if (Session)
    {
        const TWeakObjectPtr<UAbilityGraphInstance> WeakRun(Run.Get());
        Session->AddCleanup(
            [WeakRun]()
            {
                if (auto* Instance = WeakRun.Get())
                {
                    Instance->Cancel();
                }
            });
    }
    const bool bStarted = Run->StartWithInput(Graph, Input, {}, Error);
    // 伤害/Cue可能同步结束并重激活；只结束发起该图的原会话。
    if (IsActive() && GetExecutionSession() == Session)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, !bStarted || (Session && !Session->IsSourceCurrent()));
    }
}

FString UMyArcaneNova::GetDescription(int32 Level)
{
    return FString::Printf(TEXT("<Title>ARCANE NOVA</>\n\n<Small>Level: </><Level>%d</>\n"
                                "<Small>ManaCost: </><ManaCost>%.1f</>\n<Small>Cooldown: </><Cooldown>%.1f</>\n\n"
                                "<Default>Release a pulse within %.0f units, dealing </><Damage>%.0f</>"
                                "<Default> arcane damage to each living enemy once.</>"),
                           Level, FMath::Abs(GetManaCost(Level)), GetCooldown(Level), NovaRadius, GetNovaDamage(Level));
}

FString UMyArcaneNova::GetNextLevelDescription(int32 Level)
{
    return FString(TEXT("<Title>NEXT LEVEL</>\n")) + GetDescription(Level);
}
