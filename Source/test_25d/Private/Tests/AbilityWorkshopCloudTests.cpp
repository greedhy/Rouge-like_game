// 测试链：真实地块图 → 云/地转换租约 → 原 Actor/Mass 接触、作用域隔离和退役。
#if WITH_DEV_AUTOMATION_TESTS

#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
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

namespace
{
    /** 测试用实际已登记图和原施法来源，不会手工向敌方应用湿润。 */
    bool ExecuteCloudGraph(FAbilityGraphTestFixture& Fixture, const TCHAR* AbilityPath, FVector Origin, FString& Error)
    {
        const auto* AbilityClass = FindObject<UClass>(nullptr, AbilityPath);
        if (!AbilityClass)
        {
            Error = FString::Printf(TEXT("Missing native cloud graph: %s"), AbilityPath);
            return false;
        }
        const auto* Ability = AbilityClass->GetDefaultObject<UWorkshopUtilityAbility>();
        FAbilityNodeInput Input;
        Input.SourceToken = Fixture.SourceToken;
        Input.Origin = Input.EventCenter = Origin;
        auto* Execution = NewObject<UAbilityGraphInstance>(Fixture.World);
        return Execution->StartWithInput(Ability->Graph.Get(), Input, {}, Error) && Execution->IsFinished();
    }

    /** 从实际世界查找特定来源和枚举的地块，而非读取节点日志。 */
    ATerrainSurface* LocateCloudTestSurface(FAbilityGraphTestFixture& Fixture, ETerrainSurfaceType Kind)
    {
        for (TActorIterator<ATerrainSurface> It(Fixture.World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed() && It->GetOwner() == Fixture.Source && It->Config &&
                It->Config->SurfaceType == Kind)
            {
                return *It;
            }
        }
        return nullptr;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopCloudLayer, "test_25d.UnifiedGA.WorkshopTerrain.CloudLayer",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopCloudLayer::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    Fixture.Source->SetActorLocation(FVector(-750.f, 0.f, 40.f));
    Fixture.Target->SetActorLocation(FVector(150.f, 170.f, 180.f));
    Fixture.Target->GetCapsuleComponent()->SetCapsuleSize(20.f, 40.f);
    Fixture.Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Fixture.Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    Fixture.Target->GetCharacterMovement()->SetMovementMode(MOVE_Falling);

    // 薄楼板仅覆盖同一云高度范围内的一侧，水平体积重叠不能穿楼板传递。
    auto* FloorAbove = Fixture.World->SpawnActor<AActor>();
    auto* Slab = NewObject<UBoxComponent>(FloorAbove);
    FloorAbove->SetRootComponent(Slab);
    Slab->SetBoxExtent(FVector(100.f, 42.f, 2.f));
    Slab->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Slab->SetCollisionObjectType(ECC_WorldStatic);
    Slab->SetCollisionResponseToAllChannels(ECR_Block);
    Slab->RegisterComponent();
    FloorAbove->SetActorLocation(FVector(150.f, 130.f, 120.f));

    auto* Sim = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    auto* Mob = NewObject<UPureMassMonsterType>(Fixture.World);
    Mob->Health = 100.f;
    Mob->Radius = 20.f;
    Mob->HalfHeight = 40.f;
    const int32 Within = Sim->SpawnMonster(Mob, FVector(150.f, -80.f, 120.f));
    const int32 Above = Sim->SpawnMonster(Mob, FVector(150.f, -80.f, 370.f));
    const int32 BehindSlab = Sim->SpawnMonster(Mob, FVector(150.f, 200.f, 180.f));
    if (!TestTrue(TEXT("Three live Mass targets exist in open air, higher air and behind the floor"),
                  Within > 0 && Above > 0 && BehindSlab > 0))
    {
        return false;
    }
    auto& Manager = Fixture.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    auto* Processor = NewObject<UPureMassProcessor>(Fixture.World);
    Processor->CallInitialize(Fixture.World, Manager.AsShared());
    auto StepSimulation = [&]()
    {
        Fixture.World->TimeSeconds += .25;
        ++GFrameCounter;
        auto Execution = Manager.CreateExecutionContext(.05f);
        Execution.SetExecutionType(EMassExecutionContextType::Processor);
        Processor->CallExecute(Manager, Execution);
    };

    const auto Water = ETerrainSurfaceType::Water;
    // 红阶段允许在尚没有云枚举的版本构建此回归。
    const auto Mist = static_cast<ETerrainSurfaceType>(6);
    const auto Charged = static_cast<ETerrainSurfaceType>(7);
    const FVector Origin(150.f, 0.f, 0.f);
    FString Error;
    const auto Cast = [&](const TCHAR* Class, FVector Aim)
    {
        if (ExecuteCloudGraph(Fixture, Class, Aim, Error))
        {
            return true;
        }
        AddError(Error);
        return false;
    };
    if (!Cast(TEXT("/Script/test_25d.WorkshopWaterShallows"), Origin))
    {
        return false;
    }
    auto* Tile = LocateCloudTestSurface(Fixture, Water);
    if (!TestNotNull(TEXT("The baseline ground tile exists"), Tile))
    {
        return false;
    }
    const auto* GroundLease = Tile->FindComponentByClass<UAbilityWorkshopTerrainLease>();
    const double Deadline = GroundLease->GetExpiry();
    const FVector2D Size = Tile->SurfaceSize;
    Tile->UpdateContacts();
    StepSimulation();
    TestEqual(TEXT("Airborne Actor is not contacted by the ground tile"),
              AbilityElements::Read(Fixture.SourceToken, Fixture.TargetToken), EAbilityExposure::None);
    TestEqual(TEXT("Airborne Mass is not contacted by the ground tile"), Sim->GetElementalExposure(Within),
              EAbilityExposure::None);

    if (!Cast(TEXT("/Script/test_25d.WorkshopMistLift"), Origin))
    {
        return false;
    }
    auto* Cloud = LocateCloudTestSurface(Fixture, Mist);
    if (!TestNotNull(TEXT("Vaporizing creates a real cloud actor"), Cloud))
    {
        return false;
    }
    TestTrue(TEXT("Native cloud replicates with actual Niagara and preserves footprint, floor anchor and deadline"),
             Cloud->GetIsReplicated() && Cloud->SurfaceParticles->GetAsset() && Cloud->SurfaceSize.Equals(Size) &&
                 Cloud->GetActorLocation().Equals(Origin) &&
                 Cloud->FindComponentByClass<UAbilityWorkshopTerrainLease>()->GetExpiry() == Deadline);
    Cloud->UpdateContacts();
    StepSimulation();
    TestEqual(TEXT("An Actor inside the volume does not receive water through a thin floor"),
              AbilityElements::Read(Fixture.SourceToken, Fixture.TargetToken), EAbilityExposure::None);
    TestEqual(TEXT("A Mass target inside the volume does not receive water through a thin floor"),
              Sim->GetElementalExposure(BehindSlab), EAbilityExposure::None);
    const auto OtherFloorAim = FVector(150.f, 130.f, 126.f);
    if (!Cast(TEXT("/Script/test_25d.WorkshopMistCondense"), OtherFloorAim))
    {
        return false;
    }
    TestTrue(TEXT("Cloud lookup from the upper floor ignores the cloud anchored below it"),
             LocateCloudTestSurface(Fixture, Mist) == Cloud && !LocateCloudTestSurface(Fixture, Water));
    Fixture.Target->SetActorLocation(FVector(150.f, 80.f, 120.f));
    Cloud->UpdateContacts();
    TestEqual(TEXT("Airborne Actor enters elevated cloud volume"),
              AbilityElements::Read(Fixture.SourceToken, Fixture.TargetToken), EAbilityExposure::Wet);
    TestEqual(TEXT("Airborne Mass uses the same cloud bounds"), Sim->GetElementalExposure(Within),
              EAbilityExposure::Wet);
    TestEqual(TEXT("Mass on a different vertical layer remains unaffected"), Sim->GetElementalExposure(Above),
              EAbilityExposure::None);

    if (!Cast(TEXT("/Script/test_25d.WorkshopTerrainDispel"), Origin))
    {
        return false;
    }
    TestTrue(TEXT("Ground-layer dispel leaves the cloud intact"), LocateCloudTestSurface(Fixture, Mist) == Cloud);
    if (!Cast(TEXT("/Script/test_25d.WorkshopStormMist"), Origin))
    {
        return false;
    }
    Cloud = LocateCloudTestSurface(Fixture, Charged);
    if (!TestNotNull(TEXT("Charging replaces the original mist lease"), Cloud))
    {
        return false;
    }
    Cloud->UpdateContacts();
    StepSimulation();
    TestEqual(TEXT("Wet airborne target reacts to charged mist"),
              AbilityElements::Read(Fixture.SourceToken, Fixture.TargetToken), EAbilityExposure::Conductive);
    TestEqual(TEXT("Mass Wet reacts to charged mist"), Sim->GetElementalExposure(Within), EAbilityExposure::Conductive);
    TestTrue(TEXT("Charging does not reset the expiry"),
             Cloud->FindComponentByClass<UAbilityWorkshopTerrainLease>()->GetExpiry() == Deadline);

    // 同一地表范围可以有不同层。驱雾绝不退役地层。
    if (!Cast(TEXT("/Script/test_25d.WorkshopWaterShallows"), Origin) ||
        !Cast(TEXT("/Script/test_25d.WorkshopMistDispel"), Origin))
    {
        return false;
    }
    TestNull(TEXT("Cloud dispel removes charged mist"), LocateCloudTestSurface(Fixture, Charged));
    TestNotNull(TEXT("Cloud dispel leaves ground intact"), LocateCloudTestSurface(Fixture, Water));
    if (!Cast(TEXT("/Script/test_25d.WorkshopMistLift"), Origin) ||
        !Cast(TEXT("/Script/test_25d.WorkshopMistCondense"), Origin))
    {
        return false;
    }
    auto* Condensed = LocateCloudTestSurface(Fixture, Water);
    TestTrue(TEXT("Condensing restores the same ground position and footprint"),
             Condensed && Condensed->GetActorLocation().Equals(Origin) && Condensed->SurfaceSize.Equals(Size) &&
                 !LocateCloudTestSurface(Fixture, Mist));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
