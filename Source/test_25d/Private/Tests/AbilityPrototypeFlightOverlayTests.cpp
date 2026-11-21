// 测试链：原真实LegacyGraph对照/显式飞行覆盖细图 -> 实际碰撞与独立批载荷 -> Mass真实血量。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFlightNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace AbilityPrototypeFlightOverlayTests
{
    struct FFixture : FAbilityGraphTestFixture
    {
        UPureMassSubsystem* Mass = nullptr;
        FFixture()
        {
            Target->Destroy();
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
        }
        int32 Spawn(FVector Position)
        {
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 200.f;
            Type->XPReward = 0;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            return Mass->SpawnMonster(Type, Position);
        }
        UAbilityPrototype* CreateParameters(EAbilityPrototypeKind Kind)
        {
            auto* Config = NewObject<UAbilityPrototype>(World);
            Config->Kind = Kind;
            Config->DamageType = FMyGameplayTags::Get().Damage_Fire;
            Config->Damage = 40.f;
            Config->DamagePerLevel = 0.f;
            Config->Cooldown = 0.f;
            Config->Duration = 2.f;
            Config->Period = .1f;
            Config->Speed = 1000.f;
            Config->Range = 400.f;
            Config->CollisionRadius = 2.f;
            Config->Count = 1;
            return Config;
        }
        FGuid Add(UAbilityGraphDefinition& Definition, FName Type, UAbilityNodeConfig* Config)
        {
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = Type;
            Node.Config = Config;
            Definition.Nodes.Add(Node);
            return Node.NodeId;
        }
        UAbilityGraphDefinition* Fine(UAbilityPrototype& Parameters, UAbilityPrototypeFlightNodeConfig*& Flight,
                                      bool bSingle)
        {
            auto* Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = TEXT("PrototypeFine.Mixed.v1");
            Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
            auto* Scope = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
            Scope->Parameters = &Parameters;
            Scope->Profile = Definition->CompatibilityProfile;
            Scope->bSingleProjectile = bSingle;
            const auto Entry = Add(*Definition, TEXT("Scope.Prototype"), Scope);
            Flight = NewObject<UAbilityPrototypeFlightNodeConfig>(Definition);
            const auto Delivery = Add(*Definition, TEXT("Delivery.PrototypeFlight"), Flight);
            auto* Effect = NewObject<UAbilityPrototypeDamageNodeConfig>(Definition);
            Effect->bConsumeInputScales = true;
            const auto Damage = Add(*Definition, TEXT("Effect.PrototypeDamageBatch"), Effect);
            Definition->EntryNodeId = Entry;
            Definition->Edges = {{Entry, Delivery, TEXT("Ready")}, {Delivery, Damage, TEXT("Contact")}};
            return Definition;
        }
        UAbilityGraphInstance* Start(UAbilityGraphDefinition& Definition, FString& Error,
                                     TOptional<FAbilityTargetToken> InitialTarget = {})
        {
            auto* Context = NewObject<UPrototypeCastContext>(World);
            Context->Caster = Source;
            Context->CastId = FGuid::NewGuid();
            Context->Level = 1;
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            if (!Context->CaptureSourceLife(Error))
            {
                return nullptr;
            }
            FAbilityNodeInput Payload;
            Payload.SourceToken = SourceToken;
            Payload.Context = Context;
            Payload.DamageParams = Context->DamageParams;
            Payload.Direction = FVector::ForwardVector;
            Payload.LiveTargetToken = InitialTarget;
            auto* Run = NewObject<UAbilityGraphInstance>(World);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition.CompatibilityProfile;
            Options.BudgetPolicy = Definition.BudgetPolicy;
            if (!Run->StartWithInput(&Definition, Payload, Options, Error) ||
                (Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed))
            {
                Error +=
                    FString::Printf(TEXT("; finished=%d reason=%d"), Run->IsFinished(), int32(Run->GetEndReason()));
                Run->Cancel();
                return nullptr;
            }
            return Run;
        }
        float Health(int32 Id) const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : -1.f;
        }
    };
} // namespace AbilityPrototypeFlightOverlayTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeSinglePelletPierceOverlay,
                                 "test_25d.DynamicMage.PrototypeFlightOverlay.SinglePelletPierceUsesExplicitSettings",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeSinglePelletPierceOverlay::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightOverlayTests;
    for (const int32 Mode : {0, 1, 2})
    {
        FFixture F;
        const bool bOld = Mode == 0;
        const bool bOverlay = Mode == 2;
        const int32 A = F.Spawn(FVector(100, 0, 0));
        const int32 B = F.Spawn(FVector(200, 0, 0));
        auto* Parameters =
            F.CreateParameters(bOverlay ? EAbilityPrototypeKind::Multishot : EAbilityPrototypeKind::Piercing);
        if (!bOverlay)
        {
            Parameters->Count = 2;
            Parameters->DamageFalloff = .5f;
        }
        FString Error;
        UAbilityGraphDefinition* Definition = nullptr;
        if (bOld)
        {
            Definition = UAbilityLegacyGraphCompiler::Compile(F.World, Parameters, TEXT("LegacyMixed.v1"), Error);
        }
        else
        {
            UAbilityPrototypeFlightNodeConfig* Flight = nullptr;
            Definition = F.Fine(*Parameters, Flight, bOverlay);
            if (bOverlay)
            {
                Flight->bOverrideImpactPolicy = true;
                Flight->ImpactPolicy = EAbilityProjectileImpactPolicy::Pierce;
                Flight->bOverrideImpactLimit = true;
                Flight->ImpactLimit = 2;
                Flight->bOverrideDamageFalloff = true;
                Flight->DamageFalloff = .5f;
                TestTrue(TEXT("Typed configuration accepts the explicit impact axis"),
                         Flight->TrySetRecipeParameter(EAbilityConfigParameter::ImpactLimit, 2.f, Error));
            }
        }
        if (!TestNotNull(TEXT("Actual original or overlay graph compiles"), Definition))
        {
            AddError(Error);
            return false;
        }
        auto* Run = F.Start(*Definition, Error);
        if (!TestNotNull(TEXT("Actual source-captured graph starts without strategy failure"), Run))
        {
            AddError(Error);
            return false;
        }
        ++GFrameCounter;
        Run->Advance(.25f);
        TestEqual(TEXT("First actual contact applies the original damage once"), F.Health(A), 160.f);
        TestEqual(TEXT("Second actual contact uses explicit limit and falloff"), F.Health(B), 180.f);
        if (bOverlay)
        {
            TestEqual(TEXT("The original single pellet retains its Multishot identity"), Parameters->Kind,
                      EAbilityPrototypeKind::Multishot);
            TestEqual(TEXT("The overlay does not mutate original Count"), Parameters->Count, 1);
            TestEqual(TEXT("The overlay does not mutate original attenuation"), Parameters->DamageFalloff, 1.f);
        }
        Run->Cancel();
        TestEqual(TEXT("Cancellation releases the one original flight resource"), Run->GetActivePhysicalSpawnCount(),
                  0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFlightRadiusOverlay,
                                 "test_25d.DynamicMage.PrototypeFlightOverlay.CollisionRadiusChangesRealContact",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeFlightRadiusOverlay::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightOverlayTests;
    for (const int32 Mode : {0, 1, 2, 3})
    {
        FFixture F;
        const int32 Id = F.Spawn(FVector(100, 15, 0));
        auto* Parameters = F.CreateParameters(EAbilityPrototypeKind::Projectile);
        Parameters->CollisionRadius = Mode == 1 || Mode == 2 ? 20.f : 2.f;
        FString Error;
        UAbilityGraphDefinition* Definition = nullptr;
        if (Mode < 2)
        {
            Definition = UAbilityLegacyGraphCompiler::Compile(F.World, Parameters, TEXT("LegacyMixed.v1"), Error);
        }
        else
        {
            UAbilityPrototypeFlightNodeConfig* Flight = nullptr;
            Definition = F.Fine(*Parameters, Flight, false);
            if (Mode == 3)
            {
                Flight->bOverrideCollisionRadius = true;
                Flight->CollisionRadius = 20.f;
                TestTrue(TEXT("Typed configuration accepts a physical collision radius"),
                         Flight->TrySetRecipeParameter(EAbilityConfigParameter::CollisionRadius, 20.f, Error));
            }
        }
        if (!TestNotNull(TEXT("The actual geometry control or override compiles"), Definition))
        {
            AddError(Error);
            return false;
        }
        auto* Run = F.Start(*Definition, Error);
        if (!TestNotNull(TEXT("Geometry graph starts with captured original context"), Run))
        {
            AddError(Error);
            return false;
        }
        ++GFrameCounter;
        Run->Advance(.2f);
        TestEqual(TEXT("Only the actual wide sweep reaches the offset target"), F.Health(Id),
                  Mode == 0 ? 200.f : 160.f);
        if (Mode == 3)
        {
            TestEqual(TEXT("Physical override leaves original DA radius unchanged"), Parameters->CollisionRadius, 2.f);
        }
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFlightHomingAndReturnOverlay,
                                 "test_25d.DynamicMage.PrototypeFlightOverlay.ActualHomingReturnAndSourceCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeFlightHomingAndReturnOverlay::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightOverlayTests;
    for (const bool bReturn : {false, true})
    {
        for (const bool bOld : {true, false})
        {
            FFixture F;
            const int32 Id = F.Spawn(bReturn ? FVector(150, 0, 0) : FVector(160, 100, 0));
            auto* Parameters =
                F.CreateParameters(bOld ? (bReturn ? EAbilityPrototypeKind::Returning : EAbilityPrototypeKind::Homing)
                                        : EAbilityPrototypeKind::Multishot);
            Parameters->Duration = 3.f;
            Parameters->Speed = 600.f;
            Parameters->Range = 300.f;
            Parameters->HomingTurnRate = 720.f;
            Parameters->DamageFalloff = bReturn && bOld ? .5f : 1.f;
            FString Error;
            FAbilityTargetToken TargetLife;
            if (!TestTrue(TEXT("Homing/return control captures an actual original Mass life"),
                          AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(Id), TargetLife,
                                                                  Error)))
            {
                AddError(Error);
                return false;
            }
            UAbilityGraphDefinition* Definition = nullptr;
            if (bOld)
            {
                Definition = UAbilityLegacyGraphCompiler::Compile(F.World, Parameters, TEXT("LegacyMixed.v1"), Error);
            }
            else
            {
                UAbilityPrototypeFlightNodeConfig* Flight = nullptr;
                Definition = F.Fine(*Parameters, Flight, true);
                TestTrue(TEXT("Runtime setter selects the actual supported kinematic motion"),
                         Flight->TrySetRecipeParameter(EAbilityConfigParameter::Motion,
                                                       float(bReturn ? EAbilityProjectileMotion::Returning
                                                                     : EAbilityProjectileMotion::LegacyTurnHoming),
                                                       Error));
                if (bReturn)
                {
                    TestTrue(TEXT("Return contacts use the explicit per-phase policy"),
                             Flight->TrySetRecipeParameter(EAbilityConfigParameter::ImpactPolicy,
                                                           float(EAbilityProjectileImpactPolicy::ReturnPerPhase),
                                                           Error));
                    TestTrue(TEXT("Return phase consumes the explicit authored attenuation"),
                             Flight->TrySetRecipeParameter(EAbilityConfigParameter::DamageFalloff, .5f, Error));
                }
            }
            if (!TestNotNull(TEXT("Actual original or independently configured motion compiles"), Definition))
            {
                AddError(Error);
                return false;
            }
            auto* Run = F.Start(*Definition, Error, TargetLife);
            if (!TestNotNull(TEXT("Motion starts with its captured original target"), Run))
            {
                AddError(Error);
                return false;
            }
            for (int32 Step = 0; Step < 50 && !Run->IsFinished(); ++Step)
            {
                ++GFrameCounter;
                Run->Advance(.025f);
            }
            TestEqual(TEXT("Actual homing reaches the off-axis life; return damages exactly once per phase"),
                      F.Health(Id), bReturn ? 140.f : 160.f);
            if (bReturn)
            {
                TestTrue(TEXT("The actual returning flight reaches its original source and closes"), Run->IsFinished());
            }
            Run->Cancel();
            if (!bOld)
            {
                TestEqual(TEXT("Dynamic motion never changes the original Multishot identity"), Parameters->Kind,
                          EAbilityPrototypeKind::Multishot);
                TestEqual(TEXT("Terminal motion releases its exact physical resource"),
                          Run->GetActivePhysicalSpawnCount(), 0);
            }
        }
    }
    FFixture F;
    const int32 Id = F.Spawn(FVector(150, 100, 0));
    auto* Parameters = F.CreateParameters(EAbilityPrototypeKind::Multishot);
    UAbilityPrototypeFlightNodeConfig* Flight = nullptr;
    auto* Definition = F.Fine(*Parameters, Flight, true);
    FString Error;
    Flight->TrySetRecipeParameter(EAbilityConfigParameter::Motion, float(EAbilityProjectileMotion::LegacyTurnHoming),
                                  Error);
    FAbilityTargetToken TargetLife;
    AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(Id), TargetLife, Error);
    auto* Run = F.Start(*Definition, Error, TargetLife);
    if (TestNotNull(TEXT("Source-retirement motion starts"), Run))
    {
        F.Source->EnableDungeonPooling();
        ++GFrameCounter;
        Run->Advance(.05f);
        TestTrue(TEXT("Original source life retirement stops the in-flight motion"), Run->IsFinished());
        TestEqual(TEXT("Source retirement never authorizes a late hit"), F.Health(Id), 200.f);
        TestEqual(TEXT("Source retirement releases the original physical lease"), Run->GetActivePhysicalSpawnCount(),
                  0);
    }
    else
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFlightOverlayInvalidValues,
                                 "test_25d.DynamicMage.PrototypeFlightOverlay.InvalidTypedValuesDoNotMutate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeFlightOverlayInvalidValues::RunTest(const FString&)
{
    auto* Config = NewObject<UAbilityPrototypeFlightNodeConfig>();
    FString Error;
    TestFalse(TEXT("NativeHoming is rejected because this prototype node has no native movement component"),
              Config->TrySetRecipeParameter(EAbilityConfigParameter::Motion,
                                            float(EAbilityProjectileMotion::NativeHoming), Error));
    TestFalse(TEXT("An invalid motion never enables an override"), Config->bOverrideMotion);
    TestFalse(TEXT("Fractional contact counts are rejected instead of silently truncated"),
              Config->TrySetRecipeParameter(EAbilityConfigParameter::ImpactLimit, 2.5f, Error));
    TestFalse(TEXT("Rejected count never changes its explicit bit"), Config->bOverrideImpactLimit);
    TestFalse(TEXT("An unbounded physical radius is rejected"),
              Config->TrySetRecipeParameter(EAbilityConfigParameter::CollisionRadius, 201.f, Error));
    TestFalse(TEXT("Rejected radius never changes its explicit bit"), Config->bOverrideCollisionRadius);
    TestFalse(
        TEXT("NaN speed is rejected"),
        Config->TrySetRecipeParameter(EAbilityConfigParameter::Speed, std::numeric_limits<float>::quiet_NaN(), Error));
    TestFalse(TEXT("Duration belongs to the Scope clock and cannot create a second flight lifespan"),
              Config->TrySetRecipeParameter(EAbilityConfigParameter::Duration, 1.f, Error));
    TestTrue(TEXT("Supported finite speed remains configurable"),
             Config->TrySetRecipeParameter(EAbilityConfigParameter::Speed, 400.f, Error));
    TestTrue(TEXT("Supported finite range remains configurable"),
             Config->TrySetRecipeParameter(EAbilityConfigParameter::Range, 800.f, Error));
    TestTrue(TEXT("Supported turn rate remains configurable"),
             Config->TrySetRecipeParameter(EAbilityConfigParameter::HomingTurnRate, 720.f, Error));
    Config->bOverrideMotion = true;
    Config->Motion = EAbilityProjectileMotion::NativeHoming;
    TestFalse(TEXT("Direct invalid authoring also fails static validation"), Config->Validate(Error));
    return true;
}
#endif
