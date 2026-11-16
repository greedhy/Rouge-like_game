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
#include "EngineUtils.h"

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
    for (const bool bSequential : {false, true})
    {
        const TCHAR* ClassPath = bSequential
            ? TEXT("/Game/CompositionBatch6/GA_SequentialChain.GA_SequentialChain_C")
            : TEXT("/Game/CompositionBatch6/GA_FissionChain.GA_FissionChain_C");
        auto* AbilityClass = LoadClass<UBatch6ChainAbility>(nullptr, ClassPath);
        if (!TestNotNull(TEXT("Production lightning asset loads"), AbilityClass))
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
        const int32 Secondary = Mass->SpawnMonster(Type, FVector(300, 0, 0));
        const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1));
        auto* Ability = Cast<UBatch6ChainAbility>(F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        if (!TestNotNull(TEXT("Production per-actor ability instance"), Ability))
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
        if (bSequential)
        {
            TestTrue(TEXT("Production ability keeps its graph task active between hops"), Ability->IsActive());
            F.ASC->TickComponent(.3f, LEVELTICK_All, &F.ASC->PrimaryComponentTick);
            TestTrue(TEXT("No damage from the previous frame's delta"), Mass->Resolve(Secondary, View));
            TestEqual(TEXT("Activation frame's elapsed world time is zero"), View.Health, 10000.f);
            F.World->Tick(LEVELTICK_TimeOnly, .09f);
            F.ASC->TickComponent(.09f, LEVELTICK_All, &F.ASC->PrimaryComponentTick);
            TestTrue(TEXT("Production secondary survives the wait"), Mass->Resolve(Secondary, View));
            TestEqual(TEXT("Production secondary has no early damage"), View.Health, 10000.f);
            F.World->Tick(LEVELTICK_TimeOnly, .011f);
            F.ASC->TickComponent(.011f, LEVELTICK_All, &F.ASC->PrimaryComponentTick);
            TestTrue(TEXT("Production secondary remains after the hop"), Mass->Resolve(Secondary, View));
            TestTrue(TEXT("Production graph task applies delayed damage"),
                     FMath::IsNearlyEqual(View.Health, 10000.f - Ability->Prototype->DamageAtLevel(1)));
            F.World->Tick(LEVELTICK_TimeOnly, .101f);
            F.ASC->TickComponent(.101f, LEVELTICK_All, &F.ASC->PrimaryComponentTick);
        }
        else
        {
            TestTrue(TEXT("Production fork hits the secondary immediately"), Mass->Resolve(Secondary, View));
            TestTrue(TEXT("Production fork uses its existing damage falloff"),
                     FMath::IsNearlyEqual(View.Health,
                         10000.f - Ability->Prototype->DamageAtLevel(1) * Ability->Prototype->DamageFalloff));
        }
        TestFalse(TEXT("Production ability ends when its graph is finished"), Ability->IsActive());
        F.ASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), FPredictionKey());
        TestTrue(TEXT("Duplicate target request cannot double commit"),
                 FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), ExpectedMana));
        F.ASC->ClearAbility(Handle);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6LightningVariants, "test_25d.UnifiedGA.Batch6.LightningVariants",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBatch6LightningVariants::RunTest(const FString&)
{
    for (const bool bSequential : {false, true})
    {
        FAbilityGraphTestFixture F;
        F.Target->Destroy();
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = 100.f;
        Type->XPReward = 0;
        const FVector FirstPosition(150, 0, 0);
        const FVector SecondPosition(300, 0, 0);
        const FVector ThirdPosition(450, 0, 0);
        const int32 First = Mass->SpawnMonster(Type, FirstPosition);
        const int32 Second = Mass->SpawnMonster(Type, SecondPosition);
        const int32 Third = Mass->SpawnMonster(Type, ThirdPosition);
        const int32 Fourth = Mass->SpawnMonster(Type, FVector(600, 0, 0));

        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        Input.Origin = F.Source->GetActorLocation();
        Input.DamageParams.BaseDamage = 25.f;
        Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Magic;
        FAbilityTargetToken Target;
        FString Error;
        if (!TestTrue(TEXT("Initial target captured"),
                      AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(First),
                                                              Target, Error)))
        {
            return false;
        }
        Input.LiveTargetToken = Target;
        UAbilityLightningNodeConfig* Config = nullptr;
        if (bSequential)
        {
            Config = NewObject<UAbilitySequentialChainNodeConfig>(F.World);
        }
        else
        {
            Config = NewObject<UAbilityForkedVolleyNodeConfig>(F.World);
            Config->DamageFalloff = .8f;
        }
        Config->MaxTargets = 4;
        Config->JumpRadius = 200.f;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        const FName Strategy = bSequential ? TEXT("Delivery.SequentialChain") : TEXT("Delivery.ForkedVolley");
        if (!TestTrue(TEXT("Registered lightning node starts"),
                      Run->StartWithInput(Single(F.World, Strategy, Config), Input, {}, Error)))
        {
            AddError(Error);
            return false;
        }
        const auto Health = [Mass](int32 Id)
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : -1.f;
        };
        const auto Segments = [&F]()
        {
            TArray<FBatch6ChainSegment> Result;
            for (TActorIterator<ABatch6ChainPresentation> It(F.World); It; ++It)
            {
                Result.Append(It->Segments);
            }
            return Result;
        };
        TestEqual(TEXT("Primary target hit on activation"), Health(First), 75.f);
        if (!bSequential)
        {
            TestEqual(TEXT("Secondary target hit in the same update with one falloff"), Health(Second), 80.f);
            TestEqual(TEXT("No expansion outside the first target's neighborhood"), Health(Third), 100.f);
            const auto Beams = Segments();
            TestEqual(TEXT("Single cast presents both beams"), Beams.Num(), 2);
            for (const auto& Beam : Beams)
            {
                TestEqual(TEXT("Fork beams start at the caster"), Beam.Start, Input.Origin);
            }
            TestTrue(TEXT("Volley completes immediately"), Run->IsFinished());
            continue;
        }
        TestEqual(TEXT("No early damage to second target"), Health(Second), 100.f);
        Run->Advance(.09f);
        TestEqual(TEXT("Second target is untouched at 90ms"), Health(Second), 100.f);
        Run->Advance(.011f);
        TestEqual(TEXT("Second target hit after 100ms"), Health(Second), 75.f);
        TestEqual(TEXT("Third target still untouched"), Health(Third), 100.f);
        Run->Advance(1.f);
        TestEqual(TEXT("Third target hit after another interval"), Health(Third), 75.f);
        TestEqual(TEXT("Long tick cannot play multiple hops"), Health(Fourth), 100.f);
        TestEqual(TEXT("First target is never hit again"), Health(First), 75.f);
        const auto Beams = Segments();
        TestEqual(TEXT("Three separate beam segments"), Beams.Num(), 3);
        TestTrue(TEXT("Last beam starts at the previous impact"), Beams.ContainsByPredicate(
            [&](const FBatch6ChainSegment& Beam)
            {
                return Beam.Start.Equals(SecondPosition) && Beam.End.Equals(ThirdPosition);
            }));
        Run->Cancel();
        Run->Advance(1.f);
        TestEqual(TEXT("Cancellation prevents pending fourth hop"), Health(Fourth), 100.f);
    }
    return true;
}
#endif
