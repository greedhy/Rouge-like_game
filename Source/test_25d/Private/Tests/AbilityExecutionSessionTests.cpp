// 调用链：Automation -> 与原生GA相同的执行会话 -> 注册动作/提交/取消重入；不创建第二套ASC或伤害服务。
#include "AbilitySystem/Unified/AbilityExecutionSession.h"
#include "Misc/AutomationTest.h"
#include "AbilityExecutionSessionTestAbility.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UAbilityExecutionSessionTestAbility::UAbilityExecutionSessionTestAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UAbilityExecutionSessionTestAbility::CommitExecute(FGameplayAbilitySpecHandle Handle,
                                                        const FGameplayAbilityActorInfo* Info,
                                                        FGameplayAbilityActivationInfo Activation)
{
    ++CommitCount;
    Super::CommitExecute(Handle, Info, Activation);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExecutionSessionCommitLife, "test_25d.UnifiedGA.Entry.CommitRejectsChangedLife",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExecutionSessionCommitLife::RunTest(const FString&)
{
    bool bCurrent = true;
    int32 Charges = 0;
    FAbilityExecutionSession Before(
        [&]()
        {
            return bCurrent;
        });
    bCurrent = false;
    TestFalse(TEXT("Changed source cannot enter cost callback"), Before.TryCommit(
                                                                     [&]()
                                                                     {
                                                                         ++Charges;
                                                                         return true;
                                                                     }));
    TestEqual(TEXT("No charge to replacement life"), Charges, 0);
    TestFalse(TEXT("Changed source cannot emit input"), Before.PublishSignal(TEXT("InputPressed")));
    bCurrent = true;
    FAbilityExecutionSession During(
        [&]()
        {
            return bCurrent;
        });
    TestFalse(TEXT("Commit callback changing life rejects continuation"), During.TryCommit(
                                                                              [&]()
                                                                              {
                                                                                  ++Charges;
                                                                                  bCurrent = false;
                                                                                  return true;
                                                                              }));
    TestEqual(TEXT("Original callback occurred only once"), Charges, 1);
    TestFalse(TEXT("Reentrant life change closes activation"), During.IsOpen());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExecutionSessionRealGA, "test_25d.UnifiedGA.Entry.RealGasCommitPassiveReactivation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecutionSessionRealGA::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor = World->SpawnActor<AEnemyCharacter>();
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(Actor->GetAbilitySystemComponent());
    ASC->InitAbilityActorInfo(Actor, Actor);
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityExecutionSessionTestAbility::StaticClass(), 1));
    auto* Ability =
        CastChecked<UAbilityExecutionSessionTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    TestTrue(TEXT("Real GAS activation"), ASC->TryActivateAbility(Handle));
    const auto First = Ability->GetExecutionSession();
    if (TestTrue(TEXT("PreActivate creates the common session"), First.IsValid()))
    {
        TestTrue(TEXT("Passive subscribed exactly to this instance"),
                 ASC->DeactivatePassiveAbility.IsBoundToObject(Ability));
        TestTrue(TEXT("First complete commit"),
                 Ability->CommitAbility(Handle, Ability->GetCurrentActorInfo(), Ability->GetCurrentActivationInfo()));
        TestTrue(TEXT("Duplicate complete commit is idempotent"),
                 Ability->CommitAbility(Handle, Ability->GetCurrentActorInfo(), Ability->GetCurrentActivationInfo()));
        TestEqual(TEXT("Actual GAS CommitExecute once"), Ability->CommitCount, 1);
        ASC->CancelAbilityHandle(Handle);
        TestFalse(TEXT("Cancelled session is closed"), First->IsOpen());
        TestFalse(TEXT("Passive delegate removed while GA object stays alive"),
                  ASC->DeactivatePassiveAbility.IsBoundToObject(Ability));
        TestTrue(TEXT("Same GA instance activates again"), ASC->TryActivateAbility(Handle));
        const auto Second = Ability->GetExecutionSession();
        TestTrue(TEXT("Second cast owns a different session"), Second && Second->GetId() != First->GetId());
        TestFalse(TEXT("Old input cannot enter next activation"), First->PublishSignal(TEXT("InputReleased")));
        TestTrue(TEXT("New commit is allowed"),
                 Ability->CommitAbility(Handle, Ability->GetCurrentActorInfo(), Ability->GetCurrentActivationInfo()));
        TestEqual(TEXT("One commit per cast"), Ability->CommitCount, 2);
        if (Second)
        {
            int32 Actions = 0;
            Second->RegisterAction(TEXT("OwnedAction"),
                                   [&](int32)
                                   {
                                       ++Actions;
                                   });
            TestTrue(TEXT("Original source can execute a registered action"),
                     Second->ExecuteAction(TEXT("OwnedAction"), 0));
            auto* Replacement = World->SpawnActor<AEnemyCharacter>();
            ASC->InitAbilityActorInfo(Actor, Replacement);
            TestFalse(TEXT("Replacing ASC Avatar rejects the old cast"), Second->ExecuteAction(TEXT("OwnedAction"), 1));
            TestEqual(TEXT("Replacement never receives old actions"), Actions, 1);
            TestFalse(TEXT("Source invalidation also removes passive listener"),
                      ASC->DeactivatePassiveAbility.IsBoundToObject(Ability));
            ASC->InitAbilityActorInfo(Actor, Actor);
        }
        ASC->CancelAbilityHandle(Handle);
        TestFalse(TEXT("Second passive listener also removed"), ASC->DeactivatePassiveAbility.IsBoundToObject(Ability));
    }
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExecutionSessionDispatch, "test_25d.UnifiedGA.Entry.ActionDispatchAndIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecutionSessionDispatch::RunTest(const FString&)
{
    FAbilityExecutionSession First;
    FAbilityExecutionSession Second;
    int32 Calls = 0;
    First.RegisterAction(TEXT("Melee"),
                         [&](int32 Sequence)
                         {
                             Calls += Sequence;
                         });
    Second.RegisterAction(TEXT("Melee"),
                          [&](int32 Sequence)
                          {
                              Calls += Sequence;
                          });
    TestFalse(TEXT("Unknown actions fail closed"), First.ExecuteAction(TEXT("Missing"), 1));
    TestTrue(TEXT("Registered action executes"), First.ExecuteAction(TEXT("Melee"), 1));
    TestFalse(TEXT("Duplicate notify does not execute"), First.ExecuteAction(TEXT("Melee"), 1));
    TestTrue(TEXT("Independent stage executes"), First.ExecuteAction(TEXT("Melee"), 2));
    TestTrue(TEXT("Second cast has independent deduplication"), Second.ExecuteAction(TEXT("Melee"), 1));
    TestEqual(TEXT("Actual callback results"), Calls, 4);
    TestTrue(TEXT("Cast identity differs"), First.GetId() != Second.GetId());
    First.Close(true);
    TestFalse(TEXT("Cancelled action rejected"), First.ExecuteAction(TEXT("Melee"), 3));
    bool bOriginalLife = true;
    FAbilityExecutionSession Life(
        [&]()
        {
            return bOriginalLife;
        });
    Life.RegisterAction(TEXT("Melee"),
                        [&](int32)
                        {
                            ++Calls;
                        });
    TestTrue(TEXT("Original life can execute"), Life.ExecuteAction(TEXT("Melee"), 0));
    bOriginalLife = false;
    TestFalse(TEXT("Changed life cannot execute an unseen action"), Life.ExecuteAction(TEXT("Melee"), 1));
    TestFalse(TEXT("Changed life closes the cast"), Life.IsOpen());
    TestEqual(TEXT("No action leaks into next life"), Calls, 5);
    FAbilityExecutionSession Stream;
    int32 StreamCalls = 0;
    Stream.RegisterAction(TEXT("Period"),
                          [&](int32)
                          {
                              ++StreamCalls;
                          });
    for (int32 Sequence = 0; Sequence < 1000; ++Sequence)
    {
        TestTrue(TEXT("Bounded sequence state supports sustained actions"),
                 Stream.ExecuteAction(TEXT("Period"), Sequence));
    }
    TestFalse(TEXT("Late sequence cannot replay after sustained actions"), Stream.ExecuteAction(TEXT("Period"), 0));
    TestEqual(TEXT("No artificial cumulative lifetime limit"), StreamCalls, 1000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExecutionSessionReentrancy, "test_25d.UnifiedGA.Entry.CommitAndCleanupReentrancy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecutionSessionReentrancy::RunTest(const FString&)
{
    FAbilityExecutionSession Session;
    int32 Commits = 0;
    int32 Cleanups = 0;
    int32 Cancelled = 0;
    int32 Completed = 0;
    auto Commit = [&]()
    {
        ++Commits;
        return true;
    };
    TestTrue(TEXT("First commit accepted"), Session.TryCommit(Commit));
    TestTrue(TEXT("Repeated commit returns previous success"), Session.TryCommit(Commit));
    TestEqual(TEXT("GAS commit executed exactly once"), Commits, 1);
    Session.AddCleanup(
        [&]()
        {
            ++Cleanups;
            TestFalse(TEXT("Input closes before cleanup"), Session.PublishSignal(TEXT("InputPressed")));
            Session.Close(false);
        });
    Session.OnSignal.AddLambda(
        [&](FName Name, FGameplayTag)
        {
            Cancelled += Name == TEXT("Cancelled");
            Completed += Name == TEXT("Completed");
            if (Name == TEXT("Cancelled"))
            {
                TestEqual(TEXT("Resources gone before terminal observers"), Cleanups, 1);
                Session.Close(false);
            }
        });
    Session.RegisterAction(TEXT("Cancel"),
                           [&](int32)
                           {
                               Session.Close(true);
                           });
    TestTrue(TEXT("Action may synchronously cancel its own session"), Session.ExecuteAction(TEXT("Cancel"), 0));
    Session.Close(false);
    TestEqual(TEXT("Cleanup exactly once"), Cleanups, 1);
    TestEqual(TEXT("Cancelled exactly once"), Cancelled, 1);
    TestEqual(TEXT("Cancellation never emits completed"), Completed, 0);
    TestFalse(TEXT("No commit after close"), Session.TryCommit(Commit));
    Session.AddCleanup(
        [&]()
        {
            ++Cleanups;
        });
    TestEqual(TEXT("Late resource registration cleans immediately"), Cleanups, 2);
    FAbilityExecutionSession Failed;
    TestFalse(TEXT("Failed commit stays failed"), Failed.TryCommit(
                                                      []()
                                                      {
                                                          return false;
                                                      }));
    TestFalse(TEXT("Failed transaction cannot retry through a different path"), Failed.TryCommit(Commit));
    TestEqual(TEXT("No fallback commit"), Commits, 1);
    return true;
}
#endif
