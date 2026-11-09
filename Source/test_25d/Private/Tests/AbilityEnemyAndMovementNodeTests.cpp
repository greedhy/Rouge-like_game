// 调用链：Automation -> 真实弹体、Shaman角色所有权、胶囊位移 -> 图/共享服务结果和取消验证。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityTraditionalTestAbility.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityEnemyActionNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "AbilitySystem/Ability/LegacyMonsterAbility.h"
#include "AbilitySystem/Ability/TelegraphedEnemyAbility.h"
#include "Actor/EnemySlingProjectile.h"
#include "Character/ShamanEnemyCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Materials/Material.h"
#include "Player/StanderWeaponActionConfig.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    UAbilityGraphDefinition* EnemyMovementGraph(UWorld* World, FName Type, UAbilityNodeConfig* Config)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Graph->EntryNodeId = Node.NodeId;
        Graph->Nodes.Add(Node);
        return Graph;
    }
    AActor* MovementTestBox(UWorld* World, const FVector& Position, const FVector& Extent)
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
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySlingSharedAction,
                                 "test_25d.UnifiedGA.EnemyActions.SlingNativeActorAndGraphCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemySlingSharedAction::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* Settings = NewObject<USlingAttackConfig>(F.World);
    Settings->ProjectileClass = AEnemySlingProjectile::StaticClass();
    Settings->Speed = 700.f;
    Settings->Lifetime = 2.f;
    Settings->MuzzleSocket = NAME_None;
    FDamageEffectParams Damage;
    Damage.BaseDamage = 37.f;
    Damage.SourceAbilitySystemComponent = F.ASC;
    bool bPrepared = false;
    auto* Native = AbilityEnemyActions::FireSling(F.SourceToken, *Settings, F.Target->GetActorLocation(), Damage,
                                                  [&](AEnemySlingProjectile* Shot)
                                                  {
                                                      bPrepared = Shot && !Shot->HasActorBegunPlay();
                                                  });
    if (!TestNotNull(TEXT("Shared sling action spawns original actor class"), Native))
    {
        return false;
    }
    TestTrue(TEXT("Host can capture projectile before FinishSpawning"), bPrepared);
    TestEqual(TEXT("Original damage snapshot retained"), Native->DamageEffectParams.BaseDamage, 37.f);
    TestEqual(TEXT("Configured velocity magnitude retained"), Native->ProjectileMovement->Velocity.Size(), 700.0);
    TestEqual(TEXT("Configured lifespan retained"), Native->GetLifeSpan(), 2.f);
    auto* Config = NewObject<UAbilityEnemySlingNodeConfig>(F.World);
    Config->Settings = Settings;
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.LiveTargetToken = F.TargetToken;
    Input.EventCenter = F.Target->GetActorLocation();
    Input.DamageParams = Damage;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Reusable sling node starts"),
             Run->StartWithInput(EnemyMovementGraph(F.World, TEXT("Delivery.EnemySling"), Config), Input, {}, Error));
    AEnemySlingProjectile* GraphShot = nullptr;
    for (TActorIterator<AEnemySlingProjectile> It(F.World); It; ++It)
    {
        if (*It != Native)
        {
            GraphShot = *It;
        }
    }
    TestNotNull(TEXT("Graph owns distinct original projectile"), GraphShot);
    Run->Cancel();
    TestTrue(TEXT("Graph cancellation destroys its projectile"), GraphShot && GraphShot->IsActorBeingDestroyed());
    TestFalse(TEXT("Unrelated native shot survives graph cancellation"), Native->IsActorBeingDestroyed());
    F.ASC->InitAbilityActorInfo(F.Source, F.Target);
    TestFalse(TEXT("Native sling cannot damage through a rebound source ASC"), Native->IsValidOverlap(F.Target));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanSummonSharedAction,
                                 "test_25d.UnifiedGA.EnemyActions.ShamanUsesExistingMinionOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShamanSummonSharedAction::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    F.Target->SetActorLocation(FVector(2000, 0, 0));
    MovementTestBox(F.World, FVector(0, 0, -10), FVector(600, 600, 10));
    auto* Shaman = F.World->SpawnActor<AShamanEnemyCharacter>();
    Shaman->SetActorLocation(FVector(200, 200, 90));
    Shaman->GetAbilitySystemComponent()->InitAbilityActorInfo(Shaman, Shaman);
    FAbilitySourceToken Source;
    FString Error;
    TestTrue(TEXT("Shaman source captured"), AbilityExecutionIdentity::CaptureSource(Shaman, Source, Error));
    auto* Settings = NewObject<UShamanSummonConfig>(F.World);
    Settings->MinionClass = AEnemyCharacter::StaticClass();
    Settings->PerCast = Settings->MaxAlive = 1;
    Settings->MinDistance = Settings->MaxDistance = 100.f;
    auto* Config = NewObject<UAbilityShamanSummonNodeConfig>(F.World);
    Config->Settings = Settings;
    FAbilityNodeInput Input;
    Input.SourceToken = Source;
    Input.Origin = Shaman->GetActorLocation();
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Shaman summon node starts"),
             Run->StartWithInput(EnemyMovementGraph(F.World, TEXT("Spawn.ShamanMinions"), Config), Input, {}, Error));
    TestEqual(TEXT("Existing character records spawned minion"), Shaman->GetOwnedMinionCount(), 1);
    TestEqual(TEXT("Existing MaxAlive prevents duplicate swarm"),
              AbilityEnemyActions::SummonShamanMinions(Source, *Settings), 0);
    AEnemyCharacter* Minion = nullptr;
    for (TActorIterator<AEnemyCharacter> It(F.World); It; ++It)
    {
        if (It->SummoningSource.Get() == Shaman)
        {
            Minion = *It;
        }
    }
    if (TestNotNull(TEXT("Spawned pawn retains original SummoningSource provenance"), Minion))
    {
        Minion->Destroy();
        TestEqual(TEXT("Existing death/destruction callback retires ownership"), Shaman->GetOwnedMinionCount(), 0);
    }
    Shaman->GetAbilitySystemComponent()->InitAbilityActorInfo(Shaman, F.Source);
    TestEqual(TEXT("Stale cast cannot summon under rebound ASC"),
              AbilityEnemyActions::SummonShamanMinions(Source, *Settings), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterMovementSharedAction,
                                 "test_25d.UnifiedGA.CharacterActions.MovementProfilesSweepAndCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterMovementSharedAction::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    F.Target->SetActorLocation(FVector(2000, 0, 0));
    MovementTestBox(F.World, FVector(0, 0, -10), FVector(600, 600, 10));
    const float GroundZ = F.Source->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
    F.Source->SetActorLocation(FVector(0, 0, GroundZ));
    FAbilityCharacterMoveState State;
    AbilityMovementActions::GroundDash(F.SourceToken, FVector::ForwardVector, 100.f, INDEX_NONE, State);
    TestEqual(TEXT("Native dash service applies actual distance"), F.Source->GetActorLocation().X, 100.0);
    AbilityMovementActions::GroundDash(F.SourceToken, FVector::ForwardVector, 100.f, INDEX_NONE, State);
    TestEqual(TEXT("Repeated animation fraction cannot duplicate displacement"), F.Source->GetActorLocation().X, 100.0);
    auto* Wall = MovementTestBox(F.World, FVector(200, 0, 100), FVector(10, 200, 100));
    F.Source->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* Config = NewObject<UAbilityCharacterMoveNodeConfig>(F.World);
    Config->Distance = 300.f;
    Config->Duration = .5f;
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Direction = FVector::ForwardVector;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Ground dash graph starts"),
             Run->StartWithInput(EnemyMovementGraph(F.World, TEXT("Movement.Character"), Config), Input, {}, Error));
    Run->Advance(.5f);
    TestTrue(TEXT("Blocked dash completes"), Run->IsFinished());
    TestTrue(TEXT("Swept dash never crosses wall"), F.Source->GetActorLocation().X < 190.f);
    TestEqual(TEXT("Graph restores owned movement mode"), F.Source->GetCharacterMovement()->MovementMode.GetValue(),
              MOVE_Walking);
    Wall->Destroy();
    F.Source->SetActorLocation(FVector(0, 0, GroundZ));
    Config->Profile = EAbilityCharacterMoveProfile::WeaponArc;
    Config->Distance = 100.f;
    Config->Height = 100.f;
    Config->Duration = 1.f;
    Input.Direction = FVector::RightVector;
    auto* Arc = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Weapon arc graph starts"),
             Arc->StartWithInput(EnemyMovementGraph(F.World, TEXT("Movement.Character"), Config), Input, {}, Error));
    Arc->Advance(.5f);
    TestTrue(TEXT("Original jump arc reaches configured peak"),
             FMath::IsNearlyEqual(F.Source->GetActorLocation().Z, GroundZ + 100.f, .001));
    Arc->Advance(.5f);
    TestTrue(TEXT("Arc finishes at grounded original height"),
             FMath::IsNearlyEqual(F.Source->GetActorLocation().Z, GroundZ, .001));
    TestTrue(TEXT("Arc preserves full horizontal displacement"),
             FMath::IsNearlyEqual(F.Source->GetActorLocation().Y, 100.0, .001));
    TestTrue(TEXT("Arc graph completes"), Arc->IsFinished());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterMovementOwnership,
                                 "test_25d.UnifiedGA.CharacterActions.ConcurrentMovementOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterMovementOwnership::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* Movement = F.Source->GetCharacterMovement();
    Movement->SetMovementMode(MOVE_Walking);
    auto* Config = NewObject<UAbilityCharacterMoveNodeConfig>(F.World);
    Config->Distance = 0.f;
    Config->Duration = 1.f;
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    auto* Definition = EnemyMovementGraph(F.World, TEXT("Movement.Character"), Config);
    auto* First = NewObject<UAbilityGraphInstance>(F.World);
    auto* Second = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("First movement graph acquires ownership"), First->StartWithInput(Definition, Input, {}, Error));
    TestTrue(TEXT("Parallel graph request reaches the movement strategy"),
             Second->StartWithInput(Definition, Input, {}, Error));
    TestTrue(TEXT("Parallel movement execution is immediately rejected"), Second->IsFinished());
    TestEqual(TEXT("Contention ends with failure, not successful movement"), Second->GetEndReason(),
              EAbilityGraphEndReason::Failed);
    TestFalse(TEXT("Original movement execution remains active"), First->IsFinished());
    Second->Cancel();
    TestEqual(TEXT("Rejected graph cannot restore active owner's movement"), Movement->MovementMode.GetValue(),
              MOVE_None);
    First->Cancel();
    TestEqual(TEXT("Actual owner restores original walking mode"), Movement->MovementMode.GetValue(), MOVE_Walking);
    auto* Third = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("New movement can acquire after release"), Third->StartWithInput(Definition, Input, {}, Error));
    First->Cancel();
    Second->Cancel();
    TestEqual(TEXT("Late graph cancellation cannot restore new ownership"), Movement->MovementMode.GetValue(),
              MOVE_None);
    Third->Cancel();
    TestEqual(TEXT("New owner restores mode exactly once"), Movement->MovementMode.GetValue(), MOVE_Walking);

    F.Source->Tags.AddUnique(TEXT("DungeonEncounter"));
    F.Source->EnableDungeonPooling(0.f);
    FAbilitySourceToken OldSource;
    TestTrue(TEXT("Pooled original source captured"),
             AbilityExecutionIdentity::CaptureSource(F.Source, OldSource, Error));
    auto* Ownership = UAbilityMovementOwnershipComponent::FindOrCreate(F.Source);
    auto* OldOwner = NewObject<UAbilityGraphInstance>(F.World);
    auto* NewOwner = NewObject<UAbilityGraphInstance>(F.World);
    const FGuid OldLease = Ownership->TryAcquire(OldOwner, OldSource, Error);
    TestTrue(TEXT("Old life owns movement"), OldLease.IsValid());
    Movement->DisableMovement();
    F.Source->SleepForDungeonPool(false);
    TestTrue(TEXT("Source wakes as a new life"), F.Source->WakeFromDungeonPool(FTransform::Identity, NAME_None));
    Movement->SetMovementMode(MOVE_Walking);
    FAbilitySourceToken NewSource;
    TestTrue(TEXT("New source captured"), AbilityExecutionIdentity::CaptureSource(F.Source, NewSource, Error));
    const FGuid NewLease = Ownership->TryAcquire(NewOwner, NewSource, Error);
    TestTrue(TEXT("New life retires stale ownership without waiting for old graph tick"), NewLease.IsValid());
    Movement->DisableMovement();
    TestFalse(TEXT("Old owner cannot release new lease"), Ownership->Release(OldOwner, OldLease));
    TestEqual(TEXT("Old life cleanup cannot alter new movement"), Movement->MovementMode.GetValue(), MOVE_None);
    TestTrue(TEXT("Current owner releases its exact lease"), Ownership->Release(NewOwner, NewLease));
    TestEqual(TEXT("New life recovers original mode"), Movement->MovementMode.GetValue(), MOVE_Walking);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterMovementNativeHostExclusion,
                                 "test_25d.UnifiedGA.CharacterActions.NativeHostsRespectMovementOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterMovementNativeHostExclusion::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* Animation = LoadObject<UStanderWeaponActionConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_Attack1.DA_Attack1"));
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander"));
    auto* SwordConfig = LoadObject<USwordShieldConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/SwordShield/DA_SwordShield.DA_SwordShield"));
    if (!TestNotNull(TEXT("Compatible montage configuration"), Animation) ||
        !TestNotNull(TEXT("Compatible character mesh"), Mesh) ||
        !TestNotNull(TEXT("Production sword configuration"), SwordConfig))
    {
        return false;
    }
    auto* Sword = F.World->SpawnActor<AAbilityTraditionalSwordTest>();
    Sword->AttachState(F.World->SpawnActor<AMyPlayerState>());
    Sword->SwordConfig = SwordConfig;
    auto* Enemy = F.World->SpawnActor<ATelegraphedEnemyCharacter>();
    Enemy->GetAbilitySystemComponent()->InitAbilityActorInfo(Enemy, Enemy);
    Enemy->CombatTarget = F.Source;
    Enemy->SetActorLocation(F.Source->GetActorLocation() + FVector(150, 0, 0));
    Enemy->CombatConfig = NewObject<UMonsterCombatConfig>(Enemy);
    Enemy->CombatConfig->DamageEffect = UGameplayEffect::StaticClass();
    for (ACharacter* Character : {static_cast<ACharacter*>(Sword), static_cast<ACharacter*>(Enemy)})
    {
        Character->GetMesh()->SetSkeletalMeshAsset(Mesh);
        Character->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        Character->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
        Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
    auto* SwordASC = Sword->GetAbilitySystemComponent();
    auto* EnemyASC = Enemy->GetAbilitySystemComponent();
    SwordASC->RefreshAbilityActorInfo();
    EnemyASC->RefreshAbilityActorInfo();
    const auto SwordHandle =
        SwordASC->GiveAbility(FGameplayAbilitySpec(UAbilityTraditionalSwordHostTest::StaticClass(), 1));
    // 原生具体攻击定义必要冷却标签；未配置的基类本来就不能通过CheckCooldown。
    const auto EnemyHandle = EnemyASC->GiveAbility(FGameplayAbilitySpec(UDemonClawAbility::StaticClass(), 1));
    auto* SwordAbility = CastChecked<UAbilityTraditionalSwordHostTest>(
        SwordASC->FindAbilitySpecFromHandle(SwordHandle)->GetPrimaryInstance());
    auto* EnemyAbility =
        CastChecked<UTelegraphedEnemyAbility>(EnemyASC->FindAbilitySpecFromHandle(EnemyHandle)->GetPrimaryInstance());
    SwordAbility->ConfigureDamage();
    EnemyAbility->AttackConfig = NewObject<UStoneGolemAttackConfig>(EnemyAbility);
    auto& Tuning = EnemyAbility->AttackConfig->Tuning;
    Tuning.Montage = Animation->Montage;
    Tuning.AnimationDelay = .3f;
    Tuning.ContactTime = .1f;
    Tuning.LockTime = .1f;
    Tuning.RecoveryEnd = Tuning.AnimationDelay + Tuning.Montage->GetPlayLength() + .1f;
    Tuning.WarningMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
    auto CheckHost = [&](ACharacter* Character, UAbilitySystemComponent* ASC, UGameplayAbility* Ability,
                         FGameplayAbilitySpecHandle Handle, const TCHAR* Host)
    {
        const auto CanActivate = [&]()
        {
            return Ability->CanActivateAbility(Handle, ASC->AbilityActorInfo.Get());
        };
        if (!TestTrue(FString::Printf(TEXT("%s native host initially available"), Host), CanActivate()))
        {
            return;
        }
        FAbilityNodeInput Input;
        FString Error;
        if (!TestTrue(FString::Printf(TEXT("%s current source captured"), Host),
                      AbilityExecutionIdentity::CaptureSource(Character, Input.SourceToken, Error)))
        {
            return;
        }
        auto* Config = NewObject<UAbilityCharacterMoveNodeConfig>(F.World);
        Config->Distance = 0.f;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        TestTrue(
            FString::Printf(TEXT("%s movement graph starts"), Host),
            Run->StartWithInput(EnemyMovementGraph(F.World, TEXT("Movement.Character"), Config), Input, {}, Error));
        TestTrue(FString::Printf(TEXT("%s has current movement claim"), Host),
                 UAbilityMovementOwnershipComponent::HasActiveClaim(Character));
        TestFalse(FString::Printf(TEXT("%s native host rejects graph movement ownership"), Host), CanActivate());
        Run->Cancel();
        TestFalse(FString::Printf(TEXT("%s releases current claim"), Host),
                  UAbilityMovementOwnershipComponent::HasActiveClaim(Character));
        TestTrue(FString::Printf(TEXT("%s native host available after release"), Host), CanActivate());
        Character->GetCharacterMovement()->DisableMovement();
        TestTrue(FString::Printf(TEXT("%s retains prior mode-independent activation policy"), Host), CanActivate());
    };
    CheckHost(Sword, SwordASC, SwordAbility, SwordHandle, TEXT("SwordShield"));
    CheckHost(Enemy, EnemyASC, EnemyAbility, EnemyHandle, TEXT("TelegraphedEnemy"));
    return true;
}
#endif
