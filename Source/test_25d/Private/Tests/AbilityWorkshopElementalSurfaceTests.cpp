// 测试链：工作台默认图 → 图执行 → 权威地表接触扫描 → Actor GAS 与 PureMass 状态。
#if WITH_DEV_AUTOMATION_TESTS

#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Misc/AutomationTest.h"
#include "NiagaraComponent.h"
#include "PureMass/PureMassProcessor.h"
#include "PureMass/PureMassSubsystem.h"
#include "Terrain/TerrainSurface.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopElementalSurfaceContact,
                                 "test_25d.UnifiedGA.WorkshopTerrain.ElementalContact",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopElementalSurfaceContact::RunTest(const FString&)
{
    const auto* AbilityClass = FindObject<UClass>(nullptr, TEXT("/Script/test_25d.WorkshopWaterShallows"));
    if (!TestNotNull(TEXT("A distinct native Ground ability grants the real Water surface"), AbilityClass))
    {
        return false;
    }
    const auto* Shallows = AbilityClass->GetDefaultObject<UWorkshopUtilityAbility>();
    FAbilityGraphTestFixture F;
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    F.Source->SetActorLocation(FVector(-750.f, 0.f, 40.f));
    F.Target->SetActorLocation(FVector(150.f, 80.f, 40.f));
    F.Target->GetCapsuleComponent()->SetCapsuleSize(20.f, 40.f);
    F.Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    F.Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    F.Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* Floor = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(1200.f, 1200.f, 20.f));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Floor->SetActorLocation(FVector(0.f, 0.f, -20.f));

    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    auto* Mob = NewObject<UPureMassMonsterType>(F.World);
    Mob->Health = 100.f;
    Mob->HalfHeight = 40.f;
    Mob->Radius = 20.f;
    const int32 Entity = Sim->SpawnMonster(Mob, FVector(150.f, -80.f, 40.f));
    if (!TestTrue(TEXT("There is a live Mass target under the same field"), Entity > 0))
    {
        return false;
    }
    auto& Manager = F.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    auto* Processor = NewObject<UPureMassProcessor>(F.World);
    Processor->CallInitialize(F.World, Manager.AsShared());
    auto StepSimulation = [&]()
    {
        F.World->TimeSeconds += .25;
        ++GFrameCounter;
        auto Execution = Manager.CreateExecutionContext(.05f);
        Execution.SetExecutionType(EMassExecutionContextType::Processor);
        Processor->CallExecute(Manager, Execution);
    };
    auto FindSurface = [&F](ETerrainSurfaceType Kind) -> ATerrainSurface*
    {
        for (TActorIterator<ATerrainSurface> It(F.World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed() && It->GetOwner() == F.Source && It->Config &&
                It->Config->SurfaceType == Kind)
            {
                return *It;
            }
        }
        return nullptr;
    };

    const auto Water = ETerrainSurfaceType::Water;
    const auto Conductive = ETerrainSurfaceType::Conductive;
    auto* Execution = NewObject<UAbilityGraphInstance>(F.World);
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = Input.EventCenter = FVector(150.f, 0.f, 0.f);
    FString Error;
    if (!TestTrue(TEXT("The real default graph creates a ground lease"),
                  Execution->StartWithInput(Shallows->Graph.Get(), Input, {}, Error) && Execution->IsFinished()))
    {
        AddInfo(Error);
        return false;
    }
    auto* Pool = FindSurface(Water);
    if (!TestNotNull(TEXT("Water is present as an actual replicated terrain actor"), Pool))
    {
        return false;
    }
    TestTrue(TEXT("The tile uses a stable native class with replicated existence and a water visual"),
             Pool->GetIsReplicated() && Pool->Ground->GetStaticMesh() && Pool->Ground->GetMaterial(0) &&
                 Pool->Ground->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    Pool->UpdateContacts();
    StepSimulation();
    TestEqual(TEXT("Actor becomes Wet through floor contact"), AbilityElements::Read(F.SourceToken, F.TargetToken),
              EAbilityExposure::Wet);
    TestEqual(TEXT("Mass becomes Wet through the same element reaction"), Sim->GetElementalExposure(Entity),
              EAbilityExposure::Wet);
    const auto* WaterLease = Pool->FindComponentByClass<UAbilityWorkshopTerrainLease>();
    if (!TestNotNull(TEXT("The created Water has its own authority lease"), WaterLease))
    {
        return false;
    }
    const double Deadline = WaterLease->GetExpiry();

    TestTrue(TEXT("Ice reacts with Water's Wet state"),
             AbilityElements::Apply(F.SourceToken, F.TargetToken, EAbilityElement::Ice, 1.f));
    Pool->UpdateContacts();
    TestEqual(TEXT("Repeated scans preserve the stronger reaction"),
              AbilityElements::Read(F.SourceToken, F.TargetToken), EAbilityExposure::Frozen);
    const auto Converted =
        AbilityWorkshopTerrainNodes::ConvertSurface(F.SourceToken, Input.Origin, 260.f, Water, Conductive);
    auto* Charged = FindSurface(Conductive);
    if (!TestTrue(TEXT("The original terrain transaction replaces Water with Charged Water"),
                  Converted.IsSet() && Charged && !FindSurface(Water)))
    {
        return false;
    }
    TestEqual(TEXT("Charging preserves the exact original deadline"),
              Charged->FindComponentByClass<UAbilityWorkshopTerrainLease>()->GetExpiry(), Deadline);
    TestNotNull(TEXT("Charged Water carries its own resident Niagara asset"), Charged->SurfaceParticles->GetAsset());
    Charged->UpdateContacts();
    StepSimulation();
    TestEqual(TEXT("Thunder contacting the previously Wet Mass creature resolves Conductive"),
              Sim->GetElementalExposure(Entity), EAbilityExposure::Conductive);
    TestEqual(TEXT("Charging leaves currently Frozen Actor state intact"),
              AbilityElements::Read(F.SourceToken, F.TargetToken), EAbilityExposure::Frozen);
    StepSimulation();
    TestEqual(TEXT("A continuous charged contact cannot alternate Conductive back to Shocked every scan"),
              Sim->GetElementalExposure(Entity), EAbilityExposure::Conductive);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
