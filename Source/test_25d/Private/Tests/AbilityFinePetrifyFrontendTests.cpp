// 测试链：真实石化目录/原ASC授予 -> 开发配方事务 -> 原立即施法前端 -> 实际Actor/Mass状态、费用与冷却。
// 定义分解和真实前端消费分别验证；不写资源，不以图中存在节点代替状态已生效。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityFinePetrifyFrontendTests
{
    const FString ClassPath = TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C");

    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        AMyPlayerState* State;
        ADungeonPlayerRunTestPawn* Pawn;
        UMyAbilitySystemComponent* PlayerASC;
        UAbilityRuntimeRecipeDeveloperComponent* Developer;

        FWorld()
        {
            Source->Destroy();
            State = World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Pawn->Tags = {TEXT("Player")};
            Pawn->AttachState(State);
            PlayerASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            PlayerASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 10000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
            Prepare(*Target);
            Target->SetActorLocation(FVector(100, 0, 0));
        }

        ~FWorld()
        {
            FString Error;
            Developer->AbandonPreview(Error);
            PlayerASC->CancelAllAbilities();
            PlayerASC->ClearAllAbilities();
        }

        /** 范围测试只保留真实敌人胶囊，排除默认血条UI平面造成的夹具范围扩张。 */
        static void Prepare(AEnemyCharacter& Enemy)
        {
            Enemy.Tags = {TEXT("Enemy")};
            for (auto* Component : TInlineComponentArray<UPrimitiveComponent*>(&Enemy))
            {
                Component->SetCollisionEnabled(Component == Enemy.GetCapsuleComponent()
                                                   ? ECollisionEnabled::QueryOnly
                                                   : ECollisionEnabled::NoCollision);
            }
            Enemy.GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Enemy.GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
            Enemy.GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            Enemy.GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        }

        AEnemyCharacter* Enemy(FVector Position)
        {
            auto* Result = World->SpawnActor<AEnemyCharacter>();
            Result->GetAbilitySystemComponent()->InitAbilityActorInfo(Result, Result);
            Result->GetAbilitySystemComponent()->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Result));
            Result->SetActorLocation(Position);
            Prepare(*Result);
            return Result;
        }

        int32 Mass(FVector Position)
        {
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->Speed = 0.f;
            Type->XPReward = 0;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            return Sim->SpawnMonster(Type, Position);
        }

        bool MassPetrified(int32 Id) const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View) && View.bPetrified;
        }

        const FAbilityRuntimeDeveloperDefinition* Definition(FString& Error)
        {
            if (!Developer->InitializeCatalog(nullptr, Error))
            {
                return nullptr;
            }
            return Developer->GetDefinitions().FindByPredicate(
                [](const FAbilityRuntimeDeveloperDefinition& Item)
                {
                    return GetPathNameSafe(Item.Base.AbilityClass.Get()) == ClassPath;
                });
        }
    };
} // namespace AbilityFinePetrifyFrontendTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFinePetrifyExport,
                                 "test_25d.DynamicMage.SpecialPetrify.DefaultSeparatesQueryAndBatchStatus",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFinePetrifyExport::RunTest(const FString&)
{
    using namespace AbilityFinePetrifyFrontendTests;
    FWorld F;
    FString Error;
    const auto* Row = F.Definition(Error);
    if (!TestNotNull(TEXT("Actual original catalog variant loads"), Row) ||
        !TestNotNull(TEXT("Actual catalog variant exports a default graph"), Row->Base.DefaultGraph.Get()))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("Selection is an independently replaceable node"), Row->Base.DefaultGraph->Nodes.ContainsByPredicate(
                                                                         [](const FAbilityGraphNode& Node)
                                                                         {
                                                                             return Node.TypeId ==
                                                                                    TEXT("Query.PetrifyArea");
                                                                         }));
    TestTrue(TEXT("Applying petrify is an independently replaceable batch payload"),
             Row->Base.DefaultGraph->Nodes.ContainsByPredicate(
                 [](const FAbilityGraphNode& Node)
                 {
                     return Node.TypeId == TEXT("Status.PetrifyBatch");
                 }));
    TestFalse(TEXT("Default does not wrap the old whole area action"),
              Row->Base.DefaultGraph->Nodes.ContainsByPredicate(
                  [](const FAbilityGraphNode& Node)
                  {
                      return Node.TypeId == TEXT("Status.PetrifyArea");
                  }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFinePetrifyPreviewRadius,
                                 "test_25d.DynamicMage.SpecialPetrify.RealFrontendConsumesPanelRadiusSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFinePetrifyPreviewRadius::RunTest(const FString&)
{
    using namespace AbilityFinePetrifyFrontendTests;
    for (const bool bPreview : {false, true})
    {
        FWorld F;
        FString Error;
        const auto* Row = F.Definition(Error);
        if (!TestNotNull(TEXT("Actual source identity exists"), Row))
        {
            AddInfo(Error);
            return false;
        }
        const auto* Defaults = CastChecked<UBatch6PetrifyAbility>(Row->Base.AbilityClass->GetDefaultObject());
        const float OriginalRadius = Defaults->Radius;
        F.Target->SetActorLocation(FVector(OriginalRadius * .2f, 0, 0));
        auto* Far = F.Enemy(FVector(OriginalRadius * 1.5f, 0, 0));
        const int32 NearMass = F.Mass(FVector(OriginalRadius * .3f, OriginalRadius * .1f, 0));
        const int32 FarMass = F.Mass(FVector(OriginalRadius * 1.5f, OriginalRadius * .1f, 0));
        FGameplayAbilitySpecHandle Handle;
        if (bPreview)
        {
            if (!TestTrue(TEXT("Exact preview lease begins without synthetic contributions"),
                          F.Developer->BeginPreview(Row->DefinitionId, Error)))
            {
                AddInfo(Error);
                return false;
            }
            Handle = F.Developer->GetPreviewHandle();
            TestFalse(TEXT("Unmodified preview truly starts with zero contributions"),
                      F.Developer->HasContributions(Row->Base.AbilityTag));
            FAbilityRuntimeDeveloperEdit Edit;
            Edit.DefinitionId = Row->DefinitionId;
            Edit.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
            Edit.ExpectedBaseHash = Row->BaseHash;
            Edit.ModifierId = TEXT("Scale.Radius");
            Edit.NodeIndex = 0;
            Edit.Value = 2.f;
            if (!TestTrue(TEXT("Trusted radius edit compiles before actual activation"),
                          F.Developer->ApplyEdit(Edit, Error)))
            {
                AddInfo(Error);
                return false;
            }
        }
        else
        {
            FGameplayAbilitySpec Spec(Row->Base.AbilityClass, 1, INDEX_NONE, F.State);
            Handle = F.PlayerASC->GiveAbility(Spec);
        }
        const float ManaBefore = F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        if (!TestTrue(TEXT("Original actual ServerOnly GA activates through its original ASC"),
                      F.PlayerASC->TryActivateAbility(Handle)))
        {
            return false;
        }
        TestTrue(TEXT("Actual near Actor state applies"), F.Target->GetMesh()->bPauseAnims);
        TestTrue(TEXT("Actual near Mass state applies"), F.MassPetrified(NearMass));
        TestEqual(TEXT("Status-only reconstruction adds no hidden inherited prototype damage"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  100.f);
        TestEqual(TEXT("Far Actor follows original radius or the accepted runtime radius exactly"),
                  Far->GetMesh()->bPauseAnims, bPreview);
        TestEqual(TEXT("Far Mass follows the same accepted runtime radius"), F.MassPetrified(FarMass), bPreview);
        TestEqual(TEXT("Original cost is paid exactly once"),
                  F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                  ManaBefore - Defaults->Prototype->ManaCost);
        TestEqual(TEXT("Original cooldown still uses the original tag and timing contract"),
                  F.PlayerASC->HasMatchingGameplayTag(Defaults->Prototype->CooldownTag),
                  Defaults->Prototype->Cooldown > 0.f);
        TestFalse(TEXT("Original immediate frontend finishes after its actual batch"),
                  F.PlayerASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    }
    return true;
}
#endif
