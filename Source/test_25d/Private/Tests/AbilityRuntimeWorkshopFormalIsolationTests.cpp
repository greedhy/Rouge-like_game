// 调用链：测试目录与正式授予 -> PlayerState ASC 激活两个真实前端 -> TargetData/Commit -> Mass 扣血。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityRuntimeRecipeFrontendTestTypes.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityRuntimeWorkshopFormalIsolationTests
{
    /** 两种测试前端只更换默认图与目标方式，实际施法、费用和效果使用共同的生产实现。 */
    UAbilityGraphDefinition* MakeBurstGraph(UObject* Outer, float DamageAmount)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Outer);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Query;
        Query.NodeId = FGuid::NewGuid();
        Query.TypeId = TEXT("Query.Shape");
        auto* Shape = NewObject<UAbilityQueryNodeConfig>(Graph);
        Shape->Query.Radius = 120.f;
        Query.Config = Shape;
        FAbilityGraphNode Damage;
        Damage.NodeId = FGuid::NewGuid();
        Damage.TypeId = TEXT("Effect.Damage");
        auto* Effect = NewObject<UAbilityDamageNodeConfig>(Graph);
        Effect->Damage = DamageAmount;
        Effect->DamagePerLevel = 0.f;
        Effect->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Damage.Config = Effect;
        Graph->Nodes = {Query, Damage};
        Graph->EntryNodeId = Query.NodeId;
        FAbilityGraphEdge Edge;
        Edge.From = Query.NodeId;
        Edge.To = Damage.NodeId;
        Edge.OutputPort = TEXT("TargetFound");
        Graph->Edges.Add(Edge);
        return Graph;
    }

    /** 真实 PlayerState 配方组件与远端 TargetData 分支，所有授予保留准确的 Spec 身份。 */
    struct FPlayer
    {
        AMyPlayerState* State = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;
        FGameplayAbilitySpecHandle FormalHandle;
        int16 NextPrediction = 2600;

        explicit FPlayer(UWorld& World, FVector Position, bool bGiveFormal = true)
        {
            State = World.SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            auto* Pawn = World.SpawnActor<ADungeonPlayerRunTestPawn>();
            Pawn->Tags = {TEXT("Player")};
            Pawn->SetActorLocation(Position);
            Pawn->AttachState(State);
            ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            ASC->AbilityActorInfo->PlayerController = World.SpawnActor<AAbilityGraphRemoteTestController>();
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
            if (bGiveFormal)
            {
                GiveFormal();
            }
        }

        void GiveFormal()
        {
            FormalHandle = ASC->GiveAbility(
                FGameplayAbilitySpec(UAbilityRuntimeRecipeFrontendTestAbility::StaticClass(), 1, INDEX_NONE, State));
        }

        ~FPlayer()
        {
            FString Error;
            Developer->AbandonPreview(Error);
            ASC->CancelAllAbilities();
        }

        /** 让两个真实服务器激活同时等目标，冻结时分别读取各自配方。 */
        UAbilityGraphTestAbility* Activate(FGameplayAbilitySpecHandle Handle)
        {
            const auto* Spec = ASC->FindAbilitySpecFromHandle(Handle);
            auto* Instance = Spec ? Cast<UAbilityGraphTestAbility>(Spec->GetPrimaryInstance()) : nullptr;
            if (!Instance)
            {
                return nullptr;
            }
            // 测试类的原生 NewObject 不复制运行期改写的普通 CDO 属性。为该实例显式配置相同的测试前端。
            const auto* Default = CastChecked<UAbilityGraphTestAbility>(Spec->Ability.Get());
            Instance->Graph = Default->Graph;
            Instance->TargetMode = Default->TargetMode;
            Instance->EditorGetAssetTags() = Default->GetAssetTags();
            FPredictionKey Key;
            Key.Current = ++NextPrediction;
            ASC->CallServerTryActivateAbility(Handle, false, Key);
            return Instance;
        }

        /** 通过正常 GAS 协议发送地面目标，随后仅移除本次施法增加的测试冷却。 */
        void SendTarget(UAbilityGraphTestAbility& Ability, FGameplayAbilitySpecHandle Handle, FVector Position)
        {
            const auto OldEffects = ASC->GetActiveEffects(FGameplayEffectQuery());
            auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.ImpactPoint = Position;
            Hit->HitResult.Item = INDEX_NONE;
            FGameplayAbilityTargetDataHandle Data;
            Data.Add(Hit);
            ASC->ServerSetReplicatedTargetData(Handle, Ability.GetCurrentActivationInfo().GetActivationPredictionKey(),
                                               Data, FGameplayTag(), FPredictionKey());
            Ability.StepGraph(0.f);
            for (const auto Effect : ASC->GetActiveEffects(FGameplayEffectQuery()))
            {
                if (!OldEffects.Contains(Effect))
                {
                    ASC->RemoveActiveGameplayEffect(Effect);
                }
            }
        }
    };

    enum class EExternalSource
    {
        None,
        ExistingBase,
        UpdatedDuringPreview,
        AddedDuringPreview,
        AbandonedWithLaterSource,
        WorkshopOnlyDependency,
        ExternalBeforeFormalGrant,
        ExternalBaseReplacement
    };

    /** 真实工作台新增节点可供预览使用，但不能成为正式来源，拒绝时不发布新版本。 */
    bool CheckExternalWorkshopDependency(FAutomationTestBase& Test, UAbilityRuntimeRecipeDeveloperComponent& Developer,
                                         FGameplayTag Tag, FString& Error)
    {
        const auto Original = Developer.GetDeveloperState();
        const auto* Query = Original.AcceptedGraph.Nodes.FindByPredicate(
            [](const FAbilityRuntimeGraphNodeView& Node)
            {
                return Node.TypeId == TEXT("Query.Shape");
            });
        if (!Test.TestNotNull(TEXT("Workshop exposes the real query node"), Query))
        {
            return false;
        }
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.DefinitionId = Original.SelectedId;
        Edit.ExpectedRevision = Original.Revision;
        Edit.ExpectedBaseHash = Original.BaseHash;
        Edit.ExpectedAcceptedHash = Original.AcceptedGraph.ContentHash;
        Edit.ExpectedPreviewSessionId = Original.PreviewSessionId;
        Edit.TargetNodeId = Query->NodeId;
        Edit.OutputPortId = TEXT("TargetFound");
        Edit.ModifierId = TEXT("Append.Registered");
        Edit.FragmentId = TEXT("Target.Petrify");
        Edit.Value = 1.f;
        if (!Test.TestTrue(TEXT("Draft adds a real temporary state node"), Developer.PreviewGraphEdit(Edit, Error)))
        {
            return false;
        }
        const FGuid DraftToken = Developer.GetDeveloperState().DraftToken;
        if (!Test.TestTrue(TEXT("Apply the trusted branch"),
                           Developer.ApplyGraphDraft(DraftToken, Original.Revision, Error)))
        {
            return false;
        }
        const auto Applied = Developer.GetDeveloperState();
        const auto* TempState = Applied.AcceptedGraph.Nodes.FindByPredicate(
            [](const FAbilityRuntimeGraphNodeView& Node)
            {
                return Node.TypeId == TEXT("Status.Petrify");
            });
        if (!Test.TestNotNull(TEXT("Accepted preview contains a newly installed state"), TempState))
        {
            return false;
        }
        FAbilityRuntimeRecipeContribution Outside;
        Outside.SourceId = TEXT("External.WorkshopOnlyTarget");
        Outside.Priority = MAX_int32;
        auto& Patch = Outside.Patches.AddDefaulted_GetRef();
        Patch.TargetNodeId = TempState->NodeId;
        Patch.Operation = EAbilityRecipePatchOperation::DurationScale;
        Patch.Scalar = 2.f;
        FAbilityRuntimeRecipeSnapshot FrozenPreview;
        UAbilityGraphDefinition* Compiled = nullptr;
        if (!Test.TestTrue(TEXT("Read the accepted preview graph"),
                           Developer.CreateCastSnapshot(Tag, &Developer, FrozenPreview, Error)) ||
            !Test.TestTrue(TEXT("The external edit is valid for the visible preview alone"),
                           AbilityRuntimeRecipes::Compile(&Developer, FrozenPreview.Graph, {Outside}, Compiled, Error)))
        {
            return false;
        }
        Test.TestFalse(TEXT("A production source cannot depend on an owned workshop node"),
                       Developer.SetContribution(Tag, Outside, Error));
        Test.TestFalse(TEXT("The rejected source describes its incompatibility"), Error.IsEmpty());
        Test.TestEqual(TEXT("Rejection keeps the accepted revision"), Developer.GetRevision(Tag), Applied.Revision);
        FAbilityRuntimeRecipeSnapshot Unchanged;
        if (Test.TestTrue(TEXT("Previously accepted preview snapshot still resolves"),
                          Developer.CreateCastSnapshot(Tag, &Developer, Unchanged, Error)))
        {
            Test.TestEqual(TEXT("The frozen graph has no external edit"), Unchanged.ContentHash,
                           FrozenPreview.ContentHash);
        }
        bool bHasRecipe = true;
        Test.TestFalse(TEXT("A different ability identity cannot borrow the formal recipe"),
                       Developer.CreateFormalCastSnapshot(Tag, GetDefault<UDungeonPlayerRunTestTaggedAbility>(),
                                                          &Developer, Unchanged, bHasRecipe, Error));
        Test.TestFalse(TEXT("Invalid source does not report an available formal recipe"), bHasRecipe);
        Test.TestNull(TEXT("Invalid source clears a previously filled snapshot"), Unchanged.Graph.Get());
        return true;
    }

    /** 开放类型选择让同一正式来源兼容两个测试前端，并可在预览期间经生产事务更新。 */
    bool SetExternalScale(FPlayer& Player, FGameplayTag Tag, float Scale, FString& Error)
    {
        FAbilityRuntimeRecipeContribution Contribution;
        Contribution.SourceId = TEXT("External.FormalBurst");
        FAbilityRuntimeRecipePatch Patch;
        Patch.TargetTypeId = TEXT("Effect.Damage");
        Patch.Operation = EAbilityRecipePatchOperation::DamageScale;
        Patch.Scalar = Scale;
        Contribution.Patches.Add(Patch);
        return Player.Developer->SetContribution(Tag, Contribution, Error);
    }

    /** 未注册正式基础时，用准确类测试预览关闭前后的外部来源校验。 */
    bool CheckExternalClose(FAutomationTestBase& Test, bool bReplaceBase)
    {
        FEditorScriptExecutionGuard ScriptGuard;
        FAbilityGraphTestFixture Fixture;
        Fixture.Target->Destroy();
        const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Prototype.Burst"));
        auto* FormalDefault = GetMutableDefault<UAbilityRuntimeRecipeFrontendTestAbility>();
        auto* PreviewDefault = GetMutableDefault<UAbilityGraphTestAbility>();
        const TGuardValue<TObjectPtr<UAbilityGraphDefinition>> FormalGraph(FormalDefault->Graph,
                                                                           MakeBurstGraph(Fixture.World, 10.f));
        const TGuardValue<TObjectPtr<UAbilityGraphDefinition>> PreviewGraph(PreviewDefault->Graph,
                                                                            MakeBurstGraph(Fixture.World, 30.f));
        const TGuardValue<EUnifiedAbilityTargetMode> FormalMode(FormalDefault->TargetMode,
                                                                EUnifiedAbilityTargetMode::Ground);
        const TGuardValue<FGameplayTagContainer> PreviewTags(PreviewDefault->EditorGetAssetTags(),
                                                             FGameplayTagContainer(Tag));
        FPlayer Player(*Fixture.World, FVector(0, -150, 0), !bReplaceBase);
        FGameplayAbilitySpecHandle SecondFormal;
        if (!bReplaceBase)
        {
            SecondFormal = Player.ASC->GiveAbility(
                FGameplayAbilitySpec(UAbilityGraphTestAbility::StaticClass(), 1, INDEX_NONE, Player.State));
            if (!Test.TestTrue(TEXT("Two same-tag formal variants are real grants on the same ASC"),
                               Player.FormalHandle.IsValid() && SecondFormal.IsValid()))
            {
                return false;
            }
        }
        auto& Developer = *Player.Developer;
        auto* Directory = NewObject<UAbilityInfo>(Fixture.World);
        auto& Row = Directory->AbilityInformation.AddDefaulted_GetRef();
        Row.AbilityTag = Tag;
        Row.Ability = UAbilityGraphTestAbility::StaticClass();
        Row.AbilityType = FMyGameplayTags::Get().Abilities_Type_Offensive;
        FString Error;
        if (!Test.TestTrue(TEXT("Load same-tag test class through the catalog"),
                           Developer.InitializeCatalog(Directory, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        const auto* Definition = Developer.GetDefinitions().FindByPredicate(
            [](const FAbilityRuntimeDeveloperDefinition& Value)
            {
                return Value.Base.AbilityClass.Get() == UAbilityGraphTestAbility::StaticClass();
            });
        if (!Test.TestNotNull(TEXT("Find the accurate temporary class"), Definition))
        {
            return false;
        }
        Test.TestEqual(TEXT("No formal base was registered before opening"), Developer.GetRevision(Tag), 0);
        const FGuid Session = FGuid::NewGuid();
        Developer.ServerBeginGraphPreview(Definition->DefinitionId, Session);
        if (!Test.TestTrue(TEXT("Graph session opens via the exact request"),
                           Developer.GetDeveloperState().bPreviewActive))
        {
            return false;
        }
        const auto Temporary = Developer.GetPreviewHandle();
        if (!bReplaceBase)
        {
            const auto Revision = Developer.GetRevision(Tag);
            Test.TestFalse(TEXT("External rule is deferred when close has no unique formal base"),
                           SetExternalScale(Player, Tag, 3.f, Error));
            Test.TestFalse(TEXT("Ambiguous formal bases produce a reason"), Error.IsEmpty());
            Test.TestEqual(TEXT("Ambiguity changes no accepted revision"), Developer.GetRevision(Tag), Revision);
            Developer.ServerCloseGraphPreview(FGuid::NewGuid(), Session, false);
            Test.TestFalse(TEXT("Rejecting the external rule leaves normal close available"),
                           Developer.GetDeveloperState().bPreviewActive);
            Test.TestNull(TEXT("Normal close removes only the temporary Spec"),
                          Player.ASC->FindAbilitySpecFromHandle(Temporary));
            Test.TestNotNull(TEXT("Original formal class remains granted"),
                             Player.ASC->FindAbilitySpecFromHandle(Player.FormalHandle));
            Test.TestNotNull(TEXT("Other permanent class remains granted"),
                             Player.ASC->FindAbilitySpecFromHandle(SecondFormal));
            Test.TestEqual(TEXT("No recipe remains after close"), Developer.GetRevision(Tag), 0);
            return true;
        }

        const auto& State = Developer.GetDeveloperState();
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.DefinitionId = State.SelectedId;
        Edit.ExpectedRevision = State.Revision;
        Edit.ExpectedBaseHash = State.BaseHash;
        Edit.NodeIndex = Definition->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Effect.Damage");
            });
        Edit.ModifierId = TEXT("Scale.Damage");
        Edit.Value = 2.f;
        if (!Test.TestTrue(TEXT("The preview owns a damage modifier"), Developer.ApplyEdit(Edit, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        auto* ExternalBase = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, Fixture.World);
        CastChecked<UAbilityDamageNodeConfig>(ExternalBase->Nodes[Edit.NodeIndex].Config)->Damage = 20.f;
        if (!Test.TestTrue(TEXT("A real external base replaces the preview base without a formal grant"),
                           Developer.RegisterBase(Tag, ExternalBase, Error)) ||
            !Test.TestTrue(TEXT("The external base accepts a compatible external rule"),
                           SetExternalScale(Player, Tag, 3.f, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        Developer.ServerCloseGraphPreview(FGuid::NewGuid(), Session, true);
        Test.TestFalse(TEXT("Abandon can release the conflicted preview"),
                       Developer.GetDeveloperState().bPreviewActive);
        Test.TestNull(TEXT("Abandon releases the actual temporary Spec"),
                      Player.ASC->FindAbilitySpecFromHandle(Temporary));
        Player.GiveFormal();
        auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
        Type->Health = 300.f;
        Type->XPReward = 0;
        const FVector Target(600, 0, 0);
        const int32 Victim = Mass->SpawnMonster(Type, Target);
        auto* Formal = Player.Activate(Player.FormalHandle);
        if (!Test.TestTrue(TEXT("Formal class awaits actual TargetData after abandon"),
                           Formal && Formal->IsActive() && Formal->HasPendingGraphWork()))
        {
            return false;
        }
        Player.SendTarget(*Formal, Player.FormalHandle, Target);
        FPureMassView View;
        if (Test.TestTrue(TEXT("Resolve actual hit victim"), Mass->Resolve(Victim, View)))
        {
            Test.TestEqual(TEXT("External base and x3 rule survive while preview x2 is removed"), View.Health, 240.f);
        }
        return true;
    }

    /** 正式配方尚未注册时，图面板不能让外部来源绑定到临时类的专用节点。 */
    void CheckExternalBeforeFormalGrant(FAutomationTestBase& Test, UAbilityRuntimeRecipeDeveloperComponent& Developer,
                                        const FAbilityRuntimeDeveloperDefinition& Definition, FString& Error)
    {
        const auto Tag = Definition.Base.AbilityTag;
        const auto Revision = Developer.GetRevision(Tag);
        FAbilityRuntimeRecipeSnapshot Before;
        if (!Test.TestTrue(TEXT("Read preview snapshot before external write"),
                           Developer.CreateCastSnapshot(Tag, &Developer, Before, Error)))
        {
            return;
        }
        const auto* Damage = Definition.Base.DefaultGraph->Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Effect.Damage");
            });
        if (!Test.TestNotNull(TEXT("The preview class default has its own distinct damage node"), Damage))
        {
            return;
        }
        FAbilityRuntimeRecipeContribution Source;
        Source.SourceId = TEXT("Outside.BeforeFormalGrant");
        auto& Patch = Source.Patches.AddDefaulted_GetRef();
        Patch.TargetNodeId = Damage->NodeId;
        Patch.Operation = EAbilityRecipePatchOperation::DamageScale;
        Patch.Scalar = 3.f;
        UAbilityGraphDefinition* Compiled = nullptr;
        Test.TestTrue(TEXT("Preview class alone accepts the external write"),
                      AbilityRuntimeRecipes::Compile(&Developer, Before.Graph.Get(), {Source}, Compiled, Error));
        Test.TestFalse(TEXT("Before any formal grant, a graph session defers external writes"),
                       Developer.SetContribution(Tag, Source, Error));
        Test.TestFalse(TEXT("Deferred external write supplies a diagnostic"), Error.IsEmpty());
        Test.TestEqual(TEXT("Deferred external write leaves revision unchanged"), Developer.GetRevision(Tag), Revision);
        FAbilityRuntimeRecipeSnapshot After;
        if (Test.TestTrue(TEXT("Read preview snapshot after external rejection"),
                          Developer.CreateCastSnapshot(Tag, &Developer, After, Error)))
        {
            Test.TestEqual(TEXT("Deferred external write changes no preview data"), After.ContentHash,
                           Before.ContentHash);
        }
    }

    /** 正式和临时类具有相同身份及不同默认伤害，每次施法命中自己的 Mass 目标。 */
    bool CheckIsolation(FAutomationTestBase& Test, EExternalSource External, float ExpectedFormalHealth,
                        float ExpectedPreviewHealth)
    {
        FEditorScriptExecutionGuard ScriptGuard;
        FAbilityGraphTestFixture Fixture;
        Fixture.Target->Destroy();
        const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Prototype.Burst"));
        auto* FormalDefault = GetMutableDefault<UAbilityRuntimeRecipeFrontendTestAbility>();
        auto* PreviewDefault = GetMutableDefault<UAbilityGraphTestAbility>();
        const TGuardValue<TObjectPtr<UAbilityGraphDefinition>> FormalGraph(FormalDefault->Graph,
                                                                           MakeBurstGraph(Fixture.World, 10.f));
        const TGuardValue<TObjectPtr<UAbilityGraphDefinition>> PreviewGraph(PreviewDefault->Graph,
                                                                            MakeBurstGraph(Fixture.World, 30.f));
        const TGuardValue<EUnifiedAbilityTargetMode> FormalMode(FormalDefault->TargetMode,
                                                                EUnifiedAbilityTargetMode::Ground);
        const TGuardValue<EUnifiedAbilityTargetMode> PreviewMode(PreviewDefault->TargetMode,
                                                                 EUnifiedAbilityTargetMode::Ground);
        const TGuardValue<FGameplayTagContainer> PreviewTags(PreviewDefault->EditorGetAssetTags(),
                                                             FGameplayTagContainer(Tag));
        const bool bLateGrant = External == EExternalSource::ExternalBeforeFormalGrant;
        FPlayer Player(*Fixture.World, FVector(0, -150, 0), !bLateGrant);
        FPlayer Other(*Fixture.World, FVector(0, 450, 0));
        auto* Directory = NewObject<UAbilityInfo>(Fixture.World);
        auto& Row = Directory->AbilityInformation.AddDefaulted_GetRef();
        Row.AbilityTag = Tag;
        Row.Ability = UAbilityGraphTestAbility::StaticClass();
        Row.AbilityType = FMyGameplayTags::Get().Abilities_Type_Offensive;
        FString Error;
        const bool bUsePriorBase = External == EExternalSource::ExistingBase ||
                                   External == EExternalSource::UpdatedDuringPreview ||
                                   External == EExternalSource::ExternalBaseReplacement;
        if (bUsePriorBase && (!Player.Developer->RegisterBase(Tag, MakeBurstGraph(Fixture.World, 15.f), Error) ||
                              !SetExternalScale(Player, Tag, 2.f, Error)))
        {
            Test.AddError(Error);
            return false;
        }
        if (!Test.TestTrue(TEXT("Register test frontend through the trusted catalog"),
                           Player.Developer->InitializeCatalog(Directory, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        const auto* Definition = Player.Developer->GetDefinitions().FindByPredicate(
            [](const FAbilityRuntimeDeveloperDefinition& Value)
            {
                return Value.Base.AbilityClass.Get() == UAbilityGraphTestAbility::StaticClass();
            });
        if (!Test.TestNotNull(TEXT("Trusted catalog exports the selected test class"), Definition))
        {
            return false;
        }
        if (External == EExternalSource::None)
        {
            const FGuid Session = FGuid::NewGuid();
            Test.TestEqual(TEXT("Giving a formal Spec does not register a recipe"), Player.Developer->GetRevision(Tag),
                           0);
            Player.Developer->ServerBeginGraphPreview(Definition->DefinitionId, Session);
            Test.TestTrue(TEXT("Exact graph session opens without edits"),
                          Player.Developer->GetDeveloperState().bPreviewActive);
            Player.Developer->ServerCloseGraphPreview(FGuid::NewGuid(), Session, false);
            Test.TestFalse(TEXT("Unmodified graph session closes"),
                           Player.Developer->GetDeveloperState().bPreviewActive);
            Test.TestEqual(TEXT("Untouched open/close leaves no formal recipe revision"),
                           Player.Developer->GetRevision(Tag), 0);
        }
        if (bLateGrant)
        {
            Player.Developer->ServerBeginGraphPreview(Definition->DefinitionId, FGuid::NewGuid());
            if (!Test.TestTrue(TEXT("Exact graph session begins before a formal grant"),
                               Player.Developer->GetDeveloperState().bPreviewActive))
            {
                return false;
            }
            CheckExternalBeforeFormalGrant(Test, *Player.Developer, *Definition, Error);
            Player.GiveFormal();
        }
        else if (!Test.TestTrue(TEXT("Begin preview creates its own grant"),
                                Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        const auto& State = Player.Developer->GetDeveloperState();
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.DefinitionId = State.SelectedId;
        Edit.ExpectedRevision = State.Revision;
        Edit.ExpectedBaseHash = State.BaseHash;
        Edit.NodeIndex = Definition->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Effect.Damage");
            });
        Edit.ModifierId = TEXT("Scale.Damage");
        Edit.Value = 2.f;
        const bool bSetLaterSource = External == EExternalSource::AddedDuringPreview ||
                                     External == EExternalSource::AbandonedWithLaterSource ||
                                     External == EExternalSource::UpdatedDuringPreview;
        if (!Test.TestTrue(TEXT("Apply an actual preview-owned damage edit"),
                           Player.Developer->ApplyEdit(Edit, Error)) ||
            (bSetLaterSource &&
             !Test.TestTrue(TEXT("Accept the later external source"), SetExternalScale(Player, Tag, 3.f, Error))))
        {
            Test.AddInfo(Error);
            return false;
        }
        const bool bCheckWorkshopDependency = External == EExternalSource::WorkshopOnlyDependency;
        if (bCheckWorkshopDependency && !CheckExternalWorkshopDependency(Test, *Player.Developer, Tag, Error))
        {
            Test.AddInfo(Error);
            return false;
        }
        if (External == EExternalSource::ExternalBaseReplacement)
        {
            // 外部基础保留节点身份，使既有临时补丁仍有效，正式快照只使用该外部基础和外部倍数。
            auto* ExternalGraph =
                DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, Fixture.World);
            CastChecked<UAbilityDamageNodeConfig>(ExternalGraph->Nodes[Edit.NodeIndex].Config)->Damage = 20.f;
            if (!Test.TestTrue(TEXT("External system registers a different compatible base"),
                               Player.Developer->RegisterBase(Tag, ExternalGraph, Error)))
            {
                Test.AddInfo(Error);
                return false;
            }
        }

        auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
        Type->Health = 300.f;
        Type->XPReward = 0;
        const FVector FormalTarget(600, 0, 0);
        const FVector PreviewTarget(600, -350, 0);
        const FVector OtherTarget(600, 450, 0);
        const int32 FormalVictim = Mass->SpawnMonster(Type, FormalTarget);
        const int32 PreviewVictim = Mass->SpawnMonster(Type, PreviewTarget);
        const int32 OtherVictim = Mass->SpawnMonster(Type, OtherTarget);
        const auto Health = [Mass](int32 Victim)
        {
            FPureMassView View;
            return Mass->Resolve(Victim, View) ? View.Health : 0.f;
        };
        const auto PreviewHandle = Player.Developer->GetPreviewHandle();
        auto* Preview = Player.Activate(PreviewHandle);
        auto* Formal = Player.Activate(Player.FormalHandle);
        auto* OtherFormal = Other.Activate(Other.FormalHandle);
        const auto IsWaiting = [&Test](const TCHAR* Name, const UAbilityGraphTestAbility* Ability)
        {
            const bool bWaiting = Ability && Ability->IsActive() && Ability->HasPendingGraphWork();
            FString Diagnostic(Name);
            if (Ability && !Ability->GetRuntimeRecipeError().IsEmpty())
            {
                Diagnostic += TEXT(" - ") + Ability->GetRuntimeRecipeError();
            }
            return Test.TestTrue(Diagnostic, bWaiting);
        };
        const bool bPreviewWaiting = IsWaiting(TEXT("Temporary Spec awaits real TargetData"), Preview);
        const bool bFormalWaiting = IsWaiting(TEXT("Same-tag formal Spec concurrently awaits real TargetData"), Formal);
        const bool bOtherWaiting = IsWaiting(TEXT("Other player's formal Spec awaits real TargetData"), OtherFormal);
        if (!bPreviewWaiting || !bFormalWaiting || !bOtherWaiting)
        {
            return false;
        }
        Test.TestTrue(TEXT("Selected test variant alone freezes an owner lease"),
                      Preview->GetRuntimeExecutionOwnerLease().IsValid() &&
                          !Formal->GetRuntimeExecutionOwnerLease().IsValid());
        const bool bExpectedFormalRecipe = bUsePriorBase || bSetLaterSource;
        Test.TestEqual(TEXT("Formal path freezes a graph only when it has an external contribution"),
                       Formal->GetRuntimeRecipeSnapshot().Graph != nullptr, bExpectedFormalRecipe);
        Player.SendTarget(*Preview, PreviewHandle, PreviewTarget);
        Player.SendTarget(*Formal, Player.FormalHandle, FormalTarget);
        Other.SendTarget(*OtherFormal, Other.FormalHandle, OtherTarget);
        Test.TestEqual(TEXT("Edited temporary variant applies its own damage"), Health(PreviewVictim),
                       ExpectedPreviewHealth);
        Test.TestEqual(TEXT("Same-tag formal cast applies its own base and current external damage"),
                       Health(FormalVictim), ExpectedFormalHealth);
        Test.TestEqual(TEXT("Other player's formal cast retains its original damage"), Health(OtherVictim), 290.f);

        const bool bAbandon = External == EExternalSource::ExternalBaseReplacement ||
                              External == EExternalSource::AbandonedWithLaterSource;
        const bool bClosed = bAbandon ? Player.Developer->AbandonPreview(Error) : Player.Developer->EndPreview(Error);
        Test.TestTrue(TEXT("Closing the preview releases its own grant"), bClosed);
        Test.TestNotNull(TEXT("Formal Spec still exists after preview close"),
                         Player.ASC->FindAbilitySpecFromHandle(Player.FormalHandle));
        if (bLateGrant)
        {
            Test.TestEqual(TEXT("Closing after late formal grant removes the temporary recipe"),
                           Player.Developer->GetRevision(Tag), 0);
        }
        if (bSetLaterSource)
        {
            auto* AfterClose = Player.Activate(Player.FormalHandle);
            if (Test.TestTrue(TEXT("Formal frontend still activates after close"),
                              AfterClose && AfterClose->IsActive()))
            {
                Player.SendTarget(*AfterClose, Player.FormalHandle, FormalTarget);
                Test.TestEqual(TEXT("Closing preserves the later source on the formal base"), Health(FormalVictim),
                               External == EExternalSource::UpdatedDuringPreview ? 210.f : 240.f);
            }
        }
        if (bCheckWorkshopDependency)
        {
            Test.TestEqual(TEXT("Closing removes the preview-only base"), Player.Developer->GetRevision(Tag), 0);
            const auto* FormalBase = AbilityRuntimeRecipes::BuildDefaultGraph(Fixture.World, FormalDefault, Error);
            if (!Test.TestNotNull(TEXT("The actual formal class exports its own base"), FormalBase) ||
                !Test.TestTrue(TEXT("Production registers that formal base before applying a source"),
                               Player.Developer->RegisterBase(Tag, FormalBase, Error)) ||
                !Test.TestTrue(TEXT("A compatible external edit can be applied in the formal context"),
                               SetExternalScale(Player, Tag, 2.f, Error)))
            {
                Test.AddInfo(Error);
                return false;
            }
            auto* AfterClose = Player.Activate(Player.FormalHandle);
            if (Test.TestTrue(TEXT("Formal cast after rejected dependency and successful close remains usable"),
                              AfterClose && AfterClose->IsActive()))
            {
                Player.SendTarget(*AfterClose, Player.FormalHandle, FormalTarget);
                Test.TestEqual(TEXT("Only the later compatible edit changes damage"), Health(FormalVictim), 270.f);
            }
        }
        return true;
    }
} // namespace AbilityRuntimeWorkshopFormalIsolationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopPreviewWithoutFormalContribution,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.OriginalAndOtherOwner",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopPreviewWithoutFormalContribution::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckIsolation(*this, EExternalSource::None, 290.f, 240.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopPreviewWithPriorFormalContribution,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.ExistingBaseAndSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopPreviewWithPriorFormalContribution::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckIsolation(*this, EExternalSource::ExistingBase, 270.f, 180.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopPreviewWithUpdatedFormalContribution,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.PreservesUpdatedSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopPreviewWithUpdatedFormalContribution::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckIsolation(*this, EExternalSource::UpdatedDuringPreview, 255.f, 120.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopPreviewWithLaterFormalContribution,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.LaterSourceUsesFormalClass",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopPreviewWithLaterFormalContribution::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckIsolation(*this, EExternalSource::AddedDuringPreview, 270.f, 120.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopAbandonPreservesFormalVariant,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.AbandonRestoresFormalClass",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopAbandonPreservesFormalVariant::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckIsolation(*this, EExternalSource::AbandonedWithLaterSource, 270.f, 120.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopRejectsExternalDependencyOnPreviewNode,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.RejectsWorkshopOnlyExternalSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopRejectsExternalDependencyOnPreviewNode::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckIsolation(*this, EExternalSource::WorkshopOnlyDependency, 290.f, 240.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopDefersExternalUntilFormalGrant,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.DefersExternalBeforeFormalGrant",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopDefersExternalUntilFormalGrant::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckIsolation(*this, EExternalSource::ExternalBeforeFormalGrant, 290.f, 240.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopPreviewWithExternalBaseReplacement,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.UsesExternalReplacementBase",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopPreviewWithExternalBaseReplacement::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckIsolation(*this, EExternalSource::ExternalBaseReplacement, 260.f, 220.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWorkshopDefersExternalForAmbiguousFormalClasses,
    "test_25d.DynamicMage.Workshop.FormalIsolation.DefersExternalForAmbiguousFormalClasses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopDefersExternalForAmbiguousFormalClasses::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckExternalClose(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopRetainsExternalBaseWithoutFormalGrant,
                                 "test_25d.DynamicMage.Workshop.FormalIsolation.RetainsExternalBaseWithoutFormalGrant",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopRetainsExternalBaseWithoutFormalGrant::RunTest(const FString&)
{
    using namespace AbilityRuntimeWorkshopFormalIsolationTests;
    return CheckExternalClose(*this, true);
}
#endif
