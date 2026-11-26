// 调用链：Automation -> 真实 Mage 目录 -> 开发组件可信编辑入口 -> 原 ASC Spec/配方快照。
// 覆盖目录遗漏、非法编辑污染、错误回收与玩家串配方；不把 NullRHI 测试当作 UMG/动画视觉验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "AbilitySystem/Unified/MageGraphAbility.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeGraphView.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Terrain/TerrainSurface.h"
#include "MassEntitySubsystem.h"
#include "Player/MyPlayerState.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "MyGameplayTags.h"
#include <limits>

namespace AbilityRuntimeRecipePanelTests
{
    /** 服务器玩家状态与 ASC 均为正式类型；不激活 AI/HUD，也不写任何目录或存档。 */
    struct FPlayer
    {
        AMyPlayerState* State = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;

        explicit FPlayer(FAbilityGraphTestFixture& F)
        {
            State = F.World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            ASC->InitAbilityActorInfo(State, F.Source);
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
            if (!Developer)
            {
                Developer = NewObject<UAbilityRuntimeRecipeDeveloperComponent>(State);
                State->AddInstanceComponent(Developer);
                Developer->RegisterComponent();
            }
        }
    };

    /** 选用实际石化的查询/状态默认定义，开发工具测试不通过手工 RegisterBase 掩盖目录导出问题。 */
    const FAbilityRuntimeDeveloperDefinition* Petrify(const UAbilityRuntimeRecipeDeveloperComponent& Component)
    {
        return Component.GetDefinitions().FindByPredicate(
            [](const FAbilityRuntimeDeveloperDefinition& Item)
            {
                return GetPathNameSafe(Item.Base.AbilityClass.Get()) ==
                       TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C");
            });
    }

    /** 编辑请求使用客户端实际可见的基础摘要及当前版本，服务器仍须逐项重新校验。 */
    FAbilityRuntimeDeveloperEdit RadiusEdit(const UAbilityRuntimeRecipeDeveloperComponent& Component, float Scale)
    {
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.DefinitionId = Component.GetDeveloperState().SelectedId;
        Edit.ModifierId = TEXT("Scale.Radius");
        Edit.NodeIndex = 0;
        Edit.Value = Scale;
        Edit.ExpectedRevision = Component.GetDeveloperState().Revision;
        Edit.ExpectedBaseHash = Component.GetDeveloperState().BaseHash;
        return Edit;
    }

    /** 读取真正下游配置，不以 UI 的 Value 字段代替编译结果。 */
    float Radius(const UAbilityGraphDefinition* Graph)
    {
        if (Graph)
        {
            for (const auto& Node : Graph->Nodes)
            {
                if (const auto* Config = Cast<UAbilityPetrifyAreaQueryConfig>(Node.Config))
                {
                    return Config->Radius;
                }
            }
        }
        return -1.f;
    }

    /** 外部来源走同一正式配方 API；用于确认关闭面板不会回滚别的系统后续写入。 */
    FAbilityRuntimeRecipeContribution ExternalRadius(FName Source, float Scale)
    {
        FAbilityRuntimeRecipeContribution Result;
        Result.SourceId = Source;
        FAbilityRuntimeRecipePatch Patch;
        Patch.Operation = EAbilityRecipePatchOperation::RadiusScale;
        Patch.TargetTypeId = TEXT("Query.PetrifyArea");
        Patch.Scalar = Scale;
        Result.Patches.Add(Patch);
        return Result;
    }
} // namespace AbilityRuntimeRecipePanelTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerCards, "test_25d.UnifiedGA.DynamicMage.PlayerCards.FireBolt",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerCards::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    AbilityRuntimeRecipePanelTests::FPlayer Player(Fixture);
    auto& Developer = *Player.Developer;
    FString Error;
    if (!TestTrue(TEXT("Load the real Mage catalog"), Developer.InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Developer.GetDefinitions().FindByPredicate(
        [](const FAbilityRuntimeDeveloperDefinition& Item)
        {
            return Item.Base.AbilityTag.ToString() == TEXT("Abilities.Fire.FireBolt") && Item.Base.bHasDefaultGraph;
        });
    if (!TestNotNull(TEXT("Real FireBolt definition"), Definition))
    {
        return false;
    }
    const auto Session = FGuid::NewGuid();
    Developer.ServerBeginCardPreview(Definition->DefinitionId, Session);
    const auto& State = Developer.GetDeveloperState();
    TestTrue(TEXT("Card session opened"), State.bCardPanelSession);
    TestTrue(TEXT("No graph in the player projection"), State.AcceptedGraph.Nodes.IsEmpty());
    const int32 OriginalRevision = State.Revision;
    for (const FName Card :
         {FName(TEXT("Chain")), FName(TEXT("WeakestJump")), FName(TEXT("Ice")), FName(TEXT("Fork"))})
    {
        if (!TestTrue(*Card.ToString(), Developer.PreviewCard(Card, Session, State.Revision, Error)))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("Draft has no technical nodes"), State.DraftGraph.Nodes.IsEmpty());
        if (!TestTrue(TEXT("Commit uses the validated draft token"),
                      Developer.ApplyGraphDraft(State.DraftToken, State.Revision, Error)))
        {
            AddError(Error);
            return false;
        }
    }
    TestFalse(TEXT("Stale selection cannot replace the accepted build"),
              Developer.PreviewCard(TEXT("Chain"), Session, OriginalRevision, Error));
    FAbilityRuntimeRecipeSnapshot Snapshot;
    if (!TestTrue(TEXT("Read the next actual cast"),
                  Developer.CreateCastSnapshot(State.AbilityTag, &Developer, Snapshot, Error)))
    {
        return false;
    }
    TestTrue(TEXT("Fork replaces sequential delivery"), Snapshot.Graph->Nodes.ContainsByPredicate(
        [](const FAbilityGraphNode& Node) { return Node.TypeId == TEXT("Propagation.Forked"); }));
    TestFalse(TEXT("Prior delivery does not remain"), Snapshot.Graph->Nodes.ContainsByPredicate(
        [](const FAbilityGraphNode& Node) { return Node.TypeId == TEXT("Propagation.Sequential"); }));
    TestTrue(TEXT("Ice survives the unrelated propagation replacement"), Snapshot.Graph->Nodes.ContainsByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            const auto* Damage = Cast<UAbilityInputDamageNodeConfig>(Node.Config);
            return Damage && Damage->ElementOverride == EAbilityElement::Ice;
        }));
    const auto* Fork = Snapshot.Graph->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Propagation.Forked");
        });
    const auto* Propagation = Fork ? Cast<UAbilityPropagationNodeConfig>(Fork->Config) : nullptr;
    if (!TestTrue(TEXT("Changing propagation preserves the installed target rule"),
                  Propagation && Propagation->TargetSelection == EAbilityTargetSelectionPolicy::LowestHealthRatio))
    {
        return false;
    }
    const int32 WithTargeting = State.Revision;
    Developer.ServerRemoveCard(TEXT("Fork"), Session, WithTargeting);
    TestEqual(TEXT("Cannot leave a targeting card without its propagation"), State.Revision, WithTargeting);
    Developer.ServerRemoveCard(TEXT("WeakestJump"), Session, State.Revision);
    if (!TestTrue(TEXT("Read the cast after removing the standalone target rule"),
                  Developer.CreateCastSnapshot(State.AbilityTag, &Developer, Snapshot, Error)))
    {
        return false;
    }
    Fork = Snapshot.Graph->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Propagation.Forked");
        });
    Propagation = Fork ? Cast<UAbilityPropagationNodeConfig>(Fork->Config) : nullptr;
    TestTrue(TEXT("Replacement does not retain a targeting rule that was removed"),
             Propagation && Propagation->TargetSelection == EAbilityTargetSelectionPolicy::Original);
    const int32 ForkRevision = State.Revision;
    Developer.ServerRemoveCard(TEXT("Ice"), Session, ForkRevision);
    TestTrue(TEXT("Individual removal uses the same recipe transaction"), State.Revision > ForkRevision);
    TestTrue(TEXT("The propagation remains installed"), State.Cards.ContainsByPredicate(
        [](const FAbilityRuntimeWorkshopCardView& Card)
        {
            return Card.Id == TEXT("Fork") && Card.bInstalled;
        }));
    TestTrue(TEXT("Release the temporary spec"), Developer.EndPreview(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerExpiryCard, "test_25d.UnifiedGA.DynamicMage.PlayerCards.FieldExpiry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerExpiryCard::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    AbilityRuntimeRecipePanelTests::FPlayer Player(Fixture);
    auto& Developer = *Player.Developer;
    FString Error;
    if (!TestTrue(TEXT("Load real definitions"), Developer.InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Developer.GetDefinitions().FindByPredicate(
        [](const FAbilityRuntimeDeveloperDefinition& Item)
        {
            return Item.Base.AbilityTag.ToString() == TEXT("Abilities.Composition.StormSeal") && Item.Base.bHasDefaultGraph;
        });
    if (!TestNotNull(TEXT("Real Storm Seal definition"), Definition))
    {
        return false;
    }
    const auto Session = FGuid::NewGuid();
    Developer.ServerBeginCardPreview(Definition->DefinitionId, Session);
    const auto& State = Developer.GetDeveloperState();
    if (!TestTrue(TEXT("A finite blast can attach to the real field expiry"),
                  Developer.PreviewCard(TEXT("EndBurst"), Session, State.Revision, Error)) ||
        !TestTrue(TEXT("Commit the exact expiry branch"),
                  Developer.ApplyGraphDraft(State.DraftToken, State.Revision, Error)))
    {
        AddError(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot Snapshot;
    if (!TestTrue(TEXT("Read the actual cast"), Developer.CreateCastSnapshot(State.AbilityTag, &Developer, Snapshot, Error)))
    {
        return false;
    }
    const auto* Field = Snapshot.Graph->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Control.FieldCadence");
        });
    if (!TestNotNull(TEXT("Retain the original time-based field"), Field))
    {
        return false;
    }
    TestTrue(TEXT("The real expiry port now sends a finite sphere query"), Snapshot.Graph->Edges.ContainsByPredicate(
        [&Snapshot, Field](const FAbilityGraphEdge& Edge)
        {
            return Edge.From == Field->NodeId && Edge.OutputPort == TEXT("Expired") &&
                   Snapshot.Graph->Nodes.ContainsByPredicate([&Edge](const FAbilityGraphNode& Node)
                   {
                       return Node.NodeId == Edge.To && Node.TypeId == TEXT("Query.Shape");
                   });
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerCardCatalog, "test_25d.UnifiedGA.DynamicMage.PlayerCards.Catalog",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerCardCatalog::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    AbilityRuntimeRecipePanelTests::FPlayer Player(Fixture);
    auto& Developer = *Player.Developer;
    FString Error;
    if (!TestTrue(TEXT("Load real definitions"), Developer.InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    TSet<FGameplayTag> Skills;
    TSet<FName> CompiledCards;
    for (const auto& Definition : Developer.GetDefinitions())
    {
        if (!Definition.Base.bSelectable || !Definition.Base.bHasDefaultGraph ||
            Developer.GetCardAbilityLabel(Definition.Base.AbilityTag).IsEmpty())
        {
            continue;
        }
        Skills.Add(Definition.Base.AbilityTag);
        const FGuid Session = FGuid::NewGuid();
        Developer.ServerBeginCardPreview(Definition.DefinitionId, Session);
        const auto& State = Developer.GetDeveloperState();
        auto Candidates = State.Cards;
        if (!TestTrue(TEXT("Every published skill has at least one selectable card"), !Candidates.IsEmpty()))
        {
            return false;
        }
        for (const auto& Card : Candidates)
        {
            const FString Name = Definition.Base.AbilityTag.ToString() + TEXT(" / ") + Card.Id.ToString();
            if (!TestTrue(*Name, Developer.PreviewCard(Card.Id, Session, State.Revision, Error)))
            {
                AddError(Error);
                continue;
            }
            CompiledCards.Add(Card.Id);
        }
        if (Definition.Base.AbilityTag.ToString() == TEXT("Abilities.Fire.FireBolt"))
        {
            if (!TestTrue(TEXT("Install the parent propagation card"),
                          Developer.PreviewCard(TEXT("Chain"), Session, State.Revision, Error) &&
                          Developer.ApplyGraphDraft(State.DraftToken, State.Revision, Error)))
            {
                AddError(Error);
                return false;
            }
            for (const FName Card : {FName(TEXT("WeakestJump")), FName(TEXT("FarthestJump"))})
            {
                if (TestTrue(*Card.ToString(), Developer.PreviewCard(Card, Session, State.Revision, Error)))
                {
                    CompiledCards.Add(Card);
                }
            }
        }
        if (!TestTrue(TEXT("Close before selecting the next skill"), Developer.EndPreview(Error)))
        {
            return false;
        }
    }
    TestEqual(TEXT("Seventy-one distinct skills"), Skills.Num(), 71);
    TestEqual(TEXT("One hundred eleven distinct server-compiled cards"), CompiledCards.Num(), 111);
    return true;
}

namespace AbilityRuntimeRecipePanelTests
{
    /** 从真实目录开始试装，依次提交独立卡片，原试装输入和权威校验继续生效。 */
    bool InstallCards(UAbilityRuntimeRecipeDeveloperComponent& Developer,
                      const FAbilityRuntimeDeveloperDefinition* Definition, TArrayView<const FName> Cards,
                      FString& Error)
    {
        if (!Definition)
        {
            Error = TEXT("Missing the real catalog definition. ") + Error;
            return false;
        }
        const FGuid Session = FGuid::NewGuid();
        Developer.ServerBeginCardPreview(Definition->DefinitionId, Session);
        const auto& State = Developer.GetDeveloperState();
        for (const FName Card : Cards)
        {
            if (!Developer.PreviewCard(Card, Session, State.Revision, Error) ||
                !Developer.ApplyGraphDraft(State.DraftToken, State.Revision, Error))
            {
                Error = Card.ToString() + TEXT(" could not be installed. ") + Error;
                return false;
            }
        }
        return true;
    }

    bool InstallRimeCards(FAbilityActualMageFrontendFixture& Fixture, TArrayView<const FName> Cards, FString& Error)
    {
        const auto* Definition =
            Fixture.Definition(TEXT("/Game/UnifiedAbilities/MageBatch/GA_RimeSigil.GA_RimeSigil_C"), Error);
        return InstallCards(*Fixture.Developer, Definition, Cards, Error);
    }

    /** 无视口世界用正式激活/目标数据 RPC 模拟拥有者传输，保留资产的 LocalPredicted 策略。 */
    bool CastRimeThroughInput(FAbilityActualMageFrontendFixture& Fixture)
    {
        Fixture.Key(IE_Pressed);
        const auto Handle = Fixture.Developer->GetPreviewHandle();
        const auto* Spec = Fixture.PlayerASC->FindAbilitySpecFromHandle(Handle);
        const auto* Ability = Spec ? Cast<UMageGraphAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!Spec || !Spec->InputPressed || !Ability)
        {
            return false;
        }
        if (!Ability->IsActive() &&
            Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalPredicted)
        {
            FPredictionKey Prediction;
            Prediction.Current = 1401;
            Fixture.PlayerASC->CallServerTryActivateAbility(Handle, Spec->InputPressed, Prediction);
        }
        if (!Ability->IsActive())
        {
            return false;
        }
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Location = Hit->HitResult.ImpactPoint = FVector(200, 0, 0);
        Hit->HitResult.Item = INDEX_NONE;
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        Fixture.PlayerASC->ServerSetReplicatedTargetData(
            Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey(), Data, FGameplayTag(),
            FPredictionKey());
        Fixture.Key(IE_Released);
        return true;
    }

    /** 使用 Mass 接入 GAS 的真实伤害 GE 驱动同一玩家元属性。 */
    void HitPlayer(FAbilityActualMageFrontendFixture& Fixture, float Damage)
    {
        auto Spec = Fixture.PlayerASC->MakeOutgoingSpec(UPureMassIncomingDamageEffect::StaticClass(), 1.f,
                                                        Fixture.PlayerASC->MakeEffectContext());
        Spec.Data->SetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(), Damage);
        Fixture.PlayerASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
} // namespace AbilityRuntimeRecipePanelTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerRhythmCast,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.RhythmCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerRhythmCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const FName Cards[] = {TEXT("FastPulse"), TEXT("RhythmMana")};
    if (!AbilityRuntimeRecipePanelTests::InstallRimeCards(Fixture, Cards, Error))
    {
        AddError(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto Mana = UMyAttributeSet::GetManaAttribute();
    const auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
    const float InitialHealth = TargetASC->GetNumericAttribute(Health);
    Fixture.PlayerASC->SetNumericAttributeBase(Mana, 100.f);
    if (!TestTrue(TEXT("The regular trial input activates the authored ability"),
                  AbilityRuntimeRecipePanelTests::CastRimeThroughInput(Fixture)))
    {
        return false;
    }
    const float ManaAfterCost = Fixture.PlayerASC->GetNumericAttribute(Mana);
    TestTrue(TEXT("The real ability paid its cost"), ManaAfterCost < 100.f);
    Fixture.Step(1.f);
    TestTrue(TEXT("The faster field has already damaged the Actor"),
             TargetASC->GetNumericAttribute(Health) < InitialHealth);
    TestEqual(TEXT("Two damage ticks do not restore mana"), Fixture.PlayerASC->GetNumericAttribute(Mana),
              ManaAfterCost);
    Fixture.Step(.4f);
    TestEqual(TEXT("The third damaging tick restores twelve mana"), Fixture.PlayerASC->GetNumericAttribute(Mana),
              ManaAfterCost + 12.f);
    Fixture.Step(3.2f);
    TestEqual(TEXT("Repeated field damage still grants only one refund"), Fixture.PlayerASC->GetNumericAttribute(Mana),
              ManaAfterCost + 12.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerKillCast, "test_25d.UnifiedGA.DynamicMage.PlayerCards.KillCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerKillCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const FName Cards[] = {TEXT("KillBurst")};
    if (!AbilityRuntimeRecipePanelTests::InstallRimeCards(Fixture, Cards, Error))
    {
        AddError(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    Fixture.Target->SetActorLocation(FVector(545, 0, 0));
    Fixture.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(Health, 1.f);
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* MonsterType = NewObject<UPureMassMonsterType>(Fixture.World);
    MonsterType->Health = 100.f;
    const int32 Nearby = Mass->SpawnMonster(MonsterType, FVector(680, 0, 0));
    const int32 OutOfRange = Mass->SpawnMonster(MonsterType, FVector(795, 0, 0));
    if (!TestTrue(TEXT("Regular trial input starts the kill-card cast"),
                  AbilityRuntimeRecipePanelTests::CastRimeThroughInput(Fixture)))
    {
        return false;
    }
    Fixture.Step(1.2f);
    TestTrue(TEXT("The original ice pulse defeated the Actor"), Fixture.Target->IsDead_Implementation());
    TestEqual(TEXT("Only the extra fire splash reaches the nearby Mass enemy"), Mass->GetElementalExposure(Nearby),
              EAbilityExposure::Burning);
    TestEqual(TEXT("The farther Mass enemy is outside the blast"), Mass->GetElementalExposure(OutOfRange),
              EAbilityExposure::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerLowHealthCast,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.LowHealthCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerLowHealthCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const FName Cards[] = {TEXT("LowHealthStone")};
    if (!AbilityRuntimeRecipePanelTests::InstallRimeCards(Fixture, Cards, Error))
    {
        AddError(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    Fixture.Target->SetActorLocation(FVector(270, 20, 0));
    Fixture.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(Health, 3505.f);
    auto* Healthy = Fixture.EnemyAt(FVector(290, -70, 0));
    const float HealthyBefore = Healthy->GetAbilitySystemComponent()->GetNumericAttribute(Health);
    if (!TestTrue(TEXT("Start the normal frost field"), AbilityRuntimeRecipePanelTests::CastRimeThroughInput(Fixture)))
    {
        return false;
    }
    Fixture.Step(1.15f);
    TestTrue(TEXT("The first damage tick crosses the threshold"),
             Fixture.Target->GetAbilitySystemComponent()->GetNumericAttribute(Health) < 3500.f);
    TestTrue(TEXT("Low-health target is petrified after damage"), Fixture.IsPetrified());
    TestTrue(TEXT("The healthy target was hit by the same first tick"),
             Healthy->GetAbilitySystemComponent()->GetNumericAttribute(Health) < HealthyBefore);
    TestFalse(TEXT("The healthy target is not petrified by the card"), Healthy->GetMesh()->bPauseAnims);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerFrozenEchoCast,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.FrozenEchoCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerFrozenEchoCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const FName Cards[] = {TEXT("FrozenEcho")};
    if (!AbilityRuntimeRecipePanelTests::InstallRimeCards(Fixture, Cards, Error))
    {
        AddError(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
    const float InitialHealth = TargetASC->GetNumericAttribute(Health);
    if (!TestTrue(TEXT("Start the real modified frost field through the ordinary input"),
                  AbilityRuntimeRecipePanelTests::CastRimeThroughInput(Fixture)))
    {
        return false;
    }
    Fixture.Step(1.15f);
    const float AfterFirst = TargetASC->GetNumericAttribute(Health);
    TestEqual(TEXT("The first chill hit only deals the original eleven damage"), InitialHealth - AfterFirst, 11.f);
    TestFalse(TEXT("The first hit has not frozen the target"), Fixture.Target->IsIceFrozen());
    Fixture.Step(1.f);
    TestTrue(TEXT("The original second ice hit freezes the target"), Fixture.Target->IsIceFrozen());
    TestEqual(TEXT("The newly frozen target took the original hit plus fourteen Arcane damage"),
              AfterFirst - TargetASC->GetNumericAttribute(Health), 25.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerDelayedBurstCast,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.DelayedBurstCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerDelayedBurstCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const FName Cards[] = {TEXT("DelayedBurst")};
    if (!AbilityRuntimeRecipePanelTests::InstallRimeCards(Fixture, Cards, Error))
    {
        AddError(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    auto* Incoming = Fixture.EnemyAt(FVector(900, 120, 0));
    const auto* IncomingASC = Incoming->GetAbilitySystemComponent();
    const auto* OriginalASC = Fixture.Target->GetAbilitySystemComponent();
    const float InitialHealth = IncomingASC->GetNumericAttribute(Health);
    if (!TestTrue(TEXT("Start a normal frost field with the delayed expiry card"),
                  AbilityRuntimeRecipePanelTests::CastRimeThroughInput(Fixture)))
    {
        return false;
    }
    Fixture.Step(4.18f);
    const float AfterOriginalBurst = OriginalASC->GetNumericAttribute(Health);
    TestTrue(TEXT("Original field has hit its target"), AfterOriginalBurst < 10000.f);
    TestEqual(TEXT("The distant enemy has not been struck"), IncomingASC->GetNumericAttribute(Health), InitialHealth);
    Fixture.Target->SetActorLocation(FVector(900, 0, 0));
    Incoming->SetActorLocation(FVector(280, 90, 0));
    Fixture.Step(.47f);
    TestEqual(TEXT("The delayed query misses the enemy that moved away"), OriginalASC->GetNumericAttribute(Health),
              AfterOriginalBurst);
    TestEqual(TEXT("The delayed query hits the new enemy at its current position"),
              InitialHealth - IncomingASC->GetNumericAttribute(Health), 12.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerFocusedGaze,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.FocusedGaze",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerFocusedGaze::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const auto* Definition =
        Fixture.Definition(TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C"), Error);
    if (!TestNotNull(TEXT("Read the actual Petrifying Gaze"), Definition))
    {
        return false;
    }
    const FGuid Session = FGuid::NewGuid();
    auto& Developer = *Fixture.Developer;
    Developer.ServerBeginCardPreview(Definition->DefinitionId, Session);
    const auto& State = Developer.GetDeveloperState();
    for (const FName Card : {FName(TEXT("FocusedGaze")), FName(TEXT("WideGaze"))})
    {
        if (!Developer.PreviewCard(Card, Session, State.Revision, Error) ||
            !Developer.ApplyGraphDraft(State.DraftToken, State.Revision, Error))
        {
            AddError(Card.ToString() + TEXT(" could not be installed. ") + Error);
            return false;
        }
    }

    const auto Health = UMyAttributeSet::GetHealthAttribute();
    Fixture.Target->SetActorLocation(FVector(125, 170, 0));
    Fixture.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(Health, 1200.f);
    auto* NearHealthy = Fixture.EnemyAt(FVector(80, -120, 0));
    auto* FarWounded = Fixture.EnemyAt(FVector(765, 85, 0));
    FarWounded->GetAbilitySystemComponent()->SetNumericAttributeBase(Health, 4000.f);
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Health = 100.f;
    const int32 Entity = Mass->SpawnMonster(Type, FVector(300, -120, 0));
    auto& Manager = Fixture.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    Manager.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Entity)).Health = 25.f;

    Fixture.Key(IE_Pressed);
    Fixture.Key(IE_Released);
    Fixture.Step(.1f);
    FPureMassView MassTarget;
    TestTrue(TEXT("The lowest-health Actor is petrified"), Fixture.IsPetrified());
    TestTrue(TEXT("A wounded Actor in the expanded part of the area is petrified"), FarWounded->GetMesh()->bPauseAnims);
    TestTrue(TEXT("The mixed health ranking includes the wounded Mass target"),
             Mass->Resolve(Entity, MassTarget) && MassTarget.bPetrified);
    TestFalse(TEXT("The nearest full-health Actor does not displace a wounded candidate"),
              NearHealthy->GetMesh()->bPauseAnims);
    auto* Component = Fixture.Target->FindComponentByClass<UBatch6PetrifyComponent>();
    if (!TestNotNull(TEXT("The ordinary petrify component owns the Actor status"), Component))
    {
        return false;
    }
    // 夹具只推进世界时钟和施法子系统，状态组件到期 Tick 由该用例显式驱动。
    Fixture.Step(3.2f);
    Component->TickComponent(0.f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("The focused state outlasts the original three seconds"), Fixture.IsPetrified());
    Fixture.Step(1.4f);
    Component->TickComponent(0.f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("Petrification still expires through the original status owner"), Fixture.IsPetrified());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerBarrierCast,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.BarrierCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerBarrierCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const auto* Definition = Fixture.Definition(TEXT("/Script/test_25d.WorkshopArcaneBarrier"), Error);
    const FName Cards[] = {TEXT("HeavyBarrier"), TEXT("LingeringBarrier")};
    if (!AbilityRuntimeRecipePanelTests::InstallCards(*Fixture.Developer, Definition, Cards, Error))
    {
        AddError(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto Mana = UMyAttributeSet::GetManaAttribute();
    Fixture.PlayerASC->SetNumericAttributeBase(Health, 100.f);
    Fixture.PlayerASC->SetNumericAttributeBase(Mana, 100.f);
    auto CastShield = [&]()
    {
        Fixture.Key(IE_Pressed);
        Fixture.Key(IE_Released);
        Fixture.Step(.05f);
    };
    CastShield();
    TestEqual(TEXT("The original input pays eighteen mana"), Fixture.PlayerASC->GetNumericAttribute(Mana), 82.f);
    CastShield();
    TestEqual(TEXT("The shield uses the original GAS cooldown"), Fixture.PlayerASC->GetNumericAttribute(Mana), 82.f);
    Fixture.Step(4.1f);
    AbilityRuntimeRecipePanelTests::HitPlayer(Fixture, 35.f);
    TestEqual(TEXT("The long-duration card is still protecting the caster"), Fixture.PlayerASC->GetNumericAttribute(Health),
              100.f);
    AbilityRuntimeRecipePanelTests::HitPlayer(Fixture, 40.f);
    TestEqual(TEXT("The capacity card absorbs sixty points in total"), Fixture.PlayerASC->GetNumericAttribute(Health),
              85.f);
    Fixture.Step(4.f);
    CastShield();
    Fixture.Step(6.1f);
    AbilityRuntimeRecipePanelTests::HitPlayer(Fixture, 15.f);
    TestEqual(TEXT("Expiration discards unused capacity"), Fixture.PlayerASC->GetNumericAttribute(Health), 70.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerStasisCast, "test_25d.UnifiedGA.DynamicMage.PlayerCards.StasisCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerStasisCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const auto* Definition = Fixture.Definition(TEXT("/Script/test_25d.WorkshopStasisField"), Error);
    const FName Cards[] = {TEXT("WideArea"), TEXT("LongStasis")};
    if (!AbilityRuntimeRecipePanelTests::InstallCards(*Fixture.Developer, Definition, Cards, Error))
    {
        AddError(Error);
        return false;
    }
    Fixture.Target->SetActorLocation(FVector(500, 80, 0));
    auto* Outside = Fixture.EnemyAt(FVector(610, 0, 0));
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Health = 100.f;
    const int32 Entity = Mass->SpawnMonster(Type, FVector(500, -80, 0));
    if (!TestTrue(TEXT("T and ground target data activate the native GA"),
                  AbilityRuntimeRecipePanelTests::CastRimeThroughInput(Fixture)))
    {
        return false;
    }
    Fixture.Step(.1f);
    FPureMassView View;
    TestTrue(TEXT("The expanded area controls an Actor beyond the base radius"), Fixture.IsPetrified());
    TestTrue(TEXT("The same expanded area controls a Mass enemy"), Mass->Resolve(Entity, View) && View.bPetrified);
    TestFalse(TEXT("An Actor beyond the expanded area keeps moving"), Outside->GetMesh()->bPauseAnims);
    auto* Status = Fixture.Target->FindComponentByClass<UBatch6PetrifyComponent>();
    if (!TestNotNull(TEXT("The original component owns the Actor status"), Status))
    {
        return false;
    }
    Fixture.Step(1.5f);
    Status->TickComponent(0.f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("The duration card exceeds the original 1.5 seconds"), Fixture.IsPetrified());
    Fixture.Step(.8f);
    Status->TickComponent(0.f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("Control expires after the modified duration"), Fixture.IsPetrified());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerBarrierFeedback,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.BarrierOnDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipePlayerBarrierFeedback::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const FName Cards[] = {TEXT("FastPulse"), TEXT("GuardOnHit")};
    if (!AbilityRuntimeRecipePanelTests::InstallRimeCards(Fixture, Cards, Error) ||
        !AbilityRuntimeRecipePanelTests::CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The field must accept and cast with Barrier on Hit. ") + Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
    const float TargetHealth = TargetASC->GetNumericAttribute(Health);
    Fixture.PlayerASC->SetNumericAttributeBase(Health, 100.f);
    AbilityRuntimeRecipePanelTests::HitPlayer(Fixture, 10.f);
    TestEqual(TEXT("Casting alone does not grant the barrier"), Fixture.PlayerASC->GetNumericAttribute(Health), 90.f);
    Fixture.Step(1.f);
    const float AfterFirstPulses = TargetASC->GetNumericAttribute(Health);
    TestTrue(TEXT("The field deals actual damage"), AfterFirstPulses < TargetHealth);
    AbilityRuntimeRecipePanelTests::HitPlayer(Fixture, 30.f);
    TestEqual(TEXT("Twenty-five points of the next hit are absorbed"), Fixture.PlayerASC->GetNumericAttribute(Health),
              85.f);
    Fixture.Step(.8f);
    TestTrue(TEXT("The field can continue hitting in the same cast"),
             TargetASC->GetNumericAttribute(Health) < AfterFirstPulses);
    AbilityRuntimeRecipePanelTests::HitPlayer(Fixture, 10.f);
    TestEqual(TEXT("Later hits do not regrant the consumed barrier"), Fixture.PlayerASC->GetNumericAttribute(Health),
              75.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeRainAndFrost, "test_25d.UnifiedGA.DynamicMage.PlayerCards.RainAndFrost",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipeRainAndFrost::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    using S = EAbilityExposure;
    FAbilityActualMageFrontendFixture Fixture;
    Fixture.Target->SetActorLocation(FVector(650, 0, 0));
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Health = 200.f;
    const int32 Entity = Mass->SpawnMonster(Type, FVector(315, -100, 0));
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* ActorASC = Fixture.Target->GetAbilitySystemComponent();
    const auto* Attributes = ActorASC->GetSet<UMyAttributeSet>();
    const float BaseHealth = ActorASC->GetNumericAttribute(Health);
    FString Error;
    auto& Developer = *Fixture.Developer;
    const auto* Rain = Fixture.Definition(TEXT("/Script/test_25d.WorkshopSoakingRain"), Error);
    const FName Cards[] = {TEXT("FastRain"), TEXT("LongExposure")};
    if (!InstallCards(Developer, Rain, Cards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Rain and its separate cadence/duration cards must cast through the workshop. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    FPureMassView View;
    TestTrue(TEXT("First pulse wets Mass targets without damage"),
             Mass->GetElementalExposure(Entity) == S::Wet && Mass->Resolve(Entity, View) && View.Health == 200.f);
    TestTrue(TEXT("An Actor outside stays dry"), Attributes->GetElementalExposure() == S::None);
    Fixture.Target->SetActorLocation(FVector(100, 80, 0));
    Fixture.Step(.6f);
    TestTrue(TEXT("The next pulse wets an Actor entering the area"), Attributes->GetElementalExposure() == S::Wet);
    TestEqual(TEXT("Rain applies no health damage"), ActorASC->GetNumericAttribute(Health), BaseHealth);
    if (!Developer.EndPreview(Error))
    {
        AddError(Error);
        return false;
    }
    const auto* Frost = Fixture.Definition(TEXT("/Script/test_25d.WorkshopCrystalBurst"), Error);
    const FName Radius[] = {TEXT("WideArea")};
    if (!InstallCards(Developer, Frost, Radius, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The new ice spell must cast through the same input. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestTrue(TEXT("Ice damage turns the existing Wet status into Frozen on both targets"),
             Attributes->GetElementalExposure() == S::Frozen && Mass->GetElementalExposure(Entity) == S::Frozen);
    TestTrue(TEXT("The ice spell uses real damage"), ActorASC->GetNumericAttribute(Health) < BaseHealth);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeElementalHit, "test_25d.UnifiedGA.DynamicMage.PlayerCards.ElementalHit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipeElementalHit::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    using S = EAbilityExposure;
    FAbilityActualMageFrontendFixture Fixture;
    auto& Developer = *Fixture.Developer;
    FString Error;
    const auto* Rain = Fixture.Definition(TEXT("/Script/test_25d.WorkshopSoakingRain"), Error);
    if (!InstallCards(Developer, Rain, {}, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Unmodified rain must cast. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    if (!Developer.EndPreview(Error))
    {
        AddError(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* ActorASC = Fixture.Target->GetAbilitySystemComponent();
    const auto* Attributes = ActorASC->GetSet<UMyAttributeSet>();
    const float BaseHealth = ActorASC->GetNumericAttribute(Health);
    const float BaseSpeed = Fixture.Target->GetCharacterMovement()->MaxWalkSpeed;
    const auto* Thunder = Fixture.Definition(TEXT("/Script/test_25d.WorkshopThunderImpact"), Error);
    if (Thunder)
    {
        FAbilityRuntimeRecipeSnapshot Snapshot;
        Snapshot.Graph = Thunder->Base.DefaultGraph;
        FAbilityRuntimeGraphView View;
        if (TestTrue(TEXT("Graph editor sees the actual Thunder graph"),
                     AbilityRuntimeGraphViews::Build(Snapshot, View, Error)))
        {
            const auto* Damage = View.Nodes.FindByPredicate([](const FAbilityRuntimeGraphNodeView& Node)
            {
                return Node.TypeId == TEXT("Effect.Damage");
            });
            TestTrue(TEXT("Damage magnitude and element are independent editable parameters"),
                     Damage && Damage->SupportedModifiers.Contains(TEXT("Scale.Damage")) &&
                         Damage->SupportedModifiers.Contains(TEXT("Set.ElementConversion")));
        }
    }
    const FName Reaction[] = {TEXT("ConductiveSnare")};
    if (!InstallCards(Developer, Thunder, Reaction, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Thunder must cast with the conditional control card. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestTrue(TEXT("Wet and lightning produce the original 25 percent amplification"),
             FMath::IsNearlyEqual(BaseHealth - ActorASC->GetNumericAttribute(Health), 35.f));
    TestTrue(TEXT("The card reads the resulting conductive status and slows the live target"),
             Attributes->GetElementalExposure() == S::Conductive &&
                 Fixture.Target->GetCharacterMovement()->MaxWalkSpeed < BaseSpeed);
    if (!Developer.EndPreview(Error))
    {
        AddError(Error);
        return false;
    }
    const auto* Fire = Fixture.Definition(TEXT("/Script/test_25d.WorkshopCinderBlast"), Error);
    const FName Fuse[] = {TEXT("QuickIgnition"), TEXT("Water")};
    if (!InstallCards(Developer, Fire, Fuse, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The fire spell must accept separate timing and damage element cards. ") + Error);
        return false;
    }
    const float BeforeFire = ActorASC->GetNumericAttribute(Health);
    Fixture.Step(.12f);
    TestEqual(TEXT("No damage before the modified fuse"), ActorASC->GetNumericAttribute(Health), BeforeFire);
    Fixture.Step(.13f);
    TestTrue(TEXT("The shortened fuse has fired while the original would still be waiting"),
             ActorASC->GetNumericAttribute(Health) < BeforeFire);
    TestTrue(TEXT("The damage element can change independently from the fuse"),
             Attributes->GetElementalExposure() == S::Wet);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeMireSequence, "test_25d.UnifiedGA.DynamicMage.PlayerCards.MireSequence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipeMireSequence::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    auto& Developer = *Fixture.Developer;
    const auto* Mire = Fixture.Definition(TEXT("/Script/test_25d.WorkshopVenomMire"), Error);
    const FName MireCards[] = {TEXT("WideMire"), TEXT("LongMire")};
    if (!InstallCards(Developer, Mire, MireCards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The original T input must cast an enlarged and extended puddle. ") + Error);
        return false;
    }
    auto FindOwnedSurface = [&](ETerrainSurfaceType Kind) -> ATerrainSurface*
    {
        for (TActorIterator<ATerrainSurface> It(Fixture.World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed() && It->GetOwner() == Fixture.Pawn && It->Config &&
                It->Config->SurfaceType == Kind)
            {
                return *It;
            }
        }
        return nullptr;
    };
    Fixture.Step(.1f);
    auto* Toxic = FindOwnedSurface(ETerrainSurfaceType::Toxic);
    if (!TestNotNull(TEXT("A real owned Toxic terrain actor was created"), Toxic))
    {
        return false;
    }
    const FVector Location = Toxic->GetActorLocation();
    const FVector2D Size = Toxic->SurfaceSize;
    TestTrue(TEXT("The wide card changes the actual terrain bounds"), Size.X > 420.f && Size.Y > 420.f);
    if (!Developer.EndPreview(Error))
    {
        AddError(Error);
        return false;
    }
    Fixture.Step(.3f);
    TestTrue(TEXT("A placed field survives switching the trial spell"), IsValid(Toxic));
    const auto* Ignite = Fixture.Definition(TEXT("/Script/test_25d.WorkshopMireIgnition"), Error);
    if (!InstallCards(Developer, Ignite, {}, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The ignition uses the same production input. ") + Error);
        return false;
    }
    Fixture.Step(.1f);
    TestNull(TEXT("An ignited patch no longer applies poison"), FindOwnedSurface(ETerrainSurfaceType::Toxic));
    auto* Flame = FindOwnedSurface(ETerrainSurfaceType::Lava);
    if (TestNotNull(TEXT("The same location now hosts real Lava terrain"), Flame))
    {
        TestTrue(TEXT("Conversion preserves the position and edited size"),
                 Flame->GetActorLocation().Equals(Location, 1.f) && Flame->SurfaceSize.Equals(Size, 1.f));
    }
    if (!Developer.EndPreview(Error))
    {
        AddError(Error);
        return false;
    }
    const auto* Freeze = Fixture.Definition(TEXT("/Script/test_25d.WorkshopLavaFreeze"), Error);
    const FName FreezeCards[] = {TEXT("FarConvert")};
    if (Freeze)
    {
        FAbilityRuntimeRecipeSnapshot Snapshot;
        Snapshot.Graph = Freeze->Base.DefaultGraph;
        FAbilityRuntimeGraphView View;
        if (TestTrue(TEXT("The graph editor exposes the actual conversion"),
                     AbilityRuntimeGraphViews::Build(Snapshot, View, Error)))
        {
            const auto* Convert = View.Nodes.FindByPredicate([](const FAbilityRuntimeGraphNodeView& Node)
            {
                return Node.TypeId == TEXT("World.ConvertSurface");
            });
            TestTrue(TEXT("Search radius and source and result types are independently editable in the editor"),
                     Convert && Convert->SupportedModifiers.Contains(TEXT("Set.Radius")) &&
                         Convert->SupportedModifiers.Contains(TEXT("Set.TerrainInputKind")) &&
                         Convert->SupportedModifiers.Contains(TEXT("Set.TerrainSurfaceKind")));
        }
    }
    if (!InstallCards(Developer, Freeze, FreezeCards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The same real input applies the extended conversion. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    const auto* Frost = FindOwnedSurface(ETerrainSurfaceType::Frost);
    if (!TestNotNull(TEXT("The old lava is now a real Frost surface"), Frost) || !Developer.EndPreview(Error))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("Freezing retains the original position and footprint"),
             Frost->GetActorLocation().Equals(Location, 1.f) && Frost->SurfaceSize.Equals(Size, 1.f));
    const auto* Corrode = Fixture.Definition(TEXT("/Script/test_25d.WorkshopFrostCorrode"), Error);
    const FName CorrodeCards[] = {TEXT("IntoVines")};
    if (!InstallCards(Developer, Corrode, CorrodeCards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The same input applies the changed result kind. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestNull(TEXT("The previously applied Frost is retired"), FindOwnedSurface(ETerrainSurfaceType::Frost));
    TestNotNull(TEXT("The independent result card replaces Frost with real Vines"),
                FindOwnedSurface(ETerrainSurfaceType::Vines));
    Fixture.Step(12.1f);
    TestNull(TEXT("The repeatedly converted patch expires"), FindOwnedSurface(ETerrainSurfaceType::Vines));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeFrostSurge, "test_25d.UnifiedGA.DynamicMage.PlayerCards.FrostSurge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipeFrostSurge::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    auto& Developer = *Fixture.Developer;
    Fixture.Target->SetActorLocation(FVector(450, -90, 0));
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Health = 200.f;
    const int32 Entity = Mass->SpawnMonster(Type, FVector(530, 60, 0));
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
    const float Before = TargetASC->GetNumericAttribute(Health);
    const auto* Wave = Fixture.Definition(TEXT("/Script/test_25d.WorkshopFrostSurge"), Error);
    const FName Cards[] = {TEXT("RushingFrost")};
    if (!InstallCards(Developer, Wave, Cards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The wave must cast with its speed edit. ") + Error);
        return false;
    }
    Fixture.Step(.08f);
    TestEqual(TEXT("No damage is applied before the visible front reaches the target"),
              TargetASC->GetNumericAttribute(Health), Before);
    Fixture.Step(.18f);
    FPureMassView View;
    TestTrue(TEXT("The modified front reaches an Actor before the original speed would"),
             TargetASC->GetNumericAttribute(Health) < Before);
    TestTrue(TEXT("The same front applies real ice to a Mass enemy"),
             Mass->Resolve(Entity, View) && View.Health < 200.f &&
                 Mass->GetElementalExposure(Entity) == EAbilityExposure::Chilled);
    const float After = TargetASC->GetNumericAttribute(Health);
    Fixture.Step(.6f);
    TestEqual(TEXT("Each original life is hit at most once per wave"), TargetASC->GetNumericAttribute(Health), After);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeWetArcFilter, "test_25d.UnifiedGA.DynamicMage.PlayerCards.WetArcFilter",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipeWetArcFilter::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    auto* Dry = Fixture.EnemyAt(FVector(360, 90, 0));
    auto* Wet = Fixture.EnemyAt(FVector(360, -110, 0));
    FString Error;
    FAbilitySourceToken Source;
    FAbilityTargetToken WetToken;
    FAbilityTargetToken SteamToken;
    if (!AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Wet), WetToken, Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Fixture.Target), SteamToken,
                                                Error) ||
        !AbilityElements::Apply(Source, WetToken, EAbilityElement::Water, 1.f) ||
        !AbilityElements::Apply(Source, SteamToken, EAbilityElement::Water, 1.f) ||
        !AbilityElements::Apply(Source, SteamToken, EAbilityElement::Fire, 1.f))
    {
        AddError(TEXT("The targets need real wet and steam exposures. ") + Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const float Before = Fixture.Target->GetAbilitySystemComponent()->GetNumericAttribute(Health);
    const auto* Arc = Fixture.Definition(TEXT("/Script/test_25d.WorkshopWetArc"), Error);
    const FName Cards[] = {TEXT("SteamConductors"), TEXT("ExtraConductors")};
    if (!InstallCards(*Fixture.Developer, Arc, Cards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The spell must honor independent target filter and cap edits. ") + Error);
        return false;
    }
    Fixture.Step(.1f);
    TestTrue(TEXT("The alternate filter picks steam and changes it into conductance"),
             Fixture.Target->GetAbilitySystemComponent()->GetNumericAttribute(Health) < Before &&
                 AbilityElements::Read(Source, SteamToken) == EAbilityExposure::Conductive);
    TestEqual(TEXT("Dry targets do not take damage"), Dry->GetAbilitySystemComponent()->GetNumericAttribute(Health), Before);
    TestEqual(TEXT("Replacing the filter does not also hit the old wet targets"),
              Wet->GetAbilitySystemComponent()->GetNumericAttribute(Health), Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeWorkshopEmberBurst,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.EmberBurst",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipeWorkshopEmberBurst::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    Fixture.Target->SetActorLocation(FVector(325, 0, 0));
    auto* Neighbor = Fixture.EnemyAt(FVector(400, 200, 0));
    auto* FarDry = Fixture.EnemyAt(FVector(650, -70, 0));
    auto* Wet = Fixture.EnemyAt(FVector(430, -140, 0));
    FAbilitySourceToken Source;
    FAbilityTargetToken Marked;
    FAbilityTargetToken WetLife;
    if (!AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Fixture.Target), Marked,
                                                 Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Wet), WetLife, Error) ||
        !AbilityElements::Apply(Source, Marked, EAbilityElement::Fire, 1.f) ||
        !AbilityElements::Apply(Source, WetLife, EAbilityElement::Water, 1.f))
    {
        AddError(TEXT("A genuine burning target is required. ") + Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
    const float Before = TargetASC->GetNumericAttribute(Health);
    const float WetSpeed = Wet->GetCharacterMovement()->MaxWalkSpeed;
    const auto* Ember = Fixture.Definition(TEXT("/Script/test_25d.WorkshopEmberDetonation"), Error);
    const FName Cards[] = {TEXT("FastEmbers"), TEXT("VolatileReach"), TEXT("SteamVeil")};
    if (!InstallCards(*Fixture.Developer, Ember, Cards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The real T input must cast the edited ember graph. ") + Error);
        return false;
    }
    Fixture.Step(.08f);
    TestEqual(TEXT("No early damage before the edited delay"), TargetASC->GetNumericAttribute(Health), Before);
    Fixture.Step(.09f);
    TestTrue(TEXT("The edited delay produces the actual detonated damage"),
             TargetASC->GetNumericAttribute(Health) < Before);
    TestTrue(TEXT("A dry enemy inside the expanded secondary blast can take damage"),
             Neighbor->GetAbilitySystemComponent()->GetNumericAttribute(Health) < Before);
    TestEqual(TEXT("A distant dry enemy cannot start its own blast"),
              FarDry->GetAbilitySystemComponent()->GetNumericAttribute(Health), Before);
    TestTrue(TEXT("A finite downstream steam control branch still runs at the end of the local blast"),
             AbilityElements::Read(Source, WetLife) == EAbilityExposure::Steam &&
                 Wet->GetCharacterMovement()->MaxWalkSpeed < WetSpeed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeWorkshopControl,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.ElementalControl",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipeWorkshopControl::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    FAbilitySourceToken Source;
    FAbilityTargetToken Chilled;
    FAbilityTargetToken Wet;
    Fixture.Target->SetActorLocation(FVector(230, 130, 0));
    auto* GaleTarget = Fixture.EnemyAt(FVector(255, -175, 0));
    // 位移服务与运行房间使用同样的脚下接触探测，空测试世界需要显式提供地面。
    auto* Floor = Fixture.World->SpawnActor<AActor>();
    auto* FloorCollision = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(FloorCollision);
    FloorCollision->InitBoxExtent(FVector(1400.f, 1400.f, 10.f));
    FloorCollision->SetCollisionObjectType(ECC_WorldStatic);
    FloorCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    FloorCollision->SetCollisionResponseToAllChannels(ECR_Block);
    FloorCollision->RegisterComponent();
    Floor->SetActorLocation(FVector(0.f, 0.f, -GaleTarget->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 10.f));
    if (!AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Fixture.Target), Chilled,
                                                 Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(GaleTarget), Wet, Error) ||
        !AbilityElements::Apply(Source, Chilled, EAbilityElement::Ice, 1.f) ||
        !AbilityElements::Apply(Source, Wet, EAbilityElement::Water, 1.f))
    {
        AddError(TEXT("Prepare actual independent statuses before release. ") + Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
    const float Before = TargetASC->GetNumericAttribute(Health);
    const auto* Shatter = Fixture.Definition(TEXT("/Script/test_25d.WorkshopIceShatter"), Error);
    const FName ShatterCards[] = {TEXT("WideShatter"), TEXT("ChilledShatter")};
    if (!InstallCards(*Fixture.Developer, Shatter, ShatterCards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The existing T entry must cast the modified line and exact consume card. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestTrue(TEXT("The widened line damages the chilled target and consumes that status"),
             TargetASC->GetNumericAttribute(Health) < Before &&
                 AbilityElements::Read(Source, Chilled) == EAbilityExposure::None);
    if (!Fixture.Developer->EndPreview(Error))
    {
        AddError(Error);
        return false;
    }
    const auto* Gale = Fixture.Definition(TEXT("/Script/test_25d.WorkshopGaleSweep"), Error);
    const FName GaleCards[] = {TEXT("WideGale"), TEXT("StrongGale")};
    if (!InstallCards(*Fixture.Developer, Gale, GaleCards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The existing T entry must start the widened cone displace graph. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestTrue(TEXT("The wet target inside the expanded cone has real root motion"),
             GaleTarget->GetCharacterMovement()->GetRootMotionSource(TEXT("ExtensionDisplace")).IsValid());
    TestFalse(TEXT("An unmarked target cannot consume a displacement"),
              Fixture.Target->GetCharacterMovement()->GetRootMotionSource(TEXT("ExtensionDisplace")).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeWorkshopTerrainDispel,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.TerrainDispel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeRecipeWorkshopTerrainDispel::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    auto& Developer = *Fixture.Developer;
    const auto* Mire = Fixture.Definition(TEXT("/Script/test_25d.WorkshopVenomMire"), Error);
    if (!InstallCards(Developer, Mire, {}, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Place an owned workshop tile through the real cast input. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TWeakObjectPtr<ATerrainSurface> Tile;
    for (TActorIterator<ATerrainSurface> It(Fixture.World); It; ++It)
    {
        if (It->GetOwner() == Fixture.Pawn)
        {
            Tile = *It;
            break;
        }
    }
    if (!TestTrue(TEXT("The real Toxic terrain exists before the dispel"), Tile.IsValid()) ||
        !Developer.EndPreview(Error))
    {
        AddInfo(Error);
        return false;
    }
    const auto* Dispel = Fixture.Definition(TEXT("/Script/test_25d.WorkshopTerrainDispel"), Error);
    const FName Cards[] = {TEXT("WideDispel"), TEXT("ToxicOnlyDispel")};
    if (!InstallCards(Developer, Dispel, Cards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("The real T entry must apply the edited terrain action. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestTrue(TEXT("The actual owned tile was retired before its original expiration"),
             !Tile.IsValid() || Tile->IsActorBeingDestroyed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopReactionShiftInput, "test_25d.UnifiedGA.DynamicMage.PlayerCards.ReactionShift",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopReactionShiftInput::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    Fixture.Target->SetActorLocation(FVector(665, 0, 0));
    FString Error;
    FAbilitySourceToken Source;
    FAbilityTargetToken Target;
    if (!AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Fixture.Target), Target, Error) ||
        !AbilityElements::Apply(Source, Target, EAbilityElement::Water, 1.f) ||
        !AbilityElements::Apply(Source, Target, EAbilityElement::Fire, 1.f))
    {
        AddError(TEXT("The actual Actor needs steam before the cast. ") + Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto Mana = UMyAttributeSet::GetManaAttribute();
    const float BeforeHealth = Fixture.Target->GetAbilitySystemComponent()->GetNumericAttribute(Health);
    auto& Developer = *Fixture.Developer;
    const auto* Condense = Fixture.Definition(TEXT("/Script/test_25d.WorkshopSteamCondensation"), Error);
    const FName Wide[] = {TEXT("WideReactions")};
    if (!InstallCards(Developer, Condense, Wide, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Cast real condensation with the expanded search. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestTrue(TEXT("The in-game card reaches steam beyond the base radius and converts it"),
             AbilityElements::Read(Source, Target) == EAbilityExposure::Wet);
    TestEqual(TEXT("Changing the exposure does not generate health damage"),
              Fixture.Target->GetAbilitySystemComponent()->GetNumericAttribute(Health), BeforeHealth);
    if (!Developer.EndPreview(Error) || !AbilityElements::Apply(Source, Target, EAbilityElement::Thunder, 1.f))
    {
        AddError(TEXT("The next cast requires a real conductive target. ") + Error);
        return false;
    }
    Fixture.PlayerASC->SetNumericAttributeBase(Mana, 80.f);
    const auto* Recharge = Fixture.Definition(TEXT("/Script/test_25d.WorkshopConductiveRecharge"), Error);
    const FName Recovery[] = {TEXT("WideReactions"), TEXT("DeepRecharge")};
    if (!InstallCards(Developer, Recharge, Recovery, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Cast real conductive recovery with its independent recovery amount. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestTrue(TEXT("The matched conductive state has been spent"),
             AbilityElements::Read(Source, Target) == EAbilityExposure::None);
    TestEqual(TEXT("The authority pays five mana before restoring the edited fifteen"),
              Fixture.PlayerASC->GetNumericAttribute(Mana), 90.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopSpatialStormInput, "test_25d.UnifiedGA.DynamicMage.PlayerCards.SpatialStorm",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopSpatialStormInput::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    Fixture.Target->SetActorLocation(FVector(250, 0, 0));
    auto* Bystander = Fixture.EnemyAt(FVector(350, 0, 0));
    if (!TestNotNull(TEXT("Spawn a live enemy near the mark"), Bystander))
    {
        return false;
    }
    FString Error;
    FAbilitySourceToken Source;
    FAbilityTargetToken Target;
    if (!AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Fixture.Target), Target, Error) ||
        !AbilityElements::Apply(Source, Target, EAbilityElement::Water, 1.f) ||
        !AbilityElements::Apply(Source, Target, EAbilityElement::Thunder, 1.f))
    {
        AddError(TEXT("Seed a conductive carrier for the alternative filter. ") + Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* BystanderASC = Bystander->GetAbilitySystemComponent();
    const float BeforeThunder = BystanderASC->GetNumericAttribute(Health);
    auto& Developer = *Fixture.Developer;
    const auto* Thunder = Fixture.Definition(TEXT("/Script/test_25d.WorkshopThunderMarkBurst"), Error);
    const FName ThunderCards[] = {TEXT("FastThunderMark"), TEXT("MarkedConductors")};
    if (!InstallCards(Developer, Thunder, ThunderCards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Cast both patched thunder stages through the real input. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestEqual(TEXT("The fuse has not fired yet"), BystanderASC->GetNumericAttribute(Health), BeforeThunder);
    Fixture.Target->SetActorLocation(FVector(800, 0, 0));
    Fixture.Step(.23f);
    TestTrue(TEXT("The shorter fuse hits the bystander at the original mark before the default delay"),
             BystanderASC->GetNumericAttribute(Health) < BeforeThunder);
    if (!Developer.EndPreview(Error))
    {
        AddError(Error);
        return false;
    }

    Bystander->SetActorLocation(FVector(465, 0, 0));
    const float BeforeFrost = BystanderASC->GetNumericAttribute(Health);
    const auto* Frost = Fixture.Definition(TEXT("/Script/test_25d.WorkshopFrostShower"), Error);
    const FName FrostCards[] = {TEXT("WideFrostShower"), TEXT("MoreFrostShower")};
    if (!InstallCards(Developer, Frost, FrostCards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Cast the edited point count and spread through the real input. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestEqual(TEXT("Only the near point resolves immediately"), BystanderASC->GetNumericAttribute(Health), BeforeFrost);
    Fixture.Step(.24f);
    TestTrue(TEXT("A scheduled outer point damages an enemy outside the first ice blast"),
             BystanderASC->GetNumericAttribute(Health) < BeforeFrost);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopShallowsChainInput, "test_25d.UnifiedGA.DynamicMage.PlayerCards.ShallowsChain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopShallowsChainInput::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    /** 每条测试链从独立世界和默认潮地开始，真实输入将前一试装留下的租约交给下一试装。 */
    const auto VerifyChain = [this](const TCHAR* ClassPath, ETerrainSurfaceType ExpectedKind, const FName ResultCard)
    {
        FAbilityActualMageFrontendFixture Fixture;
        auto& Developer = *Fixture.Developer;
        FString Error;
        const auto FindSurface = [&Fixture](ETerrainSurfaceType Kind) -> ATerrainSurface*
        {
            for (TActorIterator<ATerrainSurface> It(Fixture.World); It; ++It)
            {
                if (!It->IsActorBeingDestroyed() && It->GetOwner() == Fixture.Pawn && It->Config &&
                    It->Config->SurfaceType == Kind)
                {
                    return *It;
                }
            }
            return nullptr;
        };
        const FName ShallowsCards[] = {TEXT("WideShallows"), TEXT("LongShallows")};
        const auto* Shallows = Fixture.Definition(TEXT("/Script/test_25d.WorkshopWaterShallows"), Error);
        if (!InstallCards(Developer, Shallows, ShallowsCards, Error) || !CastRimeThroughInput(Fixture))
        {
            AddError(TEXT("Place an edited water tile through the real T input. ") + Error);
            return;
        }
        Fixture.Step(.08f);
        const auto* Tile = FindSurface(ETerrainSurfaceType::Water);
        if (!TestNotNull(TEXT("The first skill placed a real Water actor"), Tile) || !Developer.EndPreview(Error))
        {
            AddInfo(Error);
            return;
        }
        const FVector TilePosition = Tile->GetActorLocation();
        const FVector2D TileSize = Tile->SurfaceSize;
        TestTrue(TEXT("The size card changes the actual footprint"), TileSize.X > 480.f && TileSize.Y > 480.f);

        const auto* Convert = Fixture.Definition(ClassPath, Error);
        TArray<FName, TInlineAllocator<2>> ConvertCards = {TEXT("FarConvert")};
        if (!ResultCard.IsNone())
        {
            ConvertCards.Add(ResultCard);
        }
        if (!InstallCards(Developer, Convert, ConvertCards, Error) || !CastRimeThroughInput(Fixture))
        {
            AddError(TEXT("Convert from the same aim and original source using the real T input. ") + Error);
            return;
        }
        Fixture.Step(.08f);
        TestNull(TEXT("The original water actor is gone after replacement"), FindSurface(ETerrainSurfaceType::Water));
        const auto* Replaced = FindSurface(ExpectedKind);
        if (TestNotNull(TEXT("The compiled graph spawned the expected surface kind"), Replaced))
        {
            TestTrue(TEXT("The tile remains at its original location with the edited dimensions"),
                     Replaced->GetActorLocation().Equals(TilePosition, 1.f) && Replaced->SurfaceSize.Equals(TileSize));
        }
    };
    VerifyChain(TEXT("/Script/test_25d.WorkshopWaterFreeze"), ETerrainSurfaceType::Frost, NAME_None);
    VerifyChain(TEXT("/Script/test_25d.WorkshopWaterElectrify"), ETerrainSurfaceType::Conductive, NAME_None);
    VerifyChain(TEXT("/Script/test_25d.WorkshopWaterFreeze"), ETerrainSurfaceType::Toxic, TEXT("ToxicTide"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopTidalPhasesInput, "test_25d.UnifiedGA.DynamicMage.PlayerCards.TidalPhases",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopTidalPhasesInput::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    Fixture.Target->SetActorLocation(FVector(270, -90, 0));
    auto* Second = Fixture.EnemyAt(FVector(270, 90, 0));
    if (!TestNotNull(TEXT("Spawn two live actors in the phase area"), Second))
    {
        return false;
    }
    FString Error;
    FAbilitySourceToken Source;
    FAbilityTargetToken FirstLife;
    FAbilityTargetToken SecondLife;
    if (!AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Fixture.Target),
                                                 FirstLife, Error) ||
        !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Second), SecondLife, Error) ||
        !AbilityElements::Apply(Source, FirstLife, EAbilityElement::Water, 1.f))
    {
        AddError(TEXT("Seed one wet and one dry target for real branch selection. ") + Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* FirstASC = Fixture.Target->GetAbilitySystemComponent();
    const auto* SecondASC = Second->GetAbilitySystemComponent();
    const float FirstBefore = FirstASC->GetNumericAttribute(Health);
    const float SecondBefore = SecondASC->GetNumericAttribute(Health);
    const auto* Phases = Fixture.Definition(TEXT("/Script/test_25d.WorkshopTidalPhases"), Error);
    const FName Cards[] = {TEXT("PhaseFrost")};
    if (!InstallCards(*Fixture.Developer, Phases, Cards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Real T input must cast a recipe with only its rejected branch patched. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    TestTrue(TEXT("Wet target only takes the original thunder branch"),
             FirstASC->GetNumericAttribute(Health) < FirstBefore &&
                 AbilityElements::Read(Source, FirstLife) == EAbilityExposure::Conductive);
    TestTrue(TEXT("Dry target only takes the independently patched ice branch"),
             SecondASC->GetNumericAttribute(Health) < SecondBefore &&
                 AbilityElements::Read(Source, SecondLife) == EAbilityExposure::Chilled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopCloudCycleInput, "test_25d.UnifiedGA.DynamicMage.PlayerCards.CloudCycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopCloudCycleInput::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    auto& Developer = *Fixture.Developer;
    FString Error;
    const auto Cast = [&](const TCHAR* Path, TArrayView<const FName> Cards = {})
    {
        const auto* Definition = Fixture.Definition(Path, Error);
        if (!InstallCards(Developer, Definition, Cards, Error) || !CastRimeThroughInput(Fixture))
        {
            AddError(FString(Path) + TEXT(" could not cast using the real T input. ") + Error);
            return false;
        }
        Fixture.Step(.08f);
        if (!Developer.EndPreview(Error))
        {
            AddError(Error);
            return false;
        }
        return true;
    };
    const auto FindKind = [&](FName Kind) -> ATerrainSurface*
    {
        for (TActorIterator<ATerrainSurface> It(Fixture.World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed() && It->GetOwner() == Fixture.Pawn && It->Config &&
                FName(*StaticEnum<ETerrainSurfaceType>()->GetNameStringByValue(
                    static_cast<int64>(It->Config->SurfaceType))) == Kind)
            {
                return *It;
            }
        }
        return nullptr;
    };
    const FName ShallowsCards[] = {TEXT("LongShallows")};
    if (!Cast(TEXT("/Script/test_25d.WorkshopWaterShallows"), ShallowsCards))
    {
        return false;
    }
    const auto* First = FindKind(TEXT("Water"));
    if (!TestNotNull(TEXT("An actual temporary ground tile is present"), First) ||
        !Cast(TEXT("/Script/test_25d.WorkshopMistLift")))
    {
        return false;
    }
    const auto* Mist = FindKind(TEXT("MistCloud"));
    if (!TestNotNull(TEXT("The regular mist is now a real terrain-layer actor"), Mist))
    {
        return false;
    }
    const auto Location = Mist->GetActorLocation();
    const auto Size = Mist->SurfaceSize;
    TestNull(TEXT("The original ground tile has been consumed"), FindKind(TEXT("Water")));
    if (!Cast(TEXT("/Script/test_25d.WorkshopStormMist")))
    {
        return false;
    }
    TestNotNull(TEXT("The converted charged cloud is present"), FindKind(TEXT("ConductiveCloud")));
    const FName Condense[] = {TEXT("CondenseChargedMist")};
    if (!Cast(TEXT("/Script/test_25d.WorkshopMistCondense"), Condense))
    {
        return false;
    }
    const auto* Pool = FindKind(TEXT("Water"));
    if (TestNotNull(TEXT("The edited condensing spell returns the cloud to Water ground"), Pool))
    {
        TestTrue(TEXT("Cloud conversion retains the exact footprint center and dimensions"),
                 Pool->GetActorLocation().Equals(Location, 1.f) && Pool->SurfaceSize.Equals(Size, 1.f));
    }
    TestNull(TEXT("Charged cloud no longer occupies the cloud layer"), FindKind(TEXT("ConductiveCloud")));
    // 前一张留潮卡的九秒租约跨转化保留。升潮六秒冷却结束后可用同一地块验证实际驱雾入口。
    Fixture.Step(6.1f);
    const FName Disperse[] = {TEXT("WideMistDispel")};
    if (Cast(TEXT("/Script/test_25d.WorkshopMistLift")) &&
        TestNotNull(TEXT("The original lease still supports another vaporize after cooldown"), FindKind(TEXT("MistCloud"))) &&
        Cast(TEXT("/Script/test_25d.WorkshopMistDispel"), Disperse))
    {
        TestNull(TEXT("The real wider dispel removes the locally owned mist"), FindKind(TEXT("MistCloud")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopConsumedExposureInput,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.ConsumedExposure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopConsumedExposureInput::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    const auto VerifyCombo = [this](bool bRelay)
    {
        FAbilityActualMageFrontendFixture Fixture;
        Fixture.Target->SetActorLocation(FVector(250, 0, 0));
        auto* Neighbor = Fixture.EnemyAt(FVector(530, 0, 0));
        FString Error;
        FAbilitySourceToken Source;
        FAbilityTargetToken Carrier;
        if (!AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error) ||
            !AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Fixture.Target),
                                                     Carrier, Error) ||
            (!bRelay && !AbilityElements::Apply(Source, Carrier, EAbilityElement::Water, 1.f)) ||
            !AbilityElements::Apply(Source, Carrier, bRelay ? EAbilityElement::Thunder : EAbilityElement::Fire, 1.f))
        {
            AddError(TEXT("Prepare the modified carrier state for the real input. ") + Error);
            return;
        }
        const auto* Definition = Fixture.Definition(bRelay ? TEXT("/Script/test_25d.WorkshopConductiveRelay")
                                                           : TEXT("/Script/test_25d.WorkshopEmberRupture"), Error);
        const FName Cards[] = {bRelay ? TEXT("RelayShocked") : TEXT("RuptureSteam"),
                               bRelay ? TEXT("QuickRelay") : TEXT("WideEmberRupture")};
        const auto Health = UMyAttributeSet::GetHealthAttribute();
        const auto* CarrierASC = Fixture.Target->GetAbilitySystemComponent();
        const auto* NeighborASC = Neighbor->GetAbilitySystemComponent();
        const float CarrierBefore = CarrierASC->GetNumericAttribute(Health);
        const float NeighborBefore = NeighborASC->GetNumericAttribute(Health);
        if (!InstallCards(*Fixture.Developer, Definition, Cards, Error) || !CastRimeThroughInput(Fixture))
        {
            AddError(TEXT("Install the paired filter and consume patch through the real panel session. ") + Error);
            return;
        }
        Fixture.Step(.06f);
        if (bRelay)
        {
            TestEqual(TEXT("The carrier state is spent while the fuse is running"),
                      AbilityElements::Read(Source, Carrier), EAbilityExposure::None);
            TestEqual(TEXT("The edited relay still has a real delay"),
                      NeighborASC->GetNumericAttribute(Health), NeighborBefore);
            Fixture.Step(.16f);
            TestEqual(TEXT("The relay does not hit the original carrier"),
                      CarrierASC->GetNumericAttribute(Health), CarrierBefore);
        }
        TestTrue(TEXT("The modified combo hits a neighbor from the actual carrier's origin"),
                 NeighborASC->GetNumericAttribute(Health) < NeighborBefore);
    };
    VerifyCombo(false);
    VerifyCombo(true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopTerrainCatalystInput,
                                 "test_25d.UnifiedGA.DynamicMage.PlayerCards.TerrainCatalyst",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopTerrainCatalystInput::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityActualMageFrontendFixture Fixture;
    Fixture.Target->SetActorLocation(FVector(470, 0, 0));
    auto& Developer = *Fixture.Developer;
    FString Error;
    const auto* Water = Fixture.Definition(TEXT("/Script/test_25d.WorkshopWaterShallows"), Error);
    if (!InstallCards(Developer, Water, {}, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Seed a real own Water tile using the normal input. ") + Error);
        return false;
    }
    Fixture.Step(.05f);
    if (!Developer.EndPreview(Error))
    {
        AddError(Error);
        return false;
    }
    ATerrainSurface* Tile = nullptr;
    for (TActorIterator<ATerrainSurface> It(Fixture.World); It; ++It)
    {
        if (It->GetOwner() == Fixture.Pawn && It->Config && It->Config->SurfaceType == ETerrainSurfaceType::Water)
        {
            Tile = *It;
            break;
        }
    }
    if (!TestNotNull(TEXT("The test owns an actual Water terrain lease"), Tile))
    {
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto* ASC = Fixture.Target->GetAbilitySystemComponent();
    const float Before = ASC->GetNumericAttribute(Health);
    const FName Cards[] = {TEXT("CatalystWater"), TEXT("FastCatalyst"), TEXT("WideCatalyst")};
    const auto* Catalyst = Fixture.Definition(TEXT("/Script/test_25d.WorkshopStormCatalyst"), Error);
    if (!InstallCards(Developer, Catalyst, Cards, Error) || !CastRimeThroughInput(Fixture))
    {
        AddError(TEXT("Cast the edited source, rate and local radius through the normal input. ") + Error);
        return false;
    }
    Fixture.Step(.06f);
    const float After = ASC->GetNumericAttribute(Health);
    TestTrue(TEXT("The first edited pulse damages outside the default damage radius"), After < Before);
    TestTrue(TEXT("The catalyst does not consume the previously placed tile"),
             IsValid(Tile) && !Tile->IsActorBeingDestroyed());
    Tile->Destroy();
    Fixture.Step(1.5f);
    TestEqual(TEXT("Later pulses do not use a stale tile position"), ASC->GetNumericAttribute(Health), After);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperCatalog,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.CatalogAndStableIds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperCatalog::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer One(F);
    FPlayer Two(F);
    FString Error;
    if (!TestTrue(TEXT("Developer reads full actual mage catalog"), One.Developer->InitializeCatalog(nullptr, Error)) ||
        !TestTrue(TEXT("Second owner independently reads same immutable catalog"),
                  Two.Developer->InitializeCatalog(nullptr, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TSet<FString> Expected;
    for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                              TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"),
                              TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo")})
    {
        auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
        if (!TestNotNull(TEXT("Original catalog remains loadable"), Catalog))
        {
            return false;
        }
        for (const auto& Item : Catalog->AbilityInformation)
        {
            if (Item.RequiredProfession == EPlayerProfession::Mage && Item.Ability)
            {
                Expected.Add(Item.AbilityTag.ToString() + TEXT("|") + Item.Ability->GetPathName());
            }
        }
    }
    Expected.Add(TEXT("Abilities.Mage.ArcaneBarrier|/Script/test_25d.WorkshopArcaneBarrier"));
    Expected.Add(TEXT("Abilities.Mage.StasisField|/Script/test_25d.WorkshopStasisField"));
    Expected.Add(TEXT("Abilities.Mage.SoakingRain|/Script/test_25d.WorkshopSoakingRain"));
    Expected.Add(TEXT("Abilities.Mage.CinderBlast|/Script/test_25d.WorkshopCinderBlast"));
    Expected.Add(TEXT("Abilities.Mage.ThunderImpact|/Script/test_25d.WorkshopThunderImpact"));
    Expected.Add(TEXT("Abilities.Mage.CrystalBurst|/Script/test_25d.WorkshopCrystalBurst"));
    Expected.Add(TEXT("Abilities.Mage.VenomMire|/Script/test_25d.WorkshopVenomMire"));
    Expected.Add(TEXT("Abilities.Mage.MireIgnition|/Script/test_25d.WorkshopMireIgnition"));
    Expected.Add(TEXT("Abilities.Mage.FrostSurge|/Script/test_25d.WorkshopFrostSurge"));
    Expected.Add(TEXT("Abilities.Mage.WetArc|/Script/test_25d.WorkshopWetArc"));
    Expected.Add(TEXT("Abilities.Mage.GaleSweep|/Script/test_25d.WorkshopGaleSweep"));
    Expected.Add(TEXT("Abilities.Mage.IceShatter|/Script/test_25d.WorkshopIceShatter"));
    Expected.Add(TEXT("Abilities.Mage.TerrainDispel|/Script/test_25d.WorkshopTerrainDispel"));
    Expected.Add(TEXT("Abilities.Mage.EmberDetonation|/Script/test_25d.WorkshopEmberDetonation"));
    Expected.Add(TEXT("Abilities.Mage.TidalMend|/Script/test_25d.WorkshopTidalMend"));
    Expected.Add(TEXT("Abilities.Mage.FrostAegis|/Script/test_25d.WorkshopFrostAegis"));
    Expected.Add(TEXT("Abilities.Mage.LavaFreeze|/Script/test_25d.WorkshopLavaFreeze"));
    Expected.Add(TEXT("Abilities.Mage.FrostCorrode|/Script/test_25d.WorkshopFrostCorrode"));
    Expected.Add(TEXT("Abilities.Mage.SteamCondensation|/Script/test_25d.WorkshopSteamCondensation"));
    Expected.Add(TEXT("Abilities.Mage.ConductiveRecharge|/Script/test_25d.WorkshopConductiveRecharge"));
    Expected.Add(TEXT("Abilities.Mage.ThunderMarkBurst|/Script/test_25d.WorkshopThunderMarkBurst"));
    Expected.Add(TEXT("Abilities.Mage.FrostShower|/Script/test_25d.WorkshopFrostShower"));
    Expected.Add(TEXT("Abilities.Mage.WaterShallows|/Script/test_25d.WorkshopWaterShallows"));
    Expected.Add(TEXT("Abilities.Mage.WaterFreeze|/Script/test_25d.WorkshopWaterFreeze"));
    Expected.Add(TEXT("Abilities.Mage.WaterElectrify|/Script/test_25d.WorkshopWaterElectrify"));
    Expected.Add(TEXT("Abilities.Mage.FrostHarvest|/Script/test_25d.WorkshopFrostHarvest"));
    Expected.Add(TEXT("Abilities.Mage.BogHarvest|/Script/test_25d.WorkshopBogHarvest"));
    Expected.Add(TEXT("Abilities.Mage.TidalPhases|/Script/test_25d.WorkshopTidalPhases"));
    Expected.Add(TEXT("Abilities.Mage.EmberRush|/Script/test_25d.WorkshopEmberRush"));
    Expected.Add(TEXT("Abilities.Mage.MistLift|/Script/test_25d.WorkshopMistLift"));
    Expected.Add(TEXT("Abilities.Mage.MistCondense|/Script/test_25d.WorkshopMistCondense"));
    Expected.Add(TEXT("Abilities.Mage.StormMist|/Script/test_25d.WorkshopStormMist"));
    Expected.Add(TEXT("Abilities.Mage.MistDispel|/Script/test_25d.WorkshopMistDispel"));
    Expected.Add(TEXT("Abilities.Mage.StormCatalyst|/Script/test_25d.WorkshopStormCatalyst"));
    Expected.Add(TEXT("Abilities.Mage.FrostCatalyst|/Script/test_25d.WorkshopFrostCatalyst"));
    Expected.Add(TEXT("Abilities.Mage.EmberRupture|/Script/test_25d.WorkshopEmberRupture"));
    Expected.Add(TEXT("Abilities.Mage.ConductiveRelay|/Script/test_25d.WorkshopConductiveRelay"));
    TestEqual(TEXT("Panel retains the actual mage frontends and the native trials"), One.Developer->GetDefinitions().Num(),
              Expected.Num());
    TSet<FName> Ids;
    int32 Protected = 0;
    for (const auto& Item : One.Developer->GetDefinitions())
    {
        const FString Key = Item.Base.AbilityTag.ToString() + TEXT("|") + Item.Base.AbilityClass->GetPathName();
        TestTrue(TEXT("No nonmage class is injected"), Expected.Contains(Key));
        TestFalse(TEXT("Every server lookup ID is nonempty"), Item.DefinitionId.IsNone());
        TestFalse(TEXT("Variants never collide"), Ids.Contains(Item.DefinitionId));
        Ids.Add(Item.DefinitionId);
        const auto* Other = Two.Developer->GetDefinitions().FindByPredicate(
            [&Item](const FAbilityRuntimeDeveloperDefinition& Candidate)
            {
                return Candidate.DefinitionId == Item.DefinitionId;
            });
        if (TestNotNull(TEXT("IDs survive independent owner object names"), Other))
        {
            TestEqual(TEXT("Stable ID resolves exactly the same class"), Other->Base.AbilityClass.Get(),
                      Item.Base.AbilityClass.Get());
        }
        if (!Item.Base.bSelectable)
        {
            ++Protected;
            TestFalse(TEXT("Protected base cannot be temporarily granted"),
                      One.Developer->BeginPreview(Item.DefinitionId, Error));
        }
    }
    TestEqual(TEXT("Existing listener and placeholder passives stay visible but protected"), Protected, 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperValidation,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.AuthoritativeValidationAndRollback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperValidation::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Real petrification definition exists"), Definition) ||
        !TestTrue(TEXT("Server accepts trusted real base"),
                  Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Before = Player.Developer->GetDeveloperState();
    const auto Handle = Player.Developer->GetPreviewHandle();
    TestFalse(TEXT("Client cannot provide arbitrary class/path as ID"),
              Player.Developer->BeginPreview(TEXT("/Game/NotRegistered/Class"), Error));
    const auto OriginalEdit = RadiusEdit(*Player.Developer, 2.f);
    for (const float Invalid :
         {-1.f, 0.f, 17.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        auto Edit = OriginalEdit;
        Edit.Value = Invalid;
        TestFalse(TEXT("Nonfinite/out-of-bounds radius is refused"), Player.Developer->ApplyEdit(Edit, Error));
        TestFalse(TEXT("Refused request includes useful diagnostic"), Error.IsEmpty());
    }
    auto Invalid = OriginalEdit;
    Invalid.ModifierId = TEXT("Reflection.ArbitraryProperty");
    TestFalse(TEXT("Unknown operation is not reflected"), Player.Developer->ApplyEdit(Invalid, Error));
    Invalid = OriginalEdit;
    Invalid.NodeIndex = 99999;
    TestFalse(TEXT("Out-of-range node cannot target another object"), Player.Developer->ApplyEdit(Invalid, Error));
    Invalid = OriginalEdit;
    ++Invalid.ExpectedRevision;
    TestFalse(TEXT("Version mismatch cannot edit the wrong cast definition"),
              Player.Developer->ApplyEdit(Invalid, Error));
    Invalid = OriginalEdit;
    Invalid.ExpectedBaseHash = TEXT("stale-base");
    TestFalse(TEXT("Index requires matching base hash"), Player.Developer->ApplyEdit(Invalid, Error));
    TestEqual(TEXT("Every invalid request leaves accepted revision untouched"),
              Player.Developer->GetDeveloperState().Revision, Before.Revision);
    TestEqual(TEXT("Invalid requests preserve compiled content"), Player.Developer->GetDeveloperState().ContentHash,
              Before.ContentHash);
    TestTrue(TEXT("Invalid requests preserve the exact temporary grant"),
             Player.Developer->GetPreviewHandle() == Handle);
    if (TestTrue(TEXT("Bounded typed edit is accepted"), Player.Developer->ApplyEdit(OriginalEdit, Error)))
    {
        FAbilityRuntimeRecipeSnapshot Snapshot;
        TestTrue(TEXT("Production snapshot is available"),
                 Player.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Snapshot, Error));
        TestEqual(TEXT("Real radius config doubles"), Radius(Snapshot.Graph),
                  Radius(Definition->Base.DefaultGraph) * 2.f);
        TestFalse(TEXT("Already consumed revision cannot be replayed"),
                  Player.Developer->ApplyEdit(OriginalEdit, Error));
    }
    TestTrue(TEXT("Preview can be cleaned up after invalid edits"), Player.Developer->EndPreview(Error));
    Player.State->Profession = EPlayerProfession::Stander;
    TestFalse(TEXT("Nonmage owner cannot bypass profession through RPC"),
              Player.Developer->BeginPreview(Definition->DefinitionId, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperRestoration,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.TemporaryGrantAndExternalSourceRestoration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperRestoration::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Original petrify variant exists"), Definition))
    {
        return false;
    }
    const auto Tag = Definition->Base.AbilityTag;
    auto* OriginalBase = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, F.World);
    CastChecked<UAbilityPetrifyAreaQueryConfig>(OriginalBase->Nodes[0].Config)->Radius = 111.f;
    TestTrue(TEXT("Existing player recipe is registered"), Player.Developer->RegisterBase(Tag, OriginalBase, Error));
    TestTrue(TEXT("Existing equipment-like source is accepted"),
             Player.Developer->SetContribution(Tag, ExternalRadius(TEXT("External.Before"), 1.5f), Error));
    FGameplayAbilitySpec Original(Definition->Base.AbilityClass, 4, INDEX_NONE, F.Target);
    Original.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().InputTag_LMB);
    Original.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
    const auto OriginalHandle = Player.ASC->GiveAbility(Original);
    const auto OriginalTags = Player.ASC->FindAbilitySpecFromHandle(OriginalHandle)->DynamicAbilityTags;
    const int32 Count = Player.ASC->GetActivatableAbilities().Num();
    if (!TestTrue(TEXT("Preview starts without borrowing original spec"),
                  Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto PreviewHandle = Player.Developer->GetPreviewHandle();
    TestTrue(TEXT("Preview owns a new independent handle"), PreviewHandle.IsValid() && PreviewHandle != OriginalHandle);
    TestTrue(TEXT("Only new handle is owned by developer"), Player.Developer->OwnsTemporaryAbility(PreviewHandle));
    TestFalse(TEXT("Permanent handle is never claimed"), Player.Developer->OwnsTemporaryAbility(OriginalHandle));
    TestEqual(TEXT("Exactly one temporary spec is added"), Player.ASC->GetActivatableAbilities().Num(), Count + 1);
    TestTrue(TEXT("Original input/status tags stay byte-equivalent"),
             Player.ASC->FindAbilitySpecFromHandle(OriginalHandle)->DynamicAbilityTags == OriginalTags);
    TestTrue(TEXT("Preview edit is applied"), Player.Developer->ApplyEdit(RadiusEdit(*Player.Developer, 2.f), Error));
    TestTrue(TEXT("Another system can add a later source"),
             Player.Developer->SetContribution(Tag, ExternalRadius(TEXT("External.During"), 1.1f), Error));
    TestTrue(TEXT("Close merges outside sources while restoring its former base"), Player.Developer->EndPreview(Error));
    TestNull(TEXT("Preview grant is removed"), Player.ASC->FindAbilitySpecFromHandle(PreviewHandle));
    const auto* Restored = Player.ASC->FindAbilitySpecFromHandle(OriginalHandle);
    if (TestNotNull(TEXT("Original permanent grant remains"), Restored))
    {
        TestEqual(TEXT("Original level is preserved"), Restored->Level, 4);
        TestTrue(TEXT("Original input and status are preserved"), Restored->DynamicAbilityTags == OriginalTags);
        TestEqual(TEXT("Original source ownership is preserved"), Restored->SourceObject.Get(),
                  static_cast<UObject*>(F.Target));
    }
    TestEqual(TEXT("Spec count exactly returns to original"), Player.ASC->GetActivatableAbilities().Num(), Count);
    FAbilityRuntimeRecipeSnapshot After;
    TestTrue(TEXT("Restored production recipe remains usable"),
             Player.Developer->CreateCastSnapshot(Tag, F.World, After, Error));
    TestTrue(TEXT("Both outside sources survive and own preview multiplier is gone"),
             FMath::IsNearlyEqual(Radius(After.Graph), 111.f * 1.5f * 1.1f));
    TestTrue(TEXT("Repeated close is idempotent"), Player.Developer->EndPreview(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperIsolation,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.OwnerIsolationAndBaseConflict",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperIsolation::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer One(F);
    FPlayer Two(F);
    FString Error;
    if (!TestTrue(TEXT("First catalog initializes"), One.Developer->InitializeCatalog(nullptr, Error)) ||
        !TestTrue(TEXT("Second catalog initializes"), Two.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*One.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("First owner starts"), One.Developer->BeginPreview(Definition->DefinitionId, Error)) ||
        !TestTrue(TEXT("Second owner starts"), Two.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    const auto OtherState = Two.Developer->GetDeveloperState();
    const auto OtherHandle = Two.Developer->GetPreviewHandle();
    TestTrue(TEXT("First player accepts its own change"),
             One.Developer->ApplyEdit(RadiusEdit(*One.Developer, 2.f), Error));
    TestEqual(TEXT("Other player's compiled hash does not change"), Two.Developer->GetDeveloperState().ContentHash,
              OtherState.ContentHash);
    TestEqual(TEXT("Other player's revision does not change"), Two.Developer->GetDeveloperState().Revision,
              OtherState.Revision);
    auto* ChangedBase = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, F.World);
    CastChecked<UAbilityPetrifyAreaQueryConfig>(ChangedBase->Nodes[0].Config)->Radius = 777.f;
    TestTrue(TEXT("External authoritative base can change"),
             One.Developer->RegisterBase(Definition->Base.AbilityTag, ChangedBase, Error));
    TestFalse(TEXT("Closing must not overwrite a base changed by another owner"), One.Developer->EndPreview(Error));
    TestFalse(TEXT("Conflict has a visible diagnostic"), Error.IsEmpty());
    FAbilityRuntimeRecipeSnapshot Snapshot;
    TestTrue(TEXT("External base is still intact"),
             One.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Snapshot, Error));
    TestTrue(TEXT("External base is not overwritten by old saved base"), Radius(Snapshot.Graph) >= 777.f);
    TestTrue(TEXT("Other player's temporary handle still exists"),
             Two.ASC->FindAbilitySpecFromHandle(OtherHandle) != nullptr);
    TestTrue(TEXT("Other player closes independently"), Two.Developer->EndPreview(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperExternalReceipt,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.ExternalRevisionReceipt",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperExternalReceipt::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("Preview starts"), Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    UAbilityRuntimeRecipeComponent* ProductionAPI = Player.Developer;
    TestTrue(TEXT("Other subsystem edits through the production base API"),
             ProductionAPI->SetContribution(Definition->Base.AbilityTag, ExternalRadius(TEXT("Outside.Event"), 1.5f),
                                            Error));
    TestEqual(TEXT("Owner receipt immediately reflects outside successful revision"),
              Player.Developer->GetDeveloperState().Revision, ProductionAPI->GetRevision(Definition->Base.AbilityTag));
    FAbilityRuntimeRecipeSnapshot Snapshot;
    TestTrue(TEXT("Authoritative compiled snapshot is available"),
             ProductionAPI->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Snapshot, Error));
    TestEqual(TEXT("Owner receipt immediately reflects outside compiled content"),
              Player.Developer->GetDeveloperState().ContentHash, Snapshot.ContentHash);
    TestTrue(TEXT("Next panel edit can use its refreshed revision"),
             Player.Developer->ApplyEdit(RadiusEdit(*Player.Developer, 2.f), Error));
    TestTrue(TEXT("Close restores around outside edit"), Player.Developer->EndPreview(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperAbandon,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.AbandonConflictKeepsExternalBase",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperAbandon::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("Preview starts"), Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    const auto Handle = Player.Developer->GetPreviewHandle();
    TestTrue(TEXT("Own preview source is accepted"),
             Player.Developer->ApplyEdit(RadiusEdit(*Player.Developer, 2.f), Error));
    auto* External = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, F.World);
    CastChecked<UAbilityPetrifyAreaQueryConfig>(External->Nodes[0].Config)->Radius = 333.f;
    TestTrue(TEXT("Another system replaces base"),
             Player.Developer->RegisterBase(Definition->Base.AbilityTag, External, Error));
    TestTrue(TEXT("Another system owns its later modifier"),
             Player.Developer->SetContribution(Definition->Base.AbilityTag, ExternalRadius(TEXT("Outside.Keep"), 1.5f),
                                               Error));
    TestFalse(TEXT("Normal restore explicitly refuses overwrite"), Player.Developer->EndPreview(Error));
    if (TestTrue(TEXT("Explicit abandonment permits exit after conflict"), Player.Developer->AbandonPreview(Error)))
    {
        TestFalse(TEXT("Preview is no longer active"), Player.Developer->GetDeveloperState().bPreviewActive);
        TestNull(TEXT("Exact temporary grant is removed"), Player.ASC->FindAbilitySpecFromHandle(Handle));
        FAbilityRuntimeRecipeSnapshot Snapshot;
        TestTrue(TEXT("External recipe remains usable"),
                 Player.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Snapshot, Error));
        TestEqual(TEXT("External base and modifier survive; preview multiplier is gone"), Radius(Snapshot.Graph),
                  499.5f);
        TestTrue(TEXT("Repeated abandon is idempotent"), Player.Developer->AbandonPreview(Error));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperWrongSelection,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.SelectionMustMatchActiveDefinition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperWrongSelection::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("Preview starts"), Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    auto Edit = RadiusEdit(*Player.Developer, 2.f);
    Edit.DefinitionId = TEXT("A-different-visible-dropdown-row");
    const auto Before = Player.Developer->GetDeveloperState();
    TestFalse(TEXT("UI selection changed without Begin cannot edit previously active definition"),
              Player.Developer->ApplyEdit(Edit, Error));
    TestEqual(TEXT("Rejected selection preserves accepted graph"), Player.Developer->GetDeveloperState().ContentHash,
              Before.ContentHash);
    TestEqual(TEXT("Rejected selection preserves version"), Player.Developer->GetDeveloperState().Revision,
              Before.Revision);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperCloseAck,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.CloseAcknowledgesExactRequest",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperCloseAck::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("Preview starts"), Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    const auto Handle = Player.Developer->GetPreviewHandle();
    TestFalse(TEXT("An older request leaves a prior error"),
              Player.Developer->BeginPreview(TEXT("not-registered"), Error));
    const FGuid Request = FGuid::NewGuid();
    FGuid Acknowledged;
    bool bSucceeded = false;
    int32 Replies = 0;
    Player.Developer->OnPreviewCloseAcknowledged.AddLambda(
        [&](FGuid Id, bool bSuccess)
        {
            Acknowledged = Id;
            bSucceeded = bSuccess;
            ++Replies;
        });
    Player.Developer->ServerClosePreview(Request, false);
    TestEqual(TEXT("Exactly one reliable close result is produced"), Replies, 1);
    TestTrue(TEXT("Reply preserves the caller's exact request ID"), Acknowledged == Request);
    TestTrue(TEXT("Prior unrelated error cannot convert successful close to failure"), bSucceeded);
    TestNull(TEXT("Acknowledged success corresponds to actual grant removal"),
             Player.ASC->FindAbilitySpecFromHandle(Handle));
    TestFalse(TEXT("Acknowledged snapshot reports inactive preview"),
              Player.Developer->GetDeveloperState().bPreviewActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeRecipeDeveloperTypedTemplates,
    "test_25d.UnifiedGA.DynamicMage.Developer.TypedPropagationAndPayloadTemplatesAreTransactional",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperTypedTemplates::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Actual catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Player.Developer->GetDefinitions().FindByPredicate(
        [](const FAbilityRuntimeDeveloperDefinition& Item)
        {
            return GetPathNameSafe(Item.Base.AbilityClass.Get()) ==
                   TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C");
        });
    if (!TestNotNull(TEXT("Original native FireBolt frontend exists"), Definition) ||
        !TestTrue(TEXT("Exact temporary preview starts"),
                  Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeDeveloperEdit Edit;
    Edit.DefinitionId = Definition->DefinitionId;
    Edit.ExpectedBaseHash = Definition->BaseHash;
    Edit.ModifierId = TEXT("Propagation.Sequential");
    Edit.NodeIndex = Definition->Base.DefaultGraph->Nodes.IndexOfByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Delivery.ProjectileAwait");
        });
    if (!TestTrue(TEXT("Actual default has an independent real delivery axis"), Edit.NodeIndex != INDEX_NONE))
    {
        return false;
    }
    Edit.Count = 3;
    Edit.Interval = .2f;
    Edit.Radius = 420.f;
    Edit.Falloff = .75f;
    Edit.ExpectedRevision = Player.Developer->GetDeveloperState().Revision;
    if (!TestTrue(TEXT("Trusted template replaces delivery without a preauthored combined GA"),
                  Player.Developer->ApplyEdit(Edit, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot First;
    TestTrue(TEXT("Edited runtime graph is available to the actual frontend"),
             Player.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, First, Error));
    const auto* Propagation = Cast<UAbilityPropagationNodeConfig>(First.Graph->Nodes[Edit.NodeIndex].Config);
    if (TestNotNull(TEXT("Replacement uses the registered independent propagation config"), Propagation))
    {
        TestTrue(TEXT("Original immutable native launch parameters are retained"),
                 Propagation->bUseNativeLaunchParameters);
        TestEqual(TEXT("Count is explicit"), Propagation->MaxTargets, 3);
        TestEqual(TEXT("Delay is explicit"), Propagation->JumpDelay, .2f);
        TestEqual(TEXT("Radius is explicit"), Propagation->JumpRadius, 420.f);
        TestEqual(TEXT("Falloff is explicit"), Propagation->DamageFalloff, .75f);
    }
    const auto Accepted = Player.Developer->GetDeveloperState();
    for (const int32 InvalidCount : {0, 65})
    {
        Edit.Count = InvalidCount;
        Edit.ExpectedRevision = Accepted.Revision;
        TestFalse(TEXT("Out-of-bound template request is rejected"), Player.Developer->ApplyEdit(Edit, Error));
        TestEqual(TEXT("Failed edit retains accepted graph"), Player.Developer->GetDeveloperState().ContentHash,
                  Accepted.ContentHash);
    }
    Edit.Count = 3;
    Edit.ModifierId = TEXT("Append.Status.Petrify");
    Edit.NodeIndex = Definition->Base.DefaultGraph->Nodes.IndexOfByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Effect.InputDamage");
        });
    Edit.Value = 1.25f;
    Edit.ExpectedRevision = Accepted.Revision;
    if (TestTrue(TEXT("A separate payload template composes with the existing propagation axis"),
                 Player.Developer->ApplyEdit(Edit, Error)))
    {
        FAbilityRuntimeRecipeSnapshot Second;
        TestTrue(TEXT("New recipe snapshot compiles"),
                 Player.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Second, Error));
        TestTrue(TEXT("Both independent axes exist in the runtime graph"),
                 Second.Graph->Nodes.ContainsByPredicate(
                     [](const FAbilityGraphNode& Node)
                     {
                         return Node.TypeId == TEXT("Propagation.Sequential");
                     }) &&
                     Second.Graph->Nodes.ContainsByPredicate(
                         [](const FAbilityGraphNode& Node)
                         {
                             return Node.TypeId == TEXT("Status.Petrify");
                         }));
        TestFalse(TEXT("In-flight prior snapshot is immutable"), First.Graph->Nodes.ContainsByPredicate(
                                                                     [](const FAbilityGraphNode& Node)
                                                                     {
                                                                         return Node.TypeId == TEXT("Status.Petrify");
                                                                     }));
    }
    TestTrue(TEXT("Closing preview restores original recipe ownership"), Player.Developer->EndPreview(Error));
    TestFalse(TEXT("Developer template sources do not leak into the permanent skill"),
              Player.Developer->HasContributions(Definition->Base.AbilityTag));
    return true;
}
#endif
