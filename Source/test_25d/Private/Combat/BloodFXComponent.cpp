// 调用链：服务器确认命中/死亡 → 组件多播 → 血迹管理器；复制视觉状态 → 有生命校验的定时滴血。
#include "Combat/BloodFXComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Interface/CombatInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UBloodFXComponent::UBloodFXComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UBloodFXComponent::BeginPlay()
{
    Super::BeginPlay();
    ResetForLife(OwnerLife());
    RefreshBleeding();
}

int32 UBloodFXComponent::OwnerLife() const
{
    const auto* Enemy = Cast<AEnemyCharacter>(GetOwner());
    return Enemy ? FMath::Abs(Enemy->GetDungeonPoolLease()) : 0;
}

float UBloodFXComponent::OwnerRadius() const
{
    const auto* Character = Cast<ACharacter>(GetOwner());
    return Character ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 42.f;
}

bool UBloodFXComponent::IsCurrentLife(int32 Life) const
{
    return Life >= 0 && Life >= FMath::Max(OwnerLife(), LatestLife);
}

void UBloodFXComponent::ResetForLife(int32 Life)
{
    if (Life < LatestLife)
    {
        return;
    }
    if (Life == LatestLife)
    {
        // 命中 RPC 可先于 Owner 的唤醒复制到达，重新协调滴血但保留去重记录。
        RefreshBleeding();
        return;
    }
    LatestLife = Life;
    DeathLife.Reset();
    RecentHits.Reset();
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(DripTimer);
    }
    // 不覆盖服务端已收到的新生命状态；旧复制状态由 Life 校验忽略。
    RefreshBleeding();
}

bool UBloodFXComponent::AcceptHit(FGuid AttackId, int32 Life)
{
    if (!Style.bEnabled || !AttackId.IsValid() || !IsCurrentLife(Life) || RecentHits.Contains(AttackId))
    {
        return false;
    }
    ResetForLife(Life);
    if (RecentHits.Num() >= 32)
    {
        RecentHits.RemoveAt(0);
    }
    RecentHits.Add(AttackId);
    ++AcceptedHits;
    return true;
}

bool UBloodFXComponent::AcceptDeath(int32 Life)
{
    if (!Style.bEnabled || !IsCurrentLife(Life) || DeathLife == Life)
    {
        return false;
    }
    ResetForLife(Life);
    DeathLife = Life;
    ++AcceptedDeaths;
    StopForDeath();
    return true;
}

void UBloodFXComponent::NotifyHit(FGuid AttackId, int32 Life, FVector Location, FVector Direction, bool bHeavy)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !IsCurrentLife(Life) || Location.ContainsNaN() ||
        Direction.ContainsNaN())
    {
        return;
    }
    MulticastHit(AttackId, Life, Location, Direction.GetSafeNormal(), bHeavy);
}

void UBloodFXComponent::MulticastHit_Implementation(FGuid AttackId, int32 Life, FVector_NetQuantize10 Location,
                                                    FVector_NetQuantizeNormal Direction, bool bHeavy)
{
    if (AcceptHit(AttackId, Life) && GetNetMode() != NM_DedicatedServer)
    {
        if (auto* FX = GetWorld()->GetSubsystem<UBloodFXSubsystem>())
        {
            FX->ShowHit(Style, Location, Direction, OwnerRadius(), bHeavy);
        }
    }
}

void UBloodFXComponent::NotifyDeath(int32 Life, FVector Location, FVector Direction, bool bLarge)
{
    if (GetOwner() && GetOwner()->HasAuthority() && IsCurrentLife(Life) && !Location.ContainsNaN() &&
        !Direction.ContainsNaN())
    {
        SetBleeding(false);
        MulticastDeath(Life, Location, Direction.GetSafeNormal(), bLarge);
    }
}

void UBloodFXComponent::MulticastDeath_Implementation(int32 Life, FVector_NetQuantize10 Location,
                                                      FVector_NetQuantizeNormal Direction, bool bLarge)
{
    if (AcceptDeath(Life) && GetNetMode() != NM_DedicatedServer)
    {
        if (auto* FX = GetWorld()->GetSubsystem<UBloodFXSubsystem>())
        {
            FX->ShowDeath(Style, Location, Direction, OwnerRadius(), bLarge);
        }
    }
}

void UBloodFXComponent::SetBleeding(bool bEnabled)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority())
    {
        return;
    }
    const bool bAlive = Owner->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(Owner);
    Bleeding.bEnabled = bEnabled && bAlive && Style.bEnabled;
    Bleeding.Life = OwnerLife();
    OnRep_Bleeding();
    Owner->ForceNetUpdate();
}

bool UBloodFXComponent::IsBleeding() const
{
    const AActor* Owner = GetOwner();
    return Bleeding.bEnabled && Bleeding.Life == OwnerLife() && Bleeding.Life >= LatestLife && Style.bEnabled &&
           Owner && Owner->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(Owner) &&
           DeathLife != Bleeding.Life;
}

void UBloodFXComponent::RefreshBleeding()
{
    if (!GetWorld())
    {
        return;
    }
    if (IsBleeding() && GetNetMode() != NM_DedicatedServer)
    {
        if (!GetWorld()->GetTimerManager().IsTimerActive(DripTimer))
        {
            GetWorld()->GetTimerManager().SetTimer(DripTimer, this, &ThisClass::Drip, 1.f, true, .01f);
        }
    }
    else
    {
        GetWorld()->GetTimerManager().ClearTimer(DripTimer);
    }
}

void UBloodFXComponent::Drip()
{
    if (!IsBleeding())
    {
        StopForDeath();
        return;
    }
    if (auto* FX = GetWorld()->GetSubsystem<UBloodFXSubsystem>())
    {
        FX->ShowDrip(Style, GetOwner()->GetActorLocation(), OwnerRadius());
        ++DripSamples;
    }
}

void UBloodFXComponent::OnRep_Bleeding()
{
    RefreshBleeding();
}

void UBloodFXComponent::StopForDeath()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(DripTimer);
    }
}

FString UBloodFXComponent::GetBloodFXReport() const
{
    return FString::Printf(TEXT("life=%d hit=%d death=%d bleeding=%d drips=%d"), OwnerLife(), AcceptedHits,
                           AcceptedDeaths, IsBleeding() ? 1 : 0, DripSamples);
}

void UBloodFXComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UBloodFXComponent, Bleeding);
}

void UBloodFXComponent::EndPlay(EEndPlayReason::Type Reason)
{
    StopForDeath();
    Super::EndPlay(Reason);
}
