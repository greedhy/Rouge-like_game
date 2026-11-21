// 调用链：服务器纯值配方 -> 私有Working拓扑事务 -> 真实Mass状态/伤害；旧正式图与原预算证明始终只读。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityAuthoredGraphFineCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityNormalizationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "Actor/MyProjectile.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"

namespace AbilityRuntimeRecipeStructuralTests
{
    struct FStage : FAbilityGraphTestFixture
    {
        int32 Victim = INDEX_NONE;
        FStage()
        {
            Target->Destroy();
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 200.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            Type->Speed = 0.f;
            Type->XPReward = 0;
            Victim = Mass->SpawnMonster(Type, FVector(50, 0, 0));
        }
        float Health() const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Victim, View) ? View.Health : -1.f;
        }
        bool Run(UAbilityGraphDefinition* Definition, FString& Error)
        {
            FAbilityNodeInput Input;
            Input.SourceToken = SourceToken;
            auto* Instance = NewObject<UAbilityGraphInstance>(World);
            return Instance->StartWithInput(Definition, Input, {}, Error) && Instance->IsFinished() &&
                   Instance->GetEndReason() == EAbilityGraphEndReason::Completed;
        }
    };

    UAbilityGraphDefinition* Empty(UObject* Outer)
    {
        auto* Result = NewObject<UAbilityGraphDefinition>(Outer);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        return Result;
    }

    /** 两个真实动作作为独立区域片段；旧节点GUID只用于核对是否错误修剪。 */
    FGuid AddArea(UAbilityGraphDefinition& Definition, float Damage, TArray<FGuid>* Added = nullptr)
    {
        const auto QueryId = FGuid::NewGuid();
        const auto DamageId = FGuid::NewGuid();
        auto* Query = NewObject<UAbilityQueryNodeConfig>(&Definition);
        Query->Query.Radius = 100.f;
        Query->Query.bRequireVisibility = false;
        auto* Effect = NewObject<UAbilityDamageNodeConfig>(&Definition);
        Effect->Damage = Damage;
        Effect->DamagePerLevel = 0.f;
        Effect->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Definition.Nodes.Add({QueryId, TEXT("Query.Shape"), Query});
        Definition.Nodes.Add({DamageId, TEXT("Effect.Damage"), Effect});
        Definition.Edges.Add({QueryId, DamageId, TEXT("TargetFound")});
        if (!Definition.EntryNodeId.IsValid())
        {
            Definition.EntryNodeId = QueryId;
        }
        if (Added)
        {
            Added->Append({QueryId, DamageId});
        }
        return QueryId;
    }

    FAbilityRuntimeRecipeContribution Replace(FName Source, FGuid Target, FName Port, UAbilityGraphDefinition* Branch)
    {
        FAbilityRuntimeRecipeContribution Result;
        Result.SourceId = Source;
        FAbilityRuntimeRecipePatch Patch;
        Patch.Operation = EAbilityRecipePatchOperation::ReplaceBranch;
        Patch.TargetNodeId = Target;
        Patch.OutputPort = Port;
        Patch.BranchGraph = Branch;
        Result.Patches.Add(Patch);
        return Result;
    }
} // namespace AbilityRuntimeRecipeStructuralTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeReplaceBranchPrivateTopology,
    "test_25d.UnifiedGA.DynamicMage.ReplaceBranch.SelectedPortKeepsOtherPortsAndSharedTail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeReplaceBranchPrivateTopology::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeStructuralTests;
    for (bool bReplace : {false, true})
    {
        FStage F;
        auto* Base = Empty(F.World);
        const auto Root = FGuid::NewGuid();
        Base->EntryNodeId = Root;
        Base->Nodes.Add({Root, TEXT("Event.OrderedCompletion"), NewObject<UAbilityOrderedCompletionNodeConfig>(Base)});
        TArray<FGuid> Private;
        TArray<FGuid> Shared;
        TArray<FGuid> Other;
        const auto Old = AddArea(*Base, 50.f, &Private);
        const auto SharedEntry = AddArea(*Base, 3.f, &Shared);
        const auto OtherEntry = AddArea(*Base, 7.f, &Other);
        Base->Edges.Append({{Root, Old, TEXT("Expired")},
                            {Root, SharedEntry, TEXT("Expired")},
                            {Root, OtherEntry, TEXT("Completed")},
                            {Root, SharedEntry, TEXT("Completed")}});
        TArray<FString> Errors;
        if (!TestTrue(TEXT("Actual old shared-tail graph is valid"), Base->ValidateForAuthoring(Errors)))
        {
            AddInfo(FString::Join(Errors, TEXT(";")));
            return false;
        }
        const auto Before = AbilityRuntimeRecipes::Fingerprint(*Base);
        UAbilityGraphDefinition* Actual = Base;
        FString Error;
        if (bReplace)
        {
            auto* Branch = Empty(F.World);
            AddArea(*Branch, 11.f);
            if (!TestTrue(TEXT("One selected port is replaced transactionally: ") + Error,
                          AbilityRuntimeRecipes::Compile(
                              F.World, Base, {Replace(TEXT("Rune.NewBranch"), Root, TEXT("Expired"), Branch)}, Actual,
                              Error)))
            {
                AddInfo(Error);
                return false;
            }
            for (const auto Id : Private)
            {
                TestFalse(TEXT("Only the now-unreachable private old tail is pruned"),
                          Actual->Nodes.ContainsByPredicate(
                              [&](const auto& Node)
                              {
                                  return Node.NodeId == Id;
                              }));
            }
            for (const auto Id : Shared)
            {
                TestTrue(TEXT("A shared old tail with another incoming port stays alive"),
                         Actual->Nodes.ContainsByPredicate(
                             [&](const auto& Node)
                             {
                                 return Node.NodeId == Id;
                             }));
            }
            for (const auto Id : Other)
            {
                TestTrue(TEXT("Other output-port topology is retained"), Actual->Nodes.ContainsByPredicate(
                                                                             [&](const auto& Node)
                                                                             {
                                                                                 return Node.NodeId == Id;
                                                                             }));
            }
        }
        if (!TestTrue(TEXT("Actual old/new topology executes real damage: ") + Error, F.Run(Actual, Error)))
        {
            return false;
        }
        TestEqual(TEXT("Only the selected old50+shared3 branch changes to new11"), F.Health(),
                  bReplace ? 179.f : 137.f);
        TestEqual(TEXT("Formal base topology/configuration is immutable"), AbilityRuntimeRecipes::Fingerprint(*Base),
                  Before);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeReplaceBranchConflicts,
    "test_25d.UnifiedGA.DynamicMage.ReplaceBranch.PortConflictsAndInvalidPayloadsLeaveNoPartialResult",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeReplaceBranchConflicts::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeStructuralTests;
    FStage F;
    auto* Base = Empty(F.World);
    AddArea(*Base, 10.f);
    auto* Branch = Empty(F.World);
    auto* Damage = NewObject<UAbilityDamageNodeConfig>(Branch);
    Damage->Damage = 3.f;
    Damage->DamagePerLevel = 0.f;
    Damage->DamageType = FMyGameplayTags::Get().Damage_Magic;
    Branch->EntryNodeId = FGuid::NewGuid();
    Branch->Nodes.Add({Branch->EntryNodeId, TEXT("Effect.Damage"), Damage});
    const auto A = Replace(TEXT("Rune.A"), Base->EntryNodeId, TEXT("TargetFound"), Branch);
    const auto B = Replace(TEXT("Rune.B"), Base->EntryNodeId, TEXT("TargetFound"), Branch);
    FString Error;
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!TestTrue(TEXT("Compatible LiveTarget replacement has a real executable result"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {A}, Compiled, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("New single-target payload runs"), F.Run(Compiled, Error));
    TestEqual(TEXT("Old10 payload is replaced by actual3"), F.Health(), 197.f);
    const auto Hash = AbilityRuntimeRecipes::Fingerprint(*Base);
    TestFalse(TEXT("Two sources cannot replace the same selected port"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {A, B}, Compiled, Error));
    TestNull(TEXT("A rejected conflict exposes no partial graph"), Compiled);
    auto Append = B;
    Append.Patches[0].Operation = EAbilityRecipePatchOperation::AppendBranch;
    TestFalse(TEXT("Append and replacement on the same port cannot depend on source ordering"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {A, Append}, Compiled, Error));
    auto Invalid = A;
    Invalid.Patches[0].OutputPort = TEXT("Completed"); // Origin不能授权分支入口的LiveTarget。
    TestFalse(TEXT("Invalid output payload is rejected"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {Invalid}, Compiled, Error));
    auto* Cycle = DuplicateObject(Branch, F.World);
    Cycle->Edges.Add({Cycle->EntryNodeId, Cycle->EntryNodeId, TEXT("Completed")});
    Invalid = A;
    Invalid.Patches[0].BranchGraph = Cycle;
    TestFalse(TEXT("A cyclic branch cannot enter the accepted recipe"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {Invalid}, Compiled, Error));
    TestEqual(TEXT("Failed edits leave the original base unchanged"), AbilityRuntimeRecipes::Fingerprint(*Base), Hash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeReplaceProjectedOriginalBranch,
    "test_25d.UnifiedGA.DynamicMage.ReplaceBranch.ProjectedOriginalBranchRequiresNormalizerRemap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeReplaceProjectedOriginalBranch::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeStructuralTests;
    FStage F;
    auto* Parameters = NewObject<UAbilityPrototype>(F.World);
    Parameters->Kind = EAbilityPrototypeKind::Burst;
    Parameters->Cooldown = 0.f;
    Parameters->DamageType = FMyGameplayTags::Get().Damage_Magic;
    FString Error;
    auto* Legacy = UAbilityLegacyGraphCompiler::Compile(F.World, Parameters, TEXT("LegacyMixed.v1"), Error);
    auto* Base = AbilityAuthoredGraphFineCompiler::Compile(F.World, Legacy, Error);
    if (!TestNotNull(TEXT("Actual projected original definition: ") + Error, Base))
    {
        return false;
    }
    const auto* TargetNode = Base->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.TypeId == TEXT("Control.PrototypeLifetime");
        });
    if (!TestNotNull(TEXT("Actual original lifetime branch exists"), TargetNode))
    {
        return false;
    }
    auto* Branch = Empty(F.World);
    AddArea(*Branch, 3.f);
    const auto Hash = AbilityRuntimeRecipes::Fingerprint(*Base);
    UAbilityGraphDefinition* Compiled = nullptr;
    TestFalse(TEXT("Replacing an original ordinal branch cannot erase proof instead of remapping it"),
              AbilityRuntimeRecipes::Compile(
                  F.World, Base, {Replace(TEXT("Rune.Projected"), TargetNode->NodeId, TEXT("Action"), Branch)},
                  Compiled, Error));
    TestTrue(TEXT("Unsupported original-proof remap is explained"), Error.Contains(TEXT("projected")));
    TestNull(TEXT("Rejected projected edit exposes no graph"), Compiled);
    TestEqual(TEXT("Original proof/hash survives rejection"), AbilityRuntimeRecipes::Fingerprint(*Base), Hash);
    return true;
}

namespace AbilityRuntimeRecipeStructuralTests
{
    UAbilityGraphDefinition* TransientPrototype(UObject* Outer, FString& Error, bool bField = false)
    {
        auto* Parameters = NewObject<UAbilityPrototype>(Outer);
        Parameters->Kind = bField ? EAbilityPrototypeKind::Field : EAbilityPrototypeKind::Burst;
        Parameters->Cooldown = 0.f;
        Parameters->Damage = 1.f;
        Parameters->DamagePerLevel = 0.f;
        Parameters->Radius = 100.f;
        Parameters->Period = .1f;
        Parameters->Duration = 1.f;
        Parameters->DamageType = FMyGameplayTags::Get().Damage_Magic;
        return AbilityPrototypeFineCompiler::Compile(Outer, Parameters, TEXT("LegacyMixed.v1"), Error);
    }
} // namespace AbilityRuntimeRecipeStructuralTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeTransientParameterHash,
    "test_25d.UnifiedGA.DynamicMage.RecipeIdentity.TransientStableReferencesUseValuesInsteadOfWorldObjectPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTransientParameterHash::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeStructuralTests;
    FStage One;
    FStage Two;
    FString Error;
    auto* Asset = LoadObject<UAbilityPrototype>(
        nullptr,
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Burst.DA_Prototype_Burst"));
    if (!TestNotNull(TEXT("Stable real asset baseline is available"), Asset))
    {
        return false;
    }
    auto* PersistentOne = AbilityPrototypeFineCompiler::Compile(One.World, Asset, TEXT("LegacyMixed.v1"), Error);
    auto* PersistentTwo = AbilityPrototypeFineCompiler::Compile(Two.World, Asset, TEXT("LegacyMixed.v1"), Error);
    if (!TestNotNull(TEXT("First persistent baseline compiles"), PersistentOne) ||
        !TestNotNull(TEXT("Second persistent baseline compiles"), PersistentTwo))
    {
        return false;
    }
    TestEqual(TEXT("Real stable assets already have the same recipe identity across owners/worlds"),
              AbilityRuntimeRecipes::Fingerprint(*PersistentOne), AbilityRuntimeRecipes::Fingerprint(*PersistentTwo));
    auto* TransientOne = TransientPrototype(One.World, Error);
    auto* TransientTwo = TransientPrototype(Two.World, Error);
    if (!TestNotNull(TEXT("First generated value configuration compiles"), TransientOne) ||
        !TestNotNull(TEXT("Second generated value configuration compiles"), TransientTwo))
    {
        return false;
    }
    TestEqual(TEXT("Equivalent generated recipes do not contain temporary world or UObject names"),
              AbilityRuntimeRecipes::Fingerprint(*TransientOne), AbilityRuntimeRecipes::Fingerprint(*TransientTwo));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeFiniteOwnerRollback,
    "test_25d.UnifiedGA.DynamicMage.RuntimeFinite.SameWorldOwnersRollbackAndFrozenCastRemainIndependent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFiniteOwnerRollback::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeStructuralTests;
    FStage F;
    auto* Other = F.World->SpawnActor<AEnemyCharacter>();
    Other->Tags.Remove(TEXT("Enemy"));
    Other->Tags.Add(TEXT("Player"));
    Other->SetActorLocation(FVector(1000, 0, 0));
    auto* OtherASC = Other->GetAbilitySystemComponent();
    OtherASC->InitAbilityActorInfo(Other, Other);
    OtherASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Other));
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 200.f;
    Type->Speed = 0.f;
    Type->Radius = 2.f;
    Type->HalfHeight = 10.f;
    Type->XPReward = 0;
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    const int32 OtherVictim = Mass->SpawnMonster(Type, FVector(1050, 0, 0));
    auto MakeComponent = [](AActor* Owner)
    {
        auto* Component = NewObject<UAbilityRuntimeRecipeComponent>(Owner);
        Owner->AddInstanceComponent(Component);
        Component->RegisterComponent();
        return Component;
    };
    auto* First = MakeComponent(F.Source);
    auto* Second = MakeComponent(Other);
    FString Error;
    auto* Base = TransientPrototype(F.World, Error, true);
    if (!TestNotNull(TEXT("Same-world owners share one immutable field base"), Base))
    {
        return false;
    }
    const auto Tag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    if (!TestTrue(TEXT("Owner one registers its private base"), First->RegisterBase(Tag, Base, Error)) ||
        !TestTrue(TEXT("Owner two registers its own base"), Second->RegisterBase(Tag, Base, Error)))
    {
        return false;
    }
    const auto* Damage = Base->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.TypeId == TEXT("Effect.PrototypeDamageBatch");
        });
    if (!TestNotNull(TEXT("Actual frame damage attachment point exists"), Damage))
    {
        return false;
    }
    auto* Status = Empty(F.World);
    const auto Impact = FGuid::NewGuid();
    const auto Resolve = FGuid::NewGuid();
    const auto Petrify = FGuid::NewGuid();
    Status->EntryNodeId = Impact;
    Status->Nodes = {{Impact, TEXT("Event.ImpactFact"), NewObject<UAbilityImpactFactNodeConfig>(Status)},
                     {Resolve, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(Status)},
                     {Petrify, TEXT("Status.Petrify"), NewObject<UAbilityPetrifyNodeConfig>(Status)}};
    Status->Edges = {{Impact, Resolve, TEXT("Completed")}, {Resolve, Petrify, TEXT("TargetFound")}};
    FAbilityRuntimeRecipeContribution Rune;
    Rune.SourceId = TEXT("Rune.OwnerOne");
    FAbilityRuntimeRecipePatch Patch;
    Patch.TargetNodeId = Damage->NodeId;
    Patch.OutputPort = TEXT("DamageResolved");
    Patch.Operation = EAbilityRecipePatchOperation::AppendBranch;
    Patch.BranchGraph = Status;
    Rune.Patches.Add(Patch);
    if (!TestTrue(TEXT("Only owner one accepts the finite state branch"), First->SetContribution(Tag, Rune, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot Old;
    FAbilityRuntimeRecipeSnapshot Plain;
    First->CreateCastSnapshot(Tag, F.World, Old, Error);
    Second->CreateCastSnapshot(Tag, F.World, Plain, Error);
    const int32 Revision = First->GetRevision(Tag);
    const auto AcceptedHash = Old.ContentHash;
    auto Invalid = Rune;
    auto* Unsafe = Empty(F.World);
    auto* Actor = NewObject<UAbilityActorProjectileNodeConfig>(Unsafe);
    Actor->Settings.ProjectileClass = AMyProjectile::StaticClass();
    Actor->Settings.Count = 65;
    Unsafe->EntryNodeId = FGuid::NewGuid();
    Unsafe->Nodes = {{Unsafe->EntryNodeId, TEXT("Delivery.ActorProjectiles"), Actor}};
    Invalid.Patches[0].BranchGraph = Unsafe;
    TestFalse(TEXT("Unsafe replacement of the same contribution is rejected transactionally"),
              First->SetContribution(Tag, Invalid, Error));
    TestEqual(TEXT("Failed admission preserves the exact revision"), First->GetRevision(Tag), Revision);
    FAbilityRuntimeRecipeSnapshot AfterRejected;
    First->CreateCastSnapshot(Tag, F.World, AfterRejected, Error);
    TestEqual(TEXT("Failed admission preserves the accepted configuration hash"), AfterRejected.ContentHash,
              AcceptedHash);
    TestFalse(TEXT("Other owner never receives that contribution"), Second->HasContributions(Tag));
    TestTrue(TEXT("Removing the first owner's accepted source succeeds"),
             First->RemoveContribution(Tag, Rune.SourceId, Error));
    FAbilityRuntimeRecipeSnapshot New;
    First->CreateCastSnapshot(Tag, F.World, New, Error);
    TestFalse(TEXT("Old in-flight snapshot still owns its finite branch"), Old.ContentHash == New.ContentHash);
    TestEqual(TEXT("Later snapshot matches the other untouched owner's base"), New.ContentHash, Plain.ContentHash);
    auto Start = [&](AEnemyCharacter* Caster, UAbilityGraphDefinition* Definition)
    {
        auto* Context = NewObject<UPrototypeCastContext>(F.World);
        Context->Caster = Caster;
        Context->Level = 1;
        Context->CastId = FGuid::NewGuid();
        Context->DamageParams.SourceAbilitySystemComponent = Caster->GetAbilitySystemComponent();
        Context->CaptureSourceLife(Error);
        FAbilityNodeInput Input;
        Input.SourceToken = Context->GetSourceLife();
        Input.Context = Context;
        Input.Origin = Caster->GetActorLocation();
        Input.Direction = FVector::ForwardVector;
        Input.DamageParams = Context->DamageParams;
        auto* Instance = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Definition->CompatibilityProfile;
        Options.BudgetPolicy = Definition->BudgetPolicy;
        if (!TestTrue(TEXT("Frozen owner snapshot starts on that owner's actual ASC/source life"),
                      Instance->StartWithInput(Definition, Input, Options, Error)))
        {
            return static_cast<UAbilityGraphInstance*>(nullptr);
        }
        return Instance;
    };
    if (!TestNotNull(TEXT("Frozen modified snapshot keeps its graph alive"), Old.Graph.Get()) ||
        !TestNotNull(TEXT("Untouched owner snapshot keeps its graph alive"), Plain.Graph.Get()))
    {
        return false;
    }
    auto* OneRun = Start(F.Source, Old.Graph);
    auto* TwoRun = Start(Other, Plain.Graph);
    if (!OneRun || !TwoRun)
    {
        if (OneRun)
        {
            OneRun->Cancel();
        }
        if (TwoRun)
        {
            TwoRun->Cancel();
        }
        return false;
    }
    ++GFrameCounter;
    OneRun->Advance(.1f);
    TwoRun->Advance(.1f);
    FPureMassView View;
    TestTrue(TEXT("Owner one's original target survives"), Mass->Resolve(F.Victim, View));
    TestEqual(TEXT("Old frozen field deals the original1 damage"), View.Health, 199.f);
    TestTrue(TEXT("Old frozen branch still applies actual petrify after its contribution was removed"),
             View.bPetrified);
    TestTrue(TEXT("Other owner's original target survives"), Mass->Resolve(OtherVictim, View));
    TestEqual(TEXT("Other field also deals its original1 damage"), View.Health, 199.f);
    TestFalse(TEXT("Other player's target receives no borrowed branch or parameter frame"), View.bPetrified);
    OneRun->Cancel();
    TwoRun->Cancel();
    return true;
}
#endif
