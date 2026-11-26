// 调用链：注册地表节点 -> 校验来源生命并生成地形类 -> 原 Actor/Mass 接触、元素反应与复制。
// 转换先保留旧地表再生成关闭接触的新地表，确认替换物可用后才停止旧触发。
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"

#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Interface/CombatInterface.h"
#include "NiagaraComponent.h"
#include "Terrain/TerrainSurface.h"
#include "Terrain/WorkshopElementalSurfaces.h"

namespace
{
    constexpr int32 MaxActiveSurfacesPerSource = 4;
    constexpr double TerrainVerticalTolerance = 150.;
    constexpr double SameFloorHeightTolerance = 25.;
    const FVector AnchorLift(0., 0., 8.);

    /** 云源锚点抬高后检测静态遮挡，避免薄楼板内外因体积重叠而串层。 */
    bool HasClearCloudPath(const ATerrainSurface& Surface, const FVector& Sample)
    {
        FHitResult Barrier;
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(WorkshopCloudBarrier), false, &Surface);
        const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
        return !Surface.GetWorld()->LineTraceSingleByObjectType(
            Barrier, Surface.GetActorLocation() + AnchorLift, Sample, Objects, Query);
    }

    /** 地表半宽至少覆盖原 Actor 最小尺寸；所有生成、查询和时长都有独立上限。 */
    bool IsValidSize(float Size)
    {
        return FMath::IsFinite(Size) && Size >= 25.f && Size <= 800.f;
    }

    bool IsValidDuration(float Duration)
    {
        return FMath::IsFinite(Duration) && Duration >= .1f && Duration <= 20.f;
    }

    /** 旧地块使用原蓝图调参与表现。新的元素地块由原生类模板在各端重建相同外观。 */
    UClass* ResolveSurfaceClass(ETerrainSurfaceType Kind)
    {
        if (Kind == ETerrainSurfaceType::Water)
        {
            return AWorkshopWaterTerrainSurface::StaticClass();
        }
        if (Kind == ETerrainSurfaceType::Conductive)
        {
            return AWorkshopConductiveTerrainSurface::StaticClass();
        }
        if (Kind == ETerrainSurfaceType::MistCloud)
        {
            return AWorkshopMistCloudSurface::StaticClass();
        }
        if (Kind == ETerrainSurfaceType::ConductiveCloud)
        {
            return AWorkshopConductiveCloudSurface::StaticClass();
        }
        static const FSoftClassPath Paths[] = {
            FSoftClassPath(TEXT("/Game/BluePrints/Actor/Terrain/BP_Terrain_Lava.BP_Terrain_Lava_C")),
            FSoftClassPath(TEXT("/Game/BluePrints/Actor/Terrain/BP_Terrain_Frost.BP_Terrain_Frost_C")),
            FSoftClassPath(TEXT("/Game/BluePrints/Actor/Terrain/BP_Terrain_Toxic.BP_Terrain_Toxic_C")),
            FSoftClassPath(TEXT("/Game/BluePrints/Actor/Terrain/BP_Terrain_Vines.BP_Terrain_Vines_C"))};
        const uint8 Index = static_cast<uint8>(Kind);
        return Index < UE_ARRAY_COUNT(Paths) ? Paths[Index].TryLoadClass<ATerrainSurface>() : nullptr;
    }

    /** 事务准备阶段不开始接触。两端从同一地表类取外观，租约只留在服务器。 */
    ATerrainSurface* PrepareSurface(const FAbilitySourceToken& Source, ETerrainSurfaceType Kind,
                                    const FTransform& Transform, FVector2D Size, double Deadline)
    {
        AActor* Caster = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Source, Caster))
        {
            return nullptr;
        }
        UWorld* World = Caster->GetWorld();
        if (Deadline <= World->GetTimeSeconds())
        {
            return nullptr;
        }
        auto* SurfaceClass = ResolveSurfaceClass(Kind);
        const auto* Template = SurfaceClass ? SurfaceClass->GetDefaultObject<ATerrainSurface>() : nullptr;
        if (!Template || !Template->Config || Template->Config->SurfaceType != Kind)
        {
            return nullptr;
        }

        auto* Surface = World->SpawnActorDeferred<ATerrainSurface>(SurfaceClass, Transform, Caster, Cast<APawn>(Caster),
                                                                   ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Surface)
        {
            return nullptr;
        }
        Surface->bEnabled = false;
        Surface->bOnlyAffectCasterEnemies = true;
        Surface->bAffectPlayers = false;
        Surface->bAffectEnemies = true;
        Surface->SurfaceSize = Size;
        Surface->FinishSpawning(Transform);
        if (!IsValid(Surface) || Surface->IsActorBeingDestroyed())
        {
            return nullptr;
        }
        if (!Surface->Config || Surface->Config->SurfaceType != Kind ||
            !AbilityExecutionIdentity::ResolveSource(Source, Caster))
        {
            Surface->Destroy();
            return nullptr;
        }
        if (Surface->Ground)
        {
            Surface->Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        auto* Lease = NewObject<UAbilityWorkshopTerrainLease>(Surface);
        Surface->AddInstanceComponent(Lease);
        Lease->RegisterComponent();
        Lease->Initialize(Source, Deadline);
        return Surface;
    }

    /** 激活地块复制。原四类保留即时接触，新元素地块在周期扫描统一结算。 */
    void EnableSurface(ATerrainSurface& Surface)
    {
        Surface.bEnabled = true;
        if (Surface.HasActorBegunPlay())
        {
            if (Surface.GetNetMode() != NM_DedicatedServer && Surface.Config && Surface.Config->SurfaceVFX)
            {
                Surface.SurfaceParticles->Activate(true);
            }
            // 同一张图可瞬时创建并转换水地。等到扫描结算，让 Actor 和 Mass 都跳过这种中间地块。
            const auto Kind = Surface.Config ? Surface.Config->SurfaceType : ETerrainSurfaceType::Lava;
            if (Kind != ETerrainSurfaceType::Water && Kind != ETerrainSurfaceType::Conductive &&
                !TerrainSurfaceTypes::IsCloud(Kind))
            {
                Surface.UpdateContacts();
            }
        }
        if (IsValid(&Surface) && !Surface.IsActorBeingDestroyed())
        {
            Surface.ForceNetUpdate();
        }
    }

    /** 每个施法来源最多持有固定数量，重复编辑节点也共享实际活动上限。 */
    bool CanAddSurface(AActor& Caster)
    {
        int32 ActiveSurfaces = 0;
        for (TActorIterator<ATerrainSurface> It(Caster.GetWorld()); It; ++It)
        {
            const auto* Lease = It->FindComponentByClass<UAbilityWorkshopTerrainLease>();
            if (Lease && Lease->IsActiveFor(Caster) && ++ActiveSurfaces >= MaxActiveSurfacesPerSource)
            {
                return false;
            }
        }
        return true;
    }

} // namespace

FVector AbilityWorkshopTerrainNodes::ResolveTerrainOrigin(AActor& Caster, const FVector& Requested)
{
    // Ground 输入可以瞄准胶囊体。投到下方真实地面，避免地块浮在敌人身上。
    FHitResult Floor;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WorkshopTerrainGround), false, &Caster);
    const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    const bool bHasFloor = Caster.GetWorld()->LineTraceSingleByObjectType(
        Floor, Requested + FVector(0.f, 0.f, 25.f), Requested - FVector(0.f, 0.f, 200.f), Objects, Query);
    return bHasFloor && Floor.ImpactNormal.Z >= .65f ? Floor.ImpactPoint : Requested;
}

bool AbilityWorkshopTerrainNodes::IsValidSearchRadius(float Radius)
{
    return FMath::IsFinite(Radius) && Radius >= 25.f && Radius <= 1200.f;
}

TArray<TWeakObjectPtr<ATerrainSurface>> AbilityWorkshopTerrainNodes::FindOwnedSurfaces(
    AActor& Caster, const FVector& Origin, float Radius, TOptional<ETerrainSurfaceType> Kind, int32 Limit,
    bool bCloudLayer)
{
    struct FCandidate
    {
        TWeakObjectPtr<ATerrainSurface> Surface;
        double DistanceSq;
        FName Name;
    };
    TArray<FCandidate, TInlineAllocator<MaxActiveSurfacesPerSource>> Candidates;
    const double RadiusSq = FMath::Square(static_cast<double>(Radius));
    for (TActorIterator<ATerrainSurface> It(Caster.GetWorld()); It; ++It)
    {
        if (!It->Config || TerrainSurfaceTypes::IsCloud(It->Config->SurfaceType) != bCloudLayer ||
            (Kind.IsSet() && It->Config->SurfaceType != Kind.GetValue()))
        {
            continue;
        }
        const FVector Location = It->GetActorLocation();
        const double DistanceSq = FVector::DistSquared2D(Origin, Location);
        if (FMath::Abs(Location.Z - Origin.Z) > TerrainVerticalTolerance || DistanceSq > RadiusSq)
        {
            continue;
        }
        const auto* Lease = It->FindComponentByClass<UAbilityWorkshopTerrainLease>();
        const bool bCheckFloorBarrier = bCloudLayer && FMath::Abs(Location.Z - Origin.Z) > SameFloorHeightTolerance;
        if (Lease && Lease->IsActiveFor(Caster) && (!bCheckFloorBarrier || HasClearCloudPath(**It, Origin + AnchorLift)))
        {
            Candidates.Add({*It, DistanceSq, It->GetFName()});
        }
    }
    // 排序内容是纯快照。同距时使用稳定的地块名称，地图遍历顺序不决定命中。
    Candidates.Sort(
        [](const FCandidate& A, const FCandidate& B)
        {
            return A.DistanceSq == B.DistanceSq ? A.Name.LexicalLess(B.Name) : A.DistanceSq < B.DistanceSq;
        });
    TArray<TWeakObjectPtr<ATerrainSurface>> Results;
    for (int32 Index = 0; Index < FMath::Min(Limit, Candidates.Num()); ++Index)
    {
        Results.Add(Candidates[Index].Surface);
    }
    return Results;
}

TOptional<FVector> AbilityWorkshopTerrainNodes::ConvertSurface(const FAbilitySourceToken& Source,
                                                               const FVector& RequestedOrigin, float Radius,
                                                               ETerrainSurfaceType InputKind,
                                                               ETerrainSurfaceType ResultKind)
{
    AActor* Caster = nullptr;
    if (InputKind == ResultKind || InputKind > ETerrainSurfaceType::ConductiveCloud ||
        ResultKind > ETerrainSurfaceType::ConductiveCloud || RequestedOrigin.ContainsNaN() ||
        !IsValidSearchRadius(Radius) ||
        !AbilityExecutionIdentity::ResolveSource(Source, Caster))
    {
        return {};
    }

    const FVector Origin = ResolveTerrainOrigin(*Caster, RequestedOrigin);
    const auto Targets = FindOwnedSurfaces(*Caster, Origin, Radius, InputKind, 1, TerrainSurfaceTypes::IsCloud(InputKind));
    auto* OldSurface = Targets.IsEmpty() ? nullptr : Targets[0].Get();
    auto* OldLease = OldSurface ? OldSurface->FindComponentByClass<UAbilityWorkshopTerrainLease>() : nullptr;
    if (!OldLease || !OldLease->IsActiveFor(*Caster))
    {
        return {};
    }

    // 完成生成时可能运行地块蓝图。预先占用旧租约，防止回调再选中旧地块，新地块尚未开启接触。
    OldLease->SetTransformReserved(true);
    const FTransform Transform = OldSurface->GetActorTransform();
    auto* Replacement = PrepareSurface(Source, ResultKind, Transform, OldSurface->SurfaceSize, OldLease->GetExpiry());

    // 解除占用后立即重新验证原租约并尝试销毁。准备或销毁失败时保留仍然存活的原地块。
    if (IsValid(OldLease))
    {
        OldLease->SetTransformReserved(false);
    }
    if (!IsValid(Replacement) || Replacement->IsActorBeingDestroyed() || !IsValid(OldLease) ||
        !AbilityExecutionIdentity::ResolveSource(Source, Caster) || !OldLease->TryDispelFor(*Caster))
    {
        if (IsValid(Replacement) && !Replacement->IsActorBeingDestroyed())
        {
            Replacement->Destroy();
        }
        return {};
    }

    if (!IsValid(Replacement) || Replacement->IsActorBeingDestroyed())
    {
        return {};
    }
    EnableSurface(*Replacement);
    if (!IsValid(Replacement) || Replacement->IsActorBeingDestroyed())
    {
        return {};
    }
    return Transform.GetLocation();
}

void UAbilityWorkshopTerrainLease::ApplyElementalContact(ATerrainSurface& Surface, const FPureMassCombatTarget& Target,
                                                         bool bNewContact)
{
    if (!IsValid(&Surface) || Surface.IsActorBeingDestroyed() || !Surface.HasAuthority() || !Surface.Config)
    {
        return;
    }
    const auto Kind = Surface.Config->SurfaceType;
    if (Kind != ETerrainSurfaceType::Water && Kind != ETerrainSurfaceType::Conductive &&
        !TerrainSurfaceTypes::IsCloud(Kind))
    {
        return;
    }
    const auto* Lease = Surface.FindComponentByClass<UAbilityWorkshopTerrainLease>();
    const auto* Caster = Lease ? Lease->SourceToken.Avatar.Get() : nullptr;
    if (!Caster || !Lease->IsActiveFor(*Caster))
    {
        return;
    }
    const auto& Source = Lease->SourceToken;

    FAbilityTargetToken Captured;
    FString Error;
    if (!AbilityExecutionIdentity::CaptureTarget(Source, Target, Captured, Error))
    {
        return;
    }
    // 持续接触只补空状态，避免每次扫描覆盖先前冰冻、导电等反应结果。
    if (!bNewContact && AbilityElements::Read(Source, Captured) != EAbilityExposure::None)
    {
        return;
    }
    if (TerrainSurfaceTypes::IsCloud(Kind))
    {
        FVector BodyCenter;
        if (!AbilityExecutionIdentity::ResolveTarget(Source, Captured, BodyCenter) ||
            !Surface.ContainsCloudSample(BodyCenter) || !HasClearCloudPath(Surface, BodyCenter))
        {
            return;
        }
    }
    const bool bUsesWater = Kind == ETerrainSurfaceType::Water || Kind == ETerrainSurfaceType::MistCloud;
    AbilityElements::Apply(Source, Captured, bUsesWater ? EAbilityElement::Water : EAbilityElement::Thunder, 1.f);
}

void UAbilityWorkshopTerrainLease::Initialize(const FAbilitySourceToken& Source, double Deadline)
{
    SourceToken = Source;
    ExpiresAt = Deadline;
    if (auto* Caster = Source.Avatar.Get())
    {
        Caster->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnSourceEnded);
        if (auto* Combat = Cast<ICombatInterface>(Caster))
        {
            Combat->GetOnDeathDelegate().AddUniqueDynamic(this, &ThisClass::OnSourceDied);
        }
    }
    auto* World = GetWorld();
    GetOwner()->SetLifeSpan(static_cast<float>(Deadline - World->GetTimeSeconds()));
    World->GetTimerManager().SetTimer(SourceCheck, this, &ThisClass::RevalidateSource, .10f, true);
}

bool UAbilityWorkshopTerrainLease::IsActiveFor(const AActor& Caster) const
{
    const auto* Surface = Cast<ATerrainSurface>(GetOwner());
    AActor* OriginalCaster = nullptr;
    return IsValid(Surface) && !Surface->IsActorBeingDestroyed() && Surface->GetOwner() == &Caster &&
           !bTransformReserved && Surface->bEnabled && ExpiresAt > GetWorld()->GetTimeSeconds() &&
           AbilityExecutionIdentity::ResolveSource(SourceToken, OriginalCaster) && OriginalCaster == &Caster;
}

bool UAbilityWorkshopTerrainLease::TryDispelFor(const AActor& Caster)
{
    if (!Caster.HasAuthority() || !IsActiveFor(Caster))
    {
        return false;
    }
    auto* Surface = CastChecked<ATerrainSurface>(GetOwner());
    bTransformReserved = true;
    Surface->bEnabled = false;
    if (Surface->Destroy())
    {
        return true;
    }
    // 没能销毁就恢复接触和可选状态，执行节点不会发布 Removed。
    Surface->bEnabled = true;
    bTransformReserved = false;
    return false;
}

void UAbilityWorkshopTerrainLease::SetTransformReserved(bool bReserved)
{
    bTransformReserved = bReserved;
}

double UAbilityWorkshopTerrainLease::GetExpiry() const
{
    return ExpiresAt;
}

void UAbilityWorkshopTerrainLease::OnUnregister()
{
    if (auto* Caster = SourceToken.Avatar.Get())
    {
        Caster->OnEndPlay.RemoveDynamic(this, &ThisClass::OnSourceEnded);
        if (auto* Combat = Cast<ICombatInterface>(Caster))
        {
            Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OnSourceDied);
        }
    }
    if (auto* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SourceCheck);
    }
    Super::OnUnregister();
}

void UAbilityWorkshopTerrainLease::RevalidateSource()
{
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(SourceToken, Caster) || ExpiresAt <= GetWorld()->GetTimeSeconds())
    {
        Retire();
    }
}

void UAbilityWorkshopTerrainLease::OnSourceDied(AActor* Actor)
{
    Retire();
}

void UAbilityWorkshopTerrainLease::OnSourceEnded(AActor* Actor, EEndPlayReason::Type Reason)
{
    Retire();
}

void UAbilityWorkshopTerrainLease::Retire()
{
    if (auto* Surface = Cast<ATerrainSurface>(GetOwner()); IsValid(Surface) && !Surface->IsActorBeingDestroyed())
    {
        Surface->bEnabled = false;
        Surface->Destroy();
    }
}

bool UAbilityElementSurfaceNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!IsValidSize(Radius) || !IsValidDuration(Duration) || Kind > ETerrainSurfaceType::Conductive)
    {
        Error = TEXT("Terrain requires half-width 25–800 cm, duration 0.1–20 seconds, and a known kind.");
    }
    return Error.IsEmpty();
}

bool UAbilityElementSurfaceNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                             FString& Error)
{
    if (Parameter == EAbilityConfigParameter::Radius && IsValidSize(Value))
    {
        Radius = Value;
        return true;
    }
    if (Parameter == EAbilityConfigParameter::Duration && IsValidDuration(Value))
    {
        Duration = Value;
        return true;
    }
    if (Parameter == EAbilityConfigParameter::TerrainSurfaceKind && FMath::IsFinite(Value) && Value >= 0.f &&
        Value <= static_cast<float>(ETerrainSurfaceType::Conductive) && Value == FMath::RoundToFloat(Value))
    {
        Kind = static_cast<ETerrainSurfaceType>(static_cast<uint8>(Value));
        return true;
    }
    Error = TEXT("This terrain parameter or value is outside its supported range.");
    return false;
}

bool UAbilityElementSurfaceNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    FString Error;
    if (Parameter == EAbilityConfigScalar::Radius)
    {
        return TrySetRecipeParameter(EAbilityConfigParameter::Radius, Radius * Multiplier, Error);
    }
    return Parameter == EAbilityConfigScalar::Duration &&
           TrySetRecipeParameter(EAbilityConfigParameter::Duration, Duration * Multiplier, Error);
}

bool UAbilityIgniteSurfaceNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!AbilityWorkshopTerrainNodes::IsValidSearchRadius(Radius))
    {
        Error = TEXT("Ignition requires a search radius from 25 to 1200 cm.");
    }
    return Error.IsEmpty();
}

bool UAbilityIgniteSurfaceNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                            FString& Error)
{
    if (Parameter == EAbilityConfigParameter::Radius && AbilityWorkshopTerrainNodes::IsValidSearchRadius(Value))
    {
        Radius = Value;
        return true;
    }
    Error = TEXT("Ignition only accepts a radius from 25 to 1200 cm.");
    return false;
}

bool UAbilityIgniteSurfaceNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    FString Error;
    return Parameter == EAbilityConfigScalar::Radius &&
           TrySetRecipeParameter(EAbilityConfigParameter::Radius, Radius * Multiplier, Error);
}

bool UAbilityElementSurfaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityElementSurfaceNodeConfig>(&Config);
    if (CanAddSurface(*Caster))
    {
        const FTransform Transform(FRotator::ZeroRotator,
                                   AbilityWorkshopTerrainNodes::ResolveTerrainOrigin(*Caster, Input.Origin));
        const double Deadline = Caster->GetWorld()->GetTimeSeconds() + Settings->Duration;
        if (auto* Surface = PrepareSurface(Input.SourceToken, Settings->Kind, Transform,
                                           FVector2D(Settings->Radius * 2.f), Deadline))
        {
            EnableSurface(*Surface);
            if (!bStopped && !Owner.IsFinished())
            {
                auto Payload = Input;
                Payload.Origin = Transform.GetLocation();
                Payload.EventCenter = Payload.Origin;
                Owner.Publish(Id, TEXT("Spawned"), Payload);
            }
        }
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

bool UAbilityIgniteSurfaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                               FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityIgniteSurfaceNodeConfig>(&Config);
    const auto Position = AbilityWorkshopTerrainNodes::ConvertSurface(
        Input.SourceToken, Input.Origin, Settings->Radius, ETerrainSurfaceType::Toxic, ETerrainSurfaceType::Lava);
    if (Position.IsSet() && !bStopped && !Owner.IsFinished())
    {
        auto Payload = Input;
        Payload.Origin = Position.GetValue();
        Payload.EventCenter = Payload.Origin;
        Owner.Publish(Id, TEXT("Transformed"), Payload);
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

void AbilityWorkshopTerrainNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Surface;
    Surface.TypeId = TEXT("World.ElementSurface");
    Surface.ConfigClass = UAbilityElementSurfaceNodeConfig::StaticClass();
    Surface.ExecutionClass = UAbilityElementSurfaceNodeExecution::StaticClass();
    Surface.InputPayload = EAbilityPortPayload::Origin;
    Surface.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    Surface.bSupportsActorTargets = Surface.bSupportsMassTargets = Surface.bSupportsSnapshotTargets = true;
    Surface.bRuntimeFiniteSafe = true;
    Surface.Ports = {{TEXT("Spawned"), EAbilityPortPayload::Origin, false},
                     {TEXT("Completed"), EAbilityPortPayload::Origin, true}};

    auto Ignite = Surface;
    Ignite.TypeId = TEXT("World.IgniteSurface");
    Ignite.ConfigClass = UAbilityIgniteSurfaceNodeConfig::StaticClass();
    Ignite.ExecutionClass = UAbilityIgniteSurfaceNodeExecution::StaticClass();
    Ignite.Ports[0].Name = TEXT("Transformed");
    for (const auto& Descriptor : {Surface, Ignite})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Terrain node registration: %s"), *Error);
        }
    }
}
