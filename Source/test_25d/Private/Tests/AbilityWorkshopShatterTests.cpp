// 测试链：原生裂霜图 -> 真实配方编译/伤害 -> Actor/Mass 原生命状态和移动恢复。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"

#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/WorkshopShatterAbilities.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopIceShatterConsumption,
                                 "test_25d.UnifiedGA.DynamicMage.Workshop.IceShatterConsumption",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopIceShatterConsumption::RunTest(const FString&)
{
    using E = EAbilityElement;
    using S = EAbilityExposure;
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    FAbilitySourceToken Source;
    if (!TestTrue(TEXT("Use the current player authority"),
                  AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error)))
    {
        AddInfo(Error);
        return false;
    }

    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Health = 200.f;
    const int32 MassId = Mass->SpawnMonster(Type, FVector(310.f, -40.f, 0.f));
    auto* Dry = Fixture.EnemyAt(FVector(245.f, 60.f, 0.f));
    auto* OffAxis = Fixture.EnemyAt(FVector(300.f, 170.f, 0.f));
    FAbilityTargetToken ActorToken;
    FAbilityTargetToken MassToken;
    FAbilityTargetToken OffAxisToken;
    if (!TestTrue(
            TEXT("Capture the exact original lives"),
            AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Fixture.Target),
                                                    ActorToken, Error) &&
                AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget(MassId), MassToken, Error) &&
                AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(OffAxis), OffAxisToken,
                                                        Error)))
    {
        AddInfo(Error);
        return false;
    }
    Fixture.Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    for (const auto& Token : {ActorToken, MassToken, OffAxisToken})
    {
        TestTrue(TEXT("Freeze through existing exposure rules"),
                 AbilityElements::Apply(Source, Token, E::Water, 1.f) &&
                     AbilityElements::Apply(Source, Token, E::Ice, 1.f) &&
                     AbilityElements::Read(Source, Token) == S::Frozen);
    }
    TestTrue(TEXT("The Actor is actually immobilized by the original freeze"),
             Fixture.Target->GetCharacterMovement()->MovementMode == MOVE_None);
    TestFalse(TEXT("A different status cannot consume the current ice"),
              AbilityElements::Consume(Source, ActorToken, S::Chilled));
    TestFalse(TEXT("An unrelated Mass handle cannot clear this creature's ice"),
              Mass->ConsumeElementalExposure(MassId, FMassEntityHandle(), Fixture.Pawn, S::Frozen));

    auto* Base = AbilityRuntimeRecipes::BuildDefaultGraph(Fixture.World, GetDefault<UWorkshopIceShatter>(), Error);
    if (!TestNotNull(TEXT("Read the actual native Ice Shatter graph"), Base))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityNodeInput Input;
    Input.SourceToken = Source;
    Input.Origin = FVector(640.f, 0.f, 0.f);
    Input.Direction = FVector::ForwardVector;
    Input.DamageParams.DamageGameplayEffectClass =
        LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C"));
    if (!TestTrue(TEXT("Load the real Actor combat gameplay effect"),
                  static_cast<bool>(Input.DamageParams.DamageGameplayEffectClass)))
    {
        return false;
    }

    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    auto Execute = [&](UAbilityGraphDefinition* Definition)
    {
        auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
        if (!TestTrue(TEXT("Run starts on the same authored source and aim"),
                      Run->StartWithInput(Definition, Input, {}, Registry, Error)))
        {
            AddInfo(Error);
            return false;
        }
        for (int32 Frame = 0; Frame < 8 && !Run->IsFinished(); ++Frame)
        {
            ++GFrameCounter;
            Run->Advance(.01f);
        }
        return TestTrue(TEXT("Graph completes"), Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    };
    auto ReadHealth = [](AEnemyCharacter* Enemy)
    {
        return Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    };
    const float Before = ReadHealth(Fixture.Target);
    const float BeforeDry = ReadHealth(Dry);
    const float BeforeOffAxis = ReadHealth(OffAxis);

    FAbilityRuntimeRecipeContribution NoDamage;
    NoDamage.SourceId = TEXT("Test.ZeroShatterDamage");
    auto& Zero = NoDamage.Patches.AddDefaulted_GetRef();
    Zero.TargetTypeId = TEXT("Effect.Damage");
    Zero.Operation = EAbilityRecipePatchOperation::DamageScale;
    Zero.Scalar = 0.f;
    UAbilityGraphDefinition* ZeroGraph = nullptr;
    if (!TestTrue(TEXT("Use a real zero-damage edit"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Base, {NoDamage}, ZeroGraph, Error)) ||
        !Execute(ZeroGraph))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Zero damage keeps Actor life unchanged"), ReadHealth(Fixture.Target), Before);
    TestTrue(TEXT("Zero damage does not consume either freeze"),
             AbilityElements::Read(Source, ActorToken) == S::Frozen &&
                 AbilityElements::Read(Source, MassToken) == S::Frozen);

    if (!Execute(Base))
    {
        return false;
    }
    FPureMassView View;
    TestTrue(TEXT("Actual physical damage applies to both original frozen lives"),
             ReadHealth(Fixture.Target) < Before && Mass->Resolve(MassId, View) && View.Health < 200.f);
    TestTrue(TEXT("The follow-up exactly consumes freeze for Actor and Mass"),
             AbilityElements::Read(Source, ActorToken) == S::None && View.IceExposure == S::None &&
                 AbilityElements::Read(Source, MassToken) == S::None);
    TestTrue(TEXT("The Actor's own movement mode is restored"),
             Fixture.Target->GetCharacterMovement()->MovementMode == MOVE_Walking);
    TestEqual(TEXT("An unprimed target in the line is skipped"), ReadHealth(Dry), BeforeDry);
    TestEqual(TEXT("A frozen target outside the line is skipped"), ReadHealth(OffAxis), BeforeOffAxis);
    TestTrue(TEXT("Outside target keeps its own state"), AbilityElements::Read(Source, OffAxisToken) == S::Frozen);

    FAbilityRuntimeRecipeContribution Chilled;
    Chilled.SourceId = TEXT("Test.ShatterChilled");
    for (const FName TypeId : {FName(TEXT("Condition.ElementalStatus")), FName(TEXT("Status.ConsumeExposure"))})
    {
        auto& Patch = Chilled.Patches.AddDefaulted_GetRef();
        Patch.TargetTypeId = TypeId;
        Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
        Patch.Parameter = EAbilityConfigParameter::ElementalStatus;
        Patch.Scalar = static_cast<float>(S::Chilled);
    }
    UAbilityGraphDefinition* ChilledGraph = nullptr;
    const float BeforeChilled = ReadHealth(Fixture.Target);
    TestTrue(TEXT("Prime only Chilled for the edited variant"),
             AbilityElements::Apply(Source, ActorToken, E::Ice, 1.f));
    if (!TestTrue(TEXT("Both status axes are independently typed"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Base, {Chilled}, ChilledGraph, Error)) ||
        !Execute(ChilledGraph))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("The edited shatter changes required and consumed state together"),
             ReadHealth(Fixture.Target) < BeforeChilled && AbilityElements::Read(Source, ActorToken) == S::None);
    return true;
}
#endif
