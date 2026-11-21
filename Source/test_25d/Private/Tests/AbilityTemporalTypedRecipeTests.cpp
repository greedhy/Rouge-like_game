// 测试链：运行时ConfigureParameter解析 -> 实际有限时钟/波形/索敌 -> 独立状态或真实投递。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityTemporalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "Actor/MyProjectile.h"
#include "PureMass/PureMassSubsystem.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace AbilityTemporalTypedRecipeTests
{
    struct FFixture : FAbilityGraphTestFixture
    {
        UAbilityGraphDefinition* Definition = nullptr;
        FFixture()
        {
            Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = TEXT("UnifiedV2");
        }
        FGuid Add(FName Type, UAbilityNodeConfig* Config)
        {
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = Type;
            Node.Config = Config;
            Definition->Nodes.Add(Node);
            if (!Definition->EntryNodeId.IsValid())
            {
                Definition->EntryNodeId = Node.NodeId;
            }
            return Node.NodeId;
        }
        FGuid Petrify(FGuid Parent, FName Port, bool bOrigin)
        {
            auto Anchor = Parent;
            auto Output = Port;
            if (bOrigin)
            {
                auto* Query = NewObject<UAbilityQueryNodeConfig>(Definition);
                Query->Query.Radius = 400.f;
                Query->Query.bRequireVisibility = false;
                Anchor = Add(TEXT("Query.Shape"), Query);
                Definition->Edges.Add({Parent, Anchor, Port});
                Output = TEXT("TargetFound");
            }
            auto* State = NewObject<UAbilityPetrifyNodeConfig>(Definition);
            State->DurationSeconds = 2.f;
            const auto Effect = Add(TEXT("Status.Petrify"), State);
            Definition->Edges.Add({Anchor, Effect, Output});
            return Effect;
        }
        UAbilityGraphDefinition* Configure(FGuid Node,
                                           std::initializer_list<TPair<EAbilityConfigParameter, float>> Values,
                                           FString& Error)
        {
            FAbilityRuntimeRecipeContribution Contribution;
            Contribution.SourceId = TEXT("Rune.TypedTemporalParameters");
            for (const auto& Value : Values)
            {
                FAbilityRuntimeRecipePatch Patch;
                Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
                Patch.TargetNodeId = Node;
                Patch.Parameter = Value.Key;
                Patch.Scalar = Value.Value;
                Contribution.Patches.Add(Patch);
            }
            UAbilityGraphDefinition* Compiled = nullptr;
            return AbilityRuntimeRecipes::Compile(World, Definition, {Contribution}, Compiled, Error) ? Compiled
                                                                                                      : nullptr;
        }
        UAbilityGraphInstance* Start(UAbilityGraphDefinition& GraphDefinition, FString& Error)
        {
            FAbilityNodeInput Payload;
            Payload.SourceToken = SourceToken;
            Payload.Direction = FVector::ForwardVector;
            auto* Run = NewObject<UAbilityGraphInstance>(World);
            if (!Run->StartWithInput(&GraphDefinition, Payload, {}, Error) ||
                (Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed))
            {
                Error +=
                    FString::Printf(TEXT("; finished=%d reason=%d"), Run->IsFinished(), int32(Run->GetEndReason()));
                return nullptr;
            }
            return Run;
        }
        void Advance(UAbilityGraphInstance& Run, float Delta)
        {
            ++GFrameCounter;
            Run.Advance(Delta);
        }
        int32 Mass()
        {
            Target->Destroy();
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 200.f;
            Type->XPReward = 0;
            Type->Speed = 0.f;
            return Sim->SpawnMonster(Type, FVector(300, 0, 0));
        }
        void Fly(UAbilityGraphInstance& Run, float Seconds)
        {
            for (float Elapsed = 0.f; Elapsed + UE_SMALL_NUMBER < Seconds && !Run.IsFinished(); Elapsed += .01f)
            {
                TArray<TWeakObjectPtr<AMyProjectile>> Shots;
                for (TActorIterator<AMyProjectile> It(World); It; ++It)
                {
                    if (!It->IsActorBeingDestroyed())
                    {
                        Shots.Add(*It);
                    }
                }
                for (const auto Shot : Shots)
                {
                    if (Shot.IsValid() && !Shot->IsActorBeingDestroyed())
                    {
                        Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
                    }
                }
                Advance(Run, .01f);
            }
        }
    };
} // namespace AbilityTemporalTypedRecipeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepeatTypedRecipe,
                                 "test_25d.DynamicMage.TypedTemporal.RepeatParserChangesCountPeriodAndDuration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRepeatTypedRecipe::RunTest(const FString&)
{
    using namespace AbilityTemporalTypedRecipeTests;
    for (const bool bShort : {false, true})
    {
        FFixture F;
        auto* Base = NewObject<UAbilityRepeatNodeConfig>(F.Definition);
        Base->Count = 1;
        Base->Period = .1f;
        Base->Duration = 2.f;
        const auto Root = F.Add(TEXT("Control.Repeat"), Base);
        F.Petrify(Root, TEXT("Pulse"), true);
        FString Error;
        auto* Compiled = F.Configure(Root,
                                     {{EAbilityConfigParameter::Count, bShort ? 3.f : 2.f},
                                      {EAbilityConfigParameter::Period, .25f},
                                      {EAbilityConfigParameter::Duration, bShort ? .4f : 1.f}},
                                     Error);
        if (!TestNotNull(TEXT("Parser accepts real finite Repeat fields"), Compiled))
        {
            AddError(Error);
            return false;
        }
        auto* Run = F.Start(*Compiled, Error);
        if (!TestNotNull(TEXT("Configured Repeat starts"), Run))
        {
            AddError(Error);
            return false;
        }
        int32 Pulses = 0;
        Run->OnEvent.AddLambda(
            [&](const auto& Event)
            {
                Pulses += Event.NodeType == TEXT("Control.Repeat") && Event.OutputPort == TEXT("Pulse") ? 1 : 0;
            });
        F.Advance(*Run, .24f);
        TestFalse(TEXT("Longer configured period prevents early actual status"), F.IsPetrified());
        F.Advance(*Run, .01f);
        TestTrue(TEXT("First configured pulse runs independent real state"), F.IsPetrified());
        F.Advance(*Run, .25f);
        TestTrue(TEXT("Count or duration actually terminates the finite clock"), Run->IsFinished());
        TestEqual(TEXT("Duration limits a longer count; count limits the ordinary clock"), Pulses, bShort ? 1 : 2);
        TestEqual(TEXT("Original Count was never mutated"), Base->Count, 1);
        TestEqual(TEXT("Original Period was never mutated"), Base->Period, .1f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaitTypedRecipe,
                                 "test_25d.DynamicMage.TypedTemporal.WaitParserChangesArmRadiusAndTotalDeadline",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWaitTypedRecipe::RunTest(const FString&)
{
    using namespace AbilityTemporalTypedRecipeTests;
    for (const bool bDeadline : {false, true})
    {
        FFixture F;
        auto* Base = NewObject<UAbilityWaitTargetNodeConfig>(F.Definition);
        Base->Radius = 80.f;
        Base->ArmDelay = .1f;
        Base->Duration = 2.f;
        const auto Root = F.Add(TEXT("Query.WaitForTarget"), Base);
        F.Petrify(Root, TEXT("Triggered"), true);
        FString Error;
        auto* Compiled = F.Configure(Root,
                                     {{EAbilityConfigParameter::Radius, 220.f},
                                      {EAbilityConfigParameter::ArmDelay, bDeadline ? .75f : .5f},
                                      {EAbilityConfigParameter::Duration, bDeadline ? .5f : 1.f}},
                                     Error);
        if (!TestNotNull(TEXT("Parser accepts real WaitTarget fields"), Compiled))
        {
            AddError(Error);
            return false;
        }
        auto* Run = F.Start(*Compiled, Error);
        if (!TestNotNull(TEXT("Configured WaitTarget starts"), Run))
        {
            AddError(Error);
            return false;
        }
        F.Advance(*Run, .49f);
        TestFalse(TEXT("Configured arm delay blocks actual early effect"), F.IsPetrified());
        F.Advance(*Run, .01f);
        TestTrue(TEXT("Trigger or total deadline closes the original wait"), Run->IsFinished());
        TestEqual(TEXT("Actual trigger geometry is independent of the payload and respects total deadline"),
                  F.IsPetrified(), !bDeadline);
        TestEqual(TEXT("Original radius is unchanged"), Base->Radius, 80.f);
        TestEqual(TEXT("Original arm timing is unchanged"), Base->ArmDelay, .1f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaveTypedRecipe,
                                 "test_25d.DynamicMage.TypedTemporal.WaveParserChangesRealFrontAndLifePermission",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWaveTypedRecipe::RunTest(const FString&)
{
    using namespace AbilityTemporalTypedRecipeTests;
    for (const int32 Mode : {0, 1, 2})
    {
        FFixture F;
        auto* Base = NewObject<UAbilityWavefrontNodeConfig>(F.Definition);
        Base->Radius = 80.f;
        Base->Speed = 1000.f;
        const auto Root = F.Add(TEXT("Spatial.Wavefront"), Base);
        F.Petrify(Root, TEXT("TargetFound"), false);
        FString Error;
        auto* Compiled = F.Configure(Root,
                                     {{EAbilityConfigParameter::Radius, 220.f},
                                      {EAbilityConfigParameter::Speed, 100.f},
                                      {EAbilityConfigParameter::Duration, Mode == 1 ? 1.f : 3.f}},
                                     Error);
        if (!TestNotNull(TEXT("Parser accepts real wavefront geometry and clock"), Compiled))
        {
            AddError(Error);
            return false;
        }
        auto* Run = F.Start(*Compiled, Error);
        if (!TestNotNull(TEXT("Configured wave starts"), Run))
        {
            AddError(Error);
            return false;
        }
        F.Advance(*Run, .5f);
        TestFalse(TEXT("Configured slow wave cannot apply state before reaching the target"), F.IsPetrified());
        if (Mode == 2)
        {
            F.Target->EnableDungeonPooling();
        }
        F.Advance(*Run, 1.01f);
        TestEqual(TEXT("Actual front reaches only an admitted original life before its configured deadline"),
                  F.IsPetrified(), Mode == 0);
        Run->Cancel();
        const auto Count = Run->GetAcceptedEventCount();
        F.Advance(*Run, 4.f);
        TestEqual(TEXT("Cancelled wave emits no new normal events"), Run->GetAcceptedEventCount(), Count);
        TestEqual(TEXT("Original speed remains unchanged"), Base->Speed, 1000.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmitterTypedRecipe,
                                 "test_25d.DynamicMage.TypedTemporal.EmitterParserDrivesActualDeliveryCountAndCadence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEmitterTypedRecipe::RunTest(const FString&)
{
    using namespace AbilityTemporalTypedRecipeTests;
    for (const bool bShort : {false, true})
    {
        FFixture F;
        const int32 Id = F.Mass();
        auto* Base = NewObject<UAbilityEmitterNodeConfig>(F.Definition);
        Base->Radius = 80.f;
        Base->Count = 1;
        Base->Period = .1f;
        Base->Duration = 2.f;
        const auto Root = F.Add(TEXT("Control.Emitter"), Base);
        auto* Flight = NewObject<UAbilityDeliveryNodeConfig>(F.Definition);
        Flight->Delivery.ProjectileClass = AMyProjectile::StaticClass();
        Flight->Delivery.Speed = 1000.f;
        Flight->Delivery.CollisionRadius = 8.f;
        Flight->Delivery.bApplyDamage = false;
        const auto Delivery = F.Add(TEXT("Delivery.ProjectileAwait"), Flight);
        const auto Resolve =
            F.Add(TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(F.Definition));
        F.Definition->Edges.Add({Root, Delivery, TEXT("Launch")});
        F.Definition->Edges.Add({Delivery, Resolve, TEXT("Hit")});
        F.Petrify(Resolve, TEXT("TargetFound"), false);
        FString Error;
        auto* Compiled = F.Configure(Root,
                                     {{EAbilityConfigParameter::Radius, 400.f},
                                      {EAbilityConfigParameter::Count, bShort ? 3.f : 2.f},
                                      {EAbilityConfigParameter::Period, .25f},
                                      {EAbilityConfigParameter::Duration, bShort ? .4f : 1.f}},
                                     Error);
        if (!TestNotNull(TEXT("Parser accepts actual finite Emitter parameters"), Compiled))
        {
            AddError(Error);
            return false;
        }
        auto* Run = F.Start(*Compiled, Error);
        if (!TestNotNull(TEXT("Configured Emitter starts its real delivery graph"), Run))
        {
            AddError(Error);
            return false;
        }
        F.Fly(*Run, .24f);
        TestEqual(TEXT("Configured period launches no premature projectile"), Run->GetPhysicalSpawnCountTotal(),
                  uint64(0));
        F.Fly(*Run, 2.f);
        TestTrue(TEXT("Clock and every real child delivery terminate"), Run->IsFinished());
        TestEqual(TEXT("Count/deadline controls actual root-budgeted projectile births"),
                  Run->GetPhysicalSpawnCountTotal(), uint64(bShort ? 1 : 2));
        FPureMassView View;
        TestTrue(TEXT("Real target remains its original Mass life"),
                 F.World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View));
        TestEqual(TEXT("Alternate state payload has no hidden damage"), View.Health, 200.f);
        TestTrue(TEXT("An actual physical contact applies the independent state"), View.bPetrified);
        TestEqual(TEXT("Original finite Emitter settings remain unchanged"), Base->Count, 1);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTemporalTypedRejectsInvalid,
                                 "test_25d.DynamicMage.TypedTemporal.InvalidValuesPreserveAllAcceptedSettings",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTemporalTypedRejectsInvalid::RunTest(const FString&)
{
    FString Error;
    auto* Repeat = NewObject<UAbilityRepeatNodeConfig>();
    TestFalse(TEXT("Fractional repeat counts are rejected"),
              Repeat->TrySetRecipeParameter(EAbilityConfigParameter::Count, 2.5f, Error));
    TestEqual(TEXT("Rejected count preserves the actual original value"), Repeat->Count, 3);
    TestFalse(TEXT("Zero repeat periods are rejected"),
              Repeat->TrySetRecipeParameter(EAbilityConfigParameter::Period, 0.f, Error));
    TestEqual(TEXT("Rejected period preserves the accepted value"), Repeat->Period, .5f);
    auto* Wait = NewObject<UAbilityWaitTargetNodeConfig>();
    TestFalse(TEXT("A wait radius above the actual query contract is rejected"),
              Wait->TrySetRecipeParameter(EAbilityConfigParameter::Radius, 3001.f, Error));
    TestEqual(TEXT("Rejected wait geometry is unchanged"), Wait->Radius, 350.f);
    TestFalse(TEXT("Negative arming time is rejected"),
              Wait->TrySetRecipeParameter(EAbilityConfigParameter::ArmDelay, -1.f, Error));
    TestEqual(TEXT("Rejected arming time is unchanged"), Wait->ArmDelay, .5f);
    auto* Wave = NewObject<UAbilityWavefrontNodeConfig>();
    TestFalse(
        TEXT("A nonfinite wave speed is rejected"),
        Wave->TrySetRecipeParameter(EAbilityConfigParameter::Speed, std::numeric_limits<float>::infinity(), Error));
    TestEqual(TEXT("Rejected wave speed is unchanged"), Wave->Speed, 600.f);
    TestFalse(TEXT("Wave does not falsely accept a projectile motion axis"),
              Wave->TrySetRecipeParameter(EAbilityConfigParameter::Motion, 1.f, Error));
    auto* Emitter = NewObject<UAbilityEmitterNodeConfig>();
    TestFalse(TEXT("Emitter cannot request an unlimited zero count"),
              Emitter->TrySetRecipeParameter(EAbilityConfigParameter::Count, 0.f, Error));
    TestEqual(TEXT("Rejected emitter count is unchanged"), Emitter->Count, 3);
    TestFalse(TEXT("Emitter lifetime cannot be zero"),
              Emitter->TrySetRecipeParameter(EAbilityConfigParameter::Duration, 0.f, Error));
    TestEqual(TEXT("Rejected emitter lifetime is unchanged"), Emitter->Duration, 6.f);
    return true;
}
#endif
