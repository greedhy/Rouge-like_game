// 测试链：真实原型目录/开发事务 -> 已接受snapshot -> 原随动或库存时钟 -> 新纯区域状态分支。
// 先以同源零编辑图证明325周期合同，再验证运行时追加不会借有限64额度截断原开关；不修改原DA。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeOrbitNodes.h"
#include "UObject/UnrealType.h"
#include "Misc/AutomationTest.h"

namespace AbilityPrototypeSustainedPayloadTests
{
    /** 只选择当前正式目录实际拥有的无限根，不用配置Outer猜测所属图或伪造来源身份。 */
    const FAbilityRuntimeDeveloperDefinition* Find(FAbilityActualMageFrontendFixture& F, bool bOrbit, bool bActor,
                                                   FString& Error)
    {
        if (!F.Developer->InitializeCatalog(nullptr, Error))
        {
            return nullptr;
        }
        for (const auto& Row : F.Developer->GetDefinitions())
        {
            if (!Row.Base.bSelectable || !Row.Base.DefaultGraph || !Row.Base.AbilityClass ||
                !Row.Base.AbilityClass->IsChildOf<UPrototypeAbility>())
            {
                continue;
            }
            const auto* Entry = Row.Base.DefaultGraph->Nodes.FindByPredicate(
                [&](const FAbilityGraphNode& Node)
                {
                    return Node.NodeId == Row.Base.DefaultGraph->EntryNodeId;
                });
            const auto* Scope = Entry ? Cast<UAbilityPrototypeScopeNodeConfig>(Entry->Config) : nullptr;
            if (!Scope || !Scope->Parameters || Scope->LogicalDepth != 0 ||
                Scope->Profile != (bActor ? TEXT("PrototypeFine.Actor.v1") : TEXT("PrototypeFine.Mixed.v1")))
            {
                continue;
            }
            const auto* Field = Cast<UBatch6FieldPrototype>(Scope->Parameters);
            if (bOrbit ? Scope->Parameters->Kind == EAbilityPrototypeKind::Orbit && Scope->Parameters->OrbitSettings
                       : Field && Field->Kind == EAbilityPrototypeKind::Field && Field->bFollowCasterToggle)
            {
                return &Row;
            }
        }
        Error = TEXT("Actual selectable catalog lacks the requested root toggle profile.");
        return nullptr;
    }

    /** 复制已接受版本用于确定性空间步进；施法来源仍是正式PlayerState ASC/原Avatar生命。 */
    UAbilityGraphInstance* Start(FAbilityActualMageFrontendFixture& F, const FAbilityRuntimeDeveloperDefinition& Row,
                                 UAbilityGraphDefinition& Definition, FString& Error)
    {
        auto* Context = NewObject<UPrototypeCastContext>(F.World);
        Context->Caster = F.Pawn;
        Context->Level = 1;
        Context->CastId = FGuid::NewGuid();
        Context->DamageParams.SourceAbilitySystemComponent = F.PlayerASC;
        const auto* Defaults = Row.Base.AbilityClass->GetDefaultObject<UPrototypeAbility>();
        const auto* Property = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("DamageEffectClass"));
        Context->DamageParams.DamageGameplayEffectClass =
            Property ? Cast<UClass>(Property->GetObjectPropertyValue_InContainer(Defaults)) : nullptr;
        if (!Context->CaptureSourceLife(Error))
        {
            return nullptr;
        }
        FAbilityNodeInput Input;
        Input.SourceToken = Context->GetSourceLife();
        Input.Context = Context;
        Input.Direction = FVector::ForwardVector;
        Input.DamageParams = Context->DamageParams;
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Definition.CompatibilityProfile;
        Options.BudgetPolicy = Definition.BudgetPolicy;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        if (!Run->StartWithInput(&Definition, Input, Options, Error) ||
            (Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed))
        {
            return nullptr;
        }
        return Run;
    }

    /** 每周期使用真实原速率，原型Orbit的SpawnInterval不冒充未消费的Scope.Period可调轴。 */
    bool Exercise(FAutomationTestBase& Test, bool bOrbit, bool bActor)
    {
        float OriginalMana = 0.f;
        for (const bool bModified : {false, true})
        {
            FAbilityActualMageFrontendFixture F;
            F.PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100000.f);
            F.PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100000.f);
            F.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),
                                                                           1000000000.f);
            F.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),
                                                                           1000000000.f);
            FString Error;
            const auto* Row = Find(F, bOrbit, bActor, Error);
            if (!Test.TestNotNull(TEXT("Real root toggle catalog/profile is present"), Row) ||
                !Test.TestTrue(TEXT("Actual toggle preview begins"),
                               F.Developer->BeginPreview(Row->DefinitionId, Error)))
            {
                Test.AddError(Error);
                return false;
            }
            const auto* Root = Row->Base.DefaultGraph->Nodes.FindByPredicate(
                [&](const FAbilityGraphNode& Node)
                {
                    return Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
                });
            const auto* Scope = CastChecked<UAbilityPrototypeScopeNodeConfig>(Root->Config);
            const float Delta =
                bOrbit ? Scope->Parameters->OrbitSettings->SpawnInterval + .001f : Scope->Parameters->Period;
            const int32 Body = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
                [&](const FAbilityGraphNode& Node)
                {
                    return Node.TypeId ==
                           (bOrbit ? TEXT("Control.PrototypeOrbitInventory") : TEXT("Control.PrototypeFollowField"));
                });
            if (!Test.TestTrue(TEXT("Actual default exposes the independently repeated period"), Body != INDEX_NONE))
            {
                return false;
            }
            if (bModified)
            {
                FAbilityRuntimeDeveloperEdit Edit;
                Edit.DefinitionId = Row->DefinitionId;
                Edit.ModifierId = TEXT("Append.Registered");
                Edit.FragmentId = TEXT("Area.Petrify");
                Edit.OutputPortId = TEXT("Period");
                Edit.NodeIndex = Body;
                Edit.Value = 2.f;
                Edit.Radius = 350.f;
                Edit.ExpectedBaseHash = Row->BaseHash;
                Edit.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
                if (!Test.TestTrue(TEXT("Trusted parser accepts a verified pure payload on the repeated period"),
                                   F.Developer->ApplyEdit(Edit, Error)))
                {
                    Test.AddError(Error);
                    return false;
                }
            }
            FAbilityRuntimeRecipeSnapshot Accepted;
            if (!Test.TestTrue(TEXT("Actual accepted immutable toggle snapshot is available"),
                               F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Accepted, Error)))
            {
                Test.AddError(Error);
                return false;
            }
            auto* Run = Start(F, *Row, *Accepted.Graph, Error);
            if (!Test.TestNotNull(TEXT("Actual accepted toggle starts without bypassing source capture"), Run))
            {
                Test.AddError(Error);
                return false;
            }
            int32 PurePeriods = 0;
            Run->OnEvent.AddLambda(
                [&](const FAbilityGraphEvent& Event)
                {
                    if (Event.NodeType == TEXT("Status.PetrifyBatch") && Event.OutputPort == TEXT("Completed"))
                    {
                        ++PurePeriods;
                    }
                });
            for (int32 Index = 0; Index < 325 && !Run->IsFinished(); ++Index)
            {
                ++GFrameCounter;
                Run->Advance(Delta);
            }
            const FString Label = bModified ? TEXT("Runtime pure branch") : TEXT("Actual unchanged control");
            Test.TestFalse(*(Label + TEXT(" retains its root after325 actual periods")), Run->IsFinished());
            const float Mana = F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
            if (!bModified)
            {
                OriginalMana = Mana;
            }
            else
            {
                Test.TestEqual(TEXT("Added pure branch runs all325 actual periods rather than stopping at64"),
                               PurePeriods, 325);
                Test.TestEqual(TEXT("Added pure work preserves exact original profile upkeep"), Mana, OriginalMana);
                Test.TestTrue(TEXT("The repeated payload changes real target state"), F.IsPetrified());
            }
            if (bOrbit)
            {
                Test.TestTrue(TEXT("Idle original inventory stays bounded by its real authored maximum"),
                              Run->GetActivePhysicalSpawnCount() <= Scope->Parameters->OrbitSettings->MaxFireBolts);
            }
            Run->Cancel();
            const auto Events = Run->GetAcceptedEventCount();
            const float EndMana = F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
            const auto Transform = F.Target->GetActorTransform();
            F.Target->EnableDungeonPooling();
            F.Target->SleepForDungeonPool(false);
            Test.TestTrue(TEXT("Real pool reuse creates the next target life"),
                          F.Target->WakeFromDungeonPool(Transform, NAME_None));
            ++GFrameCounter;
            Run->Advance(Delta * 3.f);
            Test.TestEqual(TEXT("Cancelled repeated slot cannot publish into a new target life"),
                           Run->GetAcceptedEventCount(), Events);
            Test.TestEqual(TEXT("Cancelled original upkeep no longer consumes mana"),
                           F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), EndMana);
            Test.TestFalse(TEXT("Cancelled pure branch cannot petrify the woken target"), F.IsPetrified());
            Test.TestEqual(TEXT("All actual original physical resources are released on cancellation"),
                           Run->GetActivePhysicalSpawnCount(), 0);
            Test.TestEqual(TEXT("The real directory definition was never rewritten"),
                           AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph), Row->BaseHash);
        }
        return true;
    }
} // namespace AbilityPrototypeSustainedPayloadTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFollowPureSustained,
                                 "test_25d.DynamicMage.PrototypeSustained.FollowPurePayload325ActualPeriods",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeFollowPureSustained::RunTest(const FString&)
{
    return AbilityPrototypeSustainedPayloadTests::Exercise(*this, false, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeOrbitPureSustained,
                                 "test_25d.DynamicMage.PrototypeSustained.OrbitPurePayload325ActualPeriods",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeOrbitPureSustained::RunTest(const FString&)
{
    return AbilityPrototypeSustainedPayloadTests::Exercise(*this, true, true) &&
           AbilityPrototypeSustainedPayloadTests::Exercise(*this, true, false);
}
#endif
