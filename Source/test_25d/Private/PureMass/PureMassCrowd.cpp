#include "PureMass/PureMassCrowd.h"
#include "PureMassMovement.h"
#include "Performance/MonsterPerf.h"
#include "PureMass/PureMassSubsystem.h"
#include "Animation/AnimBank.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogPureMassCrowd, Log, All);

namespace
{
    bool IsPureMassAsset(const UObject* Asset)
    {
        return Asset && Asset->GetOutermost()->GetName().StartsWith(TEXT("/Game/PureMass/"));
    }

    FVector QuantizeVector(const FVector& Vector)
    {
        return FVector(FMath::RoundToDouble(Vector.X * 10.0) * 0.1,
            FMath::RoundToDouble(Vector.Y * 10.0) * 0.1,
            FMath::RoundToDouble(Vector.Z * 10.0) * 0.1);
    }
}

FPureMassView FPureMassNetEntity::ToView() const
{
    FPureMassView View;
    View.Id = Id;
    View.Location = Location;
    View.Velocity = Velocity;
    View.Health = Health;
    View.MaxHealth = MaxHealth;
    View.bPetrified = bPetrified; View.State = State;
    View.Type = Type;
    return View;
}

bool FPureMassNetEntity::SetFromView(const FPureMassView& View)
{
    const FVector NewLocation = QuantizeVector(View.Location);
    const FVector NewVelocity = QuantizeVector(View.Velocity);
    const bool bChanged = Id != View.Id || Location != NewLocation || Velocity != NewVelocity
        || Health != View.Health || MaxHealth != View.MaxHealth || State != View.State || bPetrified != View.bPetrified || Type != View.Type;
    Id = View.Id;
    Location = NewLocation;
    Velocity = NewVelocity;
    Health = View.Health;
    MaxHealth = View.MaxHealth;
    bPetrified = View.bPetrified; State = View.State;
    Type = View.Type;
    return bChanged;
}

void FPureMassNetEntities::PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
    // Also called when an initially unmapped type asset becomes available.
    if (APureMassCrowd* Crowd = Owner.Get()) Crowd->ApplyReplicatedSnapshot();
}

APureMassCrowd::APureMassCrowd()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    SetNetUpdateFrequency(60.f);
    SetMinNetUpdateFrequency(60.f);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CrowdRoot"));
    NetEntities.Owner = this;
}

void APureMassCrowd::BeginPlay()
{
    Super::BeginPlay();
    NetEntities.Owner = this;
    // Initial property replication can precede BeginPlay.
    if (!HasAuthority()) ApplyReplicatedSnapshot();
}

void APureMassCrowd::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APureMassCrowd, NetEntities);
}

void APureMassCrowd::Tick(float DeltaSeconds)
{
    MONSTER_PERF_SCOPE(CrowdTick);
    Super::Tick(DeltaSeconds);
    if (HasAuthority())
    {
        // One bounded RPC per frame, emitted during normal world ticking even when
        // the lethal damage originated from an editor/Python callback.
        if (!PendingDeaths.IsEmpty())
        {
            TArray<FPureMassDeathPresentation> Deaths = MoveTemp(PendingDeaths);
            PendingDeaths.Reset();
            MulticastDeaths(Deaths);
        }
        UPureMassSubsystem* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>();
        if (!Mass) return;
        TimeUntilPublish -= DeltaSeconds;
        // Dedicated servers only collect copies when a network update is due.
        if (GetNetMode() != NM_DedicatedServer || TimeUntilPublish <= 0.f)
        {
            const TArray<FPureMassView> Views = Mass->GetSnapshots();
            if (TimeUntilPublish <= 0.f)
            {
                PublishSnapshots(Views);
                // Preserve fractional remainder rather than slipping to every second frame near 60 FPS.
                TimeUntilPublish = FMath::Max(0.f, TimeUntilPublish + SnapshotInterval);
            }
            if (GetNetMode() != NM_DedicatedServer) ReconcileInstances(Views);
        }
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        UpdateTransforms(DeltaSeconds);
        for (int32 Index = 0; Index < DeathMeshes.Num(); ++Index)
            if (DeathExpiry[Index] > 0.f && GetWorld()->GetTimeSeconds() >= DeathExpiry[Index])
            {
                DeathMeshes[Index]->SetSimulatePhysics(false);
                DeathMeshes[Index]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                DeathMeshes[Index]->SetVisibility(false);
                DeathMeshes[Index]->SetComponentTickEnabled(false);
                if (DeathWeapons.IsValidIndex(Index) && DeathWeapons[Index]) DeathWeapons[Index]->SetVisibility(false);
                DeathExpiry[Index] = 0.f;
            }
    }
}

void APureMassCrowd::PublishSnapshots(const TArray<FPureMassView>& Views)
{
    MONSTER_PERF_SCOPE(NetSnapshotPublish);
    TSet<int32> Living;
    Living.Reserve(Views.Num());
    for (const FPureMassView& View : Views)
        if (View.Id != INDEX_NONE && View.Health > 0.f && View.State != 3) Living.Add(View.Id);

    bool bChanged = false;
    for (int32 Index = NetEntities.Items.Num() - 1; Index >= 0; --Index)
    {
        if (!Living.Contains(NetEntities.Items[Index].Id))
        {
            NetEntities.Items.RemoveAtSwap(Index);
            NetEntities.MarkArrayDirty();
            bChanged = true;
        }
    }
    TMap<int32, int32> Indices;
    Indices.Reserve(NetEntities.Items.Num());
    for (int32 Index = 0; Index < NetEntities.Items.Num(); ++Index)
        Indices.Add(NetEntities.Items[Index].Id, Index);

    for (const FPureMassView& View : Views)
    {
        if (!Living.Contains(View.Id)) continue;
        int32* Existing = Indices.Find(View.Id);
        const int32 Index = Existing ? *Existing : NetEntities.Items.AddDefaulted();
        FPureMassNetEntity& Item = NetEntities.Items[Index];
        if (Item.SetFromView(View))
        {
            NetEntities.MarkItemDirty(Item);
            bChanged = true;
        }
        if (!Existing) Indices.Add(View.Id, Index);
    }
    if (bChanged) ForceNetUpdate();
}

void APureMassCrowd::ApplyReplicatedSnapshot()
{
    if (HasAuthority() || !GetWorld()) return;
    TArray<FPureMassView> Views;
    Views.Reserve(NetEntities.Items.Num());
    for (const FPureMassNetEntity& Item : NetEntities.Items)
        if (Item.Id != INDEX_NONE && Item.Type && Item.Health > 0.f && Item.State != 3) Views.Add(Item.ToView());
    if (UPureMassSubsystem* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>()) Mass->SetClientSnapshots(Views);
    if (GetNetMode() != NM_DedicatedServer) ReconcileInstances(Views);
}

int32 APureMassCrowd::FindOrCreateBatch(UPureMassMonsterType* Type, bool bPetrified)
{
    for (int32 Index = 0; Index < RenderBatches.Num(); ++Index)
        if (RenderBatches[Index].Type == Type && RenderBatches[Index].bPetrified == bPetrified) return Index;
    if (!Type || RejectedTypes.Contains(Type)) return INDEX_NONE;

    // Registering original meshes can trigger material usage edits in the editor.
    // Rendering is deliberately restricted to the new cloned asset tree.
    bool bValid = IsPureMassAsset(Type->Mesh) && IsPureMassAsset(Type->AnimationProvider);
    if (bValid)
    {
        bValid = Type->AnimationProvider->GetSkinnedAsset() == Type->Mesh
            && !Type->AnimationProvider->GetLayers().IsEmpty()
            && Type->AnimationProvider->GetSequences().IsValidIndex(Type->IdleAnimation)
            && Type->AnimationProvider->GetSequences().IsValidIndex(Type->RunAnimation)
            && Type->AnimationProvider->GetSequences().IsValidIndex(Type->AttackAnimation);
        for (const FSkeletalMaterial& Material : Type->Mesh->GetMaterials())
            bValid &= IsPureMassAsset(Material.MaterialInterface);
    }
    if (!bValid)
    {
        RejectedTypes.Add(Type);
        UE_LOG(LogPureMassCrowd, Error, TEXT("Cannot render %s: use matching /Game/PureMass/ cloned mesh, materials and animation provider with valid state indices."), *GetNameSafe(Type));
        return INDEX_NONE;
    }

    if (bPetrified && !PetrifyMaterial)
    {
        PetrifyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CompositionBatch6/M_PetrifiedStone.M_PetrifiedStone"));
        if (!PetrifyMaterial) PetrifyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
    }
    FPureMassRenderBatch Batch;
    Batch.Type = Type;
    Batch.bPetrified = bPetrified;
    Batch.Component = NewObject<UInstancedSkinnedMeshComponent>(this);
    // UE 5.8 exposes this as an editor property, but has no public setter and
    // does not export all virtuals for subclassing. Configure before registration.
    if (FBoolProperty* Sockets = FindFProperty<FBoolProperty>(UInstancedSkinnedMeshComponent::StaticClass(),TEXT("bForceAnimateSockets")))
        Sockets->SetPropertyValue_InContainer(Batch.Component,true);
    Batch.Component->SetupAttachment(RootComponent);
    Batch.Component->SetMobility(EComponentMobility::Movable);
    Batch.Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Batch.Component->SetGenerateOverlapEvents(false);
    Batch.Component->SetCanEverAffectNavigation(false);
    Batch.Component->SetSkinnedAssetAndUpdate(Type->Mesh);
    Batch.Component->SetAnimationMinScreenSize(-1.f);
    if (bPetrified && PetrifyMaterial) for (int32 Slot = 0; Slot < Type->Mesh->GetMaterials().Num(); ++Slot) Batch.Component->SetMaterial(Slot, PetrifyMaterial);
    Batch.Playback = UAnimSequenceTransformProviderDataInstance::CreateAnimSequenceTransformProviderDataInstance(
        Type->AnimationProvider, Batch.Component);
    if (!Batch.Playback)
    {
        Batch.Component->DestroyComponent();
        RejectedTypes.Add(Type);
        return INDEX_NONE;
    }
    Batch.Component->SetTransformProvider(Batch.Playback);
    AddInstanceComponent(Batch.Component);
    Batch.Component->RegisterComponent();
    if (Type->WeaponMesh && IsPureMassAsset(Type->WeaponMesh) && Type->Mesh->FindSocket(Type->WeaponSocket))
    {
        Batch.Weapons = NewObject<UInstancedSkinnedMeshComponent>(this);
        Batch.Weapons->SetupAttachment(RootComponent);
        Batch.Weapons->SetSkinnedAssetAndUpdate(Type->WeaponMesh);
        Batch.Weapons->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch.Weapons->SetGenerateOverlapEvents(false);
        Batch.Weapons->SetCanEverAffectNavigation(false);
        if (bPetrified && PetrifyMaterial)
            for (int32 Slot=0; Slot<Type->WeaponMesh->GetMaterials().Num(); ++Slot) Batch.Weapons->SetMaterial(Slot,PetrifyMaterial);
        AddInstanceComponent(Batch.Weapons);
        Batch.Weapons->RegisterComponent();
    }
    return RenderBatches.Add(Batch);
}

void APureMassCrowd::SetAnimation(FPureMassRenderInstance& Instance, uint8 State)
{
    if (Instance.State == State) return;
    const FPureMassRenderBatch& Batch = RenderBatches[Instance.BatchIndex];
    const bool bWindupClip = Batch.Type->AttackContactTime>0.f && Batch.Type->AttackWindupSeconds>0.f;
    // The warning and impact are phases of one clip, not two animation restarts.
    if (bWindupClip && Instance.State==4 && State==2) { Instance.State=State; return; }
    const bool bAttack = State==2 || (State==4 && bWindupClip);
    FAnimSequenceTrackAutoPlayData Play;
    Play.SequenceIndex = bAttack ? Batch.Type->AttackAnimation : State == 1 ? Batch.Type->RunAnimation : Batch.Type->IdleAnimation;
    Play.Position = State==2 && bWindupClip ? Batch.Type->AttackContactTime : 0.f;
    Play.PlayRate = Batch.bPetrified ? 0.f : bAttack ? Batch.Type->AttackPlayRate() : 1.f;
    Play.BlendTime = Instance.State == 255 ? 0.f : 0.1f;
    Play.LoopMode = bAttack ? EAnimSequenceTrackLoopMode::Clamp : EAnimSequenceTrackLoopMode::Loop;
    Batch.Playback->SetAutoPlayData(Instance.Track, 0, Play);
    Instance.State = State;
}

void APureMassCrowd::ReconcileInstances(const TArray<FPureMassView>& Views)
{
    MONSTER_PERF_SCOPE(InstanceReconcile);
    TSet<int32> Present;
    Present.Reserve(Views.Num());
    bool bTopologyChanged = false;
    for (const FPureMassView& View : Views)
    {
        if (View.Id == INDEX_NONE || !View.Type || View.Health <= 0.f || View.State == 3) continue;
        const int32 BatchIndex = FindOrCreateBatch(View.Type, View.bPetrified);
        if (BatchIndex == INDEX_NONE) continue;
        Present.Add(View.Id);
        FPureMassRenderInstance* Existing = RenderInstances.Find(View.Id);
        if (Existing && Existing->BatchIndex != BatchIndex)
        {
            bTopologyChanged = true;
            RemoveRenderInstance(*Existing);
            RenderInstances.Remove(View.Id);
            Existing = nullptr;
        }
        if (!Existing)
        {
            bTopologyChanged = true;
            FPureMassRenderInstance NewInstance;
            NewInstance.BatchIndex = BatchIndex;
            NewInstance.Track = RenderBatches[BatchIndex].Playback->AllocateTrack();
            if (NewInstance.Track == INDEX_NONE) continue;
            NewInstance.DisplayLocation = View.Location;
            NewInstance.StartLocation = View.Location;
            NewInstance.TargetLocation = View.Location;
            if (!View.Velocity.IsNearlyZero())
                NewInstance.Facing = FRotator(0.f, View.Velocity.Rotation().Yaw, 0.f).Quaternion();
            NewInstance.TargetFacing = NewInstance.Facing;
            SetAnimation(NewInstance, View.State);
            const FTransform WorldTransform = View.Type->MeshOffset * FTransform(NewInstance.Facing, View.Location);
            NewInstance.Instance = RenderBatches[BatchIndex].Component->AddInstance(WorldTransform, NewInstance.Track, true);
            if (!NewInstance.Instance.IsValid())
            {
                RenderBatches[BatchIndex].Playback->DeallocateTrack(NewInstance.Track);
                continue;
            }
            if (auto* Weapons = RenderBatches[BatchIndex].Weapons.Get())
            {
                // The reference-pose provider has exactly one valid animation slot.
                NewInstance.WeaponInstance=Weapons->AddInstance(View.Type->WeaponOffset,0,false);
            }
            Existing = &RenderInstances.Add(View.Id, NewInstance);
        }
        Existing->StartLocation = Existing->DisplayLocation;
        Existing->TargetLocation = View.Location;
        Existing->InterpolationAge = 0.f;
        if (View.Velocity.SizeSquared2D() > FMath::Square(10.f))
            Existing->TargetFacing = FRotator(0.f, View.Velocity.Rotation().Yaw, 0.f).Quaternion();
        SetAnimation(*Existing, View.State);
    }
    for (auto Iterator = RenderInstances.CreateIterator(); Iterator; ++Iterator)
    {
        if (!Present.Contains(Iterator.Key()))
        {
            bTopologyChanged = true;
            RemoveRenderInstance(Iterator.Value());
            Iterator.RemoveCurrent();
        }
    }
    if (bTopologyChanged)
    {
        // UE 5.8's attachment API consumes the parent's dense index despite its
        // FPrimitiveInstanceId parameter. Rebuild only after add/remove/swap.
        TArray<TMap<int32, int32>> DenseIndices;
        DenseIndices.SetNum(RenderBatches.Num());
        for (int32 BatchIndex=0; BatchIndex<RenderBatches.Num(); ++BatchIndex)
        {
            const auto* Body=RenderBatches[BatchIndex].Component.Get();
            for (int32 Index=0; Index<Body->GetInstanceCount(); ++Index)
                DenseIndices[BatchIndex].Add(Body->GetInstanceId(Index).GetAsIndex(),Index);
        }
        for (const auto& Entry : RenderInstances)
        {
            const auto& Instance=Entry.Value;
            const auto& Batch=RenderBatches[Instance.BatchIndex];
            if (!Batch.Weapons || !Instance.WeaponInstance.IsValid()) continue;
            FPrimitiveInstanceId ParentIndex;
            ParentIndex.Id=DenseIndices[Instance.BatchIndex].FindChecked(Instance.Instance.GetAsIndex());
            Batch.Weapons->SetInstanceBoneAttachment(Instance.WeaponInstance,ParentIndex,Batch.Component,Batch.Type->WeaponSocket);
        }
    }
}

void APureMassCrowd::UpdateTransforms(float DeltaSeconds)
{
    MONSTER_PERF_SCOPE(InstanceTransforms);
    if (WarningInstances) WarningInstances->ClearInstances();
    for (TPair<int32, FPureMassRenderInstance>& Entry : RenderInstances)
    {
        FPureMassRenderInstance& Instance = Entry.Value;
        Instance.Facing = PureMassMovement::Turn(Instance.Facing, Instance.TargetFacing, DeltaSeconds);
        Instance.InterpolationAge += DeltaSeconds;
        const float Alpha = HasAuthority() ? 1.f : FMath::Clamp(Instance.InterpolationAge / SnapshotInterval, 0.f, 1.f);
        Instance.DisplayLocation = FMath::Lerp(Instance.StartLocation, Instance.TargetLocation, Alpha);
        const FPureMassRenderBatch& Batch = RenderBatches[Instance.BatchIndex];
        const FTransform WorldTransform = Batch.Type->MeshOffset * FTransform(Instance.Facing, Instance.DisplayLocation);
        Batch.Component->SetInstanceTransform(Instance.Instance, WorldTransform, true);
        if (Instance.State == 4 && !Batch.bPetrified)
        {
            if (!WarningInstances)
            {
                WarningInstances = NewObject<UInstancedStaticMeshComponent>(this);
                WarningInstances->SetupAttachment(RootComponent);
                WarningInstances->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
                WarningInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                WarningInstances->SetCanEverAffectNavigation(false);
                AddInstanceComponent(WarningInstances);
                WarningInstances->RegisterComponent();
            }
            // One shared instanced component, two pieces forming an overhead exclamation mark.
            const FVector Top = Instance.DisplayLocation + FVector(0, 0, 130);
            WarningInstances->AddInstance(FTransform(FQuat::Identity, Top, FVector(.07, .07, .30)), true);
            WarningInstances->AddInstance(FTransform(FQuat::Identity, Top - FVector(0, 0, 24), FVector(.08)), true);
        }
    }
}

void APureMassCrowd::RemoveRenderInstance(FPureMassRenderInstance& Instance)
{
    if (!RenderBatches.IsValidIndex(Instance.BatchIndex)) return;
    const FPureMassRenderBatch& Batch = RenderBatches[Instance.BatchIndex];
    if (Batch.Weapons && Instance.WeaponInstance.IsValid()) Batch.Weapons->RemoveInstance(Instance.WeaponInstance);
    if (Batch.Component) Batch.Component->RemoveInstance(Instance.Instance);
    if (Batch.Playback && Instance.Track != INDEX_NONE) Batch.Playback->DeallocateTrack(Instance.Track);
    Instance.Track = INDEX_NONE;
}

void APureMassCrowd::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    for (TPair<int32, FPureMassRenderInstance>& Entry : RenderInstances) RemoveRenderInstance(Entry.Value);
    RenderInstances.Empty();
    for (FPureMassRenderBatch& Batch : RenderBatches)
    {
        if (Batch.Weapons) Batch.Weapons->DestroyComponent();
        if (Batch.Component) Batch.Component->DestroyComponent();
    }
    RenderBatches.Empty();
    if (!HasAuthority() && GetWorld())
        if (UPureMassSubsystem* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>()) Mass->SetClientSnapshots({});
    PendingDeaths.Reset();
    NetEntities.Owner.Reset();
    Super::EndPlay(EndPlayReason);
}

int32 APureMassCrowd::GetRenderedInstanceCount() const
{
    int32 Count = 0;
    for (const FPureMassRenderBatch& Batch : RenderBatches)
        if (Batch.Component) Count += Batch.Component->GetInstanceCount();
    return Count;
}

int32 APureMassCrowd::GetReplicatedEntityCount() const { return NetEntities.Items.Num(); }
int32 APureMassCrowd::GetRenderComponentCount() const { return RenderBatches.Num(); }
int32 APureMassCrowd::GetAnimationTrackCount() const { return RenderInstances.Num(); }
int32 APureMassCrowd::GetWeaponInstanceCount() const
{
    int32 Count=0;
    for (const auto& Batch:RenderBatches) if (Batch.Weapons) Count+=Batch.Weapons->GetInstanceCount();
    return Count;
}

UAnimSequenceTransformProviderData* APureMassCrowd::CreateAnimationProvider(UAnimBank* Bank)
{
#if WITH_EDITOR
    if (!IsPureMassAsset(Bank) || !IsPureMassAsset(Bank->Asset)) return nullptr;
    const FString NewName = Bank->GetName() + TEXT("_ASTP");
    const FString NewPackage = FPackageName::GetLongPackagePath(Bank->GetOutermost()->GetName()) / NewName;
    // The engine helper does not reliably check an existing on-disk asset. Never overwrite one.
    if (FPackageName::DoesPackageExist(NewPackage) || FindObject<UPackage>(nullptr, *NewPackage)) return nullptr;
    UAnimSequenceTransformProviderData* Provider = UAnimSequenceTransformProviderData::CreateFromAnimBank(Bank);
    if (Provider) Provider->PostEditChange();
    return Provider;
#else
    return nullptr;
#endif
}

void APureMassCrowd::NotifyKilled(const FPureMassView& View, FVector DeathImpulse)
{
    if (!HasAuthority() || !View.Type || PendingDeaths.Num() >= 12) return;
    FPureMassDeathPresentation& Death = PendingDeaths.AddDefaulted_GetRef();
    Death.Type = View.Type;
    Death.Location = View.Location;
    Death.Yaw = View.Velocity.Rotation().Yaw;
    // Keep malformed cosmetic input away from network quantization and physics.
    Death.DeathImpulse = DeathImpulse.ContainsNaN() ? FVector::ZeroVector : DeathImpulse.GetClampedToMaxSize(100000.f);
}

void APureMassCrowd::MulticastDeaths_Implementation(const TArray<FPureMassDeathPresentation>& Deaths)
{
    for (int32 Index = 0; Index < FMath::Min(Deaths.Num(), 12); ++Index)
        PresentDeath(Deaths[Index].Type, Deaths[Index].Location, Deaths[Index].Yaw, Deaths[Index].DeathImpulse);
}

void APureMassCrowd::PresentDeath(UPureMassMonsterType* Type, FVector_NetQuantize10 Location, float Yaw, FVector DeathImpulse)
{
    if (GetNetMode() == NM_DedicatedServer || !Type || !Type->Mesh || !Type->Mesh->GetPhysicsAsset()) return;
    int32 Slot = DeathExpiry.IndexOfByKey(0.f);
    if (Slot == INDEX_NONE)
    {
        if (DeathMeshes.Num() >= 12) return; // Cosmetic overload drops, never allocates unbounded bodies.
        USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(this);
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetGenerateOverlapEvents(false);
        AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
        Slot = DeathMeshes.Add(Mesh);
        DeathWeapons.Add(nullptr);
        DeathExpiry.Add(0.f);
    }
    USkeletalMeshComponent* Mesh = DeathMeshes[Slot];
    Mesh->SetSimulatePhysics(false);
    Mesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
    Mesh->SetSkeletalMesh(Type->Mesh);
    Mesh->SetWorldTransform(Type->MeshOffset * FTransform(FRotator(0, Yaw, 0), Location));
    Mesh->SetCollisionObjectType(ECC_PhysicsBody);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Mesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    Mesh->SetVisibility(true);
    Mesh->SetComponentTickEnabled(true);
    Mesh->SetSimulatePhysics(true);
    Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
    Mesh->SetAllPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    Mesh->WakeAllRigidBodies();
    if (Type->WeaponMesh)
    {
        if (!DeathWeapons[Slot])
        {
            auto* Weapon=NewObject<USkeletalMeshComponent>(this);
            Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Weapon->SetCanEverAffectNavigation(false);
            AddInstanceComponent(Weapon); Weapon->RegisterComponent(); DeathWeapons[Slot]=Weapon;
        }
        auto* Weapon=DeathWeapons[Slot].Get();
        Weapon->AttachToComponent(Mesh,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Type->WeaponSocket);
        Weapon->SetRelativeTransform(Type->WeaponOffset);
        Weapon->SetSkeletalMesh(Type->WeaponMesh); Weapon->SetVisibility(true);
    }
    else if (DeathWeapons[Slot]) DeathWeapons[Slot]->SetVisibility(false);
    if (!DeathImpulse.ContainsNaN() && !DeathImpulse.IsNearlyZero())
        Mesh->AddImpulse(DeathImpulse, NAME_None, true);
    DeathExpiry[Slot] = GetWorld()->GetTimeSeconds() + 1.5f;
}

int32 APureMassCrowd::GetActiveDeathCount() const
{
    int32 Count = 0;
    for (float Expiry : DeathExpiry) if (Expiry > 0.f) ++Count;
    return Count;
}

int32 APureMassCrowd::GetWarningCount() const
{
    return WarningInstances ? WarningInstances->GetInstanceCount() / 2 : 0;
}
