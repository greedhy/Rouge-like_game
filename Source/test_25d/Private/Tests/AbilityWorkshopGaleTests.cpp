// 验证链：原生风压图 -> 类型化配方 -> Actor RootMotion 与 Mass 位移处理器。
// 无视口测试使用真实地面和原生命状态，禁用表现不代替玩法行为。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "AbilitySystem/Unified/WorkshopGaleAbilities.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "PureMass/PureMassSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopGaleDirectionalFilter, "test_25d.UnifiedGA.WorkshopGale.DirectionalFilter",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopGaleDirectionalFilter::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    auto* Floor = Fixture.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);
    Floor->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(2000.f, 2000.f, 50.f));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Floor->SetActorLocation(FVector(0.f, 0.f, -51.f));

    // Actor 使用现有根运动命令；实体下方使用同一地面，让世界位移服务复核终点。
    const float ActorHeight = Fixture.Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Fixture.Target->SetActorLocation(FVector(220.f, 88.f, ActorHeight + 2.f));
    auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
    // 这个未 BeginPlay 的测试世界没有自动应用角色出生属性。
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 200.f);
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 200.f);
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Radius = 20.f;
    Type->HalfHeight = 40.f;
    Type->Health = 200.f;
    auto* ImmuneType = DuplicateObject(Type, Fixture.World);
    const FVector Start(340.f, -130.f, 40.f);
    const FVector DryStart(210.f, -28.f, 40.f);
    const FVector OutsideStart(150.f, -265.f, 40.f);
    const int32 Wet = Mass->SpawnMonster(Type, Start);
    const int32 Dry = Mass->SpawnMonster(Type, DryStart);
    const int32 Outside = Mass->SpawnMonster(Type, OutsideStart);
    const int32 Immune = Mass->SpawnMonster(ImmuneType, FVector(280.f, 27.f, 40.f));
    if (!TestTrue(TEXT("All four Mass test targets exist"), Wet > 0 && Dry > 0 && Outside > 0 && Immune > 0))
    {
        return false;
    }
    FString Error;
    FAbilityTargetToken ActorLife;
    if (!TestTrue(TEXT("The fixture captures the current Actor life"),
                  AbilityExecutionIdentity::CaptureTarget(
                      Fixture.SourceToken, FPureMassCombatTarget::FromActor(Fixture.Target), ActorLife, Error)) ||
        !TestTrue(TEXT("The live Actor receives real wet exposure"),
                  AbilityElements::Apply(Fixture.SourceToken, ActorLife, EAbilityElement::Water, 1.f)) ||
        !TestTrue(TEXT("All three designated Mass targets receive real wet exposure"),
                  Mass->ApplyElementalExposure(Wet, Fixture.Source, EAbilityElement::Water, 1.f) &&
                      Mass->ApplyElementalExposure(Outside, Fixture.Source, EAbilityElement::Water, 1.f) &&
                      Mass->ApplyElementalExposure(Immune, Fixture.Source, EAbilityElement::Water, 1.f)))
    {
        AddError(TEXT("The world fixture needs current wet Actor and Mass targets. ") + Error);
        return false;
    }

    auto* Graph = DuplicateObject(GetDefault<UWorkshopGaleSweep>()->Graph.Get(), Fixture.World);
    const auto* Movement = Graph->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Movement.TargetDisplace");
        });
    if (!TestNotNull(TEXT("The native trial owns an independent displacement node"), Movement))
    {
        return false;
    }
    CastChecked<UAbilityTargetDisplaceNodeConfig>(Movement->Config)->Settings->ImmuneMassTypes.Add(ImmuneType);
    FAbilityRuntimeRecipeContribution Contribution;
    Contribution.SourceId = TEXT("Test.Gale.Force");
    auto& Edit = Contribution.Patches.AddDefaulted_GetRef();
    Edit.TargetNodeId = Movement->NodeId;
    Edit.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
    Edit.Parameter = EAbilityConfigParameter::MoveDistance;
    Edit.Scalar = 295.f;
    UAbilityGraphDefinition* Edited = nullptr;
    if (!TestTrue(TEXT("An independent distance patch compiles on the real graph"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Graph, {Contribution}, Edited, Error)))
    {
        AddError(Error);
        return false;
    }

    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    int32 Rejections = 0;
    Run->OnEvent.AddLambda(
        [&Rejections](const FAbilityGraphEvent& Event)
        {
            if (Event.NodeType == TEXT("Movement.TargetDisplace") && Event.OutputPort == TEXT("Rejected"))
            {
                ++Rejections;
            }
        });
    FAbilityNodeInput Input;
    Input.SourceToken = Fixture.SourceToken;
    Input.Origin = FVector(950.f, 0.f, 0.f);
    Input.Direction = FVector::ForwardVector;
    if (!TestTrue(TEXT("The server graph acquires the original world move without failing on the immune target"),
                  Run->StartWithInput(Edited, Input, {}, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("The actor receives the real replicated-motion source"),
             Fixture.Target->GetCharacterMovement()->GetRootMotionSource(TEXT("ExtensionDisplace")).IsValid());
    TestEqual(TEXT("One immune target rejects without cancelling other targets"), Rejections, 1);

    auto& Entities = Fixture.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    auto* MoveState = Fixture.World->GetSubsystem<UExtensionWorldSubsystem>();
    for (const int32 Id : {Wet, Dry, Outside})
    {
        auto& Monster = Entities.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Id));
        MoveState->MoveMass(Monster, .24f);
        if (Id == Wet)
        {
            TestTrue(TEXT("The wet Mass target moves the edited distance in the cast direction"),
                     FMath::IsNearlyEqual(Monster.Location.X, Start.X + 295.f, 1.) &&
                         FMath::IsNearlyEqual(Monster.Location.Y, Start.Y, 1.));
            TestEqual(TEXT("The control spell preserves health"), Monster.Health, 200.f);
        }
        else
        {
            const FVector Expected = Id == Dry ? DryStart : OutsideStart;
            TestTrue(TEXT("Dry and outside-cone targets remain in place"), Monster.Location.Equals(Expected, 1.f));
        }
    }
    Run->Cancel();
    return true;
}
#endif
