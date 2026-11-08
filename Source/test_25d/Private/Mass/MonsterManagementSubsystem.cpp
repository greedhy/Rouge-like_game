#include "Mass/MonsterManagementSubsystem.h"
#include "Mass/MonsterManagementConfig.h"
#include "Mass/MonsterFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassCommandBuffer.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"

bool UMonsterManagementSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UMonsterManagementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Mass = Collection.InitializeDependency<UMassEntitySubsystem>();
}

void UMonsterManagementSubsystem::Enable(UMonsterManagementConfig* InConfig)
{
    check(IsInGameThread());
    if (bEnabled || GetWorld()->GetNetMode() == NM_Client || !Mass)
        return;
    // Snapshot the asset so runtime changes cannot invalidate existing grid membership.
    Config = InConfig ? DuplicateObject<UMonsterManagementConfig>(InConfig, this)
                      : NewObject<UMonsterManagementConfig>(this);
    auto Safe = [](float Value, float Default, float Min, float Max)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Default;
    };
    Config->CellSize = Safe(Config->CellSize, 1000, 100, 1000000);
    Config->UpdateInterval = Safe(Config->UpdateInterval, .2f, .02f, 5);
    Config->WakeRadius = Safe(Config->WakeRadius, 2500, 0, 1000000);
    Config->SleepRadius = FMath::Max(Config->WakeRadius, Safe(Config->SleepRadius, 3500, 0, 1000000));
    Config->DamageWakeSeconds = Safe(Config->DamageWakeSeconds, 5, 0, 3600);
    Grid.Reset(Config->CellSize);
    const UScriptStruct* Fragments[] = {FMonsterFragment::StaticStruct()};
    Archetype = Mass->GetMutableEntityManager().CreateArchetype(Fragments);
    bEnabled = true;
    for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
        if (It->HasActorBegunPlay())
            RegisterEnemy(*It);
}

bool UMonsterManagementSubsystem::IsTickable() const
{
    return bEnabled && Super::IsTickable();
}

TStatId UMonsterManagementSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMonsterManagementSubsystem, STATGROUP_Tickables);
}

void UMonsterManagementSubsystem::Tick(float DeltaTime)
{
    if (!Mass || Mass->GetEntityManager().IsProcessing())
        return;
    TArray<TWeakObjectPtr<AEnemyCharacter>> Pending = MoveTemp(PendingRegistrations);
    PendingRegistrations.Reset();
    for (const auto& Enemy : Pending)
        if (Enemy.IsValid())
            RegisterEnemy(Enemy.Get());
}

void UMonsterManagementSubsystem::RegisterEnemy(AEnemyCharacter* Enemy)
{
    check(IsInGameThread());
    if (!bEnabled || !IsValid(Enemy) || Enemy->GetWorld() != GetWorld() || !Enemy->HasAuthority() ||
        ICombatInterface::Execute_IsDead(Enemy) || ActorIds.Contains(Enemy))
        return;
    auto& Entities = Mass->GetMutableEntityManager();
    if (Entities.IsProcessing())
    {
        PendingRegistrations.AddUnique(Enemy);
        return;
    }
    FRecord Record;
    Record.Enemy = Enemy;
    Record.Entity = Entities.CreateEntity(Archetype);
    const uint64 Id = NextId++;
    FMonsterFragment& Fragment = Entities.GetFragmentDataChecked<FMonsterFragment>(Record.Entity);
    Fragment.Actor = Enemy;
    Fragment.RegistrationId = Id;
    Fragment.Location = Enemy->GetActorLocation();
    Fragment.RoomId = Enemy->MonsterRoomId;
    Fragment.MonsterType = Enemy->GetClass()->GetFName();
    ActorIds.Add(Enemy, Id);
    Records.Add(Id, Record);
    Grid.Update(Id, Fragment.Location, Fragment.RoomId);
}

void UMonsterManagementSubsystem::SetActive(FRecord& Record, bool bActive)
{
    AEnemyCharacter* Enemy = Record.Enemy.Get();
    if (!Enemy)
        return;
    Record.bActive = bActive;
    if (bActive)
    {
        if (auto* Brain = Record.PausedBrain.Get())
        {
            auto* AI = Cast<AAIController>(Enemy->GetController());
            if (AI && AI->GetBrainComponent() == Brain && Brain->IsPaused() && !ICombatInterface::Execute_IsDead(Enemy))
                Brain->ResumeLogic(TEXT("Mass monster awake"));
        }
        Record.PausedBrain.Reset();
    }
    else if (Config->bPauseDistantAI && !Record.PausedBrain.IsValid())
    {
        if (auto* AI = Cast<AAIController>(Enemy->GetController()))
            if (auto* Brain = AI->GetBrainComponent(); Brain && Brain->IsRunning() && !Brain->IsPaused())
            {
                Record.PausedBrain = Brain;
                Brain->PauseLogic(TEXT("Mass monster outside activity radius"));
                AI->StopMovement();
            }
    }
}

void UMonsterManagementSubsystem::RemoveRecord(uint64 Id)
{
    FRecord* Record = Records.Find(Id);
    if (!Record)
        return;
    ActorIds.Remove(Record->Enemy);
    if (Mass)
    {
        auto& Entities = Mass->GetMutableEntityManager();
        if (Entities.IsEntityValid(Record->Entity))
        {
            if (Entities.IsProcessing())
                Entities.Defer().DestroyEntity(Record->Entity);
            else
                Entities.DestroyEntity(Record->Entity);
        }
    }
    Grid.Remove(Id);
    Records.Remove(Id);
}

void UMonsterManagementSubsystem::UnregisterEnemy(AEnemyCharacter* Enemy)
{
    PendingRegistrations.Remove(Enemy);
    if (const uint64* Id = ActorIds.Find(Enemy))
        RemoveRecord(*Id);
}

void UMonsterManagementSubsystem::NotifyDamaged(AEnemyCharacter* Enemy)
{
    if (const uint64* Id = ActorIds.Find(Enemy))
        if (FRecord* Record = Records.Find(*Id))
        {
            Record->ProtectedUntil = GetWorld()->GetTimeSeconds() + Config->DamageWakeSeconds;
            SetActive(*Record, true);
        }
}

bool UMonsterManagementSubsystem::BeginUpdate(float DeltaSeconds)
{
    check(IsInGameThread());
    if (!bEnabled || GetWorld()->GetNetMode() == NM_Client)
        return false;
    AccumulatedTime += DeltaSeconds;
    if (AccumulatedTime < Config->UpdateInterval)
        return false;
    AccumulatedTime = FMath::Fmod(AccumulatedTime, Config->UpdateInterval);
    ++UpdateCount;
    PlayerLocations.Reset();
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (const APlayerController* PC = It->Get())
            if (const APawn* Pawn = PC->GetPawn(); IsValid(Pawn))
                if (!Pawn->Implements<UCombatInterface>() || !ICombatInterface::Execute_IsDead(Pawn))
                    PlayerLocations.Add(Pawn->GetActorLocation());
    return true;
}

void UMonsterManagementSubsystem::UpdateEnemy(FMonsterFragment& Fragment)
{
    FRecord* Record = Records.Find(Fragment.RegistrationId);
    AEnemyCharacter* Enemy = Fragment.Actor.Get();
    if (!Record || !IsValid(Enemy) || ICombatInterface::Execute_IsDead(Enemy))
        return;
    Fragment.Location = Enemy->GetActorLocation();
    Fragment.RoomId = Enemy->MonsterRoomId;
    Grid.Update(Fragment.RegistrationId, Fragment.Location, Fragment.RoomId);
    double NearestSquared = TNumericLimits<double>::Max();
    for (const FVector& Location : PlayerLocations)
        NearestSquared = FMath::Min(NearestSquared, FVector::DistSquared(Location, Fragment.Location));
    bool bProtected = GetWorld()->GetTimeSeconds() < Record->ProtectedUntil || Enemy->bHitReacting;
    if (UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent())
        for (const FGameplayAbilitySpec& Ability : ASC->GetActivatableAbilities())
            if (Ability.IsActive())
            {
                bProtected = true;
                break;
            }
    const bool bActive =
        FMonsterActivityPolicy::ShouldStayActive(Record->bActive, NearestSquared, !PlayerLocations.IsEmpty(),
                                                 bProtected, Config->WakeRadius, Config->SleepRadius);
    SetActive(*Record, bActive);
    Fragment.bActive = Record->bActive;
    if (Config->bDrawDebug)
        DrawDebugSphere(GetWorld(), Fragment.Location + FVector(0, 0, 100), 40, 8,
                        bActive ? FColor::Green : FColor::Blue, false, Config->UpdateInterval);
}

void UMonsterManagementSubsystem::FinishUpdate()
{
    TArray<uint64> Stale;
    for (const auto& Pair : Records)
        if (!Pair.Value.Enemy.IsValid() || ICombatInterface::Execute_IsDead(Pair.Value.Enemy.Get()))
            Stale.Add(Pair.Key);
    for (uint64 Id : Stale)
        RemoveRecord(Id);
}

TArray<AEnemyCharacter*> UMonsterManagementSubsystem::FindMonstersInRadius(FVector Center, float Radius,
                                                                           FName RoomId) const
{
    TArray<AEnemyCharacter*> Result;
    for (uint64 Id : Grid.Query(Center, Radius, RoomId))
        if (const FRecord* Record = Records.Find(Id))
            if (AEnemyCharacter* Enemy = Record->Enemy.Get();
                IsValid(Enemy) && !ICombatInterface::Execute_IsDead(Enemy))
                Result.Add(Enemy);
    return Result;
}

bool UMonsterManagementSubsystem::IsMonsterActive(AEnemyCharacter* Enemy) const
{
    if (const uint64* Id = ActorIds.Find(Enemy))
        if (const FRecord* Record = Records.Find(*Id))
            return Record->bActive;
    return false;
}

FMassEntityHandle UMonsterManagementSubsystem::GetEntityForEnemy(const AEnemyCharacter* Enemy) const
{
    if (const uint64* Id = ActorIds.Find(Enemy))
        if (const FRecord* Record = Records.Find(*Id))
            return Record->Entity;
    return FMassEntityHandle();
}

FMonsterManagementStats UMonsterManagementSubsystem::GetStats() const
{
    FMonsterManagementStats Stats;
    Stats.bEnabled = bEnabled;
    Stats.Registered = Records.Num();
    Stats.OccupiedCells = Grid.NumCells();
    Stats.UpdateCount = UpdateCount;
    for (const auto& Pair : Records)
    {
        if (Pair.Value.bActive)
            ++Stats.Active;
        else
            ++Stats.Sleeping;
        if (Pair.Value.PausedBrain.IsValid())
            ++Stats.PausedBrains;
    }
    return Stats;
}

void UMonsterManagementSubsystem::Deinitialize()
{
    bEnabled = false;
    // World teardown does not guarantee reverse subsystem dependency order.
    // Mass owns and releases all world entities; its manager may already be gone.
    // Do not resume AI or touch another subsystem while the world is being destroyed.
    Records.Reset();
    Archetype = FMassArchetypeHandle();
    PendingRegistrations.Reset();
    PlayerLocations.Reset();
    Grid.Reset();
    ActorIds.Reset();
    Config = nullptr;
    Mass = nullptr;
    Super::Deinitialize();
}
