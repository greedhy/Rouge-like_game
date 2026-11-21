// 测试链：实际Surface资产的私有本次参数 -> 旧完整图/细默认编译 -> 真实配置TerrainActor、辅助片段和资源生命周期。
// Actor/Mixed独立世界对照；外部地表不得被取消或正常结束误删，不写任何原资产。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityNativePhaseScalarTestTypes.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Terrain/TerrainSurface.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeSurfaceFragmentTests
{
    /** 保留资产的地表类/表现引用，时钟与孩子只写本测试私有副本。 */
    UAbilityPrototype* ActualParameters(UObject* Outer, bool bActor)
    {
        const auto* Catalog = LoadObject<UAbilityInfo>(
            nullptr, bActor ? TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo")
                            : TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"));
        if (Catalog)
        {
            for (const auto& Row : Catalog->AbilityInformation)
            {
                const auto* Ability = Row.Ability ? Cast<UPrototypeAbility>(Row.Ability->GetDefaultObject()) : nullptr;
                if (Ability && Ability->Prototype && !Ability->Prototype->IsA<UExtensionNode>() &&
                    Ability->Prototype->Kind == EAbilityPrototypeKind::Surface)
                {
                    auto* Copy = DuplicateObject<UAbilityPrototype>(Ability->Prototype, Outer);
                    Copy->Cooldown = 0.f;
                    Copy->Duration = .5f;
                    Copy->Period = .125f;
                    auto Child = [&](float Damage)
                    {
                        auto* Result = NewObject<UAbilityPrototype>(Copy);
                        Result->Kind = EAbilityPrototypeKind::Burst;
                        Result->Damage = Damage;
                        Result->DamagePerLevel = 0.f;
                        Result->DamageType = UAbilityNativeThunderScalarEffect::GetDamageTag();
                        Result->Cooldown = 0.f;
                        Result->Duration = .1f;
                        Result->Radius = 200.f;
                        return Result;
                    };
                    Copy->OnHit = nullptr;
                    Copy->OnPeriod = Child(3.f);
                    Copy->OnExpire = Child(5.f);
                    return Copy;
                }
            }
        }
        return nullptr;
    }

    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Context = nullptr;
        ATerrainSurface* Foreign = nullptr;
        FWorld()
        {
            for (AEnemyCharacter* Enemy : {Source, Target})
            {
                for (auto* Primitive : TInlineComponentArray<UPrimitiveComponent*>(Enemy))
                {
                    Primitive->SetCollisionEnabled(Primitive == Enemy->GetCapsuleComponent()
                                                       ? ECollisionEnabled::QueryOnly
                                                       : ECollisionEnabled::NoCollision);
                }
                Enemy->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
                Enemy->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
                Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),
                                                                            100.f);
                Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),
                                                                            100.f);
            }
            Target->SetActorLocation(FVector(300, 0, 0));
            auto* Floor = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(2000, 2000, 50));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Floor->SetActorLocation(FVector(0, 0, -100));
            Foreign = World->SpawnActor<ATerrainSurface>(FVector(1200, 0, 0), FRotator::ZeroRotator);
        }
        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }
        bool Start(UAbilityPrototype* Parameters, bool bActor, bool bFine, FString& Error)
        {
            const FName Profile = bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1");
            auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(World, Parameters, Profile, Error)
                                     : UAbilityLegacyGraphCompiler::Compile(World, Parameters, Profile, Error);
            if (!Definition)
            {
                return false;
            }
            Context = NewObject<UPrototypeCastContext>(World);
            Context->Caster = Source;
            Context->CastId = FGuid::NewGuid();
            Context->Level = 1;
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            Context->DamageParams.DamageGameplayEffectClass = UAbilityNativeThunderScalarEffect::StaticClass();
            if (!Context->CaptureSourceLife(Error))
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = SourceToken;
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            Input.Origin = Input.EventCenter = FVector(300, 0, 0);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Run = NewObject<UAbilityGraphInstance>(World);
            return Run->StartWithInput(Definition, Input, Options, Error) &&
                   !(Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
        }
        ATerrainSurface* OwnedSurface() const
        {
            for (TActorIterator<ATerrainSurface> It(World); It; ++It)
            {
                if (It->GetOwner() == Source && !It->IsActorBeingDestroyed())
                {
                    return *It;
                }
            }
            return nullptr;
        }
        void Advance(float Delta)
        {
            ++GFrameCounter;
            Run->Advance(Delta);
        }
        float Health() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
    };
} // namespace AbilityPrototypeSurfaceFragmentTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeSurfaceResourceParity,
                                 "test_25d.DynamicMage.PrototypeSurface.ActualClassLifetimeAndIndependentHooks",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeSurfaceResourceParity::RunTest(const FString&)
{
    using namespace AbilityPrototypeSurfaceFragmentTests;
    for (const bool bActor : {true, false})
    {
        float BaselineHealth = 0.f;
        int32 BaselineLogical = 0;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            auto* Parameters = ActualParameters(F.World, bActor);
            if (!TestNotNull(TEXT("Actual catalog supplies the configured prototype Surface"), Parameters))
            {
                return false;
            }
            FString Error;
            if (!TestTrue(
                    *FString::Printf(TEXT("%s Surface starts: %s"), bFine ? TEXT("Fine") : TEXT("Original"), *Error),
                    F.Start(Parameters, bActor, bFine, Error)))
            {
                AddInfo(Error);
                continue;
            }
            auto* Surface = F.OwnedSurface();
            if (!TestNotNull(TEXT("One real configured terrain resource belongs to the caster"), Surface))
            {
                continue;
            }
            TestTrue(TEXT("No substitute terrain class replaces the original asset"),
                     Surface->GetClass() == Parameters->SurfaceClass.Get());
            TestTrue(TEXT("Same actual diameter is set before terrain construction"),
                     Surface->SurfaceSize.Equals(FVector2D(Parameters->Radius * 2.f)));
            TestTrue(TEXT("Original caster-enemy-only rule remains active"), Surface->bOnlyAffectCasterEnemies);
            TestEqual(TEXT("Original surface ground mesh remains noncolliding"), Surface->Ground->GetCollisionEnabled(),
                      ECollisionEnabled::NoCollision);
            F.Advance(.25f);
            F.Advance(.25f);
            TestNull(TEXT("Normal duration releases this exact surface"), F.OwnedSurface());
            TestFalse(TEXT("Normal completion preserves unrelated world surfaces"), F.Foreign->IsActorBeingDestroyed());
            F.Advance(.2f);
            TestTrue(TEXT("Surface and independently emitted children finish"), F.Run->IsFinished());
            if (!bFine)
            {
                BaselineHealth = F.Health();
                BaselineLogical = F.Context->Spawned;
                TestTrue(TEXT("Original periodic/expiry hooks cause actual independent effects"),
                         BaselineHealth < 100.f && BaselineLogical > 1);
            }
            else
            {
                TestEqual(TEXT("Fine keeps actual periodic and expiry effect count"), F.Health(), BaselineHealth);
                TestEqual(TEXT("Fine does not duplicate resource creation as another logical fragment"),
                          F.Context->Spawned, BaselineLogical);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeSurfaceCancelOwnership,
                                 "test_25d.DynamicMage.PrototypeSurface.CancelReleasesOnlyOwnedTerrainWithoutExpiry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeSurfaceCancelOwnership::RunTest(const FString&)
{
    using namespace AbilityPrototypeSurfaceFragmentTests;
    for (const bool bActor : {true, false})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            auto* Parameters = ActualParameters(F.World, bActor);
            if (!TestNotNull(TEXT("Actual surface parameters exist"), Parameters))
            {
                return false;
            }
            FString Error;
            if (!TestTrue(TEXT("Cancellable surface starts"), F.Start(Parameters, bActor, bFine, Error)))
            {
                AddInfo(Error);
                continue;
            }
            TestNotNull(TEXT("Cancellation exercises a real owned surface"), F.OwnedSurface());
            F.Run->Cancel();
            F.Advance(2.f);
            TestNull(TEXT("Cancellation releases its own terrain"), F.OwnedSurface());
            TestFalse(TEXT("Other terrain is never borrowed for cleanup"), F.Foreign->IsActorBeingDestroyed());
            TestEqual(TEXT("Cancel suppresses normal expiry and period payloads"), F.Health(), 100.f);
            TestEqual(TEXT("Cancel admits no further logical fragment"), F.Context->Spawned, 1);
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeSurfaceHitchCadence,
                                 "test_25d.DynamicMage.PrototypeSurface.ActorSingleAndMixedDoubleCatchupHitch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeSurfaceHitchCadence::RunTest(const FString&)
{
    using namespace AbilityPrototypeSurfaceFragmentTests;
    for (const bool bActor : {true, false})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            auto* Parameters = ActualParameters(F.World, bActor);
            if (!TestNotNull(TEXT("Actual profile Surface supplies the original resource class"), Parameters))
            {
                return false;
            }
            Parameters->Duration = 10.f;
            Parameters->Period = .1f;
            FString Error;
            if (!TestTrue(FString::Printf(TEXT("%s %s hitch surface starts: %s"),
                                          bFine ? TEXT("Fine") : TEXT("Original"),
                                          bActor ? TEXT("Actor") : TEXT("Mixed"), *Error),
                          F.Start(Parameters, bActor, bFine, Error)))
            {
                continue;
            }
            F.Advance(4.f);
            const int32 ExpectedPulses = bActor ? 1 : 16;
            TestEqual(TEXT("Actual Surface cadence preserves Actor fmod and both original Mixed max8 passes"),
                      F.Health(), 100.f - ExpectedPulses * 3.f);
            TestEqual(TEXT("Each actually admitted periodic child owns one original logical spawn"), F.Context->Spawned,
                      1 + ExpectedPulses);
            F.Advance(7.f);
            TestEqual(TEXT("Hard lifetime skips the final overdue periods, then emits one actual expiry child"),
                      F.Health(), 95.f - ExpectedPulses * 3.f);
            TestNull(TEXT("Terrain closes before independent expiry payload settles"), F.OwnedSurface());
            TestEqual(TEXT("Surface, periodic children and one expiry child account exactly"), F.Context->Spawned,
                      2 + ExpectedPulses);
            F.Advance(.2f);
            TestTrue(TEXT("Complete hitch scenario terminates normally"), F.Run->IsFinished());
            TestTrue(TEXT("Surface hitch does not substitute helper exhaustion for original cadence"),
                     F.Run->GetEndReason() == EAbilityGraphEndReason::Completed);
        }
    }
    return true;
}
#endif
