// 调用链：配置校验 -> 当前来源/目标ASC -> MakeOutgoingSpec与SetByCaller -> Apply；取消只移除本次句柄。
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

bool UAbilityGameplayEffectNodeConfig::MatchesType(FName TypeId) const
{
    return TypeId == (bApplyToLiveActor ? TEXT("Effect.GameplayEffectTarget") : TEXT("Effect.GameplayEffect"));
}

bool UAbilityOwnedEffectNodeConfig::MatchesType(FName TypeId) const
{
    return TypeId == (bApplyToLiveActor ? TEXT("Status.OwnedEffectTarget") : TEXT("Status.OwnedEffect"));
}

bool UAbilityGameplayEffectNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!EffectClass || !FMath::IsFinite(EffectLevel) || EffectLevel < 0.f)
    {
        Error = TEXT("Gameplay effect requires a class and finite nonnegative spec level.");
    }
    for (const auto& Pair : SetByCallerMagnitudes)
    {
        if (!Pair.Key.IsValid() || !FMath::IsFinite(Pair.Value))
        {
            Error = TEXT("SetByCaller requires valid tags and finite magnitudes.");
            break;
        }
    }
    return Error.IsEmpty();
}

bool UAbilityOwnedEffectNodeConfig::Validate(FString& Error) const
{
    if (!Super::Validate(Error))
    {
        return false;
    }
    const auto* Effect = EffectClass->GetDefaultObject<UGameplayEffect>();
    if (!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.f ||
        Effect->DurationPolicy == EGameplayEffectDurationType::Instant ||
        Effect->GetStackingType() != EGameplayEffectStackingType::None)
    {
        Error = TEXT("Owned effect requires a nonstacking persistent GE and finite positive ownership duration.");
    }
    return Error.IsEmpty();
}

bool AbilityGameplayEffectActions::BuildEventMagnitudes(FGameplayTag EventTag, float Magnitude,
                                                        const TArray<FGameplayTag>& InitializedTags,
                                                        bool bInitializeListedTags, TMap<FGameplayTag, float>& Out)
{
    Out.Reset();
    if (!EventTag.IsValid() || !FMath::IsFinite(Magnitude))
    {
        return false;
    }
    if (!bInitializeListedTags)
    {
        Out.Add(EventTag, Magnitude);
        return true;
    }
    for (const auto& Tag : InitializedTags)
    {
        if (!Tag.IsValid())
        {
            Out.Reset();
            return false;
        }
        Out.Add(Tag, Tag == EventTag ? Magnitude : 0.f);
    }
    return true;
}

FAbilityGameplayEffectApplication AbilityGameplayEffectActions::Apply(const FAbilitySourceToken& Source,
                                                                      const UAbilityGameplayEffectNodeConfig& Settings,
                                                                      const TOptional<FAbilityTargetToken>& Target)
{
    FAbilityGameplayEffectApplication Result;
    FString Error;
    AActor* Avatar = nullptr;
    if (!Settings.Validate(Error) || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return Result;
    }
    auto* TargetASC = Source.ASC.Get();
    if (Settings.bApplyToLiveActor)
    {
        FVector Location;
        if (!Target.IsSet() || !Target->Target.IsActor() ||
            !AbilityExecutionIdentity::ResolveTarget(Source, Target.GetValue(), Location))
        {
            return Result;
        }
        TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target->Target.Actor.Get());
        if (!IsValid(TargetASC) || TargetASC->GetAvatarActor() != Target->Target.Actor.Get())
        {
            return Result;
        }
    }
    auto Context = Source.ASC->MakeEffectContext();
    Context.AddSourceObject(Avatar);
    auto Spec = Source.ASC->MakeOutgoingSpec(Settings.EffectClass, Settings.EffectLevel, Context);
    if (!Spec.IsValid())
    {
        return Result;
    }
    for (const auto& Pair : Settings.SetByCallerMagnitudes)
    {
        Spec.Data->SetSetByCallerMagnitude(Pair.Key, Pair.Value);
    }
    Result.TargetASC = TargetASC;
    Result.Handle = TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    Result.bApplied = Result.Handle.WasSuccessfullyApplied();
    return Result;
}

void AbilityGameplayEffectActions::Remove(FAbilityGameplayEffectApplication& Applied)
{
    const auto Handle = Applied.Handle;
    auto* ASC = Applied.TargetASC.Get();
    Applied = {};
    if (IsValid(ASC) && Handle.IsValid())
    {
        ASC->RemoveActiveGameplayEffect(Handle);
    }
}

bool UAbilityGameplayEffectNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto Applied = AbilityGameplayEffectActions::Apply(
        Input.SourceToken, *CastChecked<UAbilityGameplayEffectNodeConfig>(&Config), Input.LiveTargetToken);
    if (!Applied.bApplied)
    {
        Error = TEXT("Gameplay effect was rejected by source/target validation or ASC application rules.");
        return false;
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Applied"), Input);
}

bool UAbilityOwnedEffectNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityOwnedEffectNodeConfig>(&Config);
    Remaining = Settings->DurationSeconds;
    auto Result = AbilityGameplayEffectActions::Apply(Input.SourceToken, *Settings, Input.LiveTargetToken);
    // 应用GE可同步结束图；Stop在句柄返回前发生时，由当前栈撤回刚返回的句柄。
    if (bStopped || Owner.IsFinished())
    {
        AbilityGameplayEffectActions::Remove(Result);
        return true;
    }
    Applied = Result;
    if (!Applied.bApplied || !Applied.Handle.IsValid())
    {
        Error = TEXT("Owned effect requires an active handle from ASC.");
        return false;
    }
    return Owner.Publish(Id, TEXT("Applied"), Input);
}

void UAbilityOwnedEffectNodeExecution::Advance(float Delta)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner)
    {
        return;
    }
    Remaining -= Delta;
    if (Remaining <= 0.f || !Applied.TargetASC.IsValid() || !Applied.TargetASC->GetActiveGameplayEffect(Applied.Handle))
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilityOwnedEffectNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    AbilityGameplayEffectActions::Remove(Applied);
}

void AbilityGameplayEffectNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Effect;
    Effect.TypeId = TEXT("Effect.GameplayEffect");
    Effect.ConfigClass = UAbilityGameplayEffectNodeConfig::StaticClass();
    Effect.ExecutionClass = UAbilityGameplayEffectNodeExecution::StaticClass();
    Effect.InputPayload = EAbilityPortPayload::Origin;
    Effect.bSupportsActorTargets = Effect.bSupportsSnapshotTargets = true;
    Effect.Ports = {{TEXT("Applied"), EAbilityPortPayload::Origin, true}};
    auto Owned = Effect;
    Owned.TypeId = TEXT("Status.OwnedEffect");
    Owned.ConfigClass = UAbilityOwnedEffectNodeConfig::StaticClass();
    Owned.ExecutionClass = UAbilityOwnedEffectNodeExecution::StaticClass();
    Owned.bIsAsync = true;
    Owned.Ports = {{TEXT("Applied"), EAbilityPortPayload::Origin, false},
                   {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    // 自身效果与活目标效果使用独立契约；查询输出可直接连到目标版本，不能靠配置暗中换输入类型。
    auto TargetEffect = Effect;
    TargetEffect.TypeId = TEXT("Effect.GameplayEffectTarget");
    TargetEffect.InputPayload = EAbilityPortPayload::LiveTarget;
    TargetEffect.bSupportsSnapshotTargets = false;
    auto TargetOwned = Owned;
    TargetOwned.TypeId = TEXT("Status.OwnedEffectTarget");
    TargetOwned.InputPayload = EAbilityPortPayload::LiveTarget;
    TargetOwned.bSupportsSnapshotTargets = false;
    for (const auto& Descriptor : {Effect, Owned, TargetEffect, TargetOwned})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Gameplay effect registration: %s"), *Error);
        }
    }
}
