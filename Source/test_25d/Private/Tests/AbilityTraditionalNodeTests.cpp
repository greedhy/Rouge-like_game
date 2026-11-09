// 调用链：Automation真实ASC/native前端或统一图 -> 原投射物/Pawn -> 参数、计数、取消、来源生命断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityTraditionalTestAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"
#include "Player/StanderWeaponActionConfig.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

namespace
{
    UAbilityGraphDefinition* TraditionalGraph(UWorld* World, FName Type, UAbilityNodeConfig* Config)
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
    TArray<AMyProjectile*> Projectiles(UWorld* World)
    {
        TArray<AMyProjectile*> Result;
        for (TActorIterator<AMyProjectile> It(World); It; ++It)
        {
            Result.Add(*It);
        }
        return Result;
    }
    int32 MinionCount(AActor* Actor)
    {
        return ICombatInterface::Execute_GetMinionCount(Actor);
    }
    TArray<APawn*> SummonedPawns(UWorld* World)
    {
        TArray<APawn*> Result;
        for (TActorIterator<APawn> It(World); It; ++It)
        {
            if (It->FindComponentByClass<UAbilitySummonedPawnLease>())
            {
                Result.Add(*It);
            }
        }
        return Result;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTraditionalProjectileActions,
                                 "test_25d.UnifiedGA.Traditional.NativeAndGraphProjectileParity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTraditionalProjectileActions::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UAbilityTraditionalFireBoltTest::StaticClass(), 3));
    TestTrue(TEXT("Original native projectile frontend activates"), F.ASC->TryActivateAbility(Handle));
    auto* Ability =
        Cast<UAbilityTraditionalFireBoltTest>(F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    if (!TestNotNull(TEXT("Native ability instance"), Ability))
    {
        return false;
    }
    Ability->SpawnProjectiles(F.Target->GetActorLocation(), {}, false, 0.f, F.Target);
    const auto Native = Projectiles(F.World);
    TestEqual(TEXT("Native count remains Min(NumProjectiles, Level)"), Native.Num(), 3);
    for (auto* Projectile : Native)
    {
        TestEqual(TEXT("Actor owner is original source"), Projectile->GetOwner(), static_cast<AActor*>(F.Source));
        TestEqual(TEXT("Original instigator retained"), Projectile->GetInstigator(), static_cast<APawn*>(F.Source));
        TestEqual(TEXT("Original damage defaults retained"), Projectile->DamageEffectParams.BaseDamage, 42.f);
        TestEqual(TEXT("Level retained"), Projectile->DamageEffectParams.AbilityLevel, 3.f);
        TestEqual(TEXT("Homing follows selected actor root"),
                  Projectile->ProjectileMovement->HomingTargetComponent.Get(), F.Target->GetRootComponent());
        TestEqual(TEXT("Homing acceleration retained"), Projectile->ProjectileMovement->HomingAccelerationMagnitude,
                  2300.f);
        TestTrue(TEXT("Original projectile replication retained"), Projectile->GetIsReplicated());
        TestTrue(TEXT("Captured source accepts enemy overlap"), Projectile->IsValidOverlap(F.Target));
    }
    auto* Config = NewObject<UAbilityActorProjectileNodeConfig>(F.World);
    Config->Settings.ProjectileClass = AMyProjectile::StaticClass();
    Config->Settings.Count = 5;
    Config->Settings.bLimitCountToLevel = true;
    Config->Settings.Spread = 90.f;
    Config->Settings.bConfigureHoming = true;
    Config->Settings.HomingAccelerationMin = Config->Settings.HomingAccelerationMax = 2300.f;
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.LiveTargetToken = F.TargetToken;
    Input.Level = 3.f;
    Input.Origin = FVector::ZeroVector;
    Input.EventCenter = F.Target->GetActorLocation();
    Input.DamageParams = Ability->MakeDamageEffectParamsFromClassDefaults();
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(
        TEXT("Registered projectile action starts"),
        Run->StartWithInput(TraditionalGraph(F.World, TEXT("Delivery.ActorProjectiles"), Config), Input, {}, Error));
    TestTrue(TEXT("Spawn action completes while projectiles remain independent"), Run->IsFinished());
    auto All = Projectiles(F.World);
    TestEqual(TEXT("Graph emits identical level-scaled count"), All.Num(), 6);
    TArray<float> NativeYaw;
    TArray<float> GraphYaw;
    for (auto* Projectile : All)
    {
        (Native.Contains(Projectile) ? NativeYaw : GraphYaw).Add(Projectile->GetActorRotation().Yaw);
    }
    NativeYaw.Sort();
    GraphYaw.Sort();
    TestTrue(TEXT("Native and graph launch spread match"), NativeYaw == GraphYaw);
    Ability->SingleShot(FVector(100, 0, 100), true, 70.f);
    All = Projectiles(F.World);
    TestEqual(TEXT("Single shot remains one actor"), All.Num(), 7);
    F.ASC->CancelAbilityHandle(Handle);
    for (auto* Projectile : All)
    {
        TestTrue(TEXT("Normal ability end preserves valid projectile flight"), Projectile->IsValidOverlap(F.Target));
    }
    F.ASC->InitAbilityActorInfo(F.Source, F.Target);
    for (auto* Projectile : All)
    {
        TestFalse(TEXT("ASC rebind rejects old projectile damage"), Projectile->IsValidOverlap(F.Target));
    }
    Ability->SpawnProjectiles(FVector(500, 0, 0), {}, false, 0.f, F.Target);
    TestEqual(TEXT("Inactive frontend cannot create late projectiles"), Projectiles(F.World).Num(), 7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTraditionalSummonActions,
                                 "test_25d.UnifiedGA.Traditional.SummonCadenceOwnershipAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTraditionalSummonActions::RunTest(const FString&)
{
    // 临时世界不BeginPlay；Actor::ProcessEvent否则跳过BlueprintNativeEvent计数接口。
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* Config = NewObject<UAbilitySummonNodeConfig>(F.World);
    Config->MinionClasses.Add(ACharacter::StaticClass());
    Config->Count = 3;
    Config->PreparedLocations = {FVector(100, 0, 0), FVector(200, 0, 0), FVector(300, 0, 0)};
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = F.Source->GetActorLocation();
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Registered summon action starts"),
             Run->StartWithInput(TraditionalGraph(F.World, TEXT("Spawn.Summon"), Config), Input, {}, Error));
    TestEqual(TEXT("First summon occurs at notify time"), MinionCount(F.Source), 1);
    TestEqual(TEXT("Native backing count changed too"), F.Source->GetMinionCount_Implementation(), 1);
    Run->Advance(.1f);
    TestEqual(TEXT("No early second summon"), MinionCount(F.Source), 1);
    Run->Advance(.1f);
    TestEqual(TEXT("One summon per original 0.2 second interval"), MinionCount(F.Source), 2);
    auto Pawns = SummonedPawns(F.World);
    TestEqual(TEXT("Both real Pawns exist"), Pawns.Num(), 2);
    for (auto* Pawn : Pawns)
    {
        TestEqual(TEXT("Original spawn height retained"), Pawn->GetActorLocation().Z, 60.0);
        TestNull(TEXT("Original null instigator retained"), Pawn->GetInstigator());
    }
    Run->Cancel();
    Run->Advance(1.f);
    TestEqual(TEXT("Cancellation prevents remaining spawn"), MinionCount(F.Source), 2);
    TestEqual(TEXT("Spawned minions survive normal action cancellation"), SummonedPawns(F.World).Num(), 2);
    if (Pawns.Num() == 2)
    {
        Pawns[0]->Destroy();
        TestEqual(TEXT("Original source count returns on pawn destruction"), MinionCount(F.Source), 1);
        F.ASC->InitAbilityActorInfo(F.Source, F.Target);
        Pawns[1]->Destroy();
        TestEqual(TEXT("Old pawn callback does not alter rebound source life"), MinionCount(F.Source), 1);
    }
    TestNull(TEXT("Stale source cannot spawn another pawn"),
             AbilityTraditionalActions::SpawnSummonedPawn(F.SourceToken, APawn::StaticClass(), FTransform::Identity));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTraditionalSummonInputBounds,
                                 "test_25d.UnifiedGA.Traditional.SummonSingleAndInvalidInputs",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTraditionalSummonInputBounds::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    const auto Locations = AbilityTraditionalActions::GatherSummonLocations(
        F.SourceToken, FVector::ZeroVector, FVector::ForwardVector, 1, 100.f, 100.f, 90.f);
    TestEqual(TEXT("Single summon returns one finite location"), Locations.Num(), 1);
    if (!Locations.IsEmpty())
    {
        TestTrue(TEXT("Single summon faces forward without zero division"), Locations[0].Equals(FVector(100, 0, 0)));
    }
    TestNull(TEXT("Empty minion list fails without random invalid index"),
             AbilityTraditionalActions::ChooseMinionClass({}).Get());
    auto* Config = NewObject<UAbilitySummonNodeConfig>(F.World);
    FString Error;
    TestFalse(TEXT("Unconfigured summon rejected before graph execution"), Config->Validate(Error));
    Config->MinionClasses.Add(ACharacter::StaticClass());
    TestTrue(TEXT("Original valid defaults accepted"), Config->Validate(Error));
    Config->Interval = -1.f;
    TestFalse(TEXT("Negative cadence rejected"), Config->Validate(Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTraditionalNativeSummonNotify,
                                 "test_25d.UnifiedGA.Traditional.NativeSummonNotifyAndCancel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTraditionalNativeSummonNotify::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* Config = LoadObject<UStanderWeaponActionConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_Attack1.DA_Attack1"));
    auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander"));
    if (!TestNotNull(TEXT("Existing compatible test montage config"), Config) ||
        !TestNotNull(TEXT("Existing skeletal mesh"), MeshAsset))
    {
        return false;
    }
    FAbilityGraphTestFixture F;
    F.Source->GetMesh()->SetSkeletalMeshAsset(MeshAsset);
    F.Source->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    F.Source->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
    F.ASC->RefreshAbilityActorInfo();
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UAbilityTraditionalSummonTest::StaticClass(), 1));
    auto* Ability = Cast<UAbilityTraditionalSummonTest>(F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    if (!TestNotNull(TEXT("Native summon instance"), Ability))
    {
        return false;
    }
    Ability->SummonMontage = Config->Montage;
    Ability->SummonGroundEffect = nullptr;
    Ability->MinionClasses = {ACharacter::StaticClass()};
    Ability->NumMinions = 3;
    Ability->MinSpawnDistance = Ability->MaxSpawnDistance = 100.f;
    TestTrue(TEXT("Production native activation starts montage frontend"), F.ASC->TryActivateAbility(Handle));
    TestTrue(TEXT("Host remains active waiting for real gameplay event"), Ability->IsActive());
    TestEqual(TEXT("No summon before notify"), MinionCount(F.Source), 0);
    const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("Montage.Attack.2"));
    FGameplayEventData Payload;
    Payload.EventTag = Tag;
    F.ASC->HandleGameplayEvent(Tag, &Payload);
    TestEqual(TEXT("Actual ASC event starts reusable summon graph"), MinionCount(F.Source), 1);
    F.ASC->HandleGameplayEvent(Tag, &Payload);
    Ability->Notify();
    TestEqual(TEXT("Duplicate event/manual replay cannot restart wave"), MinionCount(F.Source), 1);
    Ability->Step(.2f);
    TestEqual(TEXT("Production host advances original spawn cadence"), MinionCount(F.Source), 2);
    F.ASC->CancelAbilityHandle(Handle);
    Ability->Step(1.f);
    TestEqual(TEXT("Cancelled host cannot spawn remaining pawn"), MinionCount(F.Source), 2);
    TestFalse(TEXT("Native host releases all task/snapshot references"), Ability->HasSummonWork());
    TestFalse(TEXT("Cancelled native host stops owned montage"),
              F.Source->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Config->Montage));
    return true;
}
#endif
