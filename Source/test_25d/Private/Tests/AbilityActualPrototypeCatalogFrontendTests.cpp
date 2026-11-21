// 测试链：65个原型及3个Batch6专用目录类 -> 同一PlayerState ASC正式输入/开发T输入 -> 原TargetData/费用 -> 真实世界图。
// 与逐资产动作测试互补：此处比较原前端和零编辑预览，不声称实际本地鼠标、网络或动画视觉已验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeCatalog.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "Components/BoxComponent.h"
#include "Misc/AutomationTest.h"

namespace AbilityActualPrototypeCatalogFrontendTests
{
    /** 模拟引擎外层Tickable分发；临时World.Tick不会替测试推进Extension世界服务。 */
    void Advance(FAbilityActualMageFrontendFixture& F, float Seconds)
    {
        for (float Elapsed = 0.f; Elapsed + UE_SMALL_NUMBER < Seconds; Elapsed += .01f)
        {
            const float Delta = FMath::Min(.01f, Seconds - Elapsed);
            F.Step(Delta);
            F.World->GetSubsystem<UExtensionWorldSubsystem>()->Tick(Delta);
        }
    }

    /** 提供真实可见性地面；Surface原前端在Commit前必须成功投影，不能绕过该判定。 */
    bool Ground(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F)
    {
        auto* Floor = F.World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(3000, 3000, 5));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Ignore);
        Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        Box->RegisterComponent();
        Floor->SetActorLocation(FVector(0, 0, -150));
        FVector Projected;
        return Test.TestTrue(TEXT("Original frontend can actually project onto the physical fixture floor"),
                             APrototypeRuntime::ProjectSurfaceToGround(F.Pawn, FVector::ZeroVector, Projected));
    }

    /** 直接枚举当前三个正式目录的具体类，不用人工缩小后的代表样本替代逐类证据。 */
    TArray<FAbilityRuntimeBaseDefinition> Definitions()
    {
        TArray<FAbilityRuntimeBaseDefinition> Rows;
        TArray<FString> Diagnostics;
        AbilityRuntimeRecipeCatalog::CollectBaseDefinitions(GetTransientPackage(), nullptr, Rows, Diagnostics);
        Rows.RemoveAll(
            [](const auto& Row)
            {
                return !Row.AbilityClass || !Row.AbilityClass->IsChildOf<UPrototypeAbility>();
            });
        return Rows;
    }

    /** 无本地视口的服务器传输夹具；输入、原网络策略与GAS目标缓存照常运行。 */
    bool Press(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F, UPrototypeAbility& Ability,
               bool bPreview)
    {
        if (bPreview)
        {
            F.Key(IE_Pressed);
        }
        else
        {
            F.PlayerASC->AbilityInputTagPressed(FMyGameplayTags::Get().InputTag_DeveloperRecipe);
            F.PlayerASC->AbilityInputTagHeld(FMyGameplayTags::Get().InputTag_DeveloperRecipe);
        }
        auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        if (!Spec)
        {
            return Test.TestNotNull(TEXT("Original input retains the actual granted spec"), Spec);
        }
        if (!Ability.IsActive() &&
            Ability.GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalPredicted)
        {
            static int16 NextPrediction = 2301;
            FPredictionKey Prediction;
            Prediction.Current = NextPrediction++;
            F.PlayerASC->CallServerTryActivateAbility(F.Handle, Spec->InputPressed, Prediction);
        }
        // 自身瞬发技能可在输入调用内自然结束；真实费用/效果会在后面独立断言。
        return Test.TestTrue(TEXT("Original target frontend activates or already completed its self cast"),
                             Ability.IsActive() || Ability.Prototype->bTargetSelf || Ability.IsInputToggleAbility());
    }

    /** 只提交原客户端目标包，不调用私有TargetReady/Execute，不直接启动一张展示图。 */
    void Target(FAbilityActualMageFrontendFixture& F, UPrototypeAbility& Ability, AActor& Enemy)
    {
        if (Ability.Prototype->bTargetSelf || Ability.IsInputToggleAbility())
        {
            return;
        }
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Item = INDEX_NONE;
        Hit->HitResult.HitObjectHandle = FActorInstanceHandle(&Enemy);
        Hit->HitResult.Location = Hit->HitResult.ImpactPoint = Enemy.GetActorLocation();
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        F.PlayerASC->ServerSetReplicatedTargetData(F.Handle,
                                                   Ability.GetCurrentActivationInfo().GetActivationPredictionKey(),
                                                   Data, FGameplayTag(), FPredictionKey());
    }

    /** 松键和开关关闭继续走生产输入，不能用ClearAbility替代渠道正常的按键结束合同。 */
    void Release(FAbilityActualMageFrontendFixture& F, UPrototypeAbility& Ability, bool bPreview)
    {
        if (bPreview)
        {
            F.Key(IE_Released);
            if (Ability.IsActive() && Ability.IsInputToggleAbility())
            {
                F.Key(IE_Pressed);
                F.Key(IE_Released);
            }
        }
        else
        {
            const auto Slot = FMyGameplayTags::Get().InputTag_DeveloperRecipe;
            F.PlayerASC->AbilityInputTagReleased(Slot);
            if (Ability.IsActive() && Ability.IsInputToggleAbility())
            {
                F.PlayerASC->AbilityInputTagPressed(Slot);
                F.PlayerASC->AbilityInputTagReleased(Slot);
            }
        }
    }

    /** 等待目标时读取实际冻结图；自身瞬发已经结束则检查真实事件与快照清理，不复活旧快照。 */
    void Snapshot(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F, UPrototypeAbility& Ability,
                  bool bPreview)
    {
        const auto& Value = Ability.GetRuntimeRecipeSnapshot();
        if (!bPreview)
        {
            Test.TestNull(TEXT("Formal no-contribution path keeps the original compatibility entry"),
                          Value.Graph.Get());
            return;
        }
        if (!Ability.IsActive())
        {
            Test.TestNull(TEXT("Completed self cast releases its frontend snapshot"), Value.Graph.Get());
            const auto& State = F.Developer->GetDeveloperState();
            Test.TestTrue(TEXT("Completed self preview really executed the acknowledged revision"),
                          F.Developer->GetObservations().ContainsByPredicate(
                              [&](const auto& Record)
                              {
                                  return Record.Revision == State.Revision && !Record.NodeType.IsNone() &&
                                         !Record.NodeType.ToString().StartsWith(TEXT("Legacy."));
                              }));
            return;
        }
        if (!Test.TestNotNull(TEXT("Real preview activation freezes its actual fine default graph"), Value.Graph.Get()))
        {
            return;
        }
        Test.TestEqual(TEXT("Frozen definition equals the acknowledged panel definition"), Value.ContentHash,
                       F.Developer->GetDeveloperState().ContentHash);
        Test.TestTrue(TEXT("Only the exact temporary grant owns this activation lease"),
                      Ability.GetRuntimeExecutionOwnerLease() == F.Developer->GetPreviewOwnerLease(F.Handle));
        for (const auto& Node : Value.Graph->Nodes)
        {
            Test.TestFalse(TEXT("Actual default snapshot has no complete legacy macro"),
                           Node.TypeId.ToString().StartsWith(TEXT("Legacy.")) ||
                               (Node.Config && Node.Config->IsA<UAbilityLegacyNodeConfig>()));
        }
    }
} // namespace AbilityActualPrototypeCatalogFrontendTests

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FActualPrototypeCatalogFrontends,
                                  "test_25d.DynamicMage.ActualPrototypeFrontend.ServerTransportDefaultAndPreview",
                                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 发现68个具体类（65原型和3专用）；资源路径作为命令，保证同短名的不同版本不会合并。 */
void FActualPrototypeCatalogFrontends::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const auto& Row : AbilityActualPrototypeCatalogFrontendTests::Definitions())
    {
        const FString Family = Row.FrontendKind == EAbilityRuntimeFrontendKind::Extension        ? TEXT("Extension")
                               : Row.FrontendKind == EAbilityRuntimeFrontendKind::ActorPrototype ? TEXT("Actor")
                                                                                                 : TEXT("Mixed");
        Names.Add(Family + TEXT(".") + Row.AbilityClass->GetName());
        Commands.Add(Row.AbilityClass->GetPathName());
    }
}

/** 同ASC先正式后预览；新目标生命隔离跨施法叠层，冷却自然结束，不修改原GA/DA/GE。 */
bool FActualPrototypeCatalogFrontends::RunTest(const FString& ClassPath)
{
    using namespace AbilityActualPrototypeCatalogFrontendTests;
    FAbilityActualMageFrontendFixture F;
    if (!Ground(*this, F))
    {
        return false;
    }
    FString Error;
    const auto* Row = F.Definition(ClassPath, Error);
    if (!TestNotNull(TEXT("Actual current catalog class"), Row))
    {
        AddInfo(Error);
        return false;
    }
    AddInfo(TEXT("ActualFrontendClass=") + ClassPath +
            TEXT("; OriginalInput/TargetData=yes; LPTransport=server fixture; Cursor/Network/Visual=unverified"));
    if (Row->Base.bRequiresMassSubsystem)
    {
        F.World->GetSubsystem<UPureMassSubsystem>()->Enable();
    }
    F.Target->Destroy();
    auto* SameASC = F.PlayerASC;
    TArray<float> OriginalTrace;
    for (const bool bPreview : {false, true})
    {
        TArray<AEnemyCharacter*> Enemies = {F.EnemyAt(FVector(150, 0, 0)), F.EnemyAt(FVector(250, 80, 0)),
                                            F.EnemyAt(FVector(350, -80, 0))};
        F.Pawn->SetActorLocation(FVector::ZeroVector);
        F.Pawn->SetActorRotation(FRotator::ZeroRotator);
        F.PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
        if (bPreview)
        {
            if (!TestTrue(TEXT("BeginPreview selects the actual production class without RegisterBase"),
                          F.Developer->BeginPreview(Row->DefinitionId, Error)))
            {
                AddInfo(Error);
                return false;
            }
            F.Handle = F.Developer->GetPreviewHandle();
        }
        else
        {
            FGameplayAbilitySpec Spec(Row->Base.AbilityClass, 1, INDEX_NONE, F.State);
            Spec.GetDynamicSpecSourceTags().AddTag(FMyGameplayTags::Get().InputTag_DeveloperRecipe);
            Spec.GetDynamicSpecSourceTags().AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
            F.Handle = F.PlayerASC->GiveAbility(Spec);
        }
        auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        auto* Ability = Spec ? Cast<UPrototypeAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestNotNull(TEXT("Actual PerActor original frontend instance"), Ability) ||
            !Press(*this, F, *Ability, bPreview))
        {
            return false;
        }
        Snapshot(*this, F, *Ability, bPreview);
        Target(F, *Ability, *Enemies[0]);
        const float Upfront = 10000.f - F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        // 库存原型可在初始化额外购买弹体；该费用由后面的逐点旧/新对照验证，不能误当第二次Commit。
        TestTrue(TEXT("Real cast consumes at least its original configured upfront cost"),
                 Upfront + .002f >= Ability->Prototype->ManaCost);
        TArray<float> Trace = {Upfront};
        auto Sample = [&]()
        {
            Trace.Add(F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()));
            for (const auto* Enemy : Enemies)
            {
                Trace.Add(
                    Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()));
            }
        };
        // 原渠道/库存先真实维持一段；逐资产动作测试另行覆盖其命中/攻击事件，不能由零命中代替。
        for (int32 Tick = 0; Tick < 5; ++Tick)
        {
            Advance(F, .1f);
            Sample();
        }
        Release(F, *Ability, bPreview);
        for (int32 Tick = 0; Tick < 3000; ++Tick)
        {
            const auto& Config = *Ability->Prototype;
            if (!Ability->IsActive() &&
                F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount() == 0 &&
                (!Config.CooldownTag.IsValid() || !F.PlayerASC->HasMatchingGameplayTag(Config.CooldownTag)))
            {
                break;
            }
            Advance(F, .01f);
        }
        TestFalse(TEXT("Original input release/natural finish closes the frontend"), Ability->IsActive());
        TestEqual(TEXT("All finite or released world execution leases close"),
                  F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount(), 0);
        TestTrue(TEXT("Original cooldown expires naturally before grant removal"),
                 !Ability->Prototype->CooldownTag.IsValid() ||
                     !F.PlayerASC->HasMatchingGameplayTag(Ability->Prototype->CooldownTag));
        Sample();
        if (bPreview)
        {
            TestEqual(TEXT("Formal/preview observations have the same sample shape"), Trace.Num(), OriginalTrace.Num());
            for (int32 Index = 0; Index < FMath::Min(Trace.Num(), OriginalTrace.Num()); ++Index)
            {
                TestTrue(FString::Printf(TEXT("Exact class preserves actual Mana/HP sample %d: old=%g fine=%g"), Index,
                                         OriginalTrace[Index], Trace[Index]),
                         FMath::IsNearlyEqual(Trace[Index], OriginalTrace[Index], .02f));
            }
            TestTrue(TEXT("Preview closes only its actual owned temporary grant"), F.Developer->EndPreview(Error));
        }
        else
        {
            OriginalTrace = MoveTemp(Trace);
            F.PlayerASC->ClearAbility(F.Handle);
        }
        for (auto* Enemy : Enemies)
        {
            Enemy->Destroy();
        }
    }
    TestTrue(TEXT("Both actual frontend casts retain the original PlayerState ASC"), SameASC == F.PlayerASC);
    return true;
}
#endif
