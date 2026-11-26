// 调用链：图策略授予玩家护盾 -> 元属性消费剩余值 -> 复制更新客户端 Niagara。
// 定时器归玩家组件所有，耗尽、到期或 EndPlay 时释放，不依赖 GA 实例继续存活。
#include "AbilitySystem/Unified/AbilityBarrier.h"

#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Interface/CombatInterface.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Player/MyPlayerState.h"

namespace
{
    /** 编译期与实际授予使用相同数值边界。 */
    bool IsValidBarrier(float Capacity, float Duration)
    {
        return FMath::IsFinite(Capacity) && Capacity >= 1.f && Capacity <= 250.f && FMath::IsFinite(Duration) &&
               Duration >= 0.1f && Duration <= 15.f;
    }
} // namespace

UAbilityBarrierComponent::UAbilityBarrierComponent()
{
    SetIsReplicatedByDefault(true);
}

void UAbilityBarrierComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UAbilityBarrierComponent, Remaining);
}

void UAbilityBarrierComponent::Grant(AActor& Avatar, float Capacity, float Duration)
{
    UWorld* World = Avatar.GetWorld();
    if (!Avatar.HasAuthority() || !World || !IsValidBarrier(Capacity, Duration))
    {
        return;
    }
    auto* Barrier = Avatar.FindComponentByClass<UAbilityBarrierComponent>();
    if (!Barrier)
    {
        Barrier = NewObject<UAbilityBarrierComponent>(&Avatar);
        Avatar.AddInstanceComponent(Barrier);
        Barrier->RegisterComponent();
        if (auto* Combat = Cast<ICombatInterface>(&Avatar))
        {
            Combat->GetOnDeathDelegate().AddUniqueDynamic(Barrier, &ThisClass::OnOwnerDied);
        }
    }
    const double Now = World->GetTimeSeconds();
    const float Active = Barrier->ExpiresAt > Now ? Barrier->Remaining : 0.f;
    Barrier->Remaining = FMath::Max(Active, Capacity);
    Barrier->ExpiresAt = FMath::Max(Barrier->ExpiresAt, Now + Duration);
    World->GetTimerManager().SetTimer(Barrier->ExpiryTimer, Barrier, &ThisClass::Clear,
                                      static_cast<float>(Barrier->ExpiresAt - Now), false);
    Barrier->OnRep_Remaining();
    Avatar.ForceNetUpdate();
}

float UAbilityBarrierComponent::ConsumeDamage(float Damage)
{
    if (!GetOwner()->HasAuthority() || Remaining <= 0.f || Damage <= 0.f)
    {
        return Damage;
    }
    if (GetWorld()->GetTimeSeconds() >= ExpiresAt)
    {
        Clear();
        return Damage;
    }
    const float Absorbed = FMath::Min(Remaining, Damage);
    Remaining -= Absorbed;
    if (Remaining <= 0.f)
    {
        Clear();
    }
    else
    {
        GetOwner()->ForceNetUpdate();
    }
    return Damage - Absorbed;
}

void UAbilityBarrierComponent::Clear()
{
    GetWorld()->GetTimerManager().ClearTimer(ExpiryTimer);
    Remaining = 0.f;
    ExpiresAt = 0.0;
    OnRep_Remaining();
    GetOwner()->ForceNetUpdate();
}

void UAbilityBarrierComponent::OnOwnerDied(AActor* DeadActor)
{
    if (DeadActor == GetOwner() && DeadActor->HasAuthority())
    {
        Clear();
    }
}

void UAbilityBarrierComponent::OnRep_Remaining()
{
    if (Remaining <= 0.f)
    {
        RemoveVisual();
        return;
    }
    const auto* Avatar = GetOwner();
    if (IsValid(Visual) || !FApp::CanEverRender() || !Avatar || Avatar->GetNetMode() == NM_DedicatedServer ||
        !Avatar->GetRootComponent())
    {
        return;
    }
    static const FSoftObjectPath AuraPath(
        TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Air.NS_Free_Spells_Aura_Air"));
    if (auto* Aura = Cast<UNiagaraSystem>(AuraPath.TryLoad()))
    {
        Visual = UNiagaraFunctionLibrary::SpawnSystemAttached(Aura, Avatar->GetRootComponent(), NAME_None,
                                                              FVector::ZeroVector, FRotator::ZeroRotator,
                                                              EAttachLocation::KeepRelativeOffset, false);
    }
}

void UAbilityBarrierComponent::RemoveVisual()
{
    if (IsValid(Visual))
    {
        Visual->DestroyComponent();
    }
    Visual = nullptr;
}

void UAbilityBarrierComponent::EndPlay(EEndPlayReason::Type EndPlayReason)
{
    if (auto* Combat = Cast<ICombatInterface>(GetOwner()))
    {
        Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OnOwnerDied);
    }
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ExpiryTimer);
    }
    RemoveVisual();
    Super::EndPlay(EndPlayReason);
}

bool UAbilityBarrierNodeConfig::Validate(FString& Error) const
{
    if (IsValidBarrier(Capacity, Duration))
    {
        return true;
    }
    Error = TEXT("Barrier requires capacity 1 to 250 and duration 0.1 to 15 seconds.");
    return false;
}

bool UAbilityBarrierNodeConfig::MatchesType(FName TypeId) const
{
    return TypeId == (bOnDamage ? TEXT("Status.BarrierOnDamage") : TEXT("Status.Barrier"));
}

bool UAbilityBarrierNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter == EAbilityConfigParameter::BarrierCapacity && IsValidBarrier(Value, Duration))
    {
        Capacity = Value;
        return true;
    }
    if (Parameter == EAbilityConfigParameter::Duration && IsValidBarrier(Capacity, Value))
    {
        Duration = Value;
        return true;
    }
    Error = TEXT("Unsupported barrier parameter or value outside its bounds.");
    return false;
}

bool UAbilityBarrierNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                         const UAbilityNodeConfig& Configuration, const FAbilityNodeInput& InInput,
                                         FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityBarrierNodeConfig>(&Configuration);
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar) ||
        !Cast<AMyPlayerState>(Input.SourceToken.ASC->GetOwnerActor()))
    {
        Error = TEXT("Barrier requires a live player avatar.");
        return false;
    }
    if (!Settings.bOnDamage || (Input.bHasDamageResult && Input.bDamageAttempted && Input.ActualDamage.Get(0.f) > 0.f &&
                                Owner.AdmitDamageTrigger(Id, 1, 1, 0.f)))
    {
        UAbilityBarrierComponent::Grant(*Avatar, Settings.Capacity, Settings.Duration);
    }
    Owner.Publish(Id, TEXT("Completed"), Input);
    return true;
}

void AbilityBarrierNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Self;
    Self.TypeId = TEXT("Status.Barrier");
    Self.ConfigClass = UAbilityBarrierNodeConfig::StaticClass();
    Self.ExecutionClass = UAbilityBarrierNodeExecution::StaticClass();
    Self.InputPayload = EAbilityPortPayload::Origin;
    Self.bSupportsActorTargets = true;
    Self.bRuntimeFiniteSafe = true;
    Self.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    Self.Ports = {{TEXT("Completed"), Self.InputPayload, true}};

    FAbilityNodeDescriptor Feedback = Self;
    Feedback.TypeId = TEXT("Status.BarrierOnDamage");
    Feedback.InputPayload = EAbilityPortPayload::DamageResult;
    Feedback.bSupportsMassTargets = true;
    Feedback.Ports = {{TEXT("Completed"), Feedback.InputPayload, true}};
    for (const auto& Descriptor : {Self, Feedback})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Barrier node registration: %s"), *Error);
        }
    }
}
