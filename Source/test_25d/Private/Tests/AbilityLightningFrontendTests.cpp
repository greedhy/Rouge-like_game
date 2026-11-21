// 测试链：实际闪电目录 -> BeginPreview/可信Append事务 -> 原ASC输入与显式客户端包夹具 -> 原TargetData -> 真实Mass命中。
// 此为服务器前端整合，不声称NullRHI具备本地鼠标/真实网络；原GA策略、费用和资产配置均保持不变。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "Misc/AutomationTest.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityLightningFrontendTests
{
    struct FPlayer : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        AMyPlayerState* State = nullptr;
        ADungeonPlayerRunTestPawn* Pawn = nullptr;
        UMyAbilitySystemComponent* PlayerASC = nullptr;
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;
        UPureMassSubsystem* Mass = nullptr;

        FPlayer()
        {
            Source->Destroy();
            Target->Destroy();
            State = World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Pawn->Tags = {TEXT("Player")};
            Pawn->AttachState(State);
            PlayerASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            PlayerASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 10000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 10000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
            auto* Controller = World->SpawnActor<AAbilityGraphRemoteTestController>();
            Controller->SetPawn(Pawn);
            PlayerASC->AbilityActorInfo->PlayerController = Controller;
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
        }

        ~FPlayer()
        {
            FString Error;
            Developer->AbandonPreview(Error);
            PlayerASC->CancelAllAbilities();
            PlayerASC->ClearAllAbilities();
        }

        int32 Spawn(FVector Position)
        {
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 10000.f;
            Type->Speed = 0.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            Type->XPReward = 0;
            return Mass->SpawnMonster(Type, Position);
        }

        FPureMassView View(int32 Id) const
        {
            FPureMassView Result;
            Mass->Resolve(Id, Result);
            return Result;
        }

        /** 正式图任务读取实际世界经过时间，不伪造把施法之前整帧Delta算入首跳。 */
        void Step(float Delta)
        {
            ++GFrameCounter;
            World->Tick(LEVELTICK_TimeOnly, Delta);
            PlayerASC->TickComponent(Delta, LEVELTICK_All, &PlayerASC->PrimaryComponentTick);
        }
    };
} // namespace AbilityLightningFrontendTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLightningFrontendSnapshot,
    "test_25d.DynamicMage.SpecialLightning.ServerTransportActualFrontendConsumesPanelSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightningFrontendSnapshot::RunTest(const FString&)
{
    using namespace AbilityLightningFrontendTests;
    for (const bool bPreview : {false, true})
    {
        for (const bool bSequential : {false, true})
        {
            FPlayer F;
            FString Error;
            if (!TestTrue(TEXT("Actual Mage catalog initializes"), F.Developer->InitializeCatalog(nullptr, Error)))
            {
                AddInfo(Error);
                continue;
            }
            const FString Path = bSequential ? TEXT("/Game/CompositionBatch6/GA_SequentialChain.GA_SequentialChain_C")
                                             : TEXT("/Game/CompositionBatch6/GA_FissionChain.GA_FissionChain_C");
            const auto* Row = F.Developer->GetDefinitions().FindByPredicate(
                [&Path](const FAbilityRuntimeDeveloperDefinition& Item)
                {
                    return GetPathNameSafe(Item.Base.AbilityClass.Get()) == Path;
                });
            if (!TestNotNull(TEXT("Real original special frontend is catalogued"), Row))
            {
                continue;
            }
            AddInfo(
                TEXT("ActualFrontendClass=") + GetPathNameSafe(Row->Base.AbilityClass.Get()) +
                TEXT(
                    "; Gate=ASC Pressed/Held/explicit prediction transport/ServerTargetData; Cursor/Network=unverified"));
            const auto InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.DeveloperRecipe"));
            FGameplayAbilitySpecHandle Handle;
            if (bPreview)
            {
                if (!TestTrue(TEXT("Preview grants its exact temporary original spec"),
                              F.Developer->BeginPreview(Row->DefinitionId, Error)))
                {
                    AddInfo(Error);
                    continue;
                }
                Handle = F.Developer->GetPreviewHandle();
                TestFalse(TEXT("The actual default has no synthetic contribution"),
                          F.Developer->HasContributions(Row->Base.AbilityTag));
                FAbilityRuntimeDeveloperEdit Edit;
                Edit.DefinitionId = Row->DefinitionId;
                Edit.ExpectedBaseHash = Row->BaseHash;
                Edit.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
                Edit.NodeIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
                    [&Row](const FAbilityGraphNode& Node)
                    {
                        return Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
                    });
                Edit.ModifierId = TEXT("Append.Status.Petrify");
                Edit.Value = 2.f;
                if (!TestTrue(TEXT("Trusted panel edit accepts a real hit-state successor"),
                              F.Developer->ApplyEdit(Edit, Error)))
                {
                    AddInfo(Error);
                    continue;
                }
            }
            else
            {
                FGameplayAbilitySpec Spec(Row->Base.AbilityClass, 1, INDEX_NONE, F.State);
                Spec.DynamicAbilityTags.AddTag(InputTag);
                Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
                Handle = F.PlayerASC->GiveAbility(Spec);
            }
            auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(Handle);
            auto* Ability = Spec ? Cast<UBatch6ChainAbility>(Spec->GetPrimaryInstance()) : nullptr;
            if (!TestNotNull(TEXT("Original per-actor frontend instance exists"), Ability) ||
                !TestNotNull(TEXT("Original configured prototype remains authoritative"), Ability->Prototype.Get()))
            {
                continue;
            }
            const int32 First = F.Spawn(FVector(150, 0, 0));
            const int32 Second = F.Spawn(FVector(300, 0, 0));
            F.PlayerASC->AbilityInputTagPressed(InputTag);
            F.PlayerASC->AbilityInputTagHeld(InputTag);
            TestTrue(TEXT("Original ASC input routes to the selected original spec"), Spec->InputPressed);
            FPredictionKey ClientActivation;
            ClientActivation.Current = 1101;
            F.PlayerASC->CallServerTryActivateAbility(Handle, Spec->InputPressed, ClientActivation);
            if (!TestTrue(TEXT("Explicit client activation packet reaches the original target wait"),
                          Ability->IsActive()))
            {
                AddInfo(Ability->GetRuntimeRecipeError());
                continue;
            }
            if (bPreview)
            {
                TestNotNull(TEXT("Accepted preview freezes a real graph before target data"),
                            Ability->GetRuntimeRecipeSnapshot().Graph.Get());
            }
            auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.Location = Hit->HitResult.ImpactPoint = FVector(150, 0, 0);
            Hit->HitResult.Item = First;
            FGameplayAbilityTargetDataHandle Data;
            Data.Add(Hit);
            const auto Key = Ability->GetCurrentActivationInfo().GetActivationPredictionKey();
            F.PlayerASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), FPredictionKey());
            const float Damage = Ability->Prototype->DamageAtLevel(1);
            if (Ability->Prototype->Cooldown > 0.f)
            {
                TestTrue(TEXT("Original configured cooldown owns a real cooldown tag"),
                         Ability->Prototype->CooldownTag.IsValid());
                TestTrue(TEXT("The same original commit applies its actual cooldown tag"),
                         F.PlayerASC->GetTagCount(Ability->Prototype->CooldownTag) > 0);
            }
            TestEqual(TEXT("Actual first Mass takes original configured damage synchronously"), F.View(First).Health,
                      10000.f - Damage);
            if (bSequential)
            {
                TestEqual(TEXT("Original sequential frontend does not hit its secondary early"), F.View(Second).Health,
                          10000.f);
                F.Step(Ability->JumpDelay + .001f);
            }
            TestEqual(TEXT("Actual secondary uses the original single additional falloff"), F.View(Second).Health,
                      10000.f - Damage * Ability->Prototype->DamageFalloff);
            TestEqual(TEXT("Actual first hit consumes the accepted panel state successor"), F.View(First).bPetrified,
                      bPreview);
            TestEqual(TEXT("Actual secondary consumes the accepted panel state successor"), F.View(Second).bPetrified,
                      bPreview);
            const float ExpectedMana = 10000.f - Ability->Prototype->ManaCost;
            TestEqual(TEXT("Original fee commits exactly once"),
                      F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), ExpectedMana);
            if (bSequential)
            {
                F.Step(Ability->JumpDelay + .001f);
            }
            TestFalse(TEXT("Original frontend finishes when its graph finishes"), Ability->IsActive());
            F.PlayerASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), FPredictionKey());
            TestEqual(TEXT("Replayed target data cannot commit a second fee"),
                      F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), ExpectedMana);
        }
    }
    return true;
}
#endif
