// 测试链：实际MageBatch/Art目录 -> 同一PlayerState ASC正式输入/零编辑开发租约 -> 原TargetData -> 原GE与图生命周期。
// LP能力仅用合法预测包替代客户端传输；不手工RegisterBase，不声称本地鼠标、动画视觉或真实网络已经验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "Algo/Find.h"
#include "AbilitySystem/Unified/MageGraphAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "PureMass/PureMassSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace AbilityActualMageCatalogFrontendTests
{
    struct FCase
    {
        const TCHAR* Name;
        const TCHAR* Directory;
        bool bMass;
        bool bPassive;
        float Mana;
        float PrimaryDamage;
    };

    // 六个MageBatch与三个法师Art；Stander三个Art属于不同前端，本次不借此声称它们已覆盖。
    const FCase Cases[] = {
        {TEXT("MeteorCinders"), TEXT("MageBatch"), true, false, 28.f, 96.f},
        {TEXT("RimeSigil"), TEXT("MageBatch"), true, false, 22.f, 72.f},
        {TEXT("PiercingTempest"), TEXT("MageBatch"), true, false, 20.f, 24.f},
        {TEXT("ArcaneSentry"), TEXT("MageBatch"), true, false, 24.f, 75.f},
        {TEXT("ArcaneMeditation"), TEXT("MageBatch"), false, true, 0.f, 0.f},
        {TEXT("ElementalWard"), TEXT("MageBatch"), false, true, 0.f, 0.f},
        {TEXT("GlacialLance"), TEXT("ArtBatch"), false, false, 20.f, 0.f},
        {TEXT("InfernoVortex"), TEXT("ArtBatch"), false, false, 30.f, 0.f},
        {TEXT("ThunderPrison"), TEXT("ArtBatch"), false, false, 25.f, 0.f},
    };

    /** 正式授予只添加原装备/输入标签；预览授予必须来自真实目录BeginPreview，不安装替换基础。 */
    UMageGraphAbility* Grant(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F,
                             const FAbilityRuntimeDeveloperDefinition& Row, bool bPreview)
    {
        FString Error;
        if (bPreview)
        {
            if (!Test.TestTrue(TEXT("Actual catalog begins an unmodified temporary preview"),
                               F.Developer->BeginPreview(Row.DefinitionId, Error)))
            {
                Test.AddInfo(Error);
                return nullptr;
            }
            F.Handle = F.Developer->GetPreviewHandle();
        }
        else
        {
            FGameplayAbilitySpec Spec(Row.Base.AbilityClass, 1, INDEX_NONE, F.State);
            Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().InputTag_DeveloperRecipe);
            Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
            F.Handle = F.PlayerASC->GiveAbility(Spec);
        }
        const auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        auto* Ability = Spec ? Cast<UMageGraphAbility>(Spec->GetPrimaryInstance()) : nullptr;
        Test.TestNotNull(TEXT("Exact actual asset retains its original PerActor frontend instance"), Ability);
        Test.TestFalse(TEXT("Zero-edit preview is not selected through a synthetic contribution"),
                       F.Developer->HasContributions(Row.Base.AbilityTag));
        return Ability;
    }

    /** 生产输入先路由；无本地视口的LP服务器再显式接收合法客户端激活包，不改GA网络策略。 */
    bool Activate(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F, UMageGraphAbility& Ability,
                  bool bPreview)
    {
        if (bPreview)
        {
            F.Key(IE_Pressed);
        }
        else
        {
            const auto Slot = FMyGameplayTags::Get().InputTag_DeveloperRecipe;
            F.PlayerASC->AbilityInputTagPressed(Slot);
            F.PlayerASC->AbilityInputTagHeld(Slot);
        }
        const auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        if (!Test.TestTrue(TEXT("Production input reaches this exact actual spec's pressed state"),
                           Spec && Spec->InputPressed))
        {
            return false;
        }
        if (!Ability.IsActive() &&
            Ability.GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalPredicted)
        {
            static int16 NextPrediction = 1301;
            FPredictionKey Prediction;
            Prediction.Current = NextPrediction++;
            F.PlayerASC->CallServerTryActivateAbility(F.Handle, Spec->InputPressed, Prediction);
        }
        return Test.TestTrue(TEXT("Original actual frontend accepts input through its configured policy"),
                             Ability.IsActive());
    }

    /** 冻结发生在原激活处；检查真实快照而非另编一个未被前端消费的展示图。 */
    void CheckSnapshot(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F, UMageGraphAbility& Ability,
                       bool bPreview)
    {
        const auto& Snapshot = Ability.GetRuntimeRecipeSnapshot();
        if (!bPreview)
        {
            Test.TestNull(TEXT("Formal zero-source cast keeps its original asset entry"), Snapshot.Graph.Get());
            return;
        }
        if (!Test.TestNotNull(TEXT("Actual temporary spec freezes the accepted panel definition"),
                              Snapshot.Graph.Get()))
        {
            return;
        }
        Test.TestTrue(TEXT("Snapshot belongs only to this exact preview grant"),
                      Ability.GetRuntimeExecutionOwnerLease() == F.Developer->GetPreviewOwnerLease(F.Handle));
        Test.TestEqual(TEXT("Actual frozen hash equals the accepted server panel hash"), Snapshot.ContentHash,
                       F.Developer->GetDeveloperState().ContentHash);
        for (const auto& Node : Snapshot.Graph->Nodes)
        {
            Test.TestFalse(TEXT("Actual preview snapshot exposes no complete legacy macro"),
                           Node.TypeId.ToString().StartsWith(TEXT("Legacy.")) ||
                               (Node.Config && Node.Config->IsA<UAbilityLegacyNodeConfig>()));
        }
    }

    /** 目标由正式PureMassTargetData的GAS复制缓存接收；不直接调用TargetReady或图节点。 */
    void SendTarget(FAbilityActualMageFrontendFixture& F, UMageGraphAbility& Ability, int32 MassId)
    {
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Item = INDEX_NONE;
        if (Ability.TargetMode == EUnifiedAbilityTargetMode::Ground)
        {
            Hit->HitResult.Location = Hit->HitResult.ImpactPoint = FVector(200, 0, 0);
        }
        else if (MassId != INDEX_NONE)
        {
            Hit->HitResult.Location = Hit->HitResult.ImpactPoint = FVector(300, 0, 0);
            Hit->HitResult.Item = MassId;
        }
        else
        {
            Hit->HitResult.Location = Hit->HitResult.ImpactPoint = F.Target->GetActorLocation();
            Hit->HitResult.HitObjectHandle = FActorInstanceHandle(F.Target);
        }
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        F.PlayerASC->ServerSetReplicatedTargetData(F.Handle,
                                                   Ability.GetCurrentActivationInfo().GetActivationPredictionKey(),
                                                   Data, FGameplayTag(), FPredictionKey());
    }

    /** 正常释放按键，不用取消代替有限技能的原始结束；被动由换槽/预览关闭单独收回。 */
    void Release(FAbilityActualMageFrontendFixture& F, bool bPreview)
    {
        if (bPreview)
        {
            F.Key(IE_Released);
        }
        else
        {
            F.PlayerASC->AbilityInputTagReleased(FMyGameplayTags::Get().InputTag_DeveloperRecipe);
        }
    }

    /** 实际原被动保持30秒；停用只撤本次持有的GE，关闭开发租约不能修改原Base属性。 */
    bool Passive(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F,
                 const FAbilityRuntimeDeveloperDefinition& Row, const FCase& Case)
    {
        const auto Regen = UMyAttributeSet::GetManaRegenerationAttribute();
        const auto Fire = UMyAttributeSet::GetFireResistanceAttribute();
        const auto Thunder = UMyAttributeSet::GetThunderResistanceAttribute();
        F.PlayerASC->SetNumericAttributeBase(Regen, 5.f);
        F.PlayerASC->SetNumericAttributeBase(Fire, 3.f);
        F.PlayerASC->SetNumericAttributeBase(Thunder, 4.f);
        const bool bMeditation = FString(Case.Name) == TEXT("ArcaneMeditation");
        for (const bool bPreview : {false, true})
        {
            auto* Ability = Grant(Test, F, Row, bPreview);
            if (!Ability || !Activate(Test, F, *Ability, bPreview))
            {
                return false;
            }
            CheckSnapshot(Test, F, *Ability, bPreview);
            Release(F, bPreview);
            F.Step(30.f);
            Test.TestTrue(TEXT("Actual original passive remains active beyond an ordinary lease duration"),
                          Ability->IsActive());
            Test.TestEqual(TEXT("Actual passive applies its original regeneration magnitude once"),
                           F.PlayerASC->GetNumericAttribute(Regen), bMeditation ? 7.f : 5.f);
            Test.TestEqual(TEXT("Actual passive applies its original fire resistance once"),
                           F.PlayerASC->GetNumericAttribute(Fire), bMeditation ? 3.f : 15.f);
            Test.TestEqual(TEXT("Actual passive applies its original thunder resistance once"),
                           F.PlayerASC->GetNumericAttribute(Thunder), bMeditation ? 4.f : 16.f);
            FString Error;
            if (bPreview)
            {
                Test.TestTrue(TEXT("Closing only the actual preview releases its owned passive GE"),
                              F.Developer->EndPreview(Error));
            }
            else
            {
                F.PlayerASC->DeactivatePassiveAbility.Broadcast(Row.Base.AbilityTag);
            }
            Test.TestFalse(TEXT("Original passive frontend ends on the owning stop path"), Ability->IsActive());
            Test.TestFalse(TEXT("No original target/task/timer remains after passive stop"),
                           Ability->HasPendingGraphWork());
            Test.TestEqual(TEXT("Owned regeneration modifier is removed without changing Base"),
                           F.PlayerASC->GetNumericAttribute(Regen), 5.f);
            Test.TestEqual(TEXT("Owned fire modifier is removed without changing Base"),
                           F.PlayerASC->GetNumericAttribute(Fire), 3.f);
            Test.TestEqual(TEXT("Owned thunder modifier is removed without changing Base"),
                           F.PlayerASC->GetNumericAttribute(Thunder), 4.f);
            if (!bPreview)
            {
                F.PlayerASC->ClearAbility(F.Handle);
            }
        }
        return true;
    }

    /** 原四Mixed图继续走真实Mass血量；原三Art图继续走实际Actor GE，不混改它们的后端合同。 */
    bool Active(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F,
                const FAbilityRuntimeDeveloperDefinition& Row, const FCase& Case)
    {
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        int32 First = INDEX_NONE;
        int32 Second = INDEX_NONE;
        int32 OutsideMass = INDEX_NONE;
        AEnemyCharacter* OutsideActor = nullptr;
        if (Case.bMass)
        {
            F.Target->Destroy();
            Mass->Enable();
            auto* Monster = NewObject<UPureMassMonsterType>(F.World);
            Monster->Health = 1000.f;
            Monster->XPReward = 0;
            First = Mass->SpawnMonster(Monster, FVector(300, 0, 0));
            Second = Mass->SpawnMonster(Monster, FVector(470, 0, 0));
            OutsideMass = Mass->SpawnMonster(Monster, FVector(1500, 900, 0));
        }
        else
        {
            OutsideActor = F.EnemyAt(FVector(9000, 0, 0));
        }
        const auto Health = [&F, Mass](int32 Id)
        {
            if (Id == INDEX_NONE)
            {
                return F.Target->GetAbilitySystemComponent()->GetNumericAttribute(
                    UMyAttributeSet::GetHealthAttribute());
            }
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : -1.f;
        };
        const float Initial = Case.bMass ? 1000.f : 10000.f;
        float OriginalDamage = 0.f;
        float OriginalSecondaryDamage = 0.f;
        for (const bool bPreview : {false, true})
        {
            auto* Ability = Grant(Test, F, Row, bPreview);
            if (!Ability ||
                !Test.TestNotNull(TEXT("Actual original cost remains configured"), Ability->GetCostGameplayEffect()) ||
                !Test.TestNotNull(TEXT("Actual original cooldown remains configured"),
                                  Ability->GetCooldownGameplayEffect()))
            {
                return false;
            }
            F.PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
            int32 Costs = 0;
            int32 Cooldowns = 0;
            const auto* Cost = Ability->GetCostGameplayEffect();
            const auto* Cooldown = Ability->GetCooldownGameplayEffect();
            const auto Hook = F.PlayerASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Applied, FActiveGameplayEffectHandle)
                {
                    Costs += Applied.Def == Cost ? 1 : 0;
                    Cooldowns += Applied.Def == Cooldown ? 1 : 0;
                });
            ON_SCOPE_EXIT
            {
                F.PlayerASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
            };
            const float BeforeHealth = Health(First);
            const float BeforeSecondary = Case.bMass ? Health(Second) : 0.f;
            FMath::RandInit(892173);
            if (!Activate(Test, F, *Ability, bPreview))
            {
                return false;
            }
            CheckSnapshot(Test, F, *Ability, bPreview);
            Test.TestEqual(TEXT("Original target wait does not commit a hidden cost"), Costs, 0);
            SendTarget(F, *Ability, First);
            Release(F, bPreview);
            Test.TestEqual(TEXT("Original TargetData commits one original cost"), Costs, 1);
            Test.TestEqual(TEXT("Original TargetData commits one original cooldown"), Cooldowns, 1);
            Test.TestEqual(TEXT("Original actual asset's mana amount is preserved"),
                           F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 10000.f - Case.Mana);
            F.Step(6.f);
            const float Damage = BeforeHealth - Health(First);
            const float SecondaryDamage = Case.bMass ? BeforeSecondary - Health(Second) : 0.f;
            Test.TestTrue(TEXT("Actual original backend removes real target health"), Damage > 0.f);
            if (Case.bMass)
            {
                Test.TestTrue(TEXT("Actual original graph also damages its second Mass enemy"), SecondaryDamage > 0.f);
                Test.TestTrue(TEXT("Four formal Mage compositions retain their original damage budget"),
                              FMath::IsNearlyEqual(Damage, Case.PrimaryDamage, .02f));
                Test.TestEqual(TEXT("Mass outside the actual shape is untouched"), Health(OutsideMass), Initial);
            }
            else
            {
                Test.TestEqual(TEXT("Actor outside the original query stays untouched"),
                               OutsideActor->GetAbilitySystemComponent()->GetNumericAttribute(
                                   UMyAttributeSet::GetHealthAttribute()),
                               Initial);
            }
            Test.TestFalse(TEXT("Original finite frontend ends naturally"), Ability->IsActive());
            Test.TestFalse(TEXT("Original finite frontend releases all target/tasks/timers"),
                           Ability->HasPendingGraphWork());
            Test.TestEqual(TEXT("Original independent cast effect is released after completion"),
                           F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount(), 0);
            const float FinalHealth = Health(First);
            F.Step(2.f);
            Test.TestEqual(TEXT("Completed graph cannot apply delayed hidden damage"), Health(First), FinalHealth);
            Test.TestEqual(TEXT("Graph nodes do not commit another cost"), Costs, 1);
            if (bPreview)
            {
                Test.TestTrue(TEXT("Zero-edit preview preserves original real primary damage"),
                              FMath::IsNearlyEqual(Damage, OriginalDamage, .02f));
                Test.TestTrue(TEXT("Zero-edit preview preserves original real secondary damage"),
                              FMath::IsNearlyEqual(SecondaryDamage, OriginalSecondaryDamage, .02f));
            }
            else
            {
                OriginalDamage = Damage;
                OriginalSecondaryDamage = SecondaryDamage;
            }
            Test.TestTrue(TEXT("Actual cooldown clears naturally before another input"), F.WaitReady(*Ability));
            FString Error;
            if (bPreview)
            {
                Test.TestTrue(TEXT("Only this exact preview grant is closed"), F.Developer->EndPreview(Error));
            }
            else
            {
                F.PlayerASC->ClearAbility(F.Handle);
            }
        }
        return true;
    }
} // namespace AbilityActualMageCatalogFrontendTests

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FActualMageCatalogFrontends,
                                  "test_25d.DynamicMage.ActualCatalog.ServerTransportDefaultAndPreview",
                                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FActualMageCatalogFrontends::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const auto& Case : AbilityActualMageCatalogFrontendTests::Cases)
    {
        Names.Add(FString(Case.Directory) + TEXT(".") + Case.Name);
        Commands.Add(Case.Name);
    }
}

bool FActualMageCatalogFrontends::RunTest(const FString& Name)
{
    using namespace AbilityActualMageCatalogFrontendTests;
    const auto* Case = Algo::FindByPredicate(Cases,
                                             [&Name](const FCase& Candidate)
                                             {
                                                 return Name == Candidate.Name;
                                             });
    if (!TestNotNull(TEXT("Known explicit nine-class cohort"), Case))
    {
        return false;
    }
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const FString Path =
        FString::Printf(TEXT("/Game/UnifiedAbilities/%s/GA_%s.GA_%s_C"), Case->Directory, Case->Name, Case->Name);
    const auto* Row = F.Definition(Path, Error);
    if (!TestNotNull(TEXT("Exact actual class is present in the production Mage catalog"), Row) ||
        !TestNotNull(TEXT("Real damage coefficient data is installed"), F.DamageDefaults))
    {
        AddInfo(Error);
        return false;
    }
    AddInfo(
        TEXT("ActualFrontendClass=") + Path +
        TEXT("; OriginalInput/TargetData=yes; LocalPredictedTransport=explicit server adapter; RegisterBase=none; ") +
        TEXT("Contributions=zero; Notify=not in original graph frontend; Cursor/Network/Visual=unverified"));
    const auto* SameASC = F.PlayerASC;
    const auto* Asset = Row->Base.AbilityClass->GetDefaultObject<UMageGraphAbility>();
    const FString AssetHash = AbilityRuntimeRecipes::Fingerprint(*Asset->Graph);
    const bool bResult = Case->bPassive ? Passive(*this, F, *Row, *Case) : Active(*this, F, *Row, *Case);
    TestTrue(TEXT("Formal and preview casts retain the same original PlayerState ASC"), SameASC == F.PlayerASC);
    TestEqual(TEXT("Read-only original authored Graph asset is unchanged"),
              AbilityRuntimeRecipes::Fingerprint(*Asset->Graph), AssetHash);
    return bResult;
}
#endif
