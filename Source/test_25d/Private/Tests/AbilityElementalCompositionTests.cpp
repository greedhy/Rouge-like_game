// 验证链：真实Actor GE/Mass承伤、已登记片段编译、图执行 → 生命比例和资源实际读回。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphGameplayExpansionFixture.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Engine/GameInstance.h"
#include "Game/MyGameModeBase.h"
#include "UObject/Script.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeGraphView.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "PureMass/PureMassCrowd.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElementalLiveDamage, "test_25d.ElementalComposition.LiveActorAndMassDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElementalLiveDamage::RunTest(const FString&)
{
    using E = EAbilityElement;
    using S = EAbilityExposure;
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    F.World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;
    URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
    F.World->SetGameMode(URL);
    auto* Mode = Cast<AMyGameModeBase>(F.World->GetAuthGameMode());
    if (!TestNotNull(TEXT("Project game mode supplies damage calculation data"), Mode))
    {
        return false;
    }
    Mode->CharacterClassInfo = LoadObject<UCharacterClassInfo>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
    const auto DamageEffect =
        LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C"));
    if (!TestTrue(TEXT("Actual balance data and GE were loaded"),
                  Mode->CharacterClassInfo && Mode->CharacterClassInfo->DamageCalConst && DamageEffect))
    {
        return false;
    }
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 200.f);
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 200.f);
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 200.f;
    const auto Entity = Mass->SpawnMonster(Type, FVector(300.f, 0.f, 0.f));
    F.Target->RefreshMovementSpeed();
    const float WalkSpeed = F.Target->GetCharacterMovement()->MaxWalkSpeed;
    FAbilityTargetToken MassTarget;
    FString Error;
    if (!TestTrue(
            TEXT("Real Mass enemy captured"),
            AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(Entity), MassTarget, Error)))
    {
        return false;
    }
    for (const auto& Token : {F.TargetToken, MassTarget})
    {
        FDamageEffectParams Params;
        Params.DamageGameplayEffectClass = DamageEffect;
        Params.BaseDamage = 20.f;
        Params.DebuffChance = 0.f;
        auto Hit = [&](E Element, S Expected)
        {
            Params.DamageType = AbilityElements::ToDamageTag(Element);
            const auto Result = AbilityDamageService::Apply(F.SourceToken, Token, Params);
            TestTrue(TEXT("Actual direct damage"), Result.ActualDamage.Get(0.f) > 0.f);
            TestTrue(TEXT("Authority state after direct hit"), AbilityElements::Read(F.SourceToken, Token) == Expected);
            return Result.ActualDamage.Get(0.f);
        };
        Hit(E::Fire, S::Burning);
        Hit(E::Water, S::Steam);
        const float Reacted = Hit(E::Thunder, S::Conductive);
        Hit(E::Arcane, S::ArcaneMark);
        Hit(E::Water, S::Wet);
        Params.BaseDamage = 0.f;
        Params.DamageType = AbilityElements::ToDamageTag(E::Fire);
        AbilityDamageService::Apply(F.SourceToken, Token, Params);
        TestTrue(TEXT("Zero damage does not consume Wet"), AbilityElements::Read(F.SourceToken, Token) == S::Wet);
        Params.BaseDamage = 20.f;
        TestEqual(TEXT("Wet reproduces the same authoritative amplification"), Hit(E::Thunder, S::Conductive), Reacted);
        TestTrue(TEXT("Conductive reaction amplifies actual damage"), FMath::IsNearlyEqual(Reacted, 25.f));
        Params.BaseDamage = 1.f;
        Hit(E::Ice, S::Chilled);
        if (Token.Target.IsActor())
        {
            TestTrue(TEXT("Actor chill slows travel"),
                     FMath::IsNearlyEqual(F.Target->GetCharacterMovement()->MaxWalkSpeed,
                                          WalkSpeed * AbilityElements::ChillMoveRate));
            TestEqual(TEXT("Actor attack clock receives reduced rate"), F.Target->GetIceAttackRate(),
                      AbilityElements::ChillAttackRate);
        }
        else
        {
            auto& Manager = F.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
            auto& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Entity));
            Fragment.AttackRemaining = 2.f;
            if (TestTrue(TEXT("Mass attack clock advances inside the normal update path"), Mass->BeginUpdate(.1f)))
            {
                Mass->UpdateMonster(Fragment);
                Mass->FinishUpdate();
            }
            TestTrue(TEXT("Mass cooldown advances at reduced speed"),
                     FMath::IsNearlyEqual(Fragment.AttackRemaining, 2.f - .1f * AbilityElements::ChillAttackRate));
            Fragment.State = 4;
            Fragment.AttackWindupRemaining = .4f;
            Fragment.PendingAttackTarget = F.Source;
        }
        Hit(E::Water, S::Frozen);
        Hit(E::Ice, S::Frozen);
    }
    TestTrue(TEXT("Actor freeze disables movement and prevents new attacks"),
             F.Target->IsIceFrozen() && F.Target->GetCharacterMovement()->MovementMode == MOVE_None);
    TestEqual(TEXT("Actor freeze holds the animation pose"), F.Target->GetMesh()->GlobalAnimRateScale, 0.f);
    FPureMassView Frozen;
    Mass->Resolve(Entity, Frozen);
    TestTrue(TEXT("Mass freeze cancels the pending attack and publishes stationary state"),
             Frozen.IceExposure == S::Frozen && Frozen.State == 0 && Frozen.Velocity.IsNearlyZero());
    FPureMassNetEntity Snapshot;
    TestTrue(TEXT("Ice state marks replicated Mass item as dirty"), Snapshot.SetFromView(Frozen));
    TestTrue(TEXT("Client view receives freeze"), Snapshot.ToView().IceExposure == S::Frozen);
    for (int32 Frame = 0; Frame < 30; ++Frame)
    {
        ++GFrameCounter;
        F.World->Tick(LEVELTICK_TimeOnly, .05f);
        F.World->GetTimerManager().Tick(.05f);
    }
    TestTrue(TEXT("Reactions expire without keeping consumed chill or wet"),
             !F.Target->IsIceFrozen() && F.Target->GetMesh()->GlobalAnimRateScale > 0.f &&
                 F.Target->GetCharacterMovement()->MaxWalkSpeed == WalkSpeed &&
                 Mass->GetElementalExposure(Entity) == S::None);
    FAbilityElementalExposure Expiring{S::Wet, 4.};
    TestTrue(TEXT("World timestamp expires status exactly at the deadline"), Expiring.Get(4.) == S::None);
    TestTrue(TEXT("Repeated ice converts chill into freeze"), AbilityElements::Evaluate(S::Chilled, E::Ice).Next == S::Frozen);
    TestTrue(TEXT("Ice also reacts when Wet comes first"), AbilityElements::Evaluate(S::Wet, E::Ice).Next == S::Frozen);
    TestTrue(TEXT("Frozen targets keep the same deadline"), AbilityElements::Evaluate(S::Frozen, E::Ice).bKeepCurrent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElementalWorkshopComposition, "test_25d.ElementalComposition.FiniteModuleToRealCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElementalWorkshopComposition::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    const auto Victim = F.Spawn(FVector(350.f, 0.f, 0.f), 600.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 20.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 90.f);
    auto* Definition = NewObject<UAbilityGraphDefinition>(F.World);
    Definition->SchemaVersion = 2;
    Definition->CompatibilityProfile = TEXT("UnifiedV2");
    auto* RepeatConfig = NewObject<UAbilityRepeatNodeConfig>(Definition);
    RepeatConfig->Count = 4;
    RepeatConfig->Period = .1f;
    auto* QueryConfig = NewObject<UAbilitySelectTargetsConfig>(Definition);
    QueryConfig->Radius = 600.f;
    QueryConfig->TargetFilter = 2;
    auto* InputConfig = NewObject<UAbilityInputDamageNodeConfig>(Definition);
    const auto Root = F.Add(*Definition, TEXT("Control.Repeat"), RepeatConfig);
    const auto Query = F.Add(*Definition, TEXT("Query.SelectTargets"), QueryConfig);
    const auto Damage = F.Add(*Definition, TEXT("Effect.InputDamage"), InputConfig);
    Definition->EntryNodeId = Root;
    Definition->Edges = {{Root, Query, TEXT("Pulse")}, {Query, Damage, TEXT("TargetFound")}};

    TArray<FAbilityRuntimeRecipeContribution> Contributions;
    auto CompileEdit = [&](const UAbilityGraphDefinition* Active, FGuid NodeId, FName Port, FName Fragment, float Value,
                           FName Modifier = TEXT("Append.Registered")) -> UAbilityGraphDefinition*
    {
        const auto* Node = Active->Nodes.FindByPredicate(
            [NodeId](const auto& Candidate)
            {
                return Candidate.NodeId == NodeId;
            });
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.ModifierId = Modifier;
        Edit.FragmentId = Fragment;
        Edit.OutputPortId = Port;
        Edit.Value = Value;
        FAbilityRuntimeRecipePatch Patch;
        FString Error;
        if (!TestTrue(TEXT("Trusted workshop creates typed patch"),
                      Node && AbilityRuntimeModifierTemplates::BuildPatch(F.World, *Node, Edit, Patch, Error, Active)))
        {
            AddInfo(Error);
            return nullptr;
        }
        FAbilityRuntimeRecipeContribution Change;
        Change.Priority = Contributions.Num() + 1;
        Change.SourceId = FName(*FString::Printf(TEXT("ElementalTest.%d"), Change.Priority));
        Change.bOrderedWorkshopEdit = true;
        Change.Patches.Add(Patch);
        Contributions.Add(Change);
        UAbilityGraphDefinition* Compiled = nullptr;
        if (!TestTrue(TEXT("Recipe really compiles"),
                      AbilityRuntimeRecipes::Compile(F.World, Definition, Contributions, Compiled, Error)))
        {
            AddInfo(Error);
            return nullptr;
        }
        return Compiled;
    };
    auto* Converted = CompileEdit(Definition, Damage, NAME_None, NAME_None, 2.f, TEXT("Set.ElementConversion"));
    if (!Converted)
    {
        return false;
    }
    auto* Counted = CompileEdit(Converted, Damage, TEXT("DamageResolved"), TEXT("Control.EveryNthDamage"), 2.f);
    if (!Counted)
    {
        return false;
    }
    auto* Counter = Counted->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.TypeId == TEXT("Control.EveryNthDamage");
        });
    if (!TestNotNull(TEXT("Counter is a real appended node"), Counter))
    {
        return false;
    }
    const FGuid CounterId = Counter->NodeId;
    auto* WithMana = CompileEdit(Counted, CounterId, TEXT("Passed"), TEXT("Effect.RestoreMana"), 8.f);
    if (!WithMana)
    {
        return false;
    }
    auto* Limited = CompileEdit(WithMana, Damage, TEXT("DamageResolved"), TEXT("Control.DamageTriggerLimit"), 3.f);
    if (!Limited)
    {
        return false;
    }
    const auto* Limit = Limited->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.TypeId == TEXT("Control.DamageTriggerLimit");
        });
    if (!TestNotNull(TEXT("Limiter is a real appended node"), Limit))
    {
        return false;
    }
    auto* Cooled = CompileEdit(Limited, Limit->NodeId, NAME_None, NAME_None, .5f, TEXT("Set.TriggerCooldown"));
    auto* Final =
        Cooled ? CompileEdit(Cooled, Limit->NodeId, TEXT("Passed"), TEXT("Effect.RestoreHealth"), 8.f) : nullptr;
    if (!Final)
    {
        return false;
    }

    FString Error;
    const auto Input = F.MakeInput(Victim, Error);
    for (const float Expected : {28.f, 36.f})
    {
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Thresholds = 0;
        int32 LimitPasses = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.NodeType == TEXT("Control.EveryNthDamage") && Event.OutputPort == TEXT("Passed"))
                {
                    ++Thresholds;
                }
                if (Event.NodeType == TEXT("Control.DamageTriggerLimit") && Event.OutputPort == TEXT("Passed"))
                {
                    ++LimitPasses;
                }
            });
        TestTrue(TEXT("Actual compiled graph starts"), Run->StartWithInput(Final, Input, {}, F.Registry(), Error));
        for (int32 Frame = 0; Frame < 30 && !Run->IsFinished(); ++Frame)
        {
            Run->Advance(.05f);
        }
        TestEqual(TEXT("Four real hits pass every second time"), Thresholds, 2);
        TestEqual(TEXT("Four same-world-time damage receipts allow just one cooldown trigger"), LimitPasses, 1);
        TestEqual(TEXT("Resource node restores at most once and resets with the next cast"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), Expected);
        TestTrue(TEXT("Real damage uses the converted Water tag"),
                 F.Mass->GetElementalExposure(Victim) == EAbilityExposure::Wet);
    }
    TestEqual(TEXT("Health feedback respects the actual resource cap"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    TestTrue(TEXT("Base config remains unmodified"), InputConfig->ElementOverride == EAbilityElement::Original);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElementalFiniteFragments, "test_25d.ElementalComposition.FiniteTemporalFragments",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElementalFiniteFragments::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    const int32 Target = F.Spawn(FVector(100.f, 0.f, 0.f));
    struct FCase
    {
        FName Type;
        FName Output;
        int32 Events;
    };
    for (const auto& Case :
         {FCase{TEXT("Control.DelayOrigin"), TEXT("Completed"), 1}, FCase{TEXT("Control.Repeat"), TEXT("Pulse"), 3},
          FCase{TEXT("Query.WaitForTarget"), TEXT("Triggered"), 1},
          FCase{TEXT("Spatial.Wavefront"), TEXT("TargetFound"), 1}})
    {
        auto* Base = NewObject<UAbilityGraphDefinition>(F.World);
        Base->SchemaVersion = 2;
        Base->CompatibilityProfile = TEXT("UnifiedV2");
        auto* Delay = NewObject<UAbilityDelayNodeConfig>(Base);
        Delay->DelaySeconds = .1f;
        Base->EntryNodeId = F.Add(*Base, TEXT("Control.DelayOrigin"), Delay);
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.ModifierId = TEXT("Append.Registered");
        Edit.OutputPortId = TEXT("Completed");
        Edit.FragmentId = Case.Type;
        Edit.Count = 3;
        Edit.Interval = .1f;
        Edit.Radius = 500.f;
        Edit.Value = 1.f;
        FAbilityRuntimeRecipeContribution Change;
        Change.SourceId = TEXT("Test.AppendFiniteTemporal");
        FAbilityRuntimeRecipePatch Patch;
        FString Error;
        if (!TestTrue(
                TEXT("Worktable offers the existing finite module on the real Origin port"),
                AbilityRuntimeModifierTemplates::SupportsFragmentPayload(Case.Type, EAbilityPortPayload::Origin) &&
                    AbilityRuntimeModifierTemplates::BuildPatch(F.World, Base->Nodes[0], Edit, Patch, Error, Base)))
        {
            AddInfo(Error);
            return false;
        }
        TestFalse(
            TEXT("Historical damage is not implicitly accepted as a location port"),
            AbilityRuntimeModifierTemplates::SupportsFragmentPayload(Case.Type, EAbilityPortPayload::DamageResult));
        Change.Patches.Add(Patch);
        UAbilityGraphDefinition* Compiled = nullptr;
        if (!TestTrue(TEXT("Finite timing append compiles without changing the base graph"),
                      AbilityRuntimeRecipes::Compile(F.World, Base, {Change}, Compiled, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const auto Added = Compiled->Nodes.Last().NodeId;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Count = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.NodeType == Case.Type && Event.OutputPort == Case.Output)
                {
                    ++Count;
                }
            });
        TestTrue(TEXT("Appended fragment graph begins"),
                 Run->StartWithInput(Compiled, F.MakeInput(Target, Error), {}, F.Registry(), Error));
        for (int32 Frame = 0; Frame < 50 && !Run->IsFinished(); ++Frame)
        {
            Run->Advance(.05f);
        }
        TestTrue(TEXT("Registered fragment ends normally"), Run->GetEndReason() == EAbilityGraphEndReason::Completed);
        TestEqual(TEXT("The actual action publishes the bounded number of expected outputs"), Count,
                  Case.Events + (Case.Type == TEXT("Control.DelayOrigin") ? 1 : 0));
        TestTrue(TEXT("A stable identity was added without changing the base"), Added != Base->EntryNodeId);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElementalExposureFragment, "test_25d.ElementalComposition.ExposureFragment",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElementalExposureFragment::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    using S = EAbilityExposure;
    FFixture Fixture;
    const int32 Victim = Fixture.Spawn(FVector(100.f, 0.f, 0.f));
    auto* Base = NewObject<UAbilityGraphDefinition>(Fixture.World);
    Base->SchemaVersion = 2;
    Base->CompatibilityProfile = TEXT("UnifiedV2");
    auto* Query = NewObject<UAbilityQueryNodeConfig>(Base);
    Query->Query.MaxTargets = 1;
    Base->EntryNodeId = Fixture.Add(*Base, TEXT("Query.Shape"), Query);

    FAbilityRuntimeDeveloperEdit Edit;
    Edit.ModifierId = TEXT("Append.Registered");
    Edit.FragmentId = TEXT("Status.ApplyExposure");
    Edit.OutputPortId = TEXT("Completed");
    Edit.Value = 5.f;
    FAbilityRuntimeRecipePatch Patch;
    FString Error;
    TestFalse(TEXT("An Origin output cannot authorize a live target"),
              AbilityRuntimeModifierTemplates::BuildPatch(Fixture.World, Base->Nodes[0], Edit, Patch, Error, Base));

    Edit.OutputPortId = TEXT("TargetFound");
    if (!TestTrue(TEXT("The real developer template appends an ice effect to the verified target output"),
                  AbilityRuntimeModifierTemplates::BuildPatch(Fixture.World, Base->Nodes[0], Edit, Patch, Error, Base)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeContribution Change;
    Change.SourceId = TEXT("Test.ApplyExposure");
    Change.Patches.Add(Patch);
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!TestTrue(TEXT("The trusted append compiles against a base graph without damage"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Base, {Change}, Compiled, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot Snapshot;
    Snapshot.Graph = Compiled;
    Snapshot.ContentHash = AbilityRuntimeRecipes::Fingerprint(*Compiled);
    FAbilityRuntimeGraphView View;
    if (TestTrue(TEXT("The real developer graph projects the composed nodes"),
                 AbilityRuntimeGraphViews::Build(Snapshot, View, Error)))
    {
        const auto* Exposure = View.Nodes.FindByPredicate(
            [](const FAbilityRuntimeGraphNodeView& Node)
            {
                return Node.TypeId == TEXT("Status.ApplyExposure");
            });
        TestTrue(TEXT("Graph editors see the independent state element and duration"),
                 Exposure && Exposure->SupportedModifiers.Contains(TEXT("Set.ExposureElement")) &&
                     Exposure->SupportedModifiers.Contains(TEXT("Scale.Duration")));
    }
    int32 Applied = 0;
    for (const S Expected : {S::Chilled, S::Frozen, S::Frozen})
    {
        auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
        Run->OnEvent.AddLambda(
            [&Applied](const FAbilityGraphEvent& Event)
            {
                if (Event.NodeType == TEXT("Status.ApplyExposure") && Event.OutputPort == TEXT("Applied"))
                {
                    ++Applied;
                }
            });
        auto Input = Fixture.MakeInput(Victim, Error);
        Input.LiveTargetToken.Reset();
        TestTrue(TEXT("The composed graph starts at the original query"),
                 Run->StartWithInput(Compiled, Input, {}, Fixture.Registry(), Error));
        Run->Advance(.01f);
        TestTrue(TEXT("The Mass enemy receives the expected shared cold reaction"),
                 Fixture.Mass->GetElementalExposure(Victim) == Expected);
    }
    TestEqual(TEXT("Applying cold again during freeze gives no second reaction event"), Applied, 2);
    TestEqual(TEXT("The reusable fragment leaves health unchanged"), Fixture.Health(Victim), 200.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopShapeReplacement, "test_25d.ElementalComposition.ShapeReplacement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopShapeReplacement::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture Fixture;
    const int32 Ahead = Fixture.Spawn(FVector(250.f, 10.f, 0.f));
    const int32 ToSide = Fixture.Spawn(FVector(240.f, 160.f, 0.f));
    auto* Base = NewObject<UAbilityGraphDefinition>(Fixture.World);
    Base->SchemaVersion = 2;
    Base->CompatibilityProfile = TEXT("UnifiedV2");
    auto* Query = NewObject<UAbilityQueryNodeConfig>(Base);
    Query->Query.Radius = 350.f;
    Base->EntryNodeId = Fixture.Add(*Base, TEXT("Query.Shape"), Query);

    FAbilityRuntimeRecipeContribution Change;
    Change.SourceId = TEXT("Test.ReplaceShape");
    FString Error;
    const TPair<FName, float> Edits[] = {
        {TEXT("Set.QueryGeometry"), 3.f}, {TEXT("Set.Range"), 350.f}, {TEXT("Set.Width"), 100.f}};
    for (const auto& Parameter : Edits)
    {
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.ModifierId = Parameter.Key;
        Edit.Value = Parameter.Value;
        FAbilityRuntimeRecipePatch Patch;
        if (!TestTrue(
                TEXT("The real graph template accepts each explicit edit"),
                AbilityRuntimeModifierTemplates::BuildPatch(Fixture.World, Base->Nodes[0], Edit, Patch, Error, Base)))
        {
            AddInfo(Error);
            return false;
        }
        Change.Patches.Add(MoveTemp(Patch));
    }
    UAbilityGraphDefinition* Line = nullptr;
    if (!TestTrue(TEXT("Geometry and its new axes compile as one transaction"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Base, {Change}, Line, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Gather = [&](UAbilityGraphDefinition& Definition)
    {
        auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
        TSet<int32> Result;
        const auto Observer = Run->OnEvent.AddLambda(
            [&Result](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("TargetFound") && Event.Input.LiveTargetToken.IsSet())
                {
                    Result.Add(Event.Input.LiveTargetToken->Target.EntityId);
                }
            });
        auto Input = Fixture.MakeInput(Ahead, Error);
        Input.LiveTargetToken.Reset();
        TestTrue(TEXT("The edited or original graph really executes"),
                 Run->StartWithInput(&Definition, Input, {}, Fixture.Registry(), Error));
        Run->Advance(.01f);
        Run->OnEvent.Remove(Observer);
        return Result;
    };
    const auto CircleHits = Gather(*Base);
    const auto LineHits = Gather(*Line);
    TestTrue(TEXT("The original sphere reaches both enemies"),
             CircleHits.Contains(Ahead) && CircleHits.Contains(ToSide));
    TestTrue(TEXT("The actual line now reaches only the enemy inside its width"),
             LineHits.Contains(Ahead) && !LineHits.Contains(ToSide));
    TestTrue(TEXT("Changing the clone leaves the original geometry intact"),
             Query->Query.Geometry == EAbilityQueryGeometry::Sphere);
    return true;
}
#endif
