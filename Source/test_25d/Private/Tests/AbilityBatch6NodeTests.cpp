// 调用链：Automation -> 注册的Batch6图策略 -> 真实Actor石化/Mass伤害；不激活旧GA作为节点。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "UObject/Script.h"
#include "Misc/AutomationTest.h"

namespace
{
    UAbilityGraphDefinition* Single(UObject* Outer, FName Type, UAbilityNodeConfig* Config)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Outer);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        Graph->EntryNodeId = Node.NodeId;
        return Graph;
    }
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6AreaNode, "test_25d.UnifiedGA.Batch6.AreaPetrifyRegisteredStrategy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBatch6AreaNode::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Config = NewObject<UAbilityAreaPetrifyNodeConfig>(F.World);
    Config->Radius = 200.f;
    Config->Duration = 2.f;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = F.Source->GetActorLocation();
    FString Error;
    auto* Graph = Single(F.World, TEXT("Status.PetrifyArea"), Config);
    TestTrue(TEXT("Reusable area strategy starts"), Run->StartWithInput(Graph, Input, {}, Error));
    Run->Advance(0.f);
    TestTrue(TEXT("Area applies actual status"), F.IsPetrified());
    TestTrue(TEXT("Instant graph completes"), Run->IsFinished());
    Run->Cancel();
    TestTrue(TEXT("Target state outlives graph"), F.IsPetrified());
    auto* Outside = F.World->SpawnActor<AEnemyCharacter>();
    Outside->Tags.AddUnique(TEXT("Enemy"));
    Outside->GetAbilitySystemComponent()->InitAbilityActorInfo(Outside, Outside);
    Outside->SetActorLocation(FVector(500, 0, 0));
    auto* Other = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Independent second invocation"), Other->StartWithInput(Graph, Input, {}, Error));
    Other->Advance(0.f);
    TestFalse(TEXT("Outside radius is not affected"), Outside->GetMesh()->bPauseAnims);
    TArray<AEnemyCharacter*> Crowd;
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 Index = 0; Index < 70; ++Index)
    {
        auto* Enemy = F.World->SpawnActor<AEnemyCharacter>(FVector(50 + Index, 50, 0), FRotator::ZeroRotator, Spawn);
        Enemy->Tags.AddUnique(TEXT("Enemy"));
        Enemy->GetAbilitySystemComponent()->InitAbilityActorInfo(Enemy, Enemy);
        Crowd.Add(Enemy);
    }
    auto* CrowdRun = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Area batch accepts more than per-graph invocation cap"),
             CrowdRun->StartWithInput(Graph, Input, {}, Error));
    CrowdRun->Advance(0.f);
    int32 Petrified = 0;
    for (const auto* Enemy : Crowd)
    {
        Petrified += Enemy->GetMesh()->bPauseAnims;
    }
    TestEqual(TEXT("All 70 original area targets receive status"), Petrified, 70);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6ForkNode, "test_25d.UnifiedGA.Batch6.ForkedChainDamageFalloffAndBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBatch6ForkNode::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 First = Mass->SpawnMonster(Type, FVector(150, 0, 0));
    const int32 Second = Mass->SpawnMonster(Type, FVector(300, 0, 0));
    const int32 Third = Mass->SpawnMonster(Type, FVector(450, 0, 0));
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = F.Source->GetActorLocation();
    Input.DamageParams.BaseDamage = 25.f;
    Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Magic;
    FAbilityTargetToken Target;
    FString Error;
    TestTrue(TEXT("Actual first Mass life captured"),
             AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(First), Target, Error));
    Input.LiveTargetToken = Target;
    auto* Config = NewObject<UAbilityForkedChainNodeConfig>(F.World);
    Config->MaxTargets = 2;
    Config->MaxDepth = 1;
    Config->BranchesPerTarget = 1;
    Config->JumpRadius = 200.f;
    Config->DamageFalloff = .5f;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Results = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("DamageResolved"))
            {
                ++Results;
                TestTrue(TEXT("Mass damage has actual result"), Event.Input.ActualDamage.IsSet());
            }
        });
    TestTrue(TEXT("Registered fork starts"),
             Run->StartWithInput(Single(F.World, TEXT("Delivery.ForkedChain"), Config), Input, {}, Error));
    Run->Advance(0.f);
    FPureMassView View;
    TestTrue(TEXT("First survives"), Mass->Resolve(First, View));
    TestEqual(TEXT("Root damage"), View.Health, 75.f);
    TestTrue(TEXT("Second survives"), Mass->Resolve(Second, View));
    TestEqual(TEXT("Depth falloff"), View.Health, 87.5f);
    TestTrue(TEXT("Third survives"), Mass->Resolve(Third, View));
    TestEqual(TEXT("Shared target/depth budget stops third"), View.Health, 100.f);
    TestEqual(TEXT("Exactly two actual damage events"), Results, 2);
    TestTrue(TEXT("Synchronous graph completes"), Run->IsFinished());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6ProductionChain, "test_25d.UnifiedGA.Batch6.ProductionChainTargetDataCommit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBatch6ProductionChain::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* AbilityClass =
        LoadClass<UBatch6ChainAbility>(nullptr, TEXT("/Game/CompositionBatch6/GA_FissionChain.GA_FissionChain_C"));
    if (!TestNotNull(TEXT("Original chain asset loads"), AbilityClass))
    {
        return false;
    }
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
    F.ASC->AbilityActorInfo->PlayerController = F.World->SpawnActor<AAbilityGraphRemoteTestController>();
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 10000.f;
    Type->XPReward = 0;
    const int32 TargetId = Mass->SpawnMonster(Type, FVector(150, 0, 0));
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1));
    auto* Ability = Cast<UBatch6ChainAbility>(F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    if (!TestNotNull(TEXT("Original per-actor ability instance"), Ability))
    {
        return false;
    }
    FPredictionKey RequestKey;
    RequestKey.Current = 101;
    F.ASC->CallServerTryActivateAbility(Handle, false, RequestKey);
    TestTrue(TEXT("Target wait is active"), Ability->IsActive());
    auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
    Hit->HitResult.bBlockingHit = true;
    Hit->HitResult.ImpactPoint = FVector(150, 0, 0);
    FGameplayAbilityTargetDataHandle Data;
    Data.Add(Hit);
    const FPredictionKey Key = Ability->GetCurrentActivationInfo().GetActivationPredictionKey();
    F.ASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), FPredictionKey());
    FPureMassView View;
    TestTrue(TEXT("Actual Mass target remains"), Mass->Resolve(TargetId, View));
    TestTrue(TEXT("Original asset deals configured root damage"),
             FMath::IsNearlyEqual(View.Health, 10000.f - Ability->Prototype->DamageAtLevel(1)));
    const float ExpectedMana = 1000.f - Ability->Prototype->ManaCost;
    TestTrue(TEXT("Cost committed once"),
             FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), ExpectedMana));
    TestFalse(TEXT("Instant chain ends through unified graph"), Ability->IsActive());
    F.ASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), FPredictionKey());
    TestTrue(TEXT("Duplicate target request cannot double commit"),
             FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), ExpectedMana));
    F.ASC->ClearAbility(Handle);
    return true;
}
#endif
