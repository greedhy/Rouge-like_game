// 调用链：Extension 纯表现服务 -> 实际旧 Runtime/Niagara -> 复制值接收和原生命清理。
// 直接服务测试不代表九个真实技能已接入，也不替代实际联机/PIE视觉验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "Components/CapsuleComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Script.h"
#include "MyGameplayTags.h"

namespace AbilityExtensionPresentationTests
{
    using EProfile = EAbilityPrototypePresentationProfile;

    UNiagaraSystem* System()
    {
        return LoadObject<UNiagaraSystem>(
            nullptr, TEXT("/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion.NS_FireExplosion"));
    }

    FAbilityPrototypePresentationFrame Frame()
    {
        FAbilityPrototypePresentationFrame Result;
        Result.bReady = true;
        Result.Transform.SetLocation(FVector(25, 30, 40));
        Result.BeamStart = FVector(25, 30, 40);
        Result.BeamEnd = FVector(500, 300, 40);
        return Result;
    }

    void BeginPlay(AActor& Host)
    {
        if (!Host.HasActorBegunPlay())
        {
            Host.DispatchBeginPlay();
        }
    }

    UExtensionNode* Parameters(UObject* Outer, EExtensionKind Behavior)
    {
        auto* Result = NewObject<UExtensionNode>(Outer);
        Result->Behavior = Behavior;
        Result->Radius = 600.f;
        Result->Damage = 25.f;
        Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Result->Cooldown = 0.f;
        Result->LoopVFX = System();
        Result->ImpactVFX = System();
        return Result;
    }

    int32 Impacts(UWorld* World, UNiagaraSystem* Asset, FVector Position)
    {
        int32 Count = 0;
        for (TObjectIterator<UNiagaraComponent> It; It; ++It)
        {
            if (It->GetWorld() == World && It->GetAsset() == Asset && It->GetComponentLocation().Equals(Position, .01))
            {
                ++Count;
            }
        }
        return Count;
    }
} // namespace AbilityExtensionPresentationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FExtensionPresentationHasNoGameplay,
    "test_25d.UnifiedGA.DynamicMage.ExtensionPresentation.PureBurstHasNoDriverDamageChildrenOrBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExtensionPresentationHasNoGameplay::RunTest(const FString&)
{
    using namespace AbilityExtensionPresentationTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    auto* Root = Parameters(F.World, EExtensionKind::Burst);
    auto* Binding = NewObject<UExtensionCastContext>(F.World);
    Binding->Caster = F.Source;
    Binding->Level = 1;
    Binding->DamageParams.SourceAbilitySystemComponent = F.ASC;
    Binding->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
    F.Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    F.Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    FString Error;
    if (!TestTrue(TEXT("Business Context has a real original source life for the no-side-effect test"),
                  Binding->CaptureSourceLife(Error)))
    {
        return false;
    }
    auto* Host = Cast<AExtensionRuntime>(
        AbilityPrototypePresentationService::Begin(F.SourceToken, *Root, nullptr, Frame(), EProfile::Extension, Error));
    if (!TestNotNull(TEXT("Actual Extension presentation-only host: ") + Error, Host))
    {
        return false;
    }
    Host->Context = Binding;
    BeginPlay(*Host);
    Host->Tick(.25f);
    TestFalse(TEXT("Pure presentation does not retain a business Driver"), Host->GetDriver().IsValid());
    TestEqual(TEXT("Actual nearby Actor receives no GE damage"),
              TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    TestEqual(TEXT("Pure visual never reserves a logical spawn or produces children"), Binding->Spawned, 0);
    auto* Visual = Host->FindComponentByClass<UNiagaraComponent>();
    TestTrue(TEXT("Real Niagara component uses the frozen resource"), Visual && Visual->GetAsset() == System());
    F.Source->Destroy();
    Host->Tick(.1f);
    TestTrue(TEXT("Original source life removal closes its actual visual"), Host->bFinished);
    TestFalse(TEXT("Presentation source cleanup does not cancel shared business Context"), Binding->bCancelled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FExtensionPresentationFrozenGeometry,
    "test_25d.UnifiedGA.DynamicMage.ExtensionPresentation.AttachAndDisplaceUseFrozenOwnGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExtensionPresentationFrozenGeometry::RunTest(const FString&)
{
    using namespace AbilityExtensionPresentationTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    for (EExtensionKind Behavior : {EExtensionKind::Attach, EExtensionKind::Displace})
    {
        auto* Original = Parameters(F.World, Behavior);
        Original->Radius = 250.f;
        auto* Frozen = DuplicateObject(Original, F.World);
        Frozen->Radius = 600.f;
        FString Error;
        auto* Host = Cast<AExtensionRuntime>(AbilityPrototypePresentationService::Begin(
            F.SourceToken, *Frozen, Original, Frame(), EProfile::Extension, Error));
        if (!TestNotNull(TEXT("Extension visual factory: ") + Error, Host))
        {
            return false;
        }
        BeginPlay(*Host);
        auto* Visual = Host->FindComponentByClass<UNiagaraComponent>();
        TestNotNull(TEXT("Actual existing Extension Niagara component"), Visual);
        TestEqual(TEXT("Behavior is not inferred from unrelated inherited Kind"),
                  Host->GetPresentationSnapshot().ExtensionBehavior, Behavior);
        TestEqual(TEXT("Snapshot uses private frozen radius"), Host->GetPresentationSnapshot().Radius, 600.f);
        if (Visual)
        {
            TestTrue(TEXT("Attach retains its original head offset"),
                     Behavior != EExtensionKind::Attach || Visual->GetRelativeLocation().Equals(FVector(0, 0, 85)));
            TestTrue(TEXT("Displace retains its original XY radius/Z scale"),
                     Behavior != EExtensionKind::Displace || Visual->GetRelativeScale3D().Equals(FVector(6, 6, .3)));
        }
        Frozen->Radius = 60.f;
        Original->Radius = 30.f;
        Host->Tick(.1f);
        TestEqual(TEXT("Already spawned visual never rereads either DA's mutable geometry"),
                  Host->GetPresentationSnapshot().Radius, 600.f);
        if (Visual && Behavior == EExtensionKind::Displace)
        {
            TestTrue(TEXT("Real component also remains frozen"),
                     Visual->GetRelativeScale3D().Equals(FVector(6, 6, .3)));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FExtensionPresentationContactAndFinish,
    "test_25d.UnifiedGA.DynamicMage.ExtensionPresentation.ChainEndpointsContactsAndExplicitCancelEndImpact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExtensionPresentationContactAndFinish::RunTest(const FString&)
{
    using namespace AbilityExtensionPresentationTests;
    for (const bool bCancelled : {false, true})
    {
        FEditorScriptExecutionGuard Guard;
        FAbilityGraphTestFixture F;
        auto* Root = Parameters(F.World, EExtensionKind::Chain);
        FString Error;
        auto* Host = Cast<AExtensionRuntime>(AbilityPrototypePresentationService::Begin(
            F.SourceToken, *Root, nullptr, Frame(), EProfile::Extension, Error));
        if (!TestNotNull(TEXT("Actual pure Extension chain host: ") + Error, Host))
        {
            return false;
        }
        BeginPlay(*Host);
        auto Next = Frame();
        Next.Revision = 2;
        Next.Transform.SetLocation(FVector(40, 50, 60));
        Next.BeamEnd = FVector(600, 350, 60);
        TestTrue(TEXT("New presentation frame is accepted"), AbilityPrototypePresentationService::Update(Host, Next));
        AbilityPrototypePresentationService::Update(Host, Next);
        TestTrue(TEXT("Host follows its explicit visual frame"), Host->GetActorLocation().Equals(FVector(40, 50, 60)));
        auto* Visual = Host->FindComponentByClass<UNiagaraComponent>();
        bool bHasEnd = false;
        TestTrue(TEXT("Actual beam receives its original Niagara endpoint key"),
                 Visual && Visual->GetVariableVec3(TEXT("Beam End"), bHasEnd).Equals(Next.BeamEnd) && bHasEnd);
        const FVector Contact(700, 800, 90);
        AbilityPrototypePresentationService::Impact(Host, 7, Contact);
        AbilityPrototypePresentationService::Impact(Host, 7, Contact);
        TestEqual(TEXT("Actual contact identity is deduplicated"), Impacts(F.World, System(), Contact), 1);
        AbilityPrototypePresentationService::Finish(Host, bCancelled, true);
        AbilityPrototypePresentationService::Finish(Host, bCancelled, true);
        TestEqual(TEXT("Explicit Extension finish impact preserves the original cancel profile too"),
                  Host->GetPresentationStats().Impacts, 2);
        TestEqual(TEXT("Finish transition executes once"), Host->GetPresentationStats().Finishes, 1);
        TestTrue(TEXT("Terminal visual does not reopen"), Host->bFinished);
        AbilityPrototypePresentationService::Impact(Host, 8, Contact);
        TestEqual(TEXT("A late contact cannot add another impact"), Host->GetPresentationStats().Impacts, 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FExtensionPresentationLateCancelledReplica,
    "test_25d.UnifiedGA.DynamicMage.ExtensionPresentation.CancelTerminalWaitsForLateImpactResource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExtensionPresentationLateCancelledReplica::RunTest(const FString&)
{
    using namespace AbilityExtensionPresentationTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    auto* Root = Parameters(F.World, EExtensionKind::Attach);
    const auto Complete = AbilityPrototypePresentationService::Capture(*Root, nullptr, EProfile::Extension);
    TestTrue(TEXT("Frozen Extension values are a valid pure visual snapshot"), Complete.bReady);
    auto Pending = Complete;
    Pending.LoopVFX = nullptr;
    Pending.ImpactVFX = nullptr;
    auto Terminal = Frame();
    Terminal.Revision = 3;
    Terminal.Transform.SetLocation(FVector(700, 800, 90));
    Terminal.bFinished = Terminal.bCancelled = Terminal.bImpactOnFinish = true;
    auto* Host = F.World->SpawnActor<AExtensionRuntime>();
    Host->ApplyPresentationReplica(Pending, Terminal);
    BeginPlay(*Host);
    TestEqual(TEXT("Missing resource does not consume the requested cancelled end impact"),
              Host->GetPresentationStats().Impacts, 0);
    Host->ApplyPresentationReplica(Complete, Frame());
    Host->ApplyPresentationReplica(Complete, Frame());
    TestTrue(TEXT("Late nonterminal frame cannot undo terminal cancellation"), Host->bFinished);
    auto* Visual = Host->FindComponentByClass<UNiagaraComponent>();
    TestTrue(TEXT("Late LoopVFX never activates after terminal frame"), Visual && !Visual->IsActive());
    TestEqual(TEXT("Late ImpactVFX resolves one end effect at the terminal position"),
              Impacts(F.World, System(), Terminal.Transform.GetLocation()), 1);
    TestEqual(TEXT("Replica end impact is presented exactly once"), Host->GetPresentationStats().Impacts, 1);
    TestEqual(TEXT("Replica finish transition is not replayed by duplicate resources"),
              Host->GetPresentationStats().Finishes, 1);
    TestFalse(TEXT("Replica value path never enables a business Driver"), Host->GetDriver().IsValid());
    return true;
}
#endif
