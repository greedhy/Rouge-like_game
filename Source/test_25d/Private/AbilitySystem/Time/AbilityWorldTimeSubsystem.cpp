// 调用链：统一节点/独立GA/蓝图 -> Acquire -> Tick真实时间 -> Recompute -> 到期/死亡/Release恢复原值。
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Interface/CombatInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Actor/StanderWeaponProjectile.h"

bool FAbilityWorldSlowSettings::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Scale) || Scale < .05f || Scale > 1.f || !FMath::IsFinite(BlendIn) || !FMath::IsFinite(Hold) ||
        !FMath::IsFinite(BlendOut) || BlendIn < 0 || BlendOut < 0 || Hold <= 0 || Duration() > 30.)
    {
        Error = TEXT("World slow requires scale [0.05,1], positive hold, nonnegative blends and at most 30 seconds.");
        return false;
    }
    return true;
}

double FAbilityWorldSlowSettings::Duration() const
{
    return double(BlendIn) + Hold + BlendOut;
}

float FAbilityWorldSlowSettings::Evaluate(double Elapsed) const
{
    if (BlendIn > 0 && Elapsed < BlendIn)
    {
        return FMath::Lerp(1.f, Scale, float(FMath::Clamp(Elapsed / BlendIn, 0., 1.)));
    }
    if (Elapsed < double(BlendIn) + Hold)
    {
        return Scale;
    }
    return BlendOut > 0 ? FMath::Lerp(Scale, 1.f, float(FMath::Clamp((Elapsed - BlendIn - Hold) / BlendOut, 0., 1.)))
                        : 1.f;
}

void UAbilityWorldTimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LastRealTime = GetWorld()->GetRealTimeSeconds();
    bClockInitialized = true;
    SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(
        FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::OnActorSpawned));
}

namespace
{
    /** 去重包含完整生命与包络配置；不同阶段或补偿策略不能共用旧请求。 */
    bool MatchesRequest(const FAbilityWorldTimeLease& Request, const FAbilitySourceToken& Source,
                        const FAbilityWorldSlowSettings& Settings, const UObject* Owner)
    {
        return Request.Owner.Get() == Owner && Request.Source.World == Source.World &&
               Request.Source.Avatar == Source.Avatar && Request.Source.ASC == Source.ASC &&
               Request.Source.DungeonRunId == Source.DungeonRunId && Request.Source.EnemyLease == Source.EnemyLease &&
               Request.Settings.Scale == Settings.Scale && Request.Settings.BlendIn == Settings.BlendIn &&
               Request.Settings.Hold == Settings.Hold && Request.Settings.BlendOut == Settings.BlendOut &&
               Request.Settings.bCompensateSource == Settings.bCompensateSource;
    }
} // namespace

bool UAbilityWorldTimeSubsystem::IsRequestLive(const FAbilityWorldTimeLease& Request) const
{
    AActor* Avatar = nullptr;
    return Request.Owner.IsValid() && AbilityExecutionIdentity::ResolveSource(Request.Source, Avatar) &&
           GameplayRealTime - Request.Started < Request.Settings.Duration();
}

bool UAbilityWorldTimeSubsystem::CanAcquire(const FAbilitySourceToken& Source,
                                            const FAbilityWorldSlowSettings& Settings, const UObject* Owner,
                                            FString& Error) const
{
    Error.Reset();
    AActor* Avatar = nullptr;
    if (bClosing || !GetWorld() || GetWorld()->GetNetMode() != NM_Standalone || !IsValid(Owner) ||
        Source.World != GetWorld() || !Settings.Validate(Error) ||
        !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        if (Error.IsEmpty())
        {
            Error = TEXT("World slow requires a live standalone source/owner and fewer than 64 active requests.");
        }
        return false;
    }
    if (bOwnsWorld && !FMath::IsNearlyEqual(GetWorld()->GetWorldSettings()->TimeDilation, LastWorldWritten, 1.e-5f))
    {
        Error = TEXT("An external system changed world time; wait for ownership cleanup before requesting again.");
        return false;
    }
    int32 LiveCount = 0;
    for (const auto& Request : Requests)
    {
        if (IsRequestLive(Request))
        {
            if (MatchesRequest(Request, Source, Settings, Owner))
            {
                return true;
            }
            ++LiveCount;
        }
    }
    if (LiveCount >= 64)
    {
        Error = TEXT("World slow has reached its 64 live request limit.");
        return false;
    }
    return true;
}

FGuid UAbilityWorldTimeSubsystem::Acquire(const FAbilitySourceToken& Source, const FAbilityWorldSlowSettings& Settings,
                                          UObject* Owner, FString& Error)
{
    if (!CanAcquire(Source, Settings, Owner, Error))
    {
        return {};
    }
    // 同帧换Run/生命后先淘汰旧请求并恢复其补偿，不能等待下一次Tick才纠正。
    PruneRequests();
    Recompute();
    RemoveUnusedDeathBindings();
    for (const auto& Request : Requests)
    {
        if (MatchesRequest(Request, Source, Settings, Owner))
        {
            return Request.Id;
        }
    }
    AActor* Avatar = Source.Avatar.Get();
    if (!bOwnsWorld)
    {
        WorldBaseline = GetWorld()->GetWorldSettings()->TimeDilation;
        LastWorldWritten = WorldBaseline;
        bOwnsWorld = true;
    }
    FAbilityWorldTimeLease Request;
    Request.Id = FGuid::NewGuid();
    Request.Source = Source;
    Request.Settings = Settings;
    Request.Owner = Owner;
    Request.Started = GameplayRealTime;
    Requests.Add(Request);
    if (auto* Combat = Cast<ICombatInterface>(Avatar))
    {
        Combat->GetOnDeathDelegate().AddUniqueDynamic(this, &ThisClass::SourceDied);
        DeathBindings.Add(Avatar);
    }
    if (Settings.bCompensateSource)
    {
        RegisterCompensatedActor(Avatar, Avatar);
        // 一次捕获现有弹道；后续生成由Spawn回调在FinishSpawning后的下一次Tick发现。
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            DiscoverProjectile(*It);
        }
    }
    Recompute();
    return IsActive(Request.Id) ? Request.Id : FGuid();
}

FGuid UAbilityWorldTimeSubsystem::RequestWorldSlow(AActor* Source, UObject* LifetimeOwner,
                                                   const FAbilityWorldSlowSettings& Settings)
{
    FAbilitySourceToken Token;
    FString Error;
    if (!AbilityExecutionIdentity::CaptureSource(Source, Token, Error))
    {
        return {};
    }
    return Acquire(Token, Settings, LifetimeOwner, Error);
}

void UAbilityWorldTimeSubsystem::Release(FGuid Handle)
{
    if (Requests.RemoveAll(
            [&](const FAbilityWorldTimeLease& Request)
            {
                return Request.Id == Handle;
            }) > 0)
    {
        Recompute();
        RemoveUnusedDeathBindings();
    }
}

void UAbilityWorldTimeSubsystem::CancelSource(AActor* Source)
{
    if (Source && Source->GetWorld() == GetWorld())
    {
        SourceDied(Source);
    }
}

bool UAbilityWorldTimeSubsystem::IsActive(FGuid Handle) const
{
    return Handle.IsValid() && Requests.ContainsByPredicate(
                                   [&](const FAbilityWorldTimeLease& Request)
                                   {
                                       return Request.Id == Handle;
                                   });
}

bool UAbilityWorldTimeSubsystem::HasExpired(FGuid Handle) const
{
    return Expired.Contains(Handle);
}

bool UAbilityWorldTimeSubsystem::HasActiveRequests() const
{
    return !Requests.IsEmpty();
}

double UAbilityWorldTimeSubsystem::GetGameplayRealTime() const
{
    return GameplayRealTime;
}

void UAbilityWorldTimeSubsystem::RegisterCompensatedActor(AActor* Actor, AActor* Beneficiary)
{
    if (!IsValid(Actor) || !IsValid(Beneficiary) || Actor->GetWorld() != GetWorld() ||
        !Requests.ContainsByPredicate(
            [&](const FAbilityWorldTimeLease& Request)
            {
                return Request.Settings.bCompensateSource && Request.Source.Avatar.Get() == Beneficiary;
            }))
    {
        return;
    }
    auto* Existing = Participants.FindByPredicate(
        [&](const FAbilityTimeParticipant& Item)
        {
            return Item.Actor.Get() == Actor;
        });
    if (Existing)
    {
        Existing->Beneficiaries.Add(Beneficiary);
    }
    else
    {
        FAbilityTimeParticipant Item;
        Item.Actor = Actor;
        Item.Beneficiaries.Add(Beneficiary);
        Item.Baseline = Item.LastWritten = Actor->CustomTimeDilation;
        Participants.Add(MoveTemp(Item));
    }
}

void UAbilityWorldTimeSubsystem::OnActorSpawned(AActor* Actor)
{
    if (!Requests.IsEmpty())
    {
        SpawnCandidates.Add(Actor);
    }
}

void UAbilityWorldTimeSubsystem::DiscoverProjectile(AActor* Actor)
{
    if (!IsValid(Actor) ||
        (!Actor->FindComponentByClass<UProjectileMovementComponent>() && !Actor->IsA<AStanderWeaponProjectile>()))
    {
        return;
    }
    for (const auto& Request : Requests)
    {
        AActor* Source = Request.Source.Avatar.Get();
        if (Request.Settings.bCompensateSource && (Actor->GetInstigator() == Source || Actor->GetOwner() == Source))
        {
            RegisterCompensatedActor(Actor, Source);
        }
    }
}

void UAbilityWorldTimeSubsystem::Tick(float DeltaSeconds)
{
    if (!bClosing && GetWorld())
    {
        SampleClock(GetWorld()->GetRealTimeSeconds(), UGameplayStatics::IsGamePaused(GetWorld()));
    }
}

void UAbilityWorldTimeSubsystem::SampleClock(double AbsoluteRealSeconds, bool bPaused)
{
    if (!FMath::IsFinite(AbsoluteRealSeconds) || bClosing)
    {
        return;
    }
    if (bClockInitialized && !bPaused)
    {
        GameplayRealTime += FMath::Max(0., AbsoluteRealSeconds - LastRealTime);
    }
    LastRealTime = AbsoluteRealSeconds;
    bClockInitialized = true;
    PruneRequests();
    const auto Candidates = MoveTemp(SpawnCandidates);
    SpawnCandidates.Reset();
    for (const auto& Candidate : Candidates)
    {
        DiscoverProjectile(Candidate.Get());
    }
    Recompute();
    RemoveUnusedDeathBindings();
}

void UAbilityWorldTimeSubsystem::PruneRequests()
{
    Requests.RemoveAll(
        [&](const FAbilityWorldTimeLease& Request)
        {
            AActor* Avatar = nullptr;
            if (!Request.Owner.IsValid() || !AbilityExecutionIdentity::ResolveSource(Request.Source, Avatar))
            {
                return true;
            }
            if (GameplayRealTime - Request.Started >= Request.Settings.Duration())
            {
                if (Expired.Num() == 256)
                {
                    Expired.RemoveAt(0);
                }
                Expired.Add(Request.Id);
                return true;
            }
            return false;
        });
}

void UAbilityWorldTimeSubsystem::Recompute()
{
    if (!bOwnsWorld || !GetWorld())
    {
        return;
    }
    auto* WorldSettings = GetWorld()->GetWorldSettings();
    if (!FMath::IsNearlyEqual(WorldSettings->TimeDilation, LastWorldWritten, 1.e-5f))
    {
        // 外部系统取得控制权：撤销我们的请求和角色补偿，不回写过期基准。
        Requests.Reset();
        RestoreParticipants();
        bOwnsWorld = false;
        RemoveUnusedDeathBindings();
        return;
    }
    float Factor = 1.f;
    for (const auto& Request : Requests)
    {
        Factor = FMath::Min(Factor, Request.Settings.Evaluate(GameplayRealTime - Request.Started));
    }
    LastWorldWritten = WorldSettings->SetTimeDilation(WorldBaseline * Factor);
    const float ActualFactor = WorldBaseline > SMALL_NUMBER ? LastWorldWritten / WorldBaseline : 1.f;
    for (int32 Index = Participants.Num() - 1; Index >= 0; --Index)
    {
        auto& Item = Participants[Index];
        auto* Actor = Item.Actor.Get();
        for (auto It = Item.Beneficiaries.CreateIterator(); It; ++It)
        {
            if (!Requests.ContainsByPredicate(
                    [&](const FAbilityWorldTimeLease& Request)
                    {
                        return Request.Settings.bCompensateSource && Request.Source.Avatar == *It;
                    }))
            {
                It.RemoveCurrent();
            }
        }
        const bool bEligible = !Item.Beneficiaries.IsEmpty();
        if (!Actor || !bEligible)
        {
            if (Actor && FMath::IsNearlyEqual(Actor->CustomTimeDilation, Item.LastWritten))
            {
                Actor->CustomTimeDilation = Item.Baseline;
            }
            Participants.RemoveAtSwap(Index);
        }
        else if (FMath::IsNearlyEqual(Actor->CustomTimeDilation, Item.LastWritten))
        {
            Item.LastWritten = Item.Baseline / FMath::Max(ActualFactor, SMALL_NUMBER);
            Actor->CustomTimeDilation = Item.LastWritten;
        }
    }
    if (Requests.IsEmpty())
    {
        bOwnsWorld = false;
        SpawnCandidates.Reset();
    }
}

void UAbilityWorldTimeSubsystem::RestoreParticipants()
{
    for (const auto& Item : Participants)
    {
        if (auto* Actor = Item.Actor.Get(); Actor && FMath::IsNearlyEqual(Actor->CustomTimeDilation, Item.LastWritten))
        {
            Actor->CustomTimeDilation = Item.Baseline;
        }
    }
    Participants.Reset();
    SpawnCandidates.Reset();
}

void UAbilityWorldTimeSubsystem::SourceDied(AActor* Actor)
{
    Requests.RemoveAll(
        [&](const FAbilityWorldTimeLease& Request)
        {
            return Request.Source.Avatar.Get() == Actor;
        });
    Recompute();
    RemoveUnusedDeathBindings();
}

void UAbilityWorldTimeSubsystem::RemoveUnusedDeathBindings()
{
    for (auto It = DeathBindings.CreateIterator(); It; ++It)
    {
        if (!Requests.ContainsByPredicate(
                [&](const FAbilityWorldTimeLease& Request)
                {
                    return Request.Source.Avatar == *It;
                }))
        {
            if (auto* Combat = Cast<ICombatInterface>(It->Get()))
            {
                Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::SourceDied);
            }
            It.RemoveCurrent();
        }
    }
}

void UAbilityWorldTimeSubsystem::Deinitialize()
{
    bClosing = true;
    Requests.Reset();
    Recompute();
    RestoreParticipants();
    RemoveUnusedDeathBindings();
    if (GetWorld())
    {
        GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
    }
    Super::Deinitialize();
}

bool UAbilityWorldTimeSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

TStatId UAbilityWorldTimeSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UAbilityWorldTimeSubsystem, STATGROUP_Tickables);
}

#if WITH_DEV_AUTOMATION_TESTS
void UAbilityWorldTimeSubsystem::SampleClockForTesting(double AbsoluteRealSeconds, bool bPaused)
{
    SampleClock(AbsoluteRealSeconds, bPaused);
}
#endif
