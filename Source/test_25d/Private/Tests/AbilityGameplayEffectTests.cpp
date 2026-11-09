// 测试链：真实ASC事件 -> 原生监听/共享GE -> Health；图拥有GE -> Cancel/重入取消 -> Armor与句柄清理。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGameplayEffectTestTypes.h"
#include "AbilitySystem/Ability/MyAttributeEventAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"

namespace
{
    UAbilityGraphDefinition* EffectGraph(FAbilityGraphTestFixture& F, bool bOwned)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = bOwned ? TEXT("Status.OwnedEffect") : TEXT("Effect.GameplayEffect");
        auto* Settings = bOwned ? NewObject<UAbilityOwnedEffectNodeConfig>(Graph)
                                : NewObject<UAbilityGameplayEffectNodeConfig>(Graph);
        Settings->EffectClass =
            bOwned ? UUtilityOwnedTestEffect::StaticClass() : UUtilityInstantTestEffect::StaticClass();
        Settings->SetByCallerMagnitudes.Add(FGameplayTag::RequestGameplayTag(TEXT("Tests.UnifiedGA.Utility.Value")),
                                            5.f);
        Node.Config = Settings;
        Graph->Nodes.Add(Node);
        Graph->EntryNodeId = Node.NodeId;
        return Graph;
    }
    FAbilityNodeInput EffectInput(FAbilityGraphTestFixture& F)
    {
        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        return Input;
    }

    /** 真查询先捕获目标生命；可选 Delay 用于验证目标退休后的异步应用边界。 */
    UAbilityGraphDefinition* QueryEffectGraph(FAbilityGraphTestFixture& F, bool bOwned, bool bDelay)
    {
        auto* Graph = EffectGraph(F, bOwned);
        auto& Effect = Graph->Nodes[0];
        Effect.TypeId = bOwned ? TEXT("Status.OwnedEffectTarget") : TEXT("Effect.GameplayEffectTarget");
        CastChecked<UAbilityGameplayEffectNodeConfig>(Effect.Config)->bApplyToLiveActor = true;
        const auto EffectId = Effect.NodeId;
        FAbilityGraphNode Query;
        Query.NodeId = FGuid::NewGuid();
        Query.TypeId = TEXT("Query.Shape");
        auto* Settings = NewObject<UAbilityQueryNodeConfig>(Graph);
        Settings->Query.Radius = 300.f;
        Settings->Query.bIncludeMass = false;
        Settings->Query.bRequireVisibility = false;
        Query.Config = Settings;
        Graph->EntryNodeId = Query.NodeId;
        Graph->Nodes.Add(Query);
        FAbilityGraphEdge Edge;
        Edge.From = Query.NodeId;
        Edge.To = EffectId;
        Edge.OutputPort = TEXT("TargetFound");
        if (bDelay)
        {
            FAbilityGraphNode Delay;
            Delay.NodeId = FGuid::NewGuid();
            Delay.TypeId = TEXT("Control.Delay");
            auto* DelaySettings = NewObject<UAbilityDelayNodeConfig>(Graph);
            DelaySettings->DelaySeconds = .25f;
            Delay.Config = DelaySettings;
            Graph->Nodes.Add(Delay);
            Edge.To = Delay.NodeId;
            FAbilityGraphEdge Apply;
            Apply.From = Delay.NodeId;
            Apply.To = EffectId;
            Apply.OutputPort = TEXT("Completed");
            Graph->Edges.Add(Apply);
        }
        Graph->Edges.Add(Edge);
        return Graph;
    }

    void PrepareQueryTarget(FAbilityGraphTestFixture& F)
    {
        F.Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        F.Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
        auto* ASC = F.Target->GetAbilitySystemComponent();
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetArmorAttribute(), 0.f);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityAttributeEvents, "test_25d.UnifiedGA.Utility.AttributeEventsApplyAndUnbind",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityAttributeEvents::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10.f);
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UMyAttributeEventAbility::StaticClass()));
    auto* Spec = F.ASC->FindAbilitySpecFromHandle(Handle);
    auto* Ability = Spec ? Cast<UMyAttributeEventAbility>(Spec->GetPrimaryInstance()) : nullptr;
    if (!TestNotNull(TEXT("Instanced listener exists"), Ability))
    {
        return false;
    }
    const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("Tests.UnifiedGA.Utility.Value"));
    Ability->ListenTag = Tag;
    Ability->bMatchEventExactly = true;
    Ability->bInitializeListedTags = false;
    Ability->EventEffectClass = UUtilityInstantTestEffect::StaticClass();
    Ability->EventEffectLevel = 0.f;
    float ObservedLevel = -1.f;
    const auto Applied = F.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec& AppliedSpec, FActiveGameplayEffectHandle)
        {
            ObservedLevel = AppliedSpec.GetLevel();
        });
    TestTrue(TEXT("Native listener activates"), F.ASC->TryActivateAbility(Handle));
    FGameplayEventData Event;
    Event.EventTag = Tag;
    Event.EventMagnitude = 5.f;
    F.ASC->HandleGameplayEvent(Tag, &Event);
    TestEqual(TEXT("One event changes real Health once"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 15.f);
    TestEqual(TEXT("Legacy Listen_New level zero is retained"), ObservedLevel, 0.f);
    F.ASC->HandleGameplayEvent(Tag, &Event);
    TestEqual(TEXT("Repeated identical GameplayEvent remains a second independent effect"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 20.f);
    F.ASC->CancelAbilityHandle(Handle);
    F.ASC->HandleGameplayEvent(Tag, &Event);
    TestEqual(TEXT("Ended listener leaves no future effect callback"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 20.f);
    Ability->ListenTag = FGameplayTag::RequestGameplayTag(TEXT("Tests.UnifiedGA.Utility"));
    Ability->bMatchEventExactly = false;
    Ability->bInitializeListedTags = true;
    Ability->InitializedEventTags = {Tag};
    Ability->EventEffectLevel = 1.f;
    TestTrue(TEXT("Original listed-tag listener can reactivate"), F.ASC->TryActivateAbility(Handle));
    Event.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Tests.UnifiedGA.Utility.Other"));
    F.ASC->HandleGameplayEvent(Event.EventTag, &Event);
    TestEqual(TEXT("Unmatched configured attribute receives exactly zero"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 20.f);
    Event.EventTag = Tag;
    F.ASC->HandleGameplayEvent(Tag, &Event);
    TestEqual(TEXT("Matched listed attribute receives its event magnitude"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 25.f);
    TestEqual(TEXT("Original listed listener level one retained"), ObservedLevel, 1.f);
    F.ASC->CancelAbilityHandle(Handle);
    F.ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Applied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityRegisteredEffect, "test_25d.UnifiedGA.Utility.RegisteredEffectChangesHealth",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityRegisteredEffect::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10.f);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Same actual GE action is registry composable"),
             Run->StartWithInput(EffectGraph(F, false), EffectInput(F), {}, Error));
    TestEqual(TEXT("Registry action uses actual SetByCaller effect"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 15.f);
    TestTrue(TEXT("One-shot effect graph completes"), Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityOwnedEffectCleanup, "test_25d.UnifiedGA.Utility.OwnedEffectCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityOwnedEffectCleanup::RunTest(const FString&)
{
    for (const bool bReentrant : {false, true})
    {
        FAbilityGraphTestFixture F;
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetArmorAttribute(), 0.f);
        auto* Outside = NewObject<UAbilityGameplayEffectNodeConfig>(F.World);
        Outside->EffectClass = UUtilityOwnedTestEffect::StaticClass();
        auto Existing = AbilityGameplayEffectActions::Apply(F.SourceToken, *Outside);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FDelegateHandle Listener;
        bool bAppliedDuringStart = false;
        if (bReentrant)
        {
            Listener = F.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
                {
                    bAppliedDuringStart = true;
                    Run->Cancel();
                });
        }
        FString Error;
        const bool bStarted = Run->StartWithInput(EffectGraph(F, true), EffectInput(F), {}, Error);
        if (bReentrant)
        {
            TestTrue(TEXT("Owned GE application actually entered its reentrant callback"), bAppliedDuringStart);
        }
        else
        {
            TestTrue(TEXT("Owned GE starts through registry"), bStarted);
            TestEqual(TEXT("Owned and outside effects both exist before cancellation"),
                      F.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), 40.f);
        }
        Run->Cancel();
        TestEqual(TEXT("Only this invocation's Armor effect is removed"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), 20.f);
        TestNotNull(TEXT("Other owner's GE remains"), F.ASC->GetActiveGameplayEffect(Existing.Handle));
        F.ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Listener);
        AbilityGameplayEffectActions::Remove(Existing);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityTargetComposition,
                                 "test_25d.UnifiedGA.Utility.QueryComposesActorTargetEffects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityTargetComposition::RunTest(const FString&)
{
    for (const bool bOwned : {false, true})
    {
        FAbilityGraphTestFixture F;
        PrepareQueryTarget(F);
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        TestTrue(TEXT("Query.LiveTarget connects to explicit target GE contract"),
                 Run->StartWithInput(QueryEffectGraph(F, bOwned, false), EffectInput(F), {}, Error));
        TestEqual(TEXT("Actual target attribute changes through shared GE service"),
                  TargetASC->GetNumericAttribute(bOwned ? UMyAttributeSet::GetArmorAttribute()
                                                        : UMyAttributeSet::GetHealthAttribute()),
                  bOwned ? 20.f : 15.f);
        Run->Cancel();
        if (bOwned)
        {
            TestEqual(TEXT("Target ownership is removed on cancel"),
                      TargetASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), 0.f);
        }
        auto* InvalidGraph = EffectGraph(F, bOwned);
        CastChecked<UAbilityGameplayEffectNodeConfig>(InvalidGraph->Nodes[0].Config)->bApplyToLiveActor = true;
        TArray<FString> Errors;
        TestFalse(TEXT("Self descriptor cannot hide a target-only configuration"),
                  InvalidGraph->ValidateForAuthoring(Errors));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityTargetLife, "test_25d.UnifiedGA.Utility.DelayedTargetEffectRejectsReusedActor",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityTargetLife::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    PrepareQueryTarget(F);
    F.Target->EnableDungeonPooling(0.f);
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Query captures old life before delayed effect"),
             Run->StartWithInput(QueryEffectGraph(F, false, true), EffectInput(F), {}, Error));
    TestFalse(TEXT("Delay remains active after the real query"), Run->IsFinished());
    TestEqual(TEXT("Effect has not run before delay"),
              TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 10.f);
    F.Target->SleepForDungeonPool();
    TestTrue(TEXT("Same Actor enters a different pool life"),
             F.Target->WakeFromDungeonPool(FTransform(FVector(150, 0, 0)), NAME_None));
    const float NewHealth = TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    Run->Advance(.5f);
    TestEqual(TEXT("Captured old target never applies GE to new life"),
              TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), NewHealth);
    TestTrue(TEXT("Lost target releases delayed execution"), Run->IsFinished());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityHitReactSourceLost,
                                 "test_25d.UnifiedGA.Utility.HitReactSourceLossEndsActivation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityHitReactSourceLost::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetArmorAttribute(), 0.f);
    bool bApplied = false;
    const auto Listener = F.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
        {
            bApplied = true;
            F.ASC->InitAbilityActorInfo(F.Source, F.Target);
        });
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UUtilityHitReactTestAbility::StaticClass()));
    F.ASC->TryActivateAbility(Handle);
    TestTrue(TEXT("Actual owned effect reaches source-invalidating callback"), bApplied);
    const auto* Spec = F.ASC->FindAbilitySpecFromHandle(Handle);
    TestTrue(TEXT("Source loss ends the original activation without leaving a taskless GA"), Spec && !Spec->IsActive());
    TestEqual(TEXT("Effect returned after source invalidation is removed"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), 0.f);
    F.ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Listener);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityClientListener,
                                 "test_25d.UnifiedGA.Utility.ClientListenerRetainsOriginalAscPermission",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityClientListener::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Actor = F.World->SpawnActor<AUtilityClientListenerTestActor>();
    auto* ASC = Actor->GetAbilitySystemComponent();
    ASC->InitAbilityActorInfo(Actor, Actor);
    ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10.f);
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UUtilityClientListenerTestAbility::StaticClass()));
    auto* Ability =
        Cast<UUtilityClientListenerTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    if (!TestNotNull(TEXT("Real client listener instance"), Ability))
    {
        return false;
    }
    const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("Tests.UnifiedGA.Utility.Value"));
    Ability->ListenTag = Tag;
    Ability->bMatchEventExactly = true;
    Ability->bInitializeListedTags = false;
    Ability->EventEffectClass = UUtilityInstantTestEffect::StaticClass();
    Ability->EventEffectLevel = 0.f;
    F.World->WorldType = EWorldType::PIE;
    F.World->SetPlayInEditorInitialNetMode(NM_Client);
    Actor->UseClientRole();
    ASC->InitAbilityActorInfo(Actor, Actor);
    // InitAbilityActorInfo不刷新ASC缓存；引擎在OnRegister/BeginPlay缓存角色，角色切换夹具必须同样刷新。
    ASC->CacheIsNetSimulated();
    TestEqual(TEXT("Fixture world uses actual client net mode"), F.World->GetNetMode(), NM_Client);
    TestFalse(TEXT("Actual component owner role is nonauthoritative"), Actor->HasAuthority());
    TestTrue(TEXT("ASC is a replicated component"), ASC->GetIsReplicated());
    TestFalse(TEXT("ASC owner is nonauthoritative"), ASC->IsOwnerActorAuthoritative());
    FScopedPredictionWindow Prediction(ASC, true);
    TestTrue(TEXT("Client has a usable scoped prediction key"), ASC->ScopedPredictionKey.IsValidForMorePrediction());
    Ability->ActivateClientForTest(Handle, ASC->AbilityActorInfo.Get(), ASC->ScopedPredictionKey);
    TestTrue(TEXT("Native frontend remains active on client"), Ability->IsActive());
    const auto* Listener = ASC->GenericGameplayEventCallbacks.Find(Tag);
    TestTrue(TEXT("Actual WaitGameplayEvent registers on client ASC"), Listener && Listener->IsBound());
    auto OriginalSpec = ASC->MakeOutgoingSpec(UUtilityInstantTestEffect::StaticClass(), 0.f, ASC->MakeEffectContext());
    OriginalSpec.Data->SetSetByCallerMagnitude(Tag, 5.f);
    TestFalse(TEXT("Original ASC BP method supplies no prediction key and rejects client GE"),
              ASC->BP_ApplyGameplayEffectSpecToSelf(OriginalSpec).WasSuccessfullyApplied());
    FGameplayEventData Event;
    Event.EventTag = Tag;
    Event.EventMagnitude = 5.f;
    ASC->HandleGameplayEvent(Tag, &Event);
    TestEqual(TEXT("Native callback preserves original client permission result"),
              ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 10.f);
    ASC->CancelAbilityHandle(Handle);
    Listener = ASC->GenericGameplayEventCallbacks.Find(Tag);
    TestTrue(TEXT("Client cancellation unbinds the actual ASC event listener"), !Listener || !Listener->IsBound());
    return true;
}
#endif
