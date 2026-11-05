#include "Roguelike/DungeonRunDirector.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Roguelike/DungeonRunController.h"
#include "Roguelike/DungeonRunEconomy.h"
#include "Roguelike/DungeonRunProfile.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Actor/MyProjectile.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Character/EnemyCharacter.h"
#include "Character/GruxEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Player/MyPlayerState.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassCrowd.h"
#include "PureMass/PureMassSubsystem.h"
#include "TimerManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
bool Living(const AActor* Actor)
{
    return IsValid(Actor) && (!Actor->Implements<UCombatInterface>() || !ICombatInterface::Execute_IsDead(Actor));
}
}
ADungeonRunDirector::ADungeonRunDirector()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    bReplicates = true; bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = 0.f;
}
void ADungeonRunDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADungeonRunDirector,RunId); DOREPLIFETIME(ADungeonRunDirector,Phase);
    DOREPLIFETIME(ADungeonRunDirector,Rooms); DOREPLIFETIME(ADungeonRunDirector,OpenDoors);
    DOREPLIFETIME(ADungeonRunDirector,FailureReason); DOREPLIFETIME(ADungeonRunDirector,ActiveRoom);
    DOREPLIFETIME(ADungeonRunDirector,Boss); DOREPLIFETIME(ADungeonRunDirector,PulseCenter);
    DOREPLIFETIME(ADungeonRunDirector,bPulseWarning);
}
ADungeonRunDirector* ADungeonRunDirector::Find(const UWorld* World)
{
    if (World) for (TActorIterator<ADungeonRunDirector> It(World); It; ++It) return *It;
    return nullptr;
}
ADungeonGenerationRuntime* ADungeonRunDirector::GetGeneration() const
{
    if (Generation) return Generation;
    if (GetWorld()) for (TActorIterator<ADungeonGenerationRuntime> It(GetWorld()); It; ++It) return *It;
    return nullptr;
}
FVector ADungeonRunDirector::GetRoomCenter(int32 Index) const
{
    const auto* G = GetGeneration();
    if (!G || !G->Manifest.Layout.Rooms.IsValidIndex(Index)) return FVector::ZeroVector;
    const auto Cell = G->Manifest.Layout.Rooms[Index].Cell;
    return FVector(Cell.X*G->Manifest.RoomSpacing,Cell.Y*G->Manifest.RoomSpacing,0);
}
int32 ADungeonRunDirector::GetRoomIndex(const FVector& P) const
{
    const auto* G = GetGeneration();
    if (!G || P.ContainsNaN() || FMath::Abs(P.Z)>1000 || G->Manifest.RoomSpacing<1
        || FMath::Abs(P.X/G->Manifest.RoomSpacing)>32769 || FMath::Abs(P.Y/G->Manifest.RoomSpacing)>32769) return INDEX_NONE;
    FIntPoint Cell(FMath::FloorToInt(P.X/G->Manifest.RoomSpacing+.5),FMath::FloorToInt(P.Y/G->Manifest.RoomSpacing+.5));
    return G->Manifest.Layout.Rooms.IndexOfByPredicate([&](const FDungeonRoomPlacement& R) { return R.Cell == Cell; });
}
bool ADungeonRunDirector::IsManagedEnemy(const AActor* Actor)
{
    if (!IsValid(Actor)) return false;
    const auto* Enemy = Cast<AEnemyCharacter>(Actor);
    return (Enemy && Enemy->bDungeonEncounter) || (Actor->HasAuthority() && Actor->ActorHasTag(TEXT("DungeonEncounter")));
}
bool ADungeonRunDirector::AllowsCombat(const AActor* Source, const AActor* Target)
{
    const UWorld* World = Target ? Target->GetWorld() : Source ? Source->GetWorld() : nullptr;
    const auto* Run = Find(World); if (!Run) return true;
    if (!Living(Source) || !Living(Target) || Source->GetWorld()!=World || Run->Phase!=EDungeonRunPhase::Encounter || Run->ActiveRoom==INDEX_NONE) return false;
    if (!(Source->IsA<ADungeonPlayerCharacter>() && IsManagedEnemy(Target)) && !(Target->IsA<ADungeonPlayerCharacter>() && IsManagedEnemy(Source))) return false;
    return Run->GetRoomIndex(Source->GetActorLocation())==Run->ActiveRoom && Run->GetRoomIndex(Target->GetActorLocation())==Run->ActiveRoom;
}
bool ADungeonRunDirector::CanPlayerAct(const ADungeonRunController* PC) const
{
    auto* G = GetGeneration();
    return PC && PC->GetWorld()==GetWorld() && PC->PlayerState && Living(PC->GetPawn()) && PC->GetPawn()->IsA<ADungeonPlayerCharacter>()
        && G && G->CanLocalPlayerMove() && (Phase==EDungeonRunPhase::Exploring || Phase==EDungeonRunPhase::Encounter || Phase==EDungeonRunPhase::Victory);
}
void ADungeonRunDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); if (!Generation) Generation=GetGeneration();
    if (HasAuthority()) { DrainPoolReturns(); TickAuthority(); }
    SamplePerformance(DeltaSeconds);
    // Creating gameplay doors before these checks would invalidate the generation nav probe.
    // A late client can receive server Ready before its own streaming/navigation
    // probe completes. Closed encounter doors would invalidate that local probe.
    if (Generation && Generation->CanLocalPlayerMove() && !Rooms.IsEmpty())
    { if (!bLocalBuilt) BuildLocalPresentation(); ApplyDoorPresentation(); }
}

bool ADungeonRunDirector::PrepareEnemyAssets()
{
    if (!Profile) { FailRun(TEXT("MissingGameplayProfile")); return false; }
    FString RosterError;
    if (!Profile->ValidateNormalRoster(RosterError)) { FailRun(RosterError); return false; }
    if (!bEnemyLoadRequested)
    {
        TArray<FSoftObjectPath> Paths;
        Paths.AddUnique(Profile->EliteClass.ToSoftObjectPath());
        Paths.AddUnique(Profile->BossClass.ToSoftObjectPath());
        if (!Profile->NormalClass.IsNull()) Paths.AddUnique(Profile->NormalClass.ToSoftObjectPath());
        for (const auto& Entry:Profile->NormalRoster) Paths.AddUnique(Entry.EnemyClass.ToSoftObjectPath());
        for (const auto& Path:Paths) if (!Path.IsValid()) { FailRun(TEXT("InvalidEnemyAssetPath")); return false; }
        bEnemyLoadRequested=true; EnemyLoadStarted=FPlatformTime::Seconds();
        EnemyLoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
        if (!EnemyLoadHandle.IsValid()) { FailRun(TEXT("EnemyAssetLoadRequestFailed")); return false; }
    }
    if (!EnemyLoadHandle->HasLoadCompleted())
    {
        if (FPlatformTime::Seconds()-EnemyLoadStarted > Profile->EnemyLoadTimeout) FailRun(TEXT("EnemyAssetLoadTimeout"));
        return false;
    }
    if (!Profile->EliteClass.Get() || !Profile->BossClass.Get() || (!Profile->NormalClass.IsNull() && !Profile->NormalClass.Get()))
    { FailRun(TEXT("EnemyAssetLoadFailed")); return false; }
    for (const auto& Entry:Profile->NormalRoster)
        if (!Entry.EnemyClass.Get() || Entry.EnemyClass->HasAnyClassFlags(CLASS_Abstract)
            || !Entry.EnemyClass->IsChildOf(AEnemyCharacter::StaticClass()))
        { FailRun(FString::Printf(TEXT("NormalRosterLoadFailed:%s"),*Entry.Id.ToString())); return false; }
    return true;
}
bool ADungeonRunDirector::InitializeRun()
{
    if (!Profile || !FMath::IsFinite(Profile->NormalHealth) || Profile->NormalHealth <= 0 || !Profile->ItemDefinitions || Profile->ChestItems.IsEmpty() || Profile->Offers.Num()!=3
        || !Profile->EliteClass.Get() || !Profile->BossClass.Get()
        || Profile->CombatCoins<1 || Profile->EliteCoins<1 || Profile->BossCoins<1
        || !FMath::IsFinite(Profile->EliteHealth) || Profile->EliteHealth<=0 || !FMath::IsFinite(Profile->BossHealth) || Profile->BossHealth<=0
        || !FMath::IsFinite(Profile->BossPulseDamage) || Profile->BossPulseDamage<=0
        || !FMath::IsFinite(Profile->BossPulseRadius) || Profile->BossPulseRadius<=0 || Profile->BossPulseRadius>1000
        || !FMath::IsFinite(Profile->ReviveFraction) || Profile->ReviveFraction<=0 || Profile->ReviveFraction>1)
    { FailRun(TEXT("InvalidGameplayProfile")); return false; }
    for (const auto& Item:Profile->ChestItems) if (!Profile->ItemDefinitions->FindDefinition(Item)) { FailRun(TEXT("UnknownChestItem")); return false; }
    for (const auto& Offer:Profile->Offers) if (Offer.Price<=0 || !Profile->ItemDefinitions->FindDefinition(Offer.ItemId)) { FailRun(TEXT("InvalidShopOffer")); return false; }
    if (!Profile->NormalClass.IsNull() && !Profile->NormalClass.Get()) { FailRun(TEXT("InvalidNormalEnemyClass")); return false; }
    if (!Model.Initialize(Generation->Manifest.Layout)) { FailRun(Model.Error); return false; }
    RunId=Generation->Manifest.GenerationId; Economy=NewObject<UDungeonRunEconomy>(this); Mass=GetWorld()->GetSubsystem<UPureMassSubsystem>();
    if (!Mass || !Economy->Initialize(RunId)) { FailRun(TEXT("RunServicesUnavailable")); return false; }
    if (Profile->bUseMassMelee)
    {
        if (!Profile->MonsterType) { FailRun(TEXT("MissingMassMeleeType")); return false; }
        bool HasCrowd = false;
        for (TActorIterator<APureMassCrowd> It(GetWorld()); It; ++It) { HasCrowd = true; break; }
        if (!HasCrowd && !GetWorld()->SpawnActor<APureMassCrowd>()) { FailRun(TEXT("MassCrowdSpawnFailed")); return false; }
    }
    Mass->Enable(); MassRemovedHandle=Mass->OnMonsterRemoved.AddUObject(this,&ADungeonRunDirector::OnMassRemoved);
    EffectSpawnHandle=GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this,&ADungeonRunDirector::TrackEffect));
    for (int32 I=0; I<Model.Rooms.Num(); ++I) if (Model.Rooms[I].Role==EDungeonRoomRole::Treasure)
    {
        FRandomStream Stream(HashCombineFast(uint32(Generation->Seed),HashCombineFast(0x43484553u,uint32(I))));
        ChestItems.Add(I,Profile->ChestItems[Stream.RandRange(0,Profile->ChestItems.Num()-1)]); ChestQuantities.Add(I,Stream.RandRange(1,2));
    }
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (auto* PC=Cast<ADungeonRunController>(It->Get())) if (auto* PS=PC->GetPlayerState<AMyPlayerState>())
            if (auto* Bag=PS->GetBackpackComponent()) Bag->SetItemDefinitions(Profile->ItemDefinitions);
    Phase=EDungeonRunPhase::Exploring; SynchronizeState(); UpdatePlayerViews(); return true;
}
void ADungeonRunDirector::TickAuthority()
{
    if (Phase==EDungeonRunPhase::Failed || Phase==EDungeonRunPhase::Finished || !Generation) return;
    if (Generation->Phase==EDungeonGenerationPhase::Failed) { FailRun(Generation->FailureReason); return; }
    if (Phase==EDungeonRunPhase::Loading && !PrepareEnemyAssets()) return;
    if (Phase==EDungeonRunPhase::Loading && (Generation->Phase!=EDungeonGenerationPhase::Ready || !InitializeRun())) return;
    EnforceBoundaries(); TArray<int32> PlayerRooms; TArray<bool> Safe; int32 Players=0;
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (auto* PC=Cast<ADungeonRunController>(It->Get())) if (auto* Pawn=Cast<ADungeonPlayerCharacter>(PC->GetPawn()))
        {
            ++Players; if (!Living(Pawn)) continue;
            const int32 Room=GetRoomIndex(Pawn->GetActorLocation()); PlayerRooms.Add(Room);
            const FVector D=Pawn->GetActorLocation()-GetRoomCenter(Room); Safe.Add(Room!=INDEX_NONE && FMath::Abs(D.X)<900 && FMath::Abs(D.Y)<900);
        }
    if (Players>0 && PlayerRooms.IsEmpty()) { FailRun(TEXT("AllPlayersDown")); return; }
    if (!Model.Encounter.Error.IsEmpty()) { FailRun(Model.Encounter.Error); return; }
    if (Model.ActiveRoom==INDEX_NONE)
    { const int32 Ready=Model.Observe(PlayerRooms,Safe); if (Ready!=INDEX_NONE) StartEncounter(Ready); }
    else
    {
        UpdateBoss(); if (!Model.Encounter.Error.IsEmpty()) { FailRun(Model.Encounter.Error); return; }
        if (Model.Encounter.CanSpawnNextWave() && !SpawnWave()) { FailRun(TEXT("WaveSpawnFailed")); return; }
        if (Model.Encounter.IsClear()) CompleteEncounter();
    }
    PumpSpawnQueue();
    if (Phase==EDungeonRunPhase::Failed) return;
    SynchronizeState();
    if (GetWorld()->GetTimeSeconds()>=NextViewsAt) { UpdatePlayerViews(); NextViewsAt=GetWorld()->GetTimeSeconds()+.2; }
}
void ADungeonRunDirector::SynchronizeState()
{
    ActiveRoom=Model.ActiveRoom;
    if (Model.Rooms.IsValidIndex(ActiveRoom)) { auto& R=Model.Rooms[ActiveRoom]; R.Wave=Model.Encounter.Wave; R.PendingWaves=Model.Encounter.PendingWaves; R.RemainingRequired=Model.Encounter.NumRequired(); }
    Rooms=Model.Rooms; OpenDoors.Reset();
    if (Generation) for (const auto& Edge:Generation->Manifest.Layout.Connections) OpenDoors.Add(Model.CanCross(Edge.From,Edge.To));
}

bool ADungeonRunDirector::FindSpawnPosition(float Radius, float HalfHeight, FVector& OutPosition, int32 RoomOverride)
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); if (!Nav) return false;
    const int32 Room=RoomOverride==INDEX_NONE?Model.ActiveRoom:RoomOverride;
    const int32 Grid = Profile && Profile->bUseMassMelee ? 11 : 7;
    const float Spacing = Grid == 11 ? 170.f : 230.f;
    for (int32 Attempt=0; Attempt<Grid*Grid; ++Attempt)
    {
        const int32 Slot=SpawnSequence++%(Grid*Grid);
        FVector Desired=GetRoomCenter(Room)+FVector((Slot%Grid-Grid/2)*Spacing,(Slot/Grid-Grid/2)*Spacing,80); FNavLocation P;
        if (!Nav->ProjectPointToNavigation(Desired,P,FVector(80,80,200))) continue;
        const FVector D=P.Location-GetRoomCenter(Room);
        if (FMath::Abs(D.X)+Radius>1050 || FMath::Abs(D.Y)+Radius>1050) continue;
        FVector Candidate=P.Location+FVector(0,0,HalfHeight+3); bool Occupied=false;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (const APawn* Pawn=It->Get()->GetPawn()) Occupied|=FVector::DistSquared2D(Pawn->GetActorLocation(),Candidate)<FMath::Square(Radius+150);
        if (Mass) for (const auto& V:Mass->GetSnapshots()) Occupied|=FVector::DistSquared2D(V.Location,Candidate)<FMath::Square(Radius+(V.Type?V.Type->Radius:40)+20);
        if (Occupied || GetWorld()->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,HalfHeight),FCollisionQueryParams(SCENE_QUERY_STAT(DungeonSpawn),false,this))) continue;
        OutPosition=Candidate; return true;
    }
    return false;
}
void ADungeonRunDirector::StartEncounter(int32 Room)
{
    if (!Model.BeginEncounter(Room,Model.Rooms[Room].Role==EDungeonRoomRole::Combat?2:1)) { FailRun(TEXT("EncounterStartRejected")); return; }
    Phase=EDungeonRunPhase::Encounter; bBossDead=bBossSummoned=false; SpawnSequence=0; NormalGroupSequence=0;
    FRandomStream Stream(HashCombineFast(uint32(Generation->Seed),HashCombineFast(0x57415645u,uint32(Room)))); TotalNormalCount=Stream.RandRange(8,16);
    if (!Mass->SetRoomBounds(Model.Rooms[Room].RoomId,GetRoomCenter(Room),FVector(1100,1100,500))) { FailRun(TEXT("InvalidEncounterBounds")); return; }
    SynchronizeState(); ApplyDoorPresentation(); ForceNetUpdate(); if (!SpawnWave()) FailRun(TEXT("WaveSpawnFailed"));
}
bool ADungeonRunDirector::SpawnWave()
{
    if (!Model.Encounter.BeginWave()) return false;
    const auto RoomRole=Model.Rooms[Model.ActiveRoom].Role; bool Success=false;
    if (RoomRole==EDungeonRoomRole::Combat) Success=SpawnActorGroup(Model.Encounter.Wave==1?TotalNormalCount/2:TotalNormalCount-TotalNormalCount/2,true);
    else if (RoomRole==EDungeonRoomRole::Elite) Success=QueueActorTarget(Profile->EliteClass.Get(),Profile->EliteHealth,true) && SpawnActorGroup(4,true);
    else if (RoomRole==EDungeonRoomRole::Boss) Success=QueueActorTarget(Profile->BossClass.Get(),Profile->BossHealth,true,true) && SpawnActorGroup(2,true) && SpawnActorGroup(2,false);
    if (Success) Success = SpawnMassGroup(Profile->MassCountForWave(RoomRole==EDungeonRoomRole::Combat, Model.Encounter.Wave), true);
    return Model.Encounter.EndWave(Success);
}
bool ADungeonRunDirector::SpawnMassGroup(int32 Count, bool bRequired)
{
    if (Count == 0) return true;
    if (!Mass || !Profile->MonsterType || MassTargets.Num()+ActorTargets.Num()+SpawnQueue.Num()+Count>Profile->EncounterCapacity()) return false;
    for (int32 I=0; I<Count; ++I)
    {
        const FName Key(*FString::Printf(TEXT("MQueued%d"),++ActorSequence));
        if (!Model.Encounter.RegisterTarget(Key,bRequired)) return false;
        SpawnQueue.Add({nullptr,0.f,bRequired,false,Key,RunId,Model.ActiveRoom,false,true});
    }
    return true;
}
bool ADungeonRunDirector::SpawnActorGroup(int32 Count, bool bRequired)
{
    if (Count < 1 || ActorTargets.Num()+MassTargets.Num()+SpawnQueue.Num()+Count > Profile->EncounterCapacity()) return false;
    if (Profile->NormalRoster.IsEmpty())
    {
        const auto Class=Profile->NormalClass.IsNull()?Profile->EliteClass.Get():Profile->NormalClass.Get();
        for (int32 I=0; I<Count; ++I) if (!QueueActorTarget(Class,Profile->NormalHealth,bRequired,false,true)) return false;
        return true;
    }
    const auto Composition=Profile->BuildNormalComposition(Generation->Seed,Model.ActiveRoom,NormalGroupSequence++,Count);
    if (Composition.Num()!=Count) return false;
    for (int32 Index:Composition)
    {
        const auto& Entry=Profile->NormalRoster[Index];
        if (!QueueActorTarget(Entry.EnemyClass.Get(),Entry.Health,bRequired,false,Entry.bPoolEligible)) return false;
    }
    return true;
}
bool ADungeonRunDirector::QueueActorTarget(TSubclassOf<AEnemyCharacter> Class, float Health, bool bRequired, bool bIsBoss, bool bPoolEligible)
{
    if (!Class || ActorTargets.Num()+MassTargets.Num()+SpawnQueue.Num()>=Profile->EncounterCapacity()) return false;
    const FName Id(*FString::Printf(TEXT("A%d"),++ActorSequence));
    if (!Model.Encounter.RegisterTarget(Id,bRequired)) return false;
    SpawnQueue.Add({Class,Health,bRequired,bIsBoss,Id,RunId,Model.ActiveRoom,bPoolEligible});
    return true;
}
void ADungeonRunDirector::PumpSpawnQueue()
{
    LastSpawnCount=0; LastSpawnMs=0;
    const double Start=FPlatformTime::Seconds();
    while (!SpawnQueue.IsEmpty() && LastSpawnCount<FMath::Clamp(Profile->SpawnsPerFrame,1,8))
    {
        const auto Job=SpawnQueue[0]; SpawnQueue.RemoveAt(0);
        if (Job.Run!=RunId || Job.Room!=Model.ActiveRoom) { FailRun(TEXT("StaleEnemySpawnRequest")); return; }
        if (!Job.Required && bBossDead) { Model.Encounter.ResolveTarget(Job.Id,EDungeonTargetResolution::Recycled); continue; }
        bool Spawned = false;
        if (Job.IsMass)
        {
            FVector P;
            if (FindSpawnPosition(Profile->MonsterType->Radius,Profile->MonsterType->HalfHeight,P))
            {
                const int32 Id = Mass->SpawnMonster(Profile->MonsterType,P,Model.Rooms[Job.Room].RoomId);
                Spawned = Id != INDEX_NONE;
                if (Spawned) { MassTargets.Add(Id,Job.Id); if (!Job.Required) OptionalMass.Add(Id); }
            }
        }
        else Spawned = SpawnActorTarget(Job.Class,Job.Health,Job.Required,Job.IsBoss,Job.Id,Job.PoolEligible);
        if (!Spawned) { FailRun(TEXT("EnemySpawnFailed")); return; }
        ++LastSpawnCount; LastSpawnMs=(FPlatformTime::Seconds()-Start)*1000;
        if (LastSpawnMs>=FMath::Max(.1f,Profile->SpawnBudgetMs)) break;
    }
}
bool ADungeonRunDirector::SpawnActorTarget(TSubclassOf<AEnemyCharacter> Class, float Health, bool bRequired, bool bIsBoss, FName ReservedId, bool bPoolEligible)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(DungeonEnemyPool_AcquireOrCreate);
    if (!Class || ActorTargets.Num()+MassTargets.Num() >= Profile->EncounterCapacity()) return false;
    const auto* CDO=Class->GetDefaultObject<AEnemyCharacter>(); FVector P;
    if (!FindSpawnPosition(CDO->GetCapsuleComponent()->GetScaledCapsuleRadius(),CDO->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),P)) return false;
    const FTransform Transform(FRotator::ZeroRotator,P);
    const double Started = FPlatformTime::Seconds();
    const FName RoomId = Model.Rooms[Model.ActiveRoom].RoomId;
    const bool PoolEnabled = bPoolEligible && !bIsBoss && Profile->NormalPoolCapacity > 0;
    auto* Enemy = PoolEnabled ? EnemyPool.Acquire(Class, RoomId) : nullptr;
    const bool Reused = Enemy != nullptr;
    if (PoolEnabled) { if (Reused) ++PoolHits; else ++PoolMisses; }
    if (Reused && !Enemy->WakeFromDungeonPool(Transform, RoomId)) { Enemy->Destroy(); return false; }
    if (!Enemy) Enemy=GetWorld()->SpawnActorDeferred<AEnemyCharacter>(Class,Transform,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Enemy) return false;
    Enemy->bDungeonEncounter = true;
    Enemy->Tags.AddUnique(TEXT("DungeonEncounter")); Enemy->MonsterRoomId=Model.Rooms[Model.ActiveRoom].RoomId;
    const FName Key=ReservedId.IsNone()?FName(*FString::Printf(TEXT("A%d"),++ActorSequence)):ReservedId; ActorTargets.Add(Enemy,Key);
    Enemy->FOnDeath.AddUniqueDynamic(this,&ADungeonRunDirector::OnActorDied); Enemy->OnDestroyed.AddUniqueDynamic(this,&ADungeonRunDirector::OnActorDestroyed);
    if (!bRequired) OptionalActors.Add(Enemy);
    if (ReservedId.IsNone() && !Model.Encounter.RegisterTarget(Key,bRequired)) { Enemy->Destroy(); return false; }
    if (!Reused) Enemy->FinishSpawning(Transform);
    if (PoolEnabled && !Reused)
    {
        if (EnemyPool.CountClass(Class)<Profile->NormalPoolQuota(Class)
            && EnemyPool.Adopt(Enemy, RoomId, Profile->NormalPoolCapacity)) Enemy->EnableDungeonPooling(Profile->NormalCorpseSeconds);
        else ++PoolOverflow;
        PoolPeak = FMath::Max(PoolPeak, EnemyPool.Entries.Num());
    }
    auto* ASC=Enemy->GetAbilitySystemComponent(); if (!ASC || !ASC->GetSet<UMyAttributeSet>()) return false;
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),Health); ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),Health);
    (Reused ? ReuseTiming : CreateTiming).Add((FPlatformTime::Seconds()-Started)*1000.0);
    LastSafeLocations.Add(Enemy,Enemy->GetActorLocation());
    if (bIsBoss) { Boss=Enemy; NextBossPulse=GetWorld()->GetTimeSeconds()+4; }
    return true;
}
AEnemyCharacter* ADungeonRunDirector::SpawnSummonedEnemy(AEnemyCharacter* Summoner,TSubclassOf<AEnemyCharacter> Class,const FTransform& Transform)
{
    if(!HasAuthority() || Phase!=EDungeonRunPhase::Encounter || !IsValid(Summoner) || Summoner->IsDead_Implementation() ||
        !ActorTargets.Contains(Summoner) || !Class || ActorTargets.Num()+MassTargets.Num()+SpawnQueue.Num()>=Profile->EncounterCapacity() ||
        GetRoomIndex(Transform.GetLocation())!=ActiveRoom || GetRoomIndex(Summoner->GetActorLocation())!=ActiveRoom) return nullptr;
    auto* Minion=GetWorld()->SpawnActorDeferred<AEnemyCharacter>(Class,Transform,Summoner,nullptr,ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
    if(!Minion) return nullptr;
    const FName Key(*FString::Printf(TEXT("A%d"),++ActorSequence));
    if(!Model.Encounter.RegisterTarget(Key,false)){Minion->Destroy();return nullptr;}
    Minion->bDungeonEncounter = true;
    Minion->Tags.AddUnique(TEXT("DungeonEncounter"));Minion->Tags.AddUnique(TEXT("SourceOwnedSummon"));
    Minion->MonsterRoomId=Summoner->MonsterRoomId;
    Minion->SummoningSource=Summoner;
    ActorTargets.Add(Minion,Key);OptionalActors.Add(Minion);
    Minion->FOnDeath.AddUniqueDynamic(this,&ADungeonRunDirector::OnActorDied);
    Minion->OnDestroyed.AddUniqueDynamic(this,&ADungeonRunDirector::OnActorDestroyed);
    Minion->FinishSpawning(Transform);
    LastSafeLocations.Add(Minion,Minion->GetActorLocation());
    return Minion;
}
void ADungeonRunDirector::RetireSummonedEnemy(AEnemyCharacter* Summoner,AEnemyCharacter* Minion)
{
    if(!HasAuthority() || !IsValid(Summoner) || !IsValid(Minion) || Minion->SummoningSource.Get()!=Summoner || !Minion->ActorHasTag(TEXT("SourceOwnedSummon"))) return;
    FName Key;
    if(ActorTargets.RemoveAndCopyValue(Minion,Key) && !bCleaningUp) Model.Encounter.ResolveTarget(Key,EDungeonTargetResolution::Recycled);
    OptionalActors.Remove(Minion);LastSafeLocations.Remove(Minion);
    Minion->Destroy();
}
void ADungeonRunDirector::OnMassRemoved(int32 Id, FName Room, EPureMassRemovalReason Reason)
{
    FName Key; if (!MassTargets.RemoveAndCopyValue(Id,Key)) return; OptionalMass.Remove(Id);
    if (!bCleaningUp) Model.Encounter.ResolveTarget(Key,Reason==EPureMassRemovalReason::Killed?EDungeonTargetResolution::Killed
        :Reason==EPureMassRemovalReason::Recycled?EDungeonTargetResolution::Recycled:EDungeonTargetResolution::Unexpected);
}
void ADungeonRunDirector::OnActorDied(AActor* Actor)
{
    FName Key; if (!ActorTargets.RemoveAndCopyValue(Actor,Key)) return; OptionalActors.Remove(Actor);
    if (!bCleaningUp) Model.Encounter.ResolveTarget(Key,EDungeonTargetResolution::Killed);
    LastSafeLocations.Remove(Actor);
    if (auto* Enemy = Cast<AEnemyCharacter>(Actor); Enemy && Enemy->ActorHasTag(TEXT("DungeonPoolManaged"))) PendingPoolReturns.AddUnique(Enemy);
    if (Actor==Boss) { bBossDead=true; bPulseWarning=false; }
}
void ADungeonRunDirector::OnActorDestroyed(AActor* Actor)
{
    FName Key; if (!ActorTargets.RemoveAndCopyValue(Actor,Key)) return; OptionalActors.Remove(Actor);
    if (!bCleaningUp) Model.Encounter.ResolveTarget(Key,EDungeonTargetResolution::Unexpected);
}
void ADungeonRunDirector::UpdateBoss()
{
    if (!Boss) return;
    if (bBossDead)
    {
        for (int32 Id:OptionalMass.Array()) Mass->RemoveMonster(Id,EPureMassRemovalReason::Recycled);
        for (auto Weak:OptionalActors.Array()) if (auto* Actor=Weak.Get())
        {
            FName Key; if (ActorTargets.RemoveAndCopyValue(Actor,Key)) Model.Encounter.ResolveTarget(Key,EDungeonTargetResolution::Recycled);
            OptionalActors.Remove(Actor); ReturnEnemyToPool(Cast<AEnemyCharacter>(Actor));
        }
        return;
    }
    // Grux owns its complete warned GAS moveset. Keep the encounter/death cleanup
    // above, but do not layer the legacy pulse or half-health summon on that moveset.
    if (Boss->IsA<AGruxEnemyCharacter>()) return;
    auto* ASC=Boss->GetAbilitySystemComponent(); if (!ASC) return; const double Now=GetWorld()->GetTimeSeconds();
    if (!bBossSummoned && ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute())<=Profile->BossHealth*.5f)
    { bBossSummoned=true; if (!SpawnActorGroup(2,true)) { Model.Encounter.Error=TEXT("BossSummonFailed"); return; } }
    if (bPulseWarning && Now>=PulseImpactAt)
    {
        bPulseWarning=false; NextBossPulse=Now+4;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (APawn* Pawn=It->Get()->GetPawn(); AllowsCombat(Boss,Pawn) && FVector::DistSquared2D(Pawn->GetActorLocation(),PulseCenter)<=FMath::Square(Profile->BossPulseRadius))
                if (auto* TargetASC=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn))
                {
                    auto Context=ASC->MakeEffectContext(); Context.AddSourceObject(Boss);
                    auto Spec=ASC->MakeOutgoingSpec(UPureMassIncomingDamageEffect::StaticClass(),1,Context);
                    if (Spec.IsValid()) { Spec.Data->SetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(),Profile->BossPulseDamage); ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data,TargetASC); }
                }
    }
    else if (!bPulseWarning && Now>=NextBossPulse) { PulseCenter=Boss->GetActorLocation(); PulseCenter.Z=0; bPulseWarning=true; PulseImpactAt=Now+1.25; ForceNetUpdate(); }
}

void ADungeonRunDirector::CompleteEncounter()
{
    const int32 Room=Model.FinishEncounter(); if (Room==INDEX_NONE) return;
    const auto RoomRole=Model.Rooms[Room].Role;
    const int32 Coins=RoomRole==EDungeonRoomRole::Boss?Profile->BossCoins:RoomRole==EDungeonRoomRole::Elite?Profile->EliteCoins:Profile->CombatCoins;
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (auto* PC=Cast<ADungeonRunController>(It->Get()))
        {
            if (auto* PS=PC->GetPlayerState<AMyPlayerState>()) Economy->CreditClear(PS->GetPlayerId(),Model.Rooms[Room].RoomId,Coins);
            if (auto* Pawn=Cast<ADungeonPlayerCharacter>(PC->GetPawn()); Pawn && Pawn->IsDungeonDowned())
            {
                FVector P;
                if (FindSpawnPosition(Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius(),Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),P,Room)
                    && Pawn->ReviveForDungeon(P,Profile->ReviveFraction)) LastSafeLocations.Add(Pawn,P);
                else { FailRun(TEXT("PlayerRevivalFailed")); return; }
            }
        }
    CleanupEncounter(); Mass->ClearRoomBounds(Model.Rooms[Room].RoomId);
    const bool Won=Model.Rooms.ContainsByPredicate([](const FDungeonRoomProgress& R) { return R.Role==EDungeonRoomRole::Boss && R.Phase==EDungeonRoomPhase::Cleared; });
    Phase=Won?EDungeonRunPhase::Victory:EDungeonRunPhase::Exploring; SynchronizeState(); UpdatePlayerViews(); ForceNetUpdate();
}
void ADungeonRunDirector::CleanupEncounter()
{
    SpawnQueue.Reset();
    TGuardValue<bool> Guard(bCleaningUp,true); TArray<int32> Ids; MassTargets.GetKeys(Ids);
    if (Mass) for (int32 Id:Ids) Mass->RemoveMonster(Id,EPureMassRemovalReason::Recycled);
    TArray<TWeakObjectPtr<AActor>> Actors; ActorTargets.GetKeys(Actors); for (auto Weak:Actors) if (auto* Actor=Weak.Get()) ReturnEnemyToPool(Cast<AEnemyCharacter>(Actor));
    DrainPoolReturns();
    // No effect from a finished encounter may retain a previous lease.
    for (auto It=EffectRooms.CreateIterator(); It; ++It) if (auto* Effect=It.Key().Get()) { Effect->Destroy(); It.RemoveCurrent(); }
    MassTargets.Reset(); OptionalMass.Reset(); OptionalActors.Reset(); ActorTargets.Reset(); Boss=nullptr; bPulseWarning=false;
}
void ADungeonRunDirector::FailRun(const FString& Reason)
{
    if (!HasAuthority() || Phase==EDungeonRunPhase::Failed) return;
    FailureReason=Reason.IsEmpty()?TEXT("RunFailed"):Reason; Model.Fail(FailureReason); Phase=EDungeonRunPhase::Failed;
    if (Mass && Model.Rooms.IsValidIndex(Model.ActiveRoom)) Mass->ClearRoomBounds(Model.Rooms[Model.ActiveRoom].RoomId);
    CleanupEncounter(); SynchronizeState(); UpdatePlayerViews(); ForceNetUpdate(); UE_LOG(LogTemp,Warning,TEXT("DungeonRun Failed: %s"),*FailureReason);
}
void ADungeonRunDirector::NotifyPlayerDown(ADungeonPlayerCharacter* Player)
{
    // Death delegates may execute during Mass/GAS processing: progression stays in Tick.
    if (HasAuthority() && Player && Player->GetWorld()==GetWorld()) NextViewsAt=0;
}
void ADungeonRunDirector::TrackEffect(AActor* Actor)
{
    if (Actor && (Actor->IsA<AMyProjectile>() || Actor->IsA<APrototypeRuntime>() || Actor->IsA<APureMassRuntime>())) EffectRooms.Add(Actor,GetRoomIndex(Actor->GetActorLocation()));
}
void ADungeonRunDirector::EnforceBoundaries()
{
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (auto* Pawn=Cast<ADungeonPlayerCharacter>(It->Get()->GetPawn()); Living(Pawn))
        {
            FVector* Last=LastSafeLocations.Find(Pawn);
            if (!Last)
            {
                FVector Entrance=GetRoomCenter(0)+FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5);
                LastSafeLocations.Add(Pawn,GetRoomIndex(Pawn->GetActorLocation())==0?Pawn->GetActorLocation():Entrance); Last=LastSafeLocations.Find(Pawn);
            }
            if (!Model.CanMove(*Last,Pawn->GetActorLocation(),Generation->Manifest.RoomSpacing))
            { Pawn->SetActorLocation(*Last,false,nullptr,ETeleportType::TeleportPhysics); Pawn->GetCharacterMovement()->StopMovementImmediately(); }
            else *Last=Pawn->GetActorLocation();
        }
    for (const auto& Pair:ActorTargets) if (auto* Enemy=Cast<AEnemyCharacter>(Pair.Key.Get()))
    {
        FVector D=Enemy->GetActorLocation()-GetRoomCenter(Model.ActiveRoom);
        if (D.ContainsNaN() || FMath::Abs(D.X)>1050 || FMath::Abs(D.Y)>1050 || FMath::Abs(D.Z)>500)
        { Enemy->SetActorLocation(LastSafeLocations.FindRef(Enemy),false,nullptr,ETeleportType::TeleportPhysics); Enemy->GetCharacterMovement()->StopMovementImmediately(); }
        else LastSafeLocations.Add(Enemy,Enemy->GetActorLocation());
    }
    for (auto It=EffectRooms.CreateIterator(); It; ++It)
    {
        auto* Effect=It.Key().Get(); if (!Effect) { It.RemoveCurrent(); continue; }
        if (It.Value()==INDEX_NONE || GetRoomIndex(Effect->GetActorLocation())!=It.Value()) { Effect->Destroy(); It.RemoveCurrent(); }
    }
}
void ADungeonRunDirector::EndPlay(EEndPlayReason::Type Reason)
{
    ExportPerformanceReport();
    if (EnemyLoadHandle.IsValid()) { EnemyLoadHandle->CancelHandle(); EnemyLoadHandle.Reset(); }
    GetWorldTimerManager().ClearTimer(ReturnTimer); if (EffectSpawnHandle.IsValid()) GetWorld()->RemoveOnActorSpawnedHandler(EffectSpawnHandle);
    if (Mass) { Mass->OnMonsterRemoved.Remove(MassRemovedHandle); CleanupEncounter(); for (const auto& R:Rooms) Mass->ClearRoomBounds(R.RoomId); }
    for (const auto& Entry : EnemyPool.Entries) if (auto* Enemy = Entry.Actor.Get())
    {
        auto* Controller = Enemy->GetController();
        Enemy->Destroy();
        if (Controller) Controller->Destroy();
    }
    EnemyPool.Entries.Reset(); PendingPoolReturns.Reset();
    for (const auto& C:GameplayDoors) if (IsValid(C)) C->DestroyComponent(); GameplayDoors.Reset();
    for (const auto& C:Markers) if (IsValid(C)) C->DestroyComponent(); Markers.Reset();
    if (PulseMesh) PulseMesh->DestroyComponent(); PulseMesh=nullptr;
    EffectRooms.Reset(); LastSafeLocations.Reset(); Super::EndPlay(Reason);
}
