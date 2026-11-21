// 调用链：细节点表现所有者 -> 共享表现服务 -> 仅表现 Runtime；此文件不执行伤害或原型子图。
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "PureMass/PureMassRuntime.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

namespace AbilityPrototypePresentationDetails
{
    /** 未解析的预期资源会延迟首帧；未配置特效的合法节点仍可正常建立/结束表现寿命。 */
    bool ResourcesReady(const FAbilityPrototypeVisualSnapshot& Snapshot)
    {
        return Snapshot.bReady && (!Snapshot.bExpectsLoopVFX || Snapshot.LoopVFX) &&
               (!Snapshot.bExpectsImpactVFX || Snapshot.ImpactVFX);
    }

    /** 注册在宿主下的非附着瞬时组件；世界落点不会随弹体后续移动，宿主销毁负责兜底清理。 */
    void SpawnImpact(AActor& Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                     FAbilityPrototypePresentationStats& Stats, const FVector& Position, const FVector& Scale,
                     const FRotator& Rotation = FRotator::ZeroRotator)
    {
        if (!Snapshot.ImpactVFX || Host.GetNetMode() == NM_DedicatedServer)
        {
            return;
        }
        FVector Location = Position;
        if (Snapshot.bGroundPresentation)
        {
            APrototypeRuntime::ProjectSurfaceToGround(Host.GetOwner() ? Host.GetOwner() : &Host, Position, Location);
        }
        auto* Component = NewObject<UNiagaraComponent>(&Host);
        Component->SetAutoActivate(false);
        Component->SetAutoDestroy(true);
        Component->SetAsset(Snapshot.ImpactVFX);
        Host.AddInstanceComponent(Component);
        Component->RegisterComponent();
        Component->SetWorldLocationAndRotation(Location, Rotation);
        Component->SetWorldScale3D(Scale);
        Component->Activate(true);
        ++Stats.Impacts;
    }

    /** 原 Mixed 基础节点命中特效使用单位缩放，Batch6 显式 authored 和 Actor 保留资产缩放。 */
    FVector ImpactScale(const FAbilityPrototypeVisualSnapshot& Snapshot)
    {
        return Snapshot.Profile == EAbilityPrototypePresentationProfile::Actor || Snapshot.bUseAuthoredScale
                   ? Snapshot.ImpactScale
                   : FVector::OneVector;
    }

    /** 所有端点在 Activate 前写入；不依赖 Owner 的 NetGUID 已解析，也不从原 DA 补范围值。 */
    void UpdateMain(AActor& Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                    const FAbilityPrototypePresentationFrame& Frame, UNiagaraComponent& Visual)
    {
        Visual.SetVariableVec3(TEXT("Beam Start"), Frame.BeamStart);
        Visual.SetVariableVec3(TEXT("Beam End"), Frame.BeamEnd);
        Visual.SetVariableVec3(TEXT("BeamEnd"), Frame.BeamEnd);
        Visual.SetVariableVec3(TEXT("User.BeamEnd"), Frame.BeamEnd);
        if (Snapshot.Profile == EAbilityPrototypePresentationProfile::Extension)
        {
            Visual.SetRelativeLocation(Snapshot.ExtensionBehavior == EExtensionKind::Attach ? FVector(0, 0, 85)
                                                                                            : FVector::ZeroVector);
            Visual.SetRelativeScale3D(Snapshot.ExtensionBehavior == EExtensionKind::Displace
                                          ? FVector(Snapshot.Radius / 100.f, Snapshot.Radius / 100.f, .3f)
                                          : FVector::OneVector);
            return;
        }
        FVector Scale = Snapshot.Profile == EAbilityPrototypePresentationProfile::Actor || Snapshot.bUseAuthoredScale
                            ? Snapshot.LoopScale
                            : FVector::OneVector;
        if ((Snapshot.Kind == EAbilityPrototypeKind::Ring &&
             (Snapshot.Profile == EAbilityPrototypePresentationProfile::Actor || !Snapshot.bUseAuthoredScale)) ||
            (Snapshot.Profile == EAbilityPrototypePresentationProfile::Mixed && !Snapshot.bUseAuthoredScale &&
             Snapshot.Kind == EAbilityPrototypeKind::Field))
        {
            Scale = FVector(Snapshot.Radius / 100.f, Snapshot.Radius / 100.f, .25f);
        }
        if (Snapshot.Kind == EAbilityPrototypeKind::Wave)
        {
            if (Snapshot.Profile == EAbilityPrototypePresentationProfile::Actor)
            {
                const float RadiusScale = FMath::Max(.01f, Frame.WaveRadius / 100.f);
                Scale = FVector(RadiusScale, RadiusScale, .25f);
            }
            else
            {
                Scale = FVector(FMath::Max(.05f, Frame.WaveRadius / 100.f));
            }
        }
        Visual.SetRelativeScale3D(Scale);
        if (Snapshot.bGroundPresentation)
        {
            FVector Ground = Host.GetActorLocation();
            APrototypeRuntime::ProjectSurfaceToGround(Host.GetOwner() ? Host.GetOwner() : &Host,
                                                      Host.GetActorLocation(), Ground);
            Visual.SetWorldLocationAndRotation(Ground, FRotator::ZeroRotator);
        }
    }

    /** Batch6 圆盘和雪花仅消费冻结几何/材质；组件由 Actor 持有，局部状态只保留弱引用。 */
    void UpdateArea(AActor& Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                    FAbilityPrototypePresentationLocalState& State)
    {
        if (Snapshot.Profile != EAbilityPrototypePresentationProfile::Mixed || !Snapshot.bShowAreaDisc ||
            Snapshot.Kind != EAbilityPrototypeKind::Field || !Snapshot.AreaMaterial)
        {
            return;
        }
        FVector Ground = Host.GetActorLocation();
        APrototypeRuntime::ProjectSurfaceToGround(Host.GetOwner() ? Host.GetOwner() : &Host, Host.GetActorLocation(),
                                                  Ground);
        auto* Area = State.Area.Get();
        if (!Area)
        {
            Area = NewObject<UStaticMeshComponent>(&Host);
            Area->SetupAttachment(Host.GetRootComponent());
            Area->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
            Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Area->SetCastShadow(false);
            Area->SetMaterial(0, Snapshot.AreaMaterial);
            Host.AddInstanceComponent(Area);
            Area->RegisterComponent();
            State.Area = Area;
        }
        Area->SetVisibility(true);
        Area->SetWorldLocationAndRotation(Ground + FVector(0, 0, 1), FRotator::ZeroRotator);
        Area->SetWorldScale3D(FVector(Snapshot.Radius / 50.f, Snapshot.Radius / 50.f, .01f));
        auto* Snow = State.Snow.Get();
        if (Snapshot.bSnowfall && !Snow)
        {
            Snow = NewObject<UInstancedStaticMeshComponent>(&Host);
            Snow->SetupAttachment(Host.GetRootComponent());
            Snow->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
            Snow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Snow->SetCastShadow(false);
            Snow->SetMaterial(0, Snapshot.AreaMaterial);
            Host.AddInstanceComponent(Snow);
            Snow->RegisterComponent();
            for (int32 Index = 0; Index < 64; ++Index)
            {
                Snow->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, 150), FVector(.035f)), false);
            }
            State.Snow = Snow;
        }
        if (!Snow)
        {
            return;
        }
        Snow->SetVisibility(true);
        Snow->SetWorldLocationAndRotation(Ground, FRotator::ZeroRotator);
        Snow->SetWorldScale3D(FVector::OneVector);
        const float Time = Host.GetWorld()->GetTimeSeconds();
        for (int32 Index = 0; Index < 64; ++Index)
        {
            const float Angle = Index * 2.39996323f + Time * .35f;
            const float Radius = Snapshot.Radius * FMath::Sqrt((Index + .5f) / 64.f);
            const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius,
                                 30.f + 260.f * (1.f - FMath::Frac(Time * .6f + Index * .618034f)));
            Snow->UpdateInstanceTransform(
                Index, FTransform(FRotator::ZeroRotator, Offset, FVector(.035f + (Index % 4) * .015f)), false,
                Index == 63, true);
        }
    }

    /** 环绕表现只接收既有业务给出的点，不在渲染层补充、发射、查询或伤害弹体。 */
    void UpdateOrbit(AActor& Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                     const FAbilityPrototypePresentationFrame& Frame, FAbilityPrototypePresentationLocalState& State)
    {
        while (State.OrbitVisuals.Num() < Frame.OrbitPositions.Num())
        {
            auto* Component = NewObject<UNiagaraComponent>(&Host);
            Component->SetAutoActivate(false);
            Component->SetupAttachment(Host.GetRootComponent());
            Component->SetAsset(Snapshot.LoopVFX);
            Host.AddInstanceComponent(Component);
            Component->RegisterComponent();
            State.OrbitVisuals.Add(Component);
        }
        for (int32 Index = 0; Index < State.OrbitVisuals.Num(); ++Index)
        {
            auto* Component = State.OrbitVisuals[Index].Get();
            if (!Component)
            {
                continue;
            }
            const bool bVisible = Frame.OrbitPositions.IsValidIndex(Index);
            Component->SetVisibility(bVisible);
            if (bVisible)
            {
                Component->SetWorldLocation(Frame.OrbitPositions[Index]);
                Component->SetRelativeScale3D(Snapshot.LoopScale);
                if (!Component->IsActive())
                {
                    Component->Activate(true);
                }
            }
            else
            {
                Component->DeactivateImmediate();
            }
        }
    }
} // namespace AbilityPrototypePresentationDetails

namespace AbilityPrototypePresentationService
{
    FAbilityPrototypeVisualSnapshot Capture(const UAbilityPrototype& Frozen, UAbilityPrototype* StableReference,
                                            EAbilityPrototypePresentationProfile Profile)
    {
        FAbilityPrototypeVisualSnapshot Result;
        Result.Profile = Profile;
        Result.Kind = Frozen.Kind;
        if (IsValid(StableReference) && StableReference->IsAsset() && !StableReference->HasAnyFlags(RF_Transient) &&
            StableReference->GetOutermost() != GetTransientPackage())
        {
            Result.StableReference = StableReference;
        }
        Result.LoopVFX = Frozen.LoopVFX;
        Result.ImpactVFX = Frozen.ImpactVFX;
        Result.bExpectsLoopVFX = Frozen.LoopVFX != nullptr;
        Result.bExpectsImpactVFX = Frozen.ImpactVFX != nullptr;
        Result.LoopScale = Frozen.LoopVFXScale;
        Result.ImpactScale = Frozen.ImpactVFXScale;
        Result.Radius = Frozen.Radius;
        Result.InnerRadius = Frozen.InnerRadius;
        Result.Range = Frozen.Range;
        Result.SpreadDegrees = Frozen.SpreadDegrees;
        Result.bGroundPresentation = Frozen.bGroundPresentation;
        const auto* Extension = Cast<UExtensionNode>(&Frozen);
        if (Profile == EAbilityPrototypePresentationProfile::Extension && Extension)
        {
            Result.ExtensionBehavior = Extension->Behavior;
            // 原 Extension 只使用自身 offset/disc 规则，不读取父原型的 authored/ground 缩放。
            Result.LoopScale = Result.ImpactScale = FVector::OneVector;
            Result.bGroundPresentation = false;
        }
        if (const auto* Batch6 = Cast<UBatch6FieldPrototype>(&Frozen))
        {
            Result.bUseAuthoredScale = Batch6->bUseAuthoredVFXScale;
            Result.bShowAreaDisc = Batch6->bShowAreaDisc;
            Result.bSnowfall = Batch6->bSnowfall;
            Result.AreaMaterial = Batch6->AreaMaterial;
        }
        Result.bReady = (Profile == EAbilityPrototypePresentationProfile::Actor ||
                         Profile == EAbilityPrototypePresentationProfile::Mixed ||
                         (Profile == EAbilityPrototypePresentationProfile::Extension && Extension &&
                          static_cast<uint8>(Extension->Behavior) <= static_cast<uint8>(EExtensionKind::Surface))) &&
                        static_cast<uint8>(Frozen.Kind) <= static_cast<uint8>(EAbilityPrototypeKind::Line) &&
                        !Result.LoopScale.ContainsNaN() && !Result.ImpactScale.ContainsNaN() &&
                        FMath::IsFinite(Result.Radius) && Result.Radius >= 0.f && FMath::IsFinite(Result.InnerRadius) &&
                        Result.InnerRadius >= 0.f && FMath::IsFinite(Result.Range) && Result.Range >= 0.f &&
                        FMath::IsFinite(Result.SpreadDegrees);
        return Result;
    }

    AActor* Begin(const FAbilitySourceToken& Source, const UAbilityPrototype& Frozen,
                  UAbilityPrototype* StableReference, const FAbilityPrototypePresentationFrame& Frame,
                  EAbilityPrototypePresentationProfile Profile, FString& Error)
    {
        Error.Reset();
        if (Profile == EAbilityPrototypePresentationProfile::Actor)
        {
            return APrototypeRuntime::SpawnPresentationOnly(Source, Frozen, StableReference, Frame, Error);
        }
        if (Profile == EAbilityPrototypePresentationProfile::Mixed)
        {
            return APureMassRuntime::SpawnPresentationOnly(Source, Frozen, StableReference, Frame, Error);
        }
        if (Profile == EAbilityPrototypePresentationProfile::Extension)
        {
            if (const auto* Extension = Cast<UExtensionNode>(&Frozen))
            {
                return AExtensionRuntime::SpawnPresentationOnly(Source, *Extension,
                                                                Cast<UExtensionNode>(StableReference), Frame, Error);
            }
        }
        Error = TEXT("Unknown prototype presentation profile");
        return nullptr;
    }

    bool Update(AActor* Host, const FAbilityPrototypePresentationFrame& Frame)
    {
        if (!IsValid(Host))
        {
            return false;
        }
        if (auto* Actor = Cast<APrototypeRuntime>(Host))
        {
            return Actor->UpdatePresentationOnly(Frame);
        }
        if (auto* Mixed = Cast<APureMassRuntime>(Host))
        {
            return Mixed->UpdatePresentationOnly(Frame);
        }
        if (auto* Extension = Cast<AExtensionRuntime>(Host))
        {
            return Extension->UpdatePresentationOnly(Frame);
        }
        return false;
    }

    bool Impact(AActor* Host, uint32 EventId, const FVector& Position)
    {
        if (!IsValid(Host))
        {
            return false;
        }
        if (auto* Actor = Cast<APrototypeRuntime>(Host))
        {
            return Actor->ImpactPresentationOnly(EventId, Position);
        }
        if (auto* Mixed = Cast<APureMassRuntime>(Host))
        {
            return Mixed->ImpactPresentationOnly(EventId, Position);
        }
        if (auto* Extension = Cast<AExtensionRuntime>(Host))
        {
            return Extension->ImpactPresentationOnly(EventId, Position);
        }
        return false;
    }

    bool Finish(AActor* Host, bool bCancelled, bool bImpactOnFinish)
    {
        if (!IsValid(Host))
        {
            return false;
        }
        if (auto* Actor = Cast<APrototypeRuntime>(Host))
        {
            return Actor->FinishPresentationOnly(bCancelled, bImpactOnFinish);
        }
        if (auto* Mixed = Cast<APureMassRuntime>(Host))
        {
            return Mixed->FinishPresentationOnly(bCancelled, bImpactOnFinish);
        }
        if (auto* Extension = Cast<AExtensionRuntime>(Host))
        {
            return Extension->FinishPresentationOnly(bCancelled, bImpactOnFinish);
        }
        return false;
    }

    bool FinishOriginal(AActor* Host, bool bCancelled, bool bImpactOnFinish)
    {
        if (auto* Mixed = Cast<APureMassRuntime>(Host); IsValid(Mixed))
        {
            // 旧Mixed的Finish始终经过RefreshVisual终态分支，取消同样产生一次结束粒子。
            // 显式保留该策略，避免把它扩散到通用表现取消或Actor/Extension调用者。
            return Mixed->FinishPresentationOnly(bCancelled, true, true);
        }
        return Finish(Host, bCancelled, bImpactOnFinish);
    }

    bool IsValidFrame(const FAbilityPrototypePresentationFrame& Frame)
    {
        if (!Frame.bReady || Frame.Revision == 0 || Frame.Transform.ContainsNaN() || Frame.BeamStart.ContainsNaN() ||
            Frame.BeamEnd.ContainsNaN() || !FMath::IsFinite(Frame.WaveRadius) || Frame.WaveRadius < 0.f)
        {
            return false;
        }
        if (Frame.bOriginalMixedFinish && (!Frame.bFinished || !Frame.bImpactOnFinish))
        {
            return false;
        }
        for (const FVector& Position : Frame.OrbitPositions)
        {
            if (Position.ContainsNaN())
            {
                return false;
            }
        }
        return true;
    }

    bool AcceptFrame(const FAbilityPrototypePresentationFrame& Incoming, FAbilityPrototypePresentationLocalState& State,
                     FAbilityPrototypePresentationStats& Stats)
    {
        if (!IsValidFrame(Incoming) || State.bTerminal)
        {
            return false;
        }
        const bool bHadFrame = State.AcceptedFrame.bReady;
        if (bHadFrame && (Incoming.Revision < State.AcceptedFrame.Revision ||
                          (Incoming.Revision == State.AcceptedFrame.Revision && !Incoming.bFinished)))
        {
            return false;
        }
        if (bHadFrame && !Incoming.bFinished)
        {
            ++Stats.Updates;
        }
        State.AcceptedFrame = Incoming;
        State.bTerminal = Incoming.bFinished;
        if (Incoming.bFinished)
        {
            State.bAwaitingFinishFrame = false;
        }
        return true;
    }

    void StopVisuals(FAbilityPrototypePresentationLocalState& State, UNiagaraComponent* Visual,
                     UNiagaraComponent* InnerVisual)
    {
        if (Visual)
        {
            Visual->DeactivateImmediate();
        }
        if (InnerVisual)
        {
            InnerVisual->DeactivateImmediate();
        }
        if (State.Area.IsValid())
        {
            State.Area->SetVisibility(false);
        }
        if (State.Snow.IsValid())
        {
            State.Snow->SetVisibility(false);
        }
        for (const auto& Weak : State.OrbitVisuals)
        {
            if (auto* Component = Weak.Get())
            {
                Component->SetVisibility(false);
                Component->DeactivateImmediate();
            }
        }
        State.PendingImpacts.Reset();
    }

    bool CanAcceptImpact(const FAbilityPrototypePresentationLocalState& State, uint32 EventId)
    {
        if (EventId == 0 || State.bTerminal || State.ImpactEvents.Contains(EventId))
        {
            return false;
        }
        if (State.LatestImpactEvent == 0)
        {
            return true;
        }
        const uint32 Forward = EventId - State.LatestImpactEvent;
        // 序列由每个表现宿主递增生成。允许自然回绕，半个序列空间以上的跳变视作旧消息，
        // 不能靠清空去重记录让非常晚的unreliable RPC重新播放旧粒子。
        return (Forward > 0 && Forward < 0x80000000u) || State.LatestImpactEvent - EventId < 256u;
    }

    bool PresentImpact(AActor& Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                       FAbilityPrototypePresentationLocalState& State, FAbilityPrototypePresentationStats& Stats,
                       uint32 EventId, const FVector& Position)
    {
        if (Position.ContainsNaN() || !CanAcceptImpact(State, EventId))
        {
            return false;
        }
        const uint32 Forward = EventId - State.LatestImpactEvent;
        if (State.LatestImpactEvent == 0 || (Forward > 0 && Forward < 0x80000000u))
        {
            State.LatestImpactEvent = EventId;
            // 只裁剪表现去重/待资源队列，不截断持续技能，也不参与TargetSet或伤害事件预算。
            // 一旦资源可用，窗口内所有未呈现事件仍会逐个播放；已就绪事件不受256总数限制。
            for (auto It = State.ImpactEvents.CreateIterator(); It; ++It)
            {
                if (State.LatestImpactEvent - *It >= 256u)
                {
                    It.RemoveCurrent();
                }
            }
            for (auto It = State.PendingImpacts.CreateIterator(); It; ++It)
            {
                if (State.LatestImpactEvent - It.Key() >= 256u)
                {
                    It.RemoveCurrent();
                }
            }
        }
        State.ImpactEvents.Add(EventId);
        if (!AbilityPrototypePresentationDetails::ResourcesReady(Snapshot))
        {
            State.PendingImpacts.Add(EventId, Position);
            return true;
        }
        AbilityPrototypePresentationDetails::SpawnImpact(Host, Snapshot, Stats, Position,
                                                         AbilityPrototypePresentationDetails::ImpactScale(Snapshot));
        return true;
    }

    void Refresh(AActor& Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                 FAbilityPrototypePresentationLocalState& State, FAbilityPrototypePresentationStats& Stats,
                 UNiagaraComponent* Visual, UNiagaraComponent* InnerVisual)
    {
        using namespace AbilityPrototypePresentationDetails;
        const auto& Frame = State.AcceptedFrame;
        if (State.bAwaitingFinishFrame)
        {
            StopVisuals(State, Visual, InnerVisual);
            return;
        }
        if (State.bTerminal)
        {
            StopVisuals(State, Visual, InnerVisual);
            const bool bOriginalMixed =
                Snapshot.Profile == EAbilityPrototypePresentationProfile::Mixed && Frame.bOriginalMixedFinish;
            if (Snapshot.Profile == EAbilityPrototypePresentationProfile::Extension || bOriginalMixed)
            {
                // 结束位/取消帧可先于资源到达。只等需要的结束资产，不等已关闭的 LoopVFX，
                // 也不能提前吞掉显式允许的 Extension 取消结束表现。
                const bool bEndReady = Snapshot.bReady && (!Snapshot.bExpectsImpactVFX || Snapshot.ImpactVFX);
                if (!State.bFinishPresented && Frame.bReady && (!Frame.bImpactOnFinish || bEndReady))
                {
                    State.bFinishPresented = true;
                    ++Stats.Finishes;
                    if (Frame.bImpactOnFinish)
                    {
                        SpawnImpact(Host, Snapshot, Stats, Frame.Transform.GetLocation(),
                                    bOriginalMixed ? ImpactScale(Snapshot) : FVector::OneVector);
                    }
                }
                return;
            }
            // Finish-first 复制不能重放 Burst 的初始命中；取消无需等待任何资源。
            if (!State.bFinishPresented && Frame.bReady &&
                (Frame.bCancelled || !Frame.bImpactOnFinish || ResourcesReady(Snapshot)))
            {
                State.bFinishPresented = true;
                ++Stats.Finishes;
                if (!Frame.bCancelled && Frame.bImpactOnFinish)
                {
                    SpawnImpact(Host, Snapshot, Stats, Frame.Transform.GetLocation(), ImpactScale(Snapshot));
                }
            }
            return;
        }
        if (!Frame.bReady || !ResourcesReady(Snapshot) || !Visual || Host.GetNetMode() == NM_DedicatedServer)
        {
            return;
        }
        Host.SetActorTransform(Frame.Transform);
        const bool bUsesMainLoop =
            Snapshot.LoopVFX && (Snapshot.Profile == EAbilityPrototypePresentationProfile::Extension ||
                                 (Snapshot.Kind != EAbilityPrototypeKind::Orbit &&
                                  (Snapshot.Kind != EAbilityPrototypeKind::Multishot || Snapshot.bSingleProjectile)));
        if (!State.bBegan && bUsesMainLoop)
        {
            // SetAsset resets Niagara's override parameter store. Install it before endpoints, then Activate.
            Visual->SetAsset(Snapshot.LoopVFX);
        }
        UpdateMain(Host, Snapshot, Frame, *Visual);
        if (!State.bBegan)
        {
            State.bBegan = true;
            ++Stats.Begins;
            if (bUsesMainLoop)
            {
                Visual->Activate(true);
            }
            if (Snapshot.Profile == EAbilityPrototypePresentationProfile::Actor)
            {
                if (Snapshot.Kind == EAbilityPrototypeKind::Burst)
                {
                    SpawnImpact(Host, Snapshot, Stats, Host.GetActorLocation(), ImpactScale(Snapshot));
                }
                if (Snapshot.Kind == EAbilityPrototypeKind::Ring && Snapshot.LoopVFX && InnerVisual &&
                    Snapshot.InnerRadius > 0.f)
                {
                    InnerVisual->SetAsset(Snapshot.LoopVFX);
                    InnerVisual->SetRelativeScale3D(
                        FVector(Snapshot.InnerRadius / 100.f, Snapshot.InnerRadius / 100.f, .25f));
                    if (Snapshot.bGroundPresentation)
                    {
                        InnerVisual->SetWorldLocationAndRotation(Visual->GetComponentLocation(), FRotator::ZeroRotator);
                    }
                    InnerVisual->Activate(true);
                }
                if (Snapshot.Kind == EAbilityPrototypeKind::Line)
                {
                    for (int32 Index = 1; Index <= 5; ++Index)
                    {
                        SpawnImpact(Host, Snapshot, Stats,
                                    Host.GetActorLocation() +
                                        Host.GetActorForwardVector() * Snapshot.Range * Index / 5.f,
                                    Snapshot.ImpactScale * .4f, Host.GetActorRotation());
                    }
                }
                if (Snapshot.Kind == EAbilityPrototypeKind::Cone)
                {
                    for (int32 Index = 0; Index < 3; ++Index)
                    {
                        const FVector Ray = Host.GetActorForwardVector().RotateAngleAxis(
                            (Index - 1) * Snapshot.SpreadDegrees * .35f, FVector::UpVector);
                        SpawnImpact(Host, Snapshot, Stats, Host.GetActorLocation() + Ray * Snapshot.Radius * .55f,
                                    FVector(.45f), Ray.Rotation());
                    }
                }
            }
        }
        UpdateArea(Host, Snapshot, State);
        if (Snapshot.Profile != EAbilityPrototypePresentationProfile::Extension)
        {
            UpdateOrbit(Host, Snapshot, Frame, State);
        }
        for (const auto& Pair : State.PendingImpacts)
        {
            SpawnImpact(Host, Snapshot, Stats, Pair.Value, ImpactScale(Snapshot));
        }
        State.PendingImpacts.Reset();
    }
} // namespace AbilityPrototypePresentationService
