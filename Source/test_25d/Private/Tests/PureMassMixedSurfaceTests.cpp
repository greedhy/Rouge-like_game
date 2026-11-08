/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.PureMass.Mixed.SkillSurfaceDamageAndControl
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Terrain/TerrainSurface.h"
#include "Terrain/TerrainStatusComponent.h"
#include "Terrain/TerrainStatusEffects.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"
#include "UObject/Script.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PureMassMixedSurfaceTests
{
    struct FFixture
    {
        FEditorScriptExecutionGuard Guard;
        UWorld* World = nullptr;
        AEnemyCharacter* Caster = nullptr;
        AEnemyCharacter* Enemy = nullptr;
        UPureMassSubsystem* Sim = nullptr;
        UMassEntitySubsystem* Mass = nullptr;
        UPureMassProcessor* Processor = nullptr;
        APureMassRuntime* Runtime = nullptr;
        ATerrainSurface* Surface = nullptr;
        int32 EntityId = INDEX_NONE;

        FFixture()
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("MixedSurface")));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            auto* Floor = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(1000, 1000, 10));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Floor->SetActorLocation(FVector(0, 0, -10));
            Caster = Character(FVector(-100, 0, 40), TEXT("Player"));
            Enemy = Character(FVector(100, 80, 40), TEXT("Enemy"));
            auto* PC = World->SpawnActor<APlayerController>();
            World->AddController(PC);
            PC->SetPawn(Caster);
            Caster->GetAbilitySystemComponent()->AbilityActorInfo->PlayerController = PC;
            Sim = World->GetSubsystem<UPureMassSubsystem>();
            Mass = World->GetSubsystem<UMassEntitySubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 1000.f;
            Type->Radius = 20.f;
            Type->HalfHeight = 40.f;
            Type->Speed = 100.f;
            Type->AttackRange = 1.f;
            Type->AttackDamage = 0.f;
            Type->XPReward = 0;
            EntityId = Sim->SpawnMonster(Type, FVector(100, 0, 40));
            auto& Manager = Mass->GetMutableEntityManager();
            auto& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Sim->GetEntity(EntityId));
            Fragment.Path = {Caster->GetActorLocation()};
            Fragment.PathGoal = Caster->GetActorLocation();
            Fragment.NextPathTime = MAX_flt;
            Processor = NewObject<UPureMassProcessor>(World);
            Processor->CallInitialize(World, Manager.AsShared());
        }

        ~FFixture()
        {
            if (Caster)
                Caster->TerrainStatus->ClearTerrainEffects();
            if (Enemy)
                Enemy->TerrainStatus->ClearTerrainEffects();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        AEnemyCharacter* Character(FVector Position, FName Team)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Character = World->SpawnActor<AEnemyCharacter>(Position, FRotator::ZeroRotator, Params);
            Character->Tags.Reset();
            Character->Tags.Add(Team);
            Character->GetCapsuleComponent()->SetCapsuleSize(20.f, 40.f);
            Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Character->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            auto* ASC = Character->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(Character, Character);
            ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Character));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
            return Character;
        }

        bool SpawnSurface(ETerrainSurfaceType Kind)
        {
            auto* Config = NewObject<UAbilityPrototype>(World);
            Config->Kind = EAbilityPrototypeKind::Surface;
            Config->SurfaceClass = ATerrainSurface::StaticClass();
            Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Config->Cooldown = 0.f;
            Config->Duration = 3.f;
            Config->Radius = 250.f;
            auto* Context = NewObject<UPrototypeCastContext>(Caster);
            Context->Caster = Caster;
            Context->CastId = FGuid::NewGuid();
            Context->DamageParams.SourceAbilitySystemComponent = Caster->GetAbilitySystemComponent();
            Runtime = APureMassRuntime::Spawn(Context, Config, FVector(0, 0, 40), FVector::ForwardVector);
            if (!Runtime)
                return false;
            if (!Runtime->HasActorBegunPlay())
                Runtime->DispatchBeginPlay();
            for (TActorIterator<ATerrainSurface> It(World); It; ++It)
                if (It->GetOwner() == Caster)
                {
                    Surface = *It;
                    break;
                }
            if (!Surface)
                return false;
            // Tune the skill-created native instance; production terrain Blueprints supply this config.
            Surface->Config = NewObject<UTerrainSurfaceConfig>(Surface);
            Surface->Config->SurfaceType = Kind;
            Surface->Config->DamagePerSecond = 20.f;
            Surface->Config->MovementMultiplier = .5f;
            Surface->Config->EntangleDuration = 2.f;
            Surface->bRequireGrounded = false;
            if (!Surface->HasActorBegunPlay())
                Surface->DispatchBeginPlay();
            Surface->UpdateContacts();
            return true;
        }

        void Advance(int32 Steps)
        {
            auto& Manager = Mass->GetMutableEntityManager();
            for (int32 Index = 0; Index < Steps; ++Index)
            {
                World->TimeSeconds += .05;
                ++GFrameCounter;
                World->GetTimerManager().Tick(.05f);
                Enemy->TerrainStatus->TickComponent(.05f, LEVELTICK_All, nullptr);
                Caster->TerrainStatus->TickComponent(.05f, LEVELTICK_All, nullptr);
                auto Execution = Manager.CreateExecutionContext(.05f);
                Execution.SetExecutionType(EMassExecutionContextType::Processor);
                Processor->CallExecute(Manager, Execution);
            }
        }
    };
} // namespace PureMassMixedSurfaceTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedSurface, "test_25d.PureMass.Mixed.SkillSurfaceDamageAndControl",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.SkillSurfaceDamageAndControl 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedSurface::RunTest(const FString&)
{
    using namespace PureMassMixedSurfaceTests;
    for (const auto Kind : {ETerrainSurfaceType::Lava, ETerrainSurfaceType::Frost, ETerrainSurfaceType::Toxic,
                            ETerrainSurfaceType::Vines})
    {
        FFixture F;
        if (!TestTrue(TEXT("Surface prototype creates owned terrain"), F.SpawnSurface(Kind)))
            return false;
        TestTrue(TEXT("Skill surface enforces enemy filter"), F.Surface->bOnlyAffectCasterEnemies);
        TestFalse(TEXT("Overlapping caster is excluded from surface"), F.Surface->CanAffectCharacter(F.Caster));
        TestTrue(TEXT("Overlapping Actor enemy participates in contact scan"), F.Surface->CanAffectCharacter(F.Enemy));
        TestTrue(TEXT("Spawned surface cannot become an extra movement wall"),
                 F.Surface->Ground->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        F.Advance(1);
        FPureMassView View;
        if (!TestTrue(TEXT("Mass enemy remains alive"), F.Sim->Resolve(F.EntityId, View)))
            return false;
        TestTrue(TEXT("Actor receives terrain status through overlap scan"),
                 F.Enemy->TerrainStatus->IsTerrainStatusActive(Kind));
        if (Kind == ETerrainSurfaceType::Frost)
        {
            TestEqual(TEXT("Actor frost uses configured movement multiplier"),
                      F.Enemy->TerrainStatus->GetTerrainMovementMultiplier(), .5f);
            TestTrue(TEXT("Mass frost halves cached-path movement"),
                     FMath::IsNearlyEqual(View.Velocity.Size2D(), 50.f, .1f));
        }
        if (Kind == ETerrainSurfaceType::Vines)
        {
            const auto& Fragment = F.Mass->GetMutableEntityManager().GetFragmentDataChecked<FPureMassFragment>(
                F.Sim->GetEntity(F.EntityId));
            TestTrue(TEXT("Mass vines apply a timed root"), Fragment.RootRemaining > 0.f);
            TestTrue(TEXT("Root prevents Mass cached-path movement"), View.Location.Equals(FVector(100, 0, 40), .01f));
            TestEqual(TEXT("Actor root prevents movement"), F.Enemy->GetCharacterMovement()->MaxWalkSpeed, 0.f);
        }
        F.Advance(23);
        F.Sim->Resolve(F.EntityId, View);
        if (Kind == ETerrainSurfaceType::Lava || Kind == ETerrainSurfaceType::Toxic)
        {
            TestTrue(TEXT("Mass terrain damage reduces HP"), View.Health < 1000.f);
            TestEqual(TEXT("Actor periodic GAS damage ticks once after one second"),
                      F.Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                      980.f);
        }
        TestEqual(TEXT("Overlapping caster takes no terrain damage"),
                  F.Caster->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  1000.f);
        TestFalse(TEXT("Overlapping caster receives no terrain status"),
                  F.Caster->TerrainStatus->IsTerrainStatusActive(Kind));
        F.Runtime->CancelCast();
        TestFalse(TEXT("Cancelling skill destroys its owned terrain"), IsValid(F.Surface));
        // An applied root keeps its finite duration after contact ends; damage/slow clear immediately.
        F.Advance(Kind == ETerrainSurfaceType::Vines ? 20 : 1);
        TestFalse(TEXT("Destroyed surface cannot leave a permanent Actor GAS status"),
                  F.Enemy->TerrainStatus->IsTerrainStatusActive(Kind));
    }
    return true;
}
#endif
