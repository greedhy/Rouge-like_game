// 测试链：真实角色/ASC -> 防御执行与实际攻击入口 -> 位移、伤害、奖励租约及生命清理。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Defense/PerfectDodgeComponent.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDefenseNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityMeleeNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "PerfectDodgeTestTypes.h"
#include "Actor/MyProjectile.h"
#include "Actor/EnemySlingProjectile.h"
#include "MyGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    AActor* DodgeBox(UWorld* World, const FVector& Position, const FVector& Extent)
    {
        auto* Actor = World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box);
        Actor->AddInstanceComponent(Box);
        Box->SetBoxExtent(Extent);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent();
        Actor->SetActorLocation(Position);
        return Actor;
    }

    FGuid BeginDodge(FAbilityGraphTestFixture& F, UPerfectDodgeComponent*& Defense, FAbilitySourceToken& AttackSource,
                     FString& Error, const FVector& Direction = FVector::ForwardVector)
    {
        DodgeBox(F.World, FVector(0, 0, -10), FVector(2000, 2000, 10));
        F.Source->SetActorLocation(FVector(0, 0, F.Source->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
        F.Target->SetActorLocation(FVector(-120, 0, F.Source->GetActorLocation().Z));
        F.Source->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        AbilityExecutionIdentity::CaptureSource(F.Target, AttackSource, Error);
        Defense = UPerfectDodgeComponent::FindOrCreate(F.Source);
        FPerfectDodgeSettings Settings;
        Settings.Reward.BlendIn = 0.f;
        const FGuid Execution = Defense->Reserve(F.SourceToken, F.Source, Settings, Direction, Error);
        Defense->Begin(Execution);
        return Execution;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeWitnessTest, "test_25d.UnifiedGA.PerfectDodge.WitnessAndDirectHit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeWitnessTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(10., false);
    UPerfectDodgeComponent* Defense = nullptr;
    FAbilitySourceToken Attack;
    FString Error;
    const FGuid Execution = BeginDodge(F, Defense, Attack, Error);
    TestTrue(TEXT("Grounded execution reserves and begins"), Execution.IsValid());
    TestFalse(TEXT("Empty roll never wins a reward"), Defense->WasPerfect(Execution));
    const FVector Start = F.Source->GetActorLocation();
    Time->SampleClockForTesting(10.08, false);
    Defense->TickComponent(.08f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Shared dash moves the real capsule out of the contact area"),
             F.Source->GetActorLocation().X > 100.f);
    const FGuid AttackId = FGuid::NewGuid();
    UPerfectDodgeComponent::ObserveActorAttack(Attack, AttackId, Start,
                                               [&](const FVector& Center, float, float)
                                               {
                                                   return FVector::DistSquared(Center, Start) < FMath::Square(50.f);
                                               });
    TestTrue(TEXT("Actual attack at vacated start wins"), Defense->WasPerfect(Execution));
    const FGuid Reward = Defense->GetRewardLease();
    TestTrue(TEXT("Reward is an actual independent time lease"), Time->IsActive(Reward));
    TestTrue(TEXT("Direct hit suppressed before any GE"),
             UPerfectDodgeComponent::TryDodgeActorHit(F.Source, Attack, AttackId));
    TestEqual(TEXT("Repeated attack callback does not create another lease"), Defense->GetRewardLease(), Reward);
    Defense->Finish(Execution, false);
    TestTrue(TEXT("Normal roll finish leaves won reward alive"), Time->IsActive(Reward));
    F.Source->GetOnDeathDelegate().Broadcast(F.Source);
    TestFalse(TEXT("Death immediately releases even a completed roll reward"), Time->IsActive(Reward));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeWindowTest, "test_25d.UnifiedGA.PerfectDodge.WindowsWallAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeWindowTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(20., false);
    UPerfectDodgeComponent* Defense = nullptr;
    FAbilitySourceToken Attack;
    FString Error;
    const FGuid Execution = BeginDodge(F, Defense, Attack, Error);
    Time->SampleClockForTesting(20.13, false);
    TestTrue(TEXT("Ordinary invulnerability remains after perfect window"),
             UPerfectDodgeComponent::TryDodgeActorHit(F.Source, Attack, FGuid::NewGuid()));
    TestFalse(TEXT("Early input cannot win after precise window"), Defense->WasPerfect(Execution));
    Time->SampleClockForTesting(20.25, false);
    TestFalse(TEXT("Invulnerability expires in gameplay real time"),
              UPerfectDodgeComponent::TryDodgeActorHit(F.Source, Attack, FGuid::NewGuid()));
    Defense->Finish(Execution, true);
    TestFalse(TEXT("Cancel releases actual movement claim"),
              UAbilityMovementOwnershipComponent::HasActiveClaim(F.Source));

    F.Source->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    const FGuid Next = Defense->Reserve(F.SourceToken, F.Source, {}, FVector::ForwardVector, Error);
    TestTrue(TEXT("New execution allowed after cancel"), Defense->Begin(Next));
    DodgeBox(F.World, FVector(-60, 0, 100), FVector(10, 100, 100));
    UPerfectDodgeComponent::ObserveActorAttack(Attack, FGuid::NewGuid(), F.Target->GetActorLocation(),
                                               [](const FVector&, float, float)
                                               {
                                                   return true;
                                               });
    TestFalse(TEXT("Wall rejects witness even inside shape"), Defense->WasPerfect(Next));
    auto* Melee = NewObject<UAbilityGraphDefinition>(F.World);
    Melee->SchemaVersion = 2;
    Melee->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode MeleeNode;
    MeleeNode.NodeId = FGuid::NewGuid();
    MeleeNode.TypeId = TEXT("Melee.Contact");
    auto* Contact = NewObject<UAbilityMeleeNodeConfig>(Melee);
    Contact->Radius = 300.f;
    MeleeNode.Config = Contact;
    Melee->Nodes.Add(MeleeNode);
    Melee->EntryNodeId = MeleeNode.NodeId;
    FAbilityNodeInput MeleeInput;
    MeleeInput.SourceToken = Attack;
    MeleeInput.Origin = F.Target->GetActorLocation();
    MeleeInput.DamageParams.DamageGameplayEffectClass = UPerfectDodgeHitTestEffect::StaticClass();
    MeleeInput.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Physics;
    auto* MeleeGraph = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Original contact path executes behind wall"),
             MeleeGraph->StartWithInput(Melee, MeleeInput, {}, Error));
    TestFalse(TEXT("Current capsule contact cannot award a through-wall perfect"), Defense->WasPerfect(Next));
    F.ASC->InitAbilityActorInfo(F.Source, F.Target);
    Defense->TickComponent(.01f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("ASC avatar replacement closes old execution"), Defense->IsRunning(Next));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeDamageTest,
                                 "test_25d.UnifiedGA.PerfectDodge.RealDamageAndDebuffSuppression",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeDamageTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(30., false);
    UPerfectDodgeComponent* Defense = nullptr;
    FAbilitySourceToken Attack;
    FString Error;
    const FGuid Execution = BeginDodge(F, Defense, Attack, Error);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    FDamageEffectParams Damage;
    Damage.SourceAbilitySystemComponent = Attack.ASC.Get();
    Damage.DamageGameplayEffectClass = UPerfectDodgeHitTestEffect::StaticClass();
    Damage.DamageType = FMyGameplayTags::Get().Damage_Physics;
    Damage.BaseDamage = 10.f;
    const auto Debuff = FGameplayTag::RequestGameplayTag(TEXT("Test.PerfectDodge.HitDebuff"));
    AbilityCharacterActions::ApplyTelegraphedTargets(Attack, Damage, {}, {F.Source}, {}, FGuid::NewGuid());
    TestEqual(TEXT("Real direct adapter preserves Health while dodging"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    TestFalse(TEXT("No hit-applied Debuff GE entered the target ASC"), F.ASC->HasMatchingGameplayTag(Debuff));
    TestTrue(TEXT("Direct attack actually won independent reward"), Defense->WasPerfect(Execution));
    const FGuid Reward = Defense->GetRewardLease();
    Defense->Finish(Execution, true);
    TestFalse(TEXT("Cancel after success releases that execution reward"), Time->IsActive(Reward));
    AbilityCharacterActions::ApplyTelegraphedTargets(Attack, Damage, {}, {F.Source}, {}, FGuid::NewGuid());
    TestEqual(TEXT("The same real adapter damages after cancellation"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    TestTrue(TEXT("The same real GE applies its hit Debuff after cancellation"), F.ASC->HasMatchingGameplayTag(Debuff));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeGraphCancelTest,
                                 "test_25d.UnifiedGA.PerfectDodge.GraphCancellationAndLifeGeneration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeGraphCancelTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(40., false);
    UPerfectDodgeComponent* Defense = nullptr;
    FAbilitySourceToken Attack;
    FString Error;
    const FGuid Old = BeginDodge(F, Defense, Attack, Error);
    Defense->InvalidateLife();
    F.Source->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    F.Graph->SchemaVersion = 2;
    F.Graph->CompatibilityProfile = TEXT("UnifiedV2");
    F.Graph->Nodes.Reset();
    F.Graph->Edges.Reset();
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Defense.PerfectDodge");
    Node.Config = NewObject<UAbilityPerfectDodgeNodeConfig>(F.Graph);
    F.Graph->Nodes.Add(Node);
    F.Graph->EntryNodeId = Node.NodeId;
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Direction = FVector::ForwardVector;
    auto* Graph = NewObject<UAbilityGraphInstance>(F.World);
    int32 Completed = 0;
    Graph->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Completed += Event.OutputPort == TEXT("Completed") ? 1 : 0;
        });
    TestTrue(TEXT("New life can start the registered defense strategy"),
             Graph->StartWithInput(F.Graph, Input, {}, Error));
    const FGameplayTagContainer AbilityRoot(FGameplayTag::RequestGameplayTag(TEXT("Abilities")));
    TestTrue(TEXT("Shared defense node blocks new abilities during action"), F.ASC->AreAbilityTagsBlocked(AbilityRoot));
    Defense->Finish(Old, true);
    TestTrue(TEXT("Old execution cleanup cannot release new movement"),
             UAbilityMovementOwnershipComponent::HasActiveClaim(F.Source));
    F.ASC->AddLooseGameplayTag(FMyGameplayTags::Get().Debuff_Stun);
    Defense->TickComponent(.01f, LEVELTICK_All, nullptr);
    Graph->Advance(.01f);
    TestTrue(TEXT("Strong control terminates real graph execution"), Graph->IsFinished());
    TestEqual(TEXT("Cancellation publishes no business Completed output"), Completed, 0);
    TestEqual(TEXT("Graph keeps cancelled terminal reason"), Graph->GetEndReason(), EAbilityGraphEndReason::Cancelled);
    TestFalse(TEXT("Cancellation releases only the node owned ability block"),
              F.ASC->AreAbilityTagsBlocked(AbilityRoot));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeDungeonLifeTest, "test_25d.UnifiedGA.PerfectDodge.SamePawnDungeonRevive",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeDungeonLifeTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    F.Source->SetActorLocation(FVector(3000, 0, 0));
    auto* Player = F.World->SpawnActor<APerfectDodgeTestDungeonPlayer>();
    Player->AttachState(F.World->SpawnActor<AMyPlayerState>());
    auto* ASC = Player->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
    Player->Tags.AddUnique(TEXT("Player"));
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* Defense = UPerfectDodgeComponent::FindOrCreate(Player);
    FAbilitySourceToken Source;
    FAbilitySourceToken Attack;
    FString Error;
    TestTrue(TEXT("Dungeon source captures actual PlayerState binding"),
             AbilityExecutionIdentity::CaptureSource(Player, Source, Error));
    AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error);
    const FGuid Old = Defense->Reserve(Source, Player, {}, FVector::ForwardVector, Error);
    TestTrue(TEXT("First life begins dodge"), Defense->Begin(Old));
    TestTrue(TEXT("First life suppresses direct hit"),
             UPerfectDodgeComponent::TryDodgeActorHit(Player, Attack, FGuid::NewGuid()));
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    const FGuid Reward = Defense->GetRewardLease();
    TestTrue(TEXT("First life owns reward"), Time->IsActive(Reward));
    Player->Die(FVector::ZeroVector);
    TestTrue(TEXT("Production death uses reversible downed state"), Player->IsDungeonDowned());
    TestFalse(TEXT("Downed path immediately releases reward without parent death event"), Time->IsActive(Reward));
    TestTrue(TEXT("Real production revive keeps same Pawn"), Player->ReviveForDungeon(FVector(0, 0, 100)));
    TestTrue(TEXT("New source capture after revive"), AbilityExecutionIdentity::CaptureSource(Player, Source, Error));
    const FGuid New = Defense->Reserve(Source, Player, {}, FVector::ForwardVector, Error);
    TestTrue(TEXT("New life starts fresh execution"), Defense->Begin(New));
    Defense->Finish(Old, true);
    TestTrue(TEXT("Late old close cannot close new life"), Defense->IsRunning(New));
    TestFalse(TEXT("New life inherits no prior reward"), Defense->WasPerfect(New));
    Defense->Finish(New, true);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeCollisionTest, "test_25d.UnifiedGA.PerfectDodge.CollisionClosesWindows",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeCollisionTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(50., false);
    UPerfectDodgeComponent* Defense = nullptr;
    FAbilitySourceToken Attack;
    FString Error;
    const FGuid Execution = BeginDodge(F, Defense, Attack, Error);
    DodgeBox(F.World, FVector(70, 0, 100), FVector(10, 200, 100));
    Time->SampleClockForTesting(50.08, false);
    Defense->TickComponent(.08f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("GroundDash actual capsule is stopped before wall"), F.Source->GetActorLocation().X < 60.f);
    TestFalse(TEXT("Collision returns movement ownership"),
              UAbilityMovementOwnershipComponent::HasActiveClaim(F.Source));
    TestFalse(TEXT("Collision closes invulnerability immediately"),
              UPerfectDodgeComponent::TryDodgeActorHit(F.Source, Attack, FGuid::NewGuid()));
    TestTrue(TEXT("Blocked roll retains recovery, preventing immediate restart"), Defense->IsRunning(Execution));
    Time->SampleClockForTesting(50.43, false);
    Defense->TickComponent(.35f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("Recovery eventually ends blocked execution"), Defense->IsRunning(Execution));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeProjectileTest,
                                 "test_25d.UnifiedGA.PerfectDodge.ProjectileWitnessDoesNotConsume",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeProjectileTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(60., false);
    UPerfectDodgeComponent* Defense = nullptr;
    FAbilitySourceToken Attack;
    FString Error;
    const FGuid Execution = BeginDodge(F, Defense, Attack, Error);
    auto* Shot = F.World->SpawnActor<AMyProjectile>();
    Shot->SetActorLocation(F.Source->GetActorLocation() - FVector(100, 0, 0));
    Shot->SetExecutionSourceToken(Attack);
    Time->SampleClockForTesting(60.08, false);
    Defense->TickComponent(.08f, LEVELTICK_All, nullptr);
    Shot->SetActorLocation(Shot->GetActorLocation() + FVector(200, 0, 0));
    Shot->Tick(.08f);
    TestTrue(TEXT("Actual projectile flight segment confirms vacated capsule"), Defense->WasPerfect(Execution));
    TestFalse(TEXT("Witness never consumes or destroys the projectile"), Shot->IsActorBeingDestroyed());
    F.Target->GetOnDeathDelegate().Broadcast(F.Target);
    TestTrue(TEXT("Original projectile source death retires late threat"), Shot->IsActorBeingDestroyed());
    Defense->Finish(Execution, true);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeTelegraphWitnessTest,
                                 "test_25d.UnifiedGA.PerfectDodge.TelegraphWitnessAndReadOnlyQuery",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeTelegraphWitnessTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(70., false);
    UPerfectDodgeComponent* Defense = nullptr;
    FAbilitySourceToken Attack;
    FString Error;
    const FGuid Execution = BeginDodge(F, Defense, Attack, Error);
    FStoneGolemAttackTuning Shape;
    Shape.Radius = 60.f;
    TArray<FVector> Surface;
    TArray<int32> Triangles;
    StoneGolemGeometry::BuildSurface(F.World, F.Target, Shape, FVector::ZeroVector, 0.f, Surface, Triangles);
    TArray<AActor*> Targets;
    AbilityCharacterActions::GatherTelegraphedTargets(Attack, Shape, FVector::ZeroVector, 0.f, Surface, Triangles,
                                                      nullptr, Targets);
    TestTrue(TEXT("Original geometry sees current target before dash"), Targets.Contains(F.Source));
    TestFalse(TEXT("A read-only query never confirms an attack"), Defense->WasPerfect(Execution));
    Time->SampleClockForTesting(70.08, false);
    Defense->TickComponent(.08f, LEVELTICK_All, nullptr);
    AbilityCharacterActions::GatherTelegraphedTargets(Attack, Shape, FVector::ZeroVector, 0.f, Surface, Triangles,
                                                      nullptr, Targets);
    TestFalse(TEXT("Real target moved outside actual telegraph geometry"), Targets.Contains(F.Source));
    AbilityCharacterActions::ObserveTelegraphedAttack(Attack, Shape, FVector::ZeroVector, 0.f, Surface, Triangles,
                                                      nullptr, FGuid::NewGuid());
    TestTrue(TEXT("Actual telegraph contact confirms original floor-projected capsule"),
             Defense->WasPerfect(Execution));
    Defense->Finish(Execution, true);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeSlingWallTest,
                                 "test_25d.UnifiedGA.PerfectDodge.SlingTerminalWallPreservesWitness",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeSlingWallTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    for (const bool bWallBeyondWitness : {false, true})
    {
        FAbilityGraphTestFixture F;
        auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
        Time->SampleClockForTesting(80., false);
        UPerfectDodgeComponent* Defense = nullptr;
        FAbilitySourceToken Attack;
        FString Error;
        const FGuid Execution = BeginDodge(F, Defense, Attack, Error, FVector::RightVector);
        const float Height = F.Source->GetActorLocation().Z;
        const FTransform Launch(FRotator::ZeroRotator, FVector(-250, 0, Height));
        auto* Shot = F.World->SpawnActorDeferred<AEnemySlingProjectile>(
            AEnemySlingProjectile::StaticClass(), Launch, F.Target, F.Target,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!TestNotNull(TEXT("Real Sling projectile is created"), Shot))
        {
            return false;
        }
        Shot->InitializeSource(F.Target);
        Shot->SetExecutionSourceToken(Attack);
        Shot->DamageEffectParams.SourceAbilitySystemComponent = Attack.ASC.Get();
        Shot->Sphere->SetSphereRadius(10.f);
        Shot->Sphere->IgnoreActorWhenMoving(F.Target, true);
        Shot->ProjectileMovement->bInitialVelocityInLocalSpace = false;
        Shot->ProjectileMovement->InitialSpeed = Shot->ProjectileMovement->MaxSpeed = 10000.f;
        Shot->ProjectileMovement->Velocity = FVector(10000, 0, 0);
        Shot->FinishSpawning(Launch);
        Shot->DispatchBeginPlay();
        int32 DamageNotifications = 0;
        Shot->DamageApplied.AddLambda(
            [&](AActor*)
            {
                ++DamageNotifications;
            });
        Time->SampleClockForTesting(80.08, false);
        Defense->TickComponent(.08f, LEVELTICK_All, nullptr);
        TestTrue(TEXT("Actual dodge moves the body perpendicular to the shot"), F.Source->GetActorLocation().Y > 100.f);
        DodgeBox(F.World, FVector(bWallBeyondWitness ? 160.f : -100.f, 0, Height), FVector(10, 200, 100));

        // Production Sling ticks before ProjectileMovement: this Tick moves only to its wall sweep hit,
        // invokes WorldStopped and destroys immediately. There is deliberately no second base Tick.
        Shot->Tick(.08f);
        TestTrue(TEXT("Original wall path still terminates the Sling in the same frame"),
                 Shot->IsActorBeingDestroyed());
        TestEqual(TEXT("Terminal segment rewards only a witness reached before the wall"),
                  Defense->WasPerfect(Execution), bWallBeyondWitness);
        TestEqual(TEXT("Witness and wall never publish player damage"), DamageNotifications, 0);
        Defense->Finish(Execution, true);
    }
    return true;
}
#endif
