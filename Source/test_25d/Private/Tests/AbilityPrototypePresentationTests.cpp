// 调用链：Automation -> 纯表现服务/复制入口 -> 实际 Runtime/Niagara/圆盘；同时检查没有 GE、子节点或预算副作用。
// 复制顺序在本进程重放真实共用入口；此测试不替代真实网络传输和用户 PIE 画面验收。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilityPrototypeNiagaraTestFixture.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "PureMass/PureMassRuntime.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "MyGameplayTags.h"
#include "EngineUtils.h"
#include "UObject/Script.h"

namespace AbilityPrototypePresentationTests
{
    using EProfile = EAbilityPrototypePresentationProfile;

    /** 使用项目目录中的稳定资源，测试真正 Niagara 组件的 Asset/Scale，不伪造 renderer。 */
    UNiagaraSystem* LoopSystem()
    {
        for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                                  TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"),
                                  TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo")})
        {
            const auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
            if (!Catalog)
            {
                continue;
            }
            for (const auto& Row : Catalog->AbilityInformation)
            {
                const auto* Ability = Row.Ability ? Cast<UPrototypeAbility>(Row.Ability->GetDefaultObject()) : nullptr;
                if (Ability && Ability->Prototype && Ability->Prototype->LoopVFX)
                {
                    return Ability->Prototype->LoopVFX;
                }
            }
        }
        return nullptr;
    }

    /** 源、目标和固定扣10血 GE 都是真实通路；只在宿主上 BeginPlay，不启动夹具敌人的 AI。 */
    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;

        FFixture()
        {
            auto* TargetASC = Target->GetAbilitySystemComponent();
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Target->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        }

        /** 让误入 Burst/OnHit/OnExpire 的实现必然有可观察伤害，而不是无效 Context 空转。 */
        UPrototypeCastContext* Context()
        {
            auto* Result = NewObject<UPrototypeCastContext>(Source);
            Result->Caster = Source;
            Result->DamageParams.SourceAbilitySystemComponent = ASC;
            Result->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            Result->Level = 1;
            Result->CastId = FGuid::NewGuid();
            FString Error;
            Result->CaptureSourceLife(Error);
            return Result;
        }

        /** 无费用参数，合法伤害类型；本测试预期这些业务值完全不被表现服务消费。 */
        UAbilityPrototype* Prototype(EAbilityPrototypeKind Kind)
        {
            auto* Result = NewObject<UAbilityPrototype>(World);
            Result->Kind = Kind;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Cooldown = 0.f;
            Result->Radius = 350.f;
            Result->Duration = 2.f;
            Result->Period = .1f;
            return Result;
        }
    };

    /** 首帧显式给出全部空间值；首粒子不等待 Owner 或下一次 Tick 补端点。 */
    FAbilityPrototypePresentationFrame Frame()
    {
        FAbilityPrototypePresentationFrame Result;
        Result.bReady = true;
        Result.Transform.SetLocation(FVector(25, 30, 40));
        Result.BeamStart = FVector(25, 30, 40);
        Result.BeamEnd = FVector(500, 300, 40);
        return Result;
    }

    /** 统一测试两种现有宿主，不通过另一套表现 Actor 逃避兼容范围。 */
    void Begin(AActor* Host)
    {
        if (Host && !Host->HasActorBegunPlay())
        {
            Host->DispatchBeginPlay();
        }
    }

    const FAbilityPrototypePresentationStats& Stats(AActor* Host)
    {
        if (const auto* Actor = Cast<APrototypeRuntime>(Host))
        {
            return Actor->GetPresentationStats();
        }
        return CastChecked<APureMassRuntime>(Host)->GetPresentationStats();
    }

    bool Finished(AActor* Host)
    {
        if (const auto* Actor = Cast<APrototypeRuntime>(Host))
        {
            return Actor->IsFinished();
        }
        return CastChecked<APureMassRuntime>(Host)->IsFinished();
    }

    void AttachContext(AActor* Host, UPrototypeCastContext* Binding)
    {
        if (auto* Actor = Cast<APrototypeRuntime>(Host))
        {
            Actor->Context = Binding;
        }
        else
        {
            CastChecked<APureMassRuntime>(Host)->Context = Binding;
        }
    }

    /** 仅用于验证 Driver 不存在，因此既不能在 BeginPlay Start，也不能在 Tick Advance。 */
    bool HasDriver(AActor* Host)
    {
        if (const auto* Actor = Cast<APrototypeRuntime>(Host))
        {
            return Actor->GetDriver().IsValid();
        }
        return CastChecked<APureMassRuntime>(Host)->GetDriver().IsValid();
    }

    void ApplyReplica(AActor* Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                      const FAbilityPrototypePresentationFrame& InFrame)
    {
        if (auto* Actor = Cast<APrototypeRuntime>(Host))
        {
            Actor->ApplyPresentationReplica(Snapshot, InFrame);
        }
        else
        {
            CastChecked<APureMassRuntime>(Host)->ApplyPresentationReplica(Snapshot, InFrame);
        }
    }

    /** 使用生产公开入口模拟包到达顺序；不修改私有标志或直接调用旧表现函数。 */
    AActor* Replica(FFixture& F, EProfile Profile, const FAbilityPrototypeVisualSnapshot& Snapshot,
                    const FAbilityPrototypePresentationFrame& InFrame)
    {
        AActor* Host = Profile == EProfile::Actor ? static_cast<AActor*>(F.World->SpawnActorDeferred<APrototypeRuntime>(
                                                        APrototypeRuntime::StaticClass(), InFrame.Transform))
                                                  : static_cast<AActor*>(F.World->SpawnActorDeferred<APureMassRuntime>(
                                                        APureMassRuntime::StaticClass(), InFrame.Transform));
        if (Host)
        {
            ApplyReplica(Host, Snapshot, InFrame);
            Host->FinishSpawning(InFrame.Transform);
        }
        return Host;
    }

    /** 每个宿主有固定命名的主 Niagara 组件，排除 Ring 的 InnerVisual。 */
    UNiagaraComponent* Visual(AActor* Host)
    {
        TArray<UNiagaraComponent*> Components;
        Host->GetComponents(Components);
        for (auto* Component : Components)
        {
            if (Component->GetFName() == TEXT("Visual"))
            {
                return Component;
            }
        }
        return nullptr;
    }

    int32 RuntimeCount(UWorld* World)
    {
        int32 Result = 0;
        for (TActorIterator<APrototypeRuntime> It(World); It; ++It)
        {
            ++Result;
        }
        for (TActorIterator<APureMassRuntime> It(World); It; ++It)
        {
            ++Result;
        }
        return Result;
    }
} // namespace AbilityPrototypePresentationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypePresentationNoGameplay,
                                 "test_25d.UnifiedGA.DynamicMage.Presentation.ActorAndMixedBurstNeverRunsGameplay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationNoGameplay::RunTest(const FString& Parameters)
{
    using namespace AbilityPrototypePresentationTests;
    for (EProfile Profile : {EProfile::Actor, EProfile::Mixed})
    {
        FFixture F;
        auto* Config = F.Prototype(EAbilityPrototypeKind::Burst);
        Config->OnHit = F.Prototype(EAbilityPrototypeKind::Burst);
        Config->OnExpire = F.Prototype(EAbilityPrototypeKind::Burst);
        auto* Binding = F.Context();
        FString Error;
        auto* Host =
            AbilityPrototypePresentationService::Begin(F.SourceToken, *Config, nullptr, Frame(), Profile, Error);
        if (!TestNotNull(TEXT("Only presentation factory supplies existing runtime host"), Host))
        {
            continue;
        }
        AttachContext(Host, Binding);
        Begin(Host);
        TestFalse(TEXT("Presentation host cannot Start/Advance an authority driver"), HasDriver(Host));
        Host->Tick(1.f);
        AbilityPrototypePresentationService::Finish(Host, false, true);
        Host->Tick(1.f);
        TestEqual(TEXT("Burst presentation never applies actual damage GE"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  100.f);
        TestEqual(TEXT("No OnHit/OnExpire runtime is generated"), RuntimeCount(F.World), 1);
        TestEqual(TEXT("Presentation is not a logical Context spawn"), Binding->Spawned, 0);
        TestFalse(TEXT("Finishing a presentation does not cancel gameplay context"), Binding->bCancelled);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypePresentationFrozenValues,
    "test_25d.UnifiedGA.DynamicMage.Presentation.FrozenGeometryAndScaleReachRealComponents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationFrozenValues::RunTest(const FString& Parameters)
{
    using namespace AbilityPrototypePresentationTests;
    auto* System = LoopSystem();
    if (!TestNotNull(TEXT("Existing stable Niagara resource"), System))
    {
        return false;
    }
    for (EProfile Profile : {EProfile::Actor, EProfile::Mixed})
    {
        FFixture F;
        auto* Original = F.Prototype(EAbilityPrototypeKind::Ring);
        Original->Radius = 100.f;
        Original->LoopVFX = System;
        auto* Frozen = F.Prototype(EAbilityPrototypeKind::Ring);
        Frozen->Radius = 750.f;
        Frozen->InnerRadius = 250.f;
        Frozen->LoopVFX = System;
        FString Error;
        auto* Host =
            AbilityPrototypePresentationService::Begin(F.SourceToken, *Frozen, Original, Frame(), Profile, Error);
        if (!TestNotNull(TEXT("Frozen ring presentation host"), Host))
        {
            continue;
        }
        Begin(Host);
        auto* Component = Visual(Host);
        if (!TestNotNull(TEXT("Real main Niagara component"), Component))
        {
            continue;
        }
        TestTrue(TEXT("Component uses stable particle resource"), Component->GetAsset() == System);
        TestTrue(TEXT("Ring consumes frozen750 radius, not original100"),
                 Component->GetRelativeScale3D().Equals(FVector(7.5, 7.5, .25)));
        Frozen->Radius = 50.f;
        Original->Radius = 25.f;
        Host->Tick(.2f);
        TestTrue(TEXT("Already spawned snapshot is independent of both DA mutations"),
                 Component->GetRelativeScale3D().Equals(FVector(7.5, 7.5, .25)));
    }

    FFixture F;
    auto* Frozen = NewObject<UBatch6FieldPrototype>(F.World);
    Frozen->Kind = EAbilityPrototypeKind::Field;
    Frozen->DamageType = FMyGameplayTags::Get().Damage_Magic;
    Frozen->Radius = 400.f;
    Frozen->LoopVFX = System;
    Frozen->LoopVFXScale = FVector(3, 4, 5);
    Frozen->ImpactVFX = System;
    Frozen->ImpactVFXScale = FVector(4, 5, 6);
    Frozen->bUseAuthoredVFXScale = true;
    Frozen->bShowAreaDisc = true;
    Frozen->AreaMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    TestNotNull(TEXT("Stable area material"), Frozen->AreaMaterial.Get());
    FString Error;
    auto* Host =
        AbilityPrototypePresentationService::Begin(F.SourceToken, *Frozen, nullptr, Frame(), EProfile::Mixed, Error);
    if (TestNotNull(TEXT("Batch6 authored-scale presentation"), Host))
    {
        Begin(Host);
        TestTrue(TEXT("Actual Niagara consumes frozen authored LoopVFXScale"),
                 Visual(Host) && Visual(Host)->GetRelativeScale3D().Equals(FVector(3, 4, 5)));
        auto* Area = Host->FindComponentByClass<UStaticMeshComponent>();
        TestTrue(TEXT("Actual area disc uses frozen radius and material"),
                 Area && Area->GetComponentScale().Equals(FVector(8, 8, .01)) &&
                     Area->GetMaterial(0) == Frozen->AreaMaterial);
        const FVector Contact(700, 800, 90);
        AbilityPrototypePresentationService::Impact(Host, 1, Contact);
        bool bActualImpactUsesFrozenScale = false;
        for (TObjectIterator<UNiagaraComponent> It; It; ++It)
        {
            if (It->GetWorld() == F.World && It->GetAsset() == System && It->GetComponentLocation().Equals(Contact) &&
                It->GetComponentScale().Equals(FVector(4, 5, 6)))
            {
                bActualImpactUsesFrozenScale = true;
                break;
            }
        }
        TestTrue(TEXT("Actual spawned impact consumes frozen authored ImpactVFXScale"), bActualImpactUsesFrozenScale);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypePresentationLifecycle,
                                 "test_25d.UnifiedGA.DynamicMage.Presentation.UpdateImpactFinishAreIdempotent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationLifecycle::RunTest(const FString& Parameters)
{
    using namespace AbilityPrototypePresentationTests;
    auto* System = LoopSystem();
    if (!TestNotNull(TEXT("Existing stable Niagara resource"), System))
    {
        return false;
    }
    for (EProfile Profile : {EProfile::Actor, EProfile::Mixed})
    {
        FFixture F;
        auto* Config = F.Prototype(EAbilityPrototypeKind::Wave);
        Config->LoopVFX = Config->ImpactVFX = System;
        FString Error;
        auto* Host =
            AbilityPrototypePresentationService::Begin(F.SourceToken, *Config, nullptr, Frame(), Profile, Error);
        if (!TestNotNull(TEXT("Wave presentation"), Host))
        {
            continue;
        }
        Begin(Host);
        auto Next = Frame();
        Next.Revision = 2;
        Next.Transform.SetLocation(FVector(40, 50, 60));
        Next.WaveRadius = 450.f;
        TestTrue(TEXT("New frame accepted"), AbilityPrototypePresentationService::Update(Host, Next));
        AbilityPrototypePresentationService::Update(Host, Next);
        TestEqual(TEXT("Duplicate frame does not rerun update"), Stats(Host).Updates, 1);
        const FVector ExpectedWaveScale = Profile == EProfile::Actor ? FVector(4.5, 4.5, .25) : FVector(4.5);
        TestTrue(TEXT("Wave frame reaches real Niagara scale"),
                 Visual(Host) && Visual(Host)->GetRelativeScale3D().Equals(ExpectedWaveScale));
        TestTrue(TEXT("Frame updates actor transform"), Host->GetActorLocation().Equals(FVector(40, 50, 60)));
        AbilityPrototypePresentationService::Impact(Host, 7, FVector(100, 20, 30));
        AbilityPrototypePresentationService::Impact(Host, 7, FVector(100, 20, 30));
        TestEqual(TEXT("Impact event identity is deduplicated"), Stats(Host).Impacts, 1);
        AbilityPrototypePresentationService::Finish(Host, true, true);
        AbilityPrototypePresentationService::Finish(Host, true, true);
        TestEqual(TEXT("Finish executes once"), Stats(Host).Finishes, 1);
        TestEqual(TEXT("Cancelled finish never creates implicit impact"), Stats(Host).Impacts, 1);
        TestTrue(TEXT("Host is terminal"), Finished(Host));
        TestTrue(TEXT("Loop is inactive after cancellation"), Visual(Host) && !Visual(Host)->IsActive());
        AbilityPrototypePresentationService::Impact(Host, 8, FVector::ZeroVector);
        Next.Revision = 3;
        AbilityPrototypePresentationService::Update(Host, Next);
        TestEqual(TEXT("Late impact stays suppressed"), Stats(Host).Impacts, 1);
        TestEqual(TEXT("Late frame cannot update terminated host"), Stats(Host).Updates, 1);

        auto* Normal =
            AbilityPrototypePresentationService::Begin(F.SourceToken, *Config, nullptr, Frame(), Profile, Error);
        if (TestNotNull(TEXT("Independent normal completion presentation"), Normal))
        {
            Begin(Normal);
            AbilityPrototypePresentationService::Finish(Normal, false, true);
            AbilityPrototypePresentationService::Finish(Normal, false, true);
            TestEqual(TEXT("Normal finish presents once"), Stats(Normal).Finishes, 1);
            TestEqual(TEXT("Normal finish impact presents once"), Stats(Normal).Impacts, 1);
            TestTrue(TEXT("Normal completion also deactivates loop"), Visual(Normal) && !Visual(Normal)->IsActive());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypePresentationLateReplica,
    "test_25d.UnifiedGA.DynamicMage.Presentation.ReplicaWaitsForResourcesFrameAndLateOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationLateReplica::RunTest(const FString& Parameters)
{
    using namespace AbilityPrototypePresentationTests;
    auto* System = LoopSystem();
    if (!TestNotNull(TEXT("Existing stable Niagara resource"), System))
    {
        return false;
    }
    for (EProfile Profile : {EProfile::Actor, EProfile::Mixed})
    {
        FFixture F;
        auto* Config = F.Prototype(EAbilityPrototypeKind::Beam);
        Config->LoopVFX = System;
        auto Snapshot = AbilityPrototypePresentationService::Capture(*Config, nullptr, Profile);
        Snapshot.LoopVFX = nullptr;
        auto IncompleteFrame = Frame();
        IncompleteFrame.bReady = false;
        auto* Host = Replica(F, Profile, Snapshot, IncompleteFrame);
        if (!TestNotNull(TEXT("Replicated host before resource/owner resolution"), Host))
        {
            continue;
        }
        Begin(Host);
        TestEqual(TEXT("Incomplete frame cannot initialize particle state"), Stats(Host).Begins, 0);
        ApplyReplica(Host, Snapshot, Frame());
        TestEqual(TEXT("Unresolved expected resource still waits"), Stats(Host).Begins, 0);
        Snapshot.LoopVFX = System;
        ApplyReplica(Host, Snapshot, Frame());
        TestEqual(TEXT("Complete frame and resource initialize once without Owner"), Stats(Host).Begins, 1);
        TestTrue(TEXT("Actual component resolves late stable resource"),
                 Visual(Host) && Visual(Host)->GetAsset() == System);
        if (auto* Component = Visual(Host))
        {
            bool bHasEnd = false;
            const FVector ActualEnd = Component->GetVariableVec3(TEXT("User.BeamEnd"), bHasEnd);
            TestTrue(TEXT("Explicit endpoint reaches Niagara before late Owner resolution"),
                     bHasEnd && ActualEnd.Equals(Frame().BeamEnd));
        }
        Host->SetOwner(F.Source);
        ApplyReplica(Host, Snapshot, Frame());
        Begin(Host);
        Host->Tick(.1f);
        TestEqual(TEXT("Late owner and duplicate replication do not replay Begin"), Stats(Host).Begins, 1);
        TestFalse(TEXT("Replica never creates a gameplay driver"), HasDriver(Host));
        TestTrue(TEXT("Frozen frame origin remains independent of late Owner"),
                 Host->GetActorLocation().Equals(Frame().Transform.GetLocation()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypePresentationFinishFirst,
                                 "test_25d.UnifiedGA.DynamicMage.Presentation.FinishedFirstReplicaCannotReactivate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationFinishFirst::RunTest(const FString& Parameters)
{
    using namespace AbilityPrototypePresentationTests;
    auto* System = LoopSystem();
    if (!TestNotNull(TEXT("Existing stable Niagara resource"), System))
    {
        return false;
    }
    for (EProfile Profile : {EProfile::Actor, EProfile::Mixed})
    {
        FFixture F;
        auto* Config = F.Prototype(EAbilityPrototypeKind::Burst);
        Config->LoopVFX = Config->ImpactVFX = System;
        auto Snapshot = AbilityPrototypePresentationService::Capture(*Config, nullptr, Profile);
        Snapshot.LoopVFX = Snapshot.ImpactVFX = nullptr;
        auto Last = Frame();
        Last.Revision = 8;
        Last.bFinished = Last.bCancelled = Last.bImpactOnFinish = true;
        auto* Host = Replica(F, Profile, Snapshot, Last);
        if (!TestNotNull(TEXT("First packet contains cancelled finish"), Host))
        {
            continue;
        }
        Begin(Host);
        Snapshot.LoopVFX = Snapshot.ImpactVFX = System;
        ApplyReplica(Host, Snapshot, Last);
        Host->SetOwner(F.Source);
        ApplyReplica(Host, Snapshot, Frame());
        Host->Tick(.1f);
        TestTrue(TEXT("Finished state survives older begin frame"), Finished(Host));
        TestEqual(TEXT("Cancelled finish is presented once after resources arrive"), Stats(Host).Finishes, 1);
        TestEqual(TEXT("No initial Burst or finish impact leaks through cancellation"), Stats(Host).Impacts, 0);
        TestEqual(TEXT("No loop initialization after terminal first frame"), Stats(Host).Begins, 0);
        TestTrue(TEXT("Actual Niagara remains inactive"), Visual(Host) && !Visual(Host)->IsActive());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypePresentationSourceLife,
    "test_25d.UnifiedGA.DynamicMage.Presentation.OriginalSourceInvalidationOnlyEndsPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationSourceLife::RunTest(const FString& Parameters)
{
    using namespace AbilityPrototypePresentationTests;
    for (EProfile Profile : {EProfile::Actor, EProfile::Mixed})
    {
        FFixture F;
        auto* Config = F.Prototype(EAbilityPrototypeKind::Field);
        Config->OnExpire = F.Prototype(EAbilityPrototypeKind::Burst);
        auto* Binding = F.Context();
        FString Error;
        auto* Host =
            AbilityPrototypePresentationService::Begin(F.SourceToken, *Config, nullptr, Frame(), Profile, Error);
        if (!TestNotNull(TEXT("Presentation with original source stamp"), Host))
        {
            continue;
        }
        AttachContext(Host, Binding);
        Begin(Host);
        F.ASC->InitAbilityActorInfo(F.Source, F.Target);
        auto Next = Frame();
        Next.Revision = 2;
        TestFalse(TEXT("Cannot continue with rebound source Avatar"),
                  AbilityPrototypePresentationService::Update(Host, Next));
        TestTrue(TEXT("Only the presentation is closed"), Finished(Host));
        TestFalse(TEXT("Presentation service does not cancel unrelated shared gameplay Context"), Binding->bCancelled);
        TestEqual(TEXT("No OnExpire logical spawn"), Binding->Spawned, 0);
        TestEqual(TEXT("No business child Actor"), RuntimeCount(F.World), 1);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypePresentationSustainedImpactWindow,
    "test_25d.UnifiedGA.DynamicMage.Presentation.SustainedImpactMetadataAndLateResourcesStayBounded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationSustainedImpactWindow::RunTest(const FString&)
{
    using namespace AbilityPrototypePresentationTests;
    FFixture F;
    auto* Host = F.World->SpawnActor<AActor>();
    auto* Config = F.Prototype(EAbilityPrototypeKind::Field);
    auto* System = LoopSystem();
    if (!TestNotNull(TEXT("Real Niagara asset for resource-arrival replay"), System))
    {
        return false;
    }
    Config->ImpactVFX = System;
    auto Snapshot = AbilityPrototypePresentationService::Capture(*Config, nullptr, EProfile::Mixed);
    Snapshot.ImpactVFX = nullptr;
    FAbilityPrototypePresentationLocalState State;
    FAbilityPrototypePresentationStats Observed;
    AbilityPrototypePresentationService::AcceptFrame(Frame(), State, Observed);

    // 一个持续宿主等待资源期间收到大量真实服务事件。窗口只约束不可靠表现历史，
    // 不触碰施法次数、命中、伤害、Context 或原持续技能的游戏寿命。
    for (uint32 EventId = 1; EventId <= 10000; ++EventId)
    {
        if (EventId != 7)
        {
            if (!AbilityPrototypePresentationService::PresentImpact(*Host, Snapshot, State, Observed, EventId,
                                                                    FVector(EventId, 0, 0)))
            {
                AddError(TEXT("A new in-order presentation event must be accepted."));
                return false;
            }
        }
    }
    TestTrue(TEXT("Continuous-cast duplicate metadata is bounded by a 256-sequence window"),
             State.ImpactEvents.Num() <= 256);
    TestTrue(TEXT("Unresolved resources cannot accumulate an unbounded delayed particle backlog"),
             State.PendingImpacts.Num() <= 256);
    TestFalse(
        TEXT("An unseen but expired unreliable event cannot reappear much later"),
        AbilityPrototypePresentationService::PresentImpact(*Host, Snapshot, State, Observed, 7, FVector::ZeroVector));
    TestFalse(TEXT("A duplicate in the active sequence window remains rejected"),
              AbilityPrototypePresentationService::PresentImpact(*Host, Snapshot, State, Observed, 10000,
                                                                 FVector::ZeroVector));
    TestEqual(TEXT("Missing resources do not present incomplete impacts"), Observed.Impacts, 0);

    // 旧实现的无界队列已经构成明确失败；不要为复现内存问题再实际创建上万粒子系统。
    if (State.PendingImpacts.Num() > 256 || State.ImpactEvents.Num() > 256)
    {
        return false;
    }

    Snapshot.ImpactVFX = System;
    auto* VisualComponent = NewObject<UNiagaraComponent>(Host);
    VisualComponent->RegisterComponent();
    AbilityPrototypePresentationService::Refresh(*Host, Snapshot, State, Observed, VisualComponent);
    TestEqual(TEXT("Resource arrival replays only the recent 256-event presentation window"), Observed.Impacts, 256);
    TestEqual(TEXT("Resource arrival drains delayed metadata"), State.PendingImpacts.Num(), 0);
    TestTrue(TEXT("A new ready event still presents immediately after a long channel"),
             AbilityPrototypePresentationService::PresentImpact(*Host, Snapshot, State, Observed, 10002,
                                                                FVector::ZeroVector));
    TestTrue(TEXT("An unseen out-of-order event inside the window is preserved"),
             AbilityPrototypePresentationService::PresentImpact(*Host, Snapshot, State, Observed, 10001,
                                                                FVector::ZeroVector));
    TestEqual(TEXT("Recent reordering does not truncate either actual ready impact"), Observed.Impacts, 258);
    TestTrue(TEXT("Ready events also retain bounded duplicate metadata"), State.ImpactEvents.Num() <= 256);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypePresentationImpactWrap,
                                 "test_25d.UnifiedGA.DynamicMage.Presentation.ImpactSequenceWrapKeepsRecentReordering",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationImpactWrap::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Host = F.World->SpawnActor<AActor>();
    FAbilityPrototypeVisualSnapshot Snapshot;
    FAbilityPrototypePresentationLocalState State;
    FAbilityPrototypePresentationStats Observed;
    for (uint32 EventId : {MAX_uint32 - 1, 1u, MAX_uint32, 2u})
    {
        TestTrue(TEXT("New identities around wrap remain accepted, including recent reordering"),
                 AbilityPrototypePresentationService::PresentImpact(*Host, Snapshot, State, Observed, EventId,
                                                                    FVector::ZeroVector));
    }
    TestFalse(
        TEXT("Zero remains an invalid presentation identity"),
        AbilityPrototypePresentationService::PresentImpact(*Host, Snapshot, State, Observed, 0, FVector::ZeroVector));
    TestFalse(TEXT("Duplicate from before wrap remains rejected"),
              AbilityPrototypePresentationService::PresentImpact(*Host, Snapshot, State, Observed, MAX_uint32,
                                                                 FVector::ZeroVector));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypePresentationActualFinishPolicy,
    "test_25d.UnifiedGA.DynamicMage.Presentation.ActualOriginalFiniteFinishAndCancellationPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FPrototypePresentationActualFinishPolicy::RunTest(const FString&)
{
    using namespace AbilityPrototypePresentationTests;
    auto* System = LoopSystem();
    if (!TestNotNull(TEXT("Stable real Niagara impact resource"), System))
    {
        return false;
    }
    for (const bool bActor : {true, false})
    {
        for (const bool bCancel : {false, true})
        {
            int32 OriginalImpacts = INDEX_NONE;
            for (const bool bFine : {false, true})
            {
                FFixture F;
                // 只留角色胶囊，隔离血条等附属组件对原Actor物理范围查询的影响。
                for (AActor* Actor : {static_cast<AActor*>(F.Source), static_cast<AActor*>(F.Target)})
                {
                    TArray<UPrimitiveComponent*> Components;
                    Actor->GetComponents(Components);
                    for (auto* Component : Components)
                    {
                        if (!Component->IsA<UCapsuleComponent>())
                        {
                            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                        }
                    }
                }
                if (!AbilityPrototypeNiagaraTestFixture::Probe(*this, *F.World, System))
                {
                    return false;
                }
                auto* Parameters = F.Prototype(EAbilityPrototypeKind::Field);
                Parameters->Radius = 10.f;
                Parameters->Duration = .2f;
                Parameters->Period = .1f;
                Parameters->LoopVFX = nullptr;
                Parameters->ImpactVFX = System;
                FString Error;
                const FName Profile = bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1");
                auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(F.World, Parameters, Profile, Error)
                                         : UAbilityLegacyGraphCompiler::Compile(F.World, Parameters, Profile, Error);
                if (!TestNotNull(FString(TEXT("Actual finite presentation definition: ")) + Error, Definition))
                {
                    return false;
                }
                FAbilityNodeInput Input;
                Input.SourceToken = F.SourceToken;
                Input.Context = F.Context();
                Input.DamageParams = CastChecked<UPrototypeCastContext>(Input.Context.Get())->DamageParams;
                FAbilityExecutionOptions Options;
                Options.CompatibilityProfile = Definition->CompatibilityProfile;
                Options.BudgetPolicy = Definition->BudgetPolicy;
                auto* Run = NewObject<UAbilityGraphInstance>(F.World);
                if (!TestTrue(FString(TEXT("Actual finite presentation starts: ")) + Error,
                              Run->StartWithInput(Definition, Input, Options, Error)))
                {
                    return false;
                }
                for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
                {
                    Begin(*It);
                }
                for (TActorIterator<APureMassRuntime> It(F.World); It; ++It)
                {
                    Begin(*It);
                }
                auto CountActualImpacts = [&]()
                {
                    int32 Count = 0;
                    for (TObjectIterator<UNiagaraComponent> It; It; ++It)
                    {
                        Count += IsValid(*It) && It->IsRegistered() && It->GetWorld() == F.World &&
                                         It->GetAsset() == System && !It->IsTemplate()
                                     ? 1
                                     : 0;
                    }
                    return Count;
                };
                TestEqual(TEXT("Field starts without an implicit impact"), CountActualImpacts(), 0);
                if (bCancel)
                {
                    Run->Cancel();
                }
                else
                {
                    ++GFrameCounter;
                    Run->Advance(.2f);
                }
                const int32 ActualImpacts = CountActualImpacts();
                AddInfo(FString::Printf(TEXT("%s %s %s actual Niagara impacts=%d"),
                                        bFine ? TEXT("Fine") : TEXT("Original"), bActor ? TEXT("Actor") : TEXT("Mixed"),
                                        bCancel ? TEXT("cancel") : TEXT("normal"), ActualImpacts));
                if (!bFine)
                {
                    OriginalImpacts = ActualImpacts;
                    TestEqual(
                        TEXT(
                            "Original Actor has two pulse-center effects only on normal advance; Mixed always has one terminal effect"),
                        OriginalImpacts, bActor ? (bCancel ? 0 : 2) : 1);
                }
                else
                {
                    TestEqual(TEXT("Fine reconstruction preserves the actual original finish/cancel particle policy"),
                              ActualImpacts, OriginalImpacts);
                }
                TestEqual(
                    TEXT("Presentation fixture has no enemy inside the small field, so no gameplay damage occurs"),
                    F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                    100.f);
                Run->Cancel();
                TestEqual(TEXT("Repeated closure never creates another real Niagara impact"), CountActualImpacts(),
                          ActualImpacts);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypePresentationOriginalMixedFinishReplica,
    "test_25d.UnifiedGA.DynamicMage.Presentation.OriginalMixedFinishReplicaWaitsOnlyForImpact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePresentationOriginalMixedFinishReplica::RunTest(const FString&)
{
    using namespace AbilityPrototypePresentationTests;
    auto* System = LoopSystem();
    if (!TestNotNull(TEXT("Stable original impact resource"), System))
    {
        return false;
    }
    for (const bool bCancel : {false, true})
    {
        FFixture F;
        auto* Config = F.Prototype(EAbilityPrototypeKind::Field);
        Config->LoopVFX = Config->ImpactVFX = System;
        auto Snapshot = AbilityPrototypePresentationService::Capture(*Config, nullptr, EProfile::Mixed);
        Snapshot.LoopVFX = Snapshot.ImpactVFX = nullptr;
        auto Last = Frame();
        Last.Revision = 8;
        Last.bFinished = Last.bImpactOnFinish = Last.bOriginalMixedFinish = true;
        Last.bCancelled = bCancel;
        auto* Host = Replica(F, EProfile::Mixed, Snapshot, Last);
        if (!TestNotNull(TEXT("Original terminal frame may arrive before any loop/resource"), Host))
        {
            return false;
        }
        Begin(Host);
        TestEqual(TEXT("Original finish waits for its impact resource"), Stats(Host).Finishes, 0);
        Snapshot.ImpactVFX = System;
        ApplyReplica(Host, Snapshot, Last);
        TestEqual(TEXT("Unresolved closed loop does not delay original finish"), Stats(Host).Finishes, 1);
        TestEqual(TEXT("Original normal and cancelled finish each present exactly once"), Stats(Host).Impacts, 1);
        Snapshot.LoopVFX = System;
        ApplyReplica(Host, Snapshot, Frame());
        ApplyReplica(Host, Snapshot, Last);
        Host->Tick(.1f);
        TestEqual(TEXT("Late old frame/resource cannot replay terminal impact"), Stats(Host).Impacts, 1);
        TestEqual(TEXT("Terminal first never starts a loop"), Stats(Host).Begins, 0);
        TestTrue(TEXT("Closed loop remains inactive"), Visual(Host) && !Visual(Host)->IsActive());
        TestFalse(TEXT("No business driver is created"), HasDriver(Host));
    }
    return true;
}
#endif
