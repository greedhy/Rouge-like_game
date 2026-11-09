// 调用链：GA/图 -> 原 Nova 球形查询 -> 冻结目标生命 -> 共享 GE/Mass 伤害 -> 脚底 Cue。
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Interface/CombatInterface.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "UObject/StrongObjectPtr.h"

bool UAbilityNovaNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Radius) || Radius <= 0.f)
    {
        Error = TEXT("Nova radius must be finite and positive.");
        return false;
    }
    return true;
}

void AbilityNovaNodes::GatherActorTargets(AActor* Source, const FVector& Center, float Radius, TArray<AActor*>& Out)
{
    Out.Reset();
    if (!IsValid(Source) || !FMath::IsFinite(Radius) || Radius <= 0.f || Center.ContainsNaN())
    {
        return;
    }
    TArray<AActor*> Candidates;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Source, Candidates, {Source}, Radius, Center);
    for (auto* Target : Candidates)
    {
        if (IsValid(Target) && Target != Source && Target->Implements<UCombatInterface>() &&
            !ICombatInterface::Execute_IsDead(Target) && UMyAbilitySystemLibrary::IsNotFriend(Source, Target) &&
            FVector::DistSquared(Center, Target->GetActorLocation()) <= FMath::Square(Radius) &&
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
        {
            Out.AddUnique(Target);
        }
    }
}

bool UAbilityNovaNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                      const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityNovaNodeConfig>(&Config);
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Error = TEXT("Nova source life expired.");
        return false;
    }
    TArray<AActor*> Actors;
    AbilityNovaNodes::GatherActorTargets(Avatar, Input.Origin, Settings.Radius, Actors);
    Actors.Sort(
        [Center = Input.Origin](const AActor& A, const AActor& B)
        {
            return FVector::DistSquared(Center, A.GetActorLocation()) <
                   FVector::DistSquared(Center, B.GetActorLocation());
        });
    TArray<FAbilityTargetToken> Targets;
    for (auto* Actor : Actors)
    {
        FAbilityTargetToken Token;
        FString Ignored;
        if (AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, FPureMassCombatTarget::FromActor(Actor), Token,
                                                    Ignored))
        {
            Targets.Add(Token);
        }
    }
    if (Settings.bIncludeMass)
    {
        for (const auto& Target :
             PureMassCombat::Gather(Input.SourceToken.World.Get(), Avatar, Input.Origin, Settings.Radius))
        {
            FAbilityTargetToken Token;
            FString Ignored;
            if (!Target.IsActor() && AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Target, Token, Ignored))
            {
                Targets.Add(Token);
            }
        }
    }
    const TStrongObjectPtr<UAbilityNovaNodeContext> Context(Cast<UAbilityNovaNodeContext>(Input.Context.Get()));
    if (Context.Get() && Context->BeforeActorDamage)
    {
        // 报告可能同步回收任一批次目标或取消本次GA；之后只解析上述令牌，绝不重新查询/捕获。
        for (const auto& Target : Targets)
        {
            if (Target.Target.IsActor())
            {
                Context->BeforeActorDamage(Target.Target.Actor.Get());
                break;
            }
        }
    }
    if (bStopped || Owner.IsFinished())
    {
        return true;
    }
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Owner.Cancel(EAbilityGraphEndReason::SourceInvalid);
        return true;
    }
    for (const auto& Target : Targets)
    {
        FVector Location;
        if (bStopped || Owner.IsFinished() ||
            !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, Location))
        {
            continue;
        }
        auto Params = Input.DamageParams;
        Params.bIsRadialDamage = false;
        Params.KnockbackForce = FVector::ZeroVector;
        Params.DeathImpulse = (Location - Avatar->GetActorLocation()).GetSafeNormal() * Params.DeathImpulseMagnitude;
        const auto Result = AbilityDamageService::Apply(Input.SourceToken, Target, Params);
        if (bStopped || Owner.IsFinished())
        {
            return true;
        }
        FAbilityNodeInput Payload = Input;
        FAbilityImpactSnapshot Impact;
        Impact.Location = Result.ImpactLocation;
        Impact.ConfirmedTarget = Target;
        Payload.Impact = Impact;
        Payload.LiveTargetToken.Reset();
        Payload.bHasDamageResult = true;
        Payload.bDamageAttempted = Result.bAttempted;
        Payload.bKilled = Result.bKilled;
        Payload.DamageApplied = Result.Applied;
        Payload.ActualDamage = Result.ActualDamage;
        Owner.Publish(Id, TEXT("DamageResolved"), Payload);
    }
    if (!bStopped && !Owner.IsFinished() && AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        if (Settings.CueTag.IsValid())
        {
            FGameplayCueParameters Cue;
            Cue.Location = Input.Origin;
            if (const auto* Character = Cast<ACharacter>(Avatar))
            {
                Cue.Location.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            }
            Cue.Normal = FVector::UpVector;
            Cue.RawMagnitude = Settings.Radius;
            Cue.Instigator = Avatar;
            Cue.EffectCauser = Avatar;
            Input.SourceToken.ASC->ExecuteGameplayCue(Settings.CueTag, Cue);
        }
        if (!bStopped && !Owner.IsFinished())
        {
            Owner.Publish(Id, TEXT("Completed"), Input);
        }
    }
    return true;
}

void AbilityNovaNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Node;
    Node.TypeId = TEXT("Shape.ArcaneNova");
    Node.ConfigClass = UAbilityNovaNodeConfig::StaticClass();
    Node.ExecutionClass = UAbilityNovaNodeExecution::StaticClass();
    Node.InputPayload = EAbilityPortPayload::Origin;
    Node.bSupportsActorTargets = Node.bSupportsMassTargets = Node.bSupportsSnapshotTargets = true;
    Node.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Node.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Node, Error), TEXT("Nova registration: %s"), *Error);
    }
}
