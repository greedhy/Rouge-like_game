// 调用链：整体新星适配器/运行时片段 -> PrepareTargets -> ApplyPreparedDamage -> PlayPreparedCue。
// 动作只使用当前调用局部值及弱生命；图/节点保活参数，Actor/GE/Cue继续走原服务器通路。
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "Equipment/EquipmentAbilityModifiers.h"
#include "PureMass/PureMassCombatTarget.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "UObject/StrongObjectPtr.h"

bool AbilityNovaNodes::PrepareTargets(UAbilityGraphInstance& Owner, const UAbilityNovaNodeConfig& Settings,
                                      const FAbilityNodeInput& InInput, FAbilityNodeInput& OutPrepared, FString& Error)
{
    OutPrepared = InInput;
    AActor* Avatar = nullptr;
    if (!Settings.Validate(Error) || !AbilityExecutionIdentity::ResolveSource(InInput.SourceToken, Avatar))
    {
        Error = TEXT("Nova preparation requires valid settings and a current source life.");
        return false;
    }
    float Radius = Settings.Radius;
    int32 ExtraChainTargets = 0;
    if (!EquipmentAbilityModifiers::ApplySpellRules(InInput.SourceToken, Settings.EquipmentAbilityTag, Radius,
                                                    OutPrepared.DamageParams, ExtraChainTargets))
    {
        Error = TEXT("Nova equipment parameters failed to resolve.");
        return false;
    }
    OutPrepared.ShapeRadius = Radius;
    auto Captured = MakeShared<FAbilityTargetSetSnapshot>();
    Captured->Source = InInput.SourceToken;
    TArray<AActor*> Actors;
    GatherActorTargets(Avatar, InInput.Origin, Radius, Actors);
    Actors.Sort(
        [Center = InInput.Origin](const AActor& A, const AActor& B)
        {
            return FVector::DistSquared(Center, A.GetActorLocation()) <
                   FVector::DistSquared(Center, B.GetActorLocation());
        });
    for (auto* Actor : Actors)
    {
        FAbilityTargetToken Token;
        FString Ignored;
        if (AbilityExecutionIdentity::CaptureTarget(InInput.SourceToken, FPureMassCombatTarget::FromActor(Actor), Token,
                                                    Ignored))
        {
            Captured->Targets.Add(Token);
        }
    }
    if (Settings.bIncludeMass)
    {
        for (const auto& Target :
             PureMassCombat::Gather(InInput.SourceToken.World.Get(), Avatar, InInput.Origin, Radius))
        {
            FAbilityTargetToken Token;
            FString Ignored;
            if (!Target.IsActor() &&
                AbilityExecutionIdentity::CaptureTarget(InInput.SourceToken, Target, Token, Ignored))
            {
                Captured->Targets.Add(Token);
            }
        }
    }
    OutPrepared.TargetSet = Captured;
    OutPrepared.LiveTargetToken.Reset();
    const TStrongObjectPtr<UAbilityNovaNodeContext> Context(Cast<UAbilityNovaNodeContext>(InInput.Context.Get()));
    if (Context.Get() && Context->BeforeActorDamage)
    {
        for (const auto& Target : Captured->Targets)
        {
            if (Target.Target.IsActor())
            {
                // 原攻击订阅者可同步取消/回收；它不能让本批重新认领同地址的新生命。
                Context->BeforeActorDamage(Target.Target.Actor.Get());
                break;
            }
        }
    }
    if (!Owner.IsFinished() && !AbilityExecutionIdentity::ResolveSource(InInput.SourceToken, Avatar))
    {
        Owner.Cancel(EAbilityGraphEndReason::SourceInvalid);
    }
    return true;
}

void AbilityNovaNodes::ApplyPreparedDamage(UAbilityGraphInstance& Owner, const FGuid& Invocation,
                                           const FAbilityNodeInput& InInput, const FAbilityNodeInput* ResultInput)
{
    const auto Captured = InInput.TargetSet;
    if (!Captured)
    {
        return;
    }
    for (const auto& Target : Captured->Targets)
    {
        AActor* Avatar = nullptr;
        FVector Location;
        if (Owner.IsFinished())
        {
            return;
        }
        if (!AbilityExecutionIdentity::ResolveSource(InInput.SourceToken, Avatar))
        {
            Owner.Cancel(EAbilityGraphEndReason::SourceInvalid);
            return;
        }
        if (!AbilityExecutionIdentity::ResolveTarget(InInput.SourceToken, Target, Location))
        {
            continue;
        }
        auto Params = InInput.DamageParams;
        Params.BaseDamage *= InInput.DamageScale;
        Params.DebuffDamage *= InInput.DebuffDamageScale;
        Params.bIsRadialDamage = false;
        Params.KnockbackForce = FVector::ZeroVector;
        Params.DeathImpulse = (Location - Avatar->GetActorLocation()).GetSafeNormal() * Params.DeathImpulseMagnitude;
        const auto Result = AbilityDamageService::Apply(InInput.SourceToken, Target, Params);
        if (Owner.IsFinished())
        {
            return;
        }
        FAbilityNodeInput Payload = ResultInput ? *ResultInput : InInput;
        Payload.Impact = FAbilityImpactSnapshot{Result.ImpactLocation, FVector::ForwardVector, Target, 0};
        Payload.LiveTargetToken.Reset();
        Payload.TargetSet.Reset();
        Payload.bHasDamageResult = true;
        Payload.bDamageAttempted = Result.bAttempted;
        Payload.bKilled = Result.bKilled;
        Payload.DamageApplied = Result.Applied;
        Payload.ActualDamage = Result.ActualDamage;
        Owner.Publish(Invocation, TEXT("DamageResolved"), Payload);
    }
}

void AbilityNovaNodes::PlayPreparedCue(const FAbilityNodeInput& InInput, FGameplayTag CueTag)
{
    AActor* Avatar = nullptr;
    if (!CueTag.IsValid() || !InInput.ShapeRadius.IsSet() ||
        !AbilityExecutionIdentity::ResolveSource(InInput.SourceToken, Avatar))
    {
        return;
    }
    FGameplayCueParameters Cue;
    Cue.Location = InInput.Origin;
    if (const auto* Character = Cast<ACharacter>(Avatar))
    {
        Cue.Location.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    }
    Cue.Normal = FVector::UpVector;
    Cue.RawMagnitude = InInput.ShapeRadius.GetValue();
    Cue.Instigator = Avatar;
    Cue.EffectCauser = Avatar;
    InInput.SourceToken.ASC->ExecuteGameplayCue(CueTag, Cue);
}
