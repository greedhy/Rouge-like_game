// 调用链：真实目录/ASC/目标数据 -> 独立飞行图移交 World -> GA 正常结束 -> 关闭准确预览租约。
// 观察器只有16订阅不是资源宿主；第17图和弹体仍必须清理，正式授予/其他租约保持运行。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "Actor/MyProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Script.h"
#include "MyGameplayTags.h"

namespace AbilityRuntimePreviewOwnershipTests
{
    /** 实际恒速飞行 + 历史命中复核 + 5点效果，不依赖原所选GA自身Damage字段。 */
    UAbilityGraphDefinition* Flight(UObject* Outer)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Outer);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Delivery;
        Delivery.NodeId = FGuid::NewGuid();
        Delivery.TypeId = TEXT("Delivery.ProjectileAwait");
        auto* Config = NewObject<UAbilityDeliveryNodeConfig>(Graph);
        Config->Delivery.ProjectileClass = AMyProjectile::StaticClass();
        Config->Delivery.Speed = 900.f;
        Config->Delivery.MaxDistance = 2000.f;
        Config->Delivery.TimeoutSeconds = 5.f;
        Config->Delivery.bApplyDamage = false;
        Delivery.Config = Config;
        FAbilityGraphNode Resolve;
        Resolve.NodeId = FGuid::NewGuid();
        Resolve.TypeId = TEXT("Target.ResolveImpact");
        Resolve.Config = NewObject<UAbilityResolveImpactNodeConfig>(Graph);
        FAbilityGraphNode Damage;
        Damage.NodeId = FGuid::NewGuid();
        Damage.TypeId = TEXT("Effect.Damage");
        auto* Effect = NewObject<UAbilityDamageNodeConfig>(Graph);
        Effect->Damage = 5.f;
        Effect->DamagePerLevel = 0.f;
        Effect->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Damage.Config = Effect;
        Graph->Nodes = {Delivery, Resolve, Damage};
        Graph->EntryNodeId = Delivery.NodeId;
        FAbilityGraphEdge Hit;
        Hit.From = Delivery.NodeId;
        Hit.OutputPort = TEXT("Hit");
        Hit.To = Resolve.NodeId;
        Graph->Edges.Add(Hit);
        FAbilityGraphEdge Live;
        Live.From = Resolve.NodeId;
        Live.OutputPort = TEXT("TargetFound");
        Live.To = Damage.NodeId;
        Graph->Edges.Add(Live);
        return Graph;
    }

    struct FPlayer
    {
        AMyPlayerState* State = nullptr;
        ADungeonPlayerRunTestPawn* Pawn = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;
        FAbilitySourceToken Source;

        FPlayer(FAbilityGraphTestFixture& F, FVector Position)
        {
            State = F.World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            Pawn = F.World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Pawn->Tags = {TEXT("Player")};
            Pawn->SetActorLocation(Position);
            Pawn->AttachState(State);
            ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100000.f);
            ASC->AbilityActorInfo->PlayerController = F.World->SpawnActor<AAbilityGraphRemoteTestController>();
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
            FString Error;
            AbilityExecutionIdentity::CaptureSource(Pawn, Source, Error);
        }
    };

    TSet<UAbilityGraphInstance*> Runs(UWorld* World)
    {
        TSet<UAbilityGraphInstance*> Result;
        for (TObjectIterator<UAbilityGraphInstance> It; It; ++It)
        {
            if (It->GetTypedOuter<UWorld>() == World && It->GetExecutionId().IsValid())
            {
                Result.Add(*It);
            }
        }
        return Result;
    }
} // namespace AbilityRuntimePreviewOwnershipTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPreviewDefaultFreezesWithoutEdits,
    "test_25d.UnifiedGA.DynamicMage.DeveloperLifecycle.UnmodifiedPreviewFreezesRegisteredDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPreviewDefaultFreezesWithoutEdits::RunTest(const FString&)
{
    using namespace AbilityRuntimePreviewOwnershipTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    FPlayer Player(F, FVector(0, 200, 0));
    FString Error;
    if (!TestTrue(TEXT("Real complete mage directory loads"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto* Definition = Player.Developer->GetDefinitions().FindByPredicate(
        [](const FAbilityRuntimeDeveloperDefinition& Row)
        {
            const auto* Ability =
                Row.Base.AbilityClass ? Cast<UUnifiedGraphAbility>(Row.Base.AbilityClass->GetDefaultObject()) : nullptr;
            return Row.Base.bSelectable && Row.Base.DefaultGraph && Ability &&
                   Ability->TargetMode == EUnifiedAbilityTargetMode::Ground;
        });
    if (!TestNotNull(TEXT("Real catalog provides a ground-target frontend with an exported default"), Definition) ||
        !TestTrue(TEXT("BeginPreview alone grants the selected exact frontend"),
                  Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    ON_SCOPE_EXIT
    {
        FString CleanupError;
        Player.Developer->AbandonPreview(CleanupError);
        Player.ASC->CancelAllAbilities();
    };
    const auto Handle = Player.Developer->GetPreviewHandle();
    const auto PreviewLease = Player.Developer->GetPreviewOwnerLease(Handle);
    // 不手工 RegisterBase，不注入 no-op 来源，不调用 ApplyEdit。零修正就是开发面板的默认体验。
    TestFalse(TEXT("Opening preview creates no synthetic modifier contribution"),
              Player.Developer->HasContributions(Definition->Base.AbilityTag));
    TestTrue(TEXT("Active temporary grant has its precise server lease"), PreviewLease.IsValid());
    FPredictionKey Key;
    Key.Current = 2381;
    Player.ASC->CallServerTryActivateAbility(Handle, false, Key);
    const auto* Spec = Player.ASC->FindAbilitySpecFromHandle(Handle);
    const auto* Ability = Spec ? Cast<UUnifiedGraphAbility>(Spec->GetPrimaryInstance()) : nullptr;
    if (!TestTrue(TEXT("Actual ASC activation reaches the real TargetData wait"), Ability && Ability->IsActive()))
    {
        return false;
    }
    const auto& Frozen = Ability->GetRuntimeRecipeSnapshot();
    TestNotNull(TEXT("Unmodified preview freezes its registered exported base before TargetData"), Frozen.Graph.Get());
    TestEqual(TEXT("Frozen preview identifies the selected actual combat identity"), Frozen.AbilityTag,
              Definition->Base.AbilityTag);
    TestEqual(TEXT("Frozen preview uses the current registered base revision"), Frozen.Revision,
              Player.Developer->GetRevision(Definition->Base.AbilityTag));
    TestEqual(TEXT("Frozen preview hash is the selected exported default, not an implicit CDO route"),
              Frozen.ContentHash, Definition->BaseHash);
    TestEqual(TEXT("Owner lease freezes during this same activation before the target returns"),
              Ability->GetRuntimeExecutionOwnerLease(), PreviewLease);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPreviewOwnsIndependentFlights,
    "test_25d.UnifiedGA.DynamicMage.DeveloperLifecycle.CloseOwnsIndependentFlightsBeyondObserverCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPreviewOwnsIndependentFlights::RunTest(const FString&)
{
    using namespace AbilityRuntimePreviewOwnershipTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    FPlayer Player(F, FVector(0, 200, 0));
    FPlayer Other(F, FVector(0, -300, 0));
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 200.f;
    Type->XPReward = 0;
    const int32 Target = Mass->SpawnMonster(Type, FVector(1000, 200, 0));
    FString Error;
    if (!TestTrue(TEXT("Real complete mage directory loads"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Player.Developer->GetDefinitions().FindByPredicate(
        [](const FAbilityRuntimeDeveloperDefinition& Row)
        {
            const auto* Ability =
                Row.Base.AbilityClass ? Cast<UUnifiedGraphAbility>(Row.Base.AbilityClass->GetDefaultObject()) : nullptr;
            return Row.Base.bSelectable && Ability && Ability->TargetMode == EUnifiedAbilityTargetMode::Ground &&
                   Ability->ExecutionLifetime == EAbilityExecutionLifetime::IndependentCastEffect;
        });
    if (!TestNotNull(TEXT("Actual catalog has an independent ground frontend"), Definition) ||
        !TestTrue(TEXT("Trusted exact class is temporarily granted"),
                  Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Handle = Player.Developer->GetPreviewHandle();
    const auto OwnerLease = Player.Developer->GetPreviewOwnerLease(Handle);
    TestTrue(TEXT("Active preview exposes an exact server owner lease"), OwnerLease.IsValid());
    const auto Formal =
        Player.ASC->GiveAbility(FGameplayAbilitySpec(Definition->Base.AbilityClass, 4, INDEX_NONE, Player.State));
    TestFalse(TEXT("Formal grant of the same class never borrows preview lease"),
              Player.Developer->GetPreviewOwnerLease(Formal).IsValid());
    auto* DefinitionGraph = Flight(F.World);
    TestTrue(TEXT("Server can test a generated flight definition"),
             Player.Developer->RegisterBase(Definition->Base.AbilityTag, DefinitionGraph, Error));
    FAbilityRuntimeRecipeContribution Contribution;
    Contribution.SourceId = TEXT("Outside.ExplicitFrozenTestRoute");
    TestTrue(TEXT("Accepted source selects actual runtime recipe front"),
             Player.Developer->SetContribution(Definition->Base.AbilityTag, Contribution, Error));
    auto* WorldGraphs = F.World->GetSubsystem<UAbilityGraphWorldSubsystem>();
    TArray<UAbilityGraphInstance*> PreviewRuns;
    TArray<TWeakObjectPtr<AMyProjectile>> PreviewShots;
    TArray<TPair<TWeakObjectPtr<UAbilityGraphInstance>, FDelegateHandle>> HitHandles;
    TArray<TPair<TWeakObjectPtr<UAbilityGraphInstance>, FDelegateHandle>> FinishHandles;
    ON_SCOPE_EXIT
    {
        for (const auto& Item : HitHandles)
        {
            if (auto* Run = Item.Key.Get())
            {
                Run->OnEvent.Remove(Item.Value);
            }
        }
        for (const auto& Item : FinishHandles)
        {
            if (auto* Run = Item.Key.Get())
            {
                Run->OnFinished.Remove(Item.Value);
            }
        }
    };
    int32 LateHits = 0;
    for (int32 Index = 0; Index < 17; ++Index)
    {
        const auto Before = Runs(F.World);
        TSet<AMyProjectile*> ExistingShots;
        for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
        {
            ExistingShots.Add(*It);
        }
        const auto OldEffects = Player.ASC->GetActiveEffects(FGameplayEffectQuery());
        FPredictionKey Key;
        Key.Current = 2100 + Index;
        Player.ASC->CallServerTryActivateAbility(Handle, false, Key);
        auto* Spec = Player.ASC->FindAbilitySpecFromHandle(Handle);
        auto* Ability = Spec ? Cast<UUnifiedGraphAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestTrue(TEXT("Actual GA waits for real server TargetData"), Ability && Ability->IsActive()))
        {
            return false;
        }
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Item = INDEX_NONE;
        Hit->HitResult.ImpactPoint = FVector(50, 200, 0);
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        Player.ASC->ServerSetReplicatedTargetData(Handle,
                                                  Ability->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                                  Data, FGameplayTag(), FPredictionKey());
        if (!TestFalse(TEXT("Original independent frontend normally ends its GA after transfer"), Ability->IsActive()))
        {
            return false;
        }
        UAbilityGraphInstance* NewRun = nullptr;
        for (auto* Run : Runs(F.World))
        {
            if (!Before.Contains(Run) && !Run->IsFinished())
            {
                NewRun = Run;
            }
        }
        if (!TestNotNull(TEXT("Real released flight remains world-owned after normal GA end"), NewRun))
        {
            return false;
        }
        PreviewRuns.Add(NewRun);
        const auto HitHandle = NewRun->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                LateHits += Event.OutputPort == TEXT("Hit") ? 1 : 0;
            });
        HitHandles.Add({NewRun, HitHandle});
        for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
        {
            if (!ExistingShots.Contains(*It))
            {
                PreviewShots.Add(*It);
            }
        }
        // 测试显式归还本次冷却句柄以便连续17次；不按类移除正式或其他玩家的效果。
        for (const auto Effect : Player.ASC->GetActiveEffects(FGameplayEffectQuery()))
        {
            if (!OldEffects.Contains(Effect))
            {
                Player.ASC->RemoveActiveGameplayEffect(Effect);
            }
        }
    }
    TestEqual(TEXT("Every real preview activation created an actual projectile"), PreviewShots.Num(), 17);
    TestEqual(TEXT("Only sixteen weak log subscriptions are allowed"), Player.Developer->GetObservedExecutionCount(),
              16);
    TestEqual(TEXT("World owns all seventeen effects independently of log cap"), WorldGraphs->GetActiveExecutionCount(),
              17);

    TestTrue(TEXT("Other player initializes same trusted catalog"), Other.Developer->InitializeCatalog(nullptr, Error));
    TestTrue(TEXT("Other player owns a separate preview lease"),
             Other.Developer->BeginPreview(Definition->DefinitionId, Error));
    const auto OtherLease = Other.Developer->GetPreviewOwnerLease(Other.Developer->GetPreviewHandle());
    TArray<UAbilityGraphInstance*> ProtectedRuns;
    for (const bool bOther : {false, true})
    {
        FAbilityNodeInput Input;
        Input.SourceToken = bOther ? Other.Source : Player.Source;
        Input.Origin = bOther ? FVector(0, -300, 0) : FVector(0, 500, 0);
        Input.Direction = FVector::ForwardVector;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
        Options.OwnerLease = bOther ? OtherLease : FGuid();
        TestTrue(TEXT("Unrelated formal/other-owner flight starts"),
                 Run->StartWithInput(DefinitionGraph, Input, Options, Error));
        TestTrue(TEXT("World owns unrelated flight"), WorldGraphs->Adopt(Run, Error));
        ProtectedRuns.Add(Run);
    }
    WorldGraphs->CancelOwnerLease(FGuid());
    TestFalse(TEXT("Empty lease cannot cancel formal effects"), ProtectedRuns[0]->IsFinished());
    int32 ReentrantAttempts = 0;
    bool bReentrantActivated = false;
    const auto Reentry = PreviewRuns[0]->OnFinished.AddLambda(
        [&](EAbilityGraphEndReason Reason)
        {
            if (Reason == EAbilityGraphEndReason::Cancelled)
            {
                ++ReentrantAttempts;
                FPredictionKey Key;
                Key.Current = 2401;
                // ClearAbility 尚未发生时使用真实原临时handle重入，不能借到无效/空租约逃逸。
                Player.ASC->CallServerTryActivateAbility(Handle, false, Key);
                const auto* Spec = Player.ASC->FindAbilitySpecFromHandle(Handle);
                const auto* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
                bReentrantActivated |= Ability && Ability->IsActive();
            }
        });
    FinishHandles.Add({PreviewRuns[0], Reentry});
    // 恢复编辑中的基础后再走正常Close；在途图仍须保留它激活时冻结的OwnerLease。
    TestTrue(TEXT("Later accepted base does not rewrite active flight snapshots"),
             Player.Developer->RegisterBase(Definition->Base.AbilityTag, Definition->Base.DefaultGraph, Error));
    Player.Developer->ServerClosePreview(FGuid::NewGuid(), false);
    TestFalse(TEXT("Preview closes while source remains alive"), Player.Developer->GetDeveloperState().bPreviewActive);
    TestEqual(TEXT("Actual close exercised an OnGraphFinished activation reentry"), ReentrantAttempts, 1);
    TestFalse(TEXT("Sealed temporary grant cannot reactivate during close callbacks"), bReentrantActivated);
    for (auto* Run : PreviewRuns)
    {
        TestTrue(TEXT("Exact owner close cancels every independent run, including unobserved seventeenth"),
                 Run->IsFinished());
        TestEqual(TEXT("Preview closes effects as cancellation"), Run->GetEndReason(),
                  EAbilityGraphEndReason::Cancelled);
    }
    for (const auto& Shot : PreviewShots)
    {
        TestFalse(TEXT("Actual projectile resources are removed immediately"), Shot.IsValid());
    }
    TestFalse(TEXT("Formal effect of same player remains active"), ProtectedRuns[0]->IsFinished());
    TestFalse(TEXT("Other player's effect remains active"), ProtectedRuns[1]->IsFinished());
    TestNotNull(TEXT("Same-class permanent grant remains"), Player.ASC->FindAbilitySpecFromHandle(Formal));
    TestTrue(TEXT("Other preview remains equipped"), Other.Developer->GetDeveloperState().bPreviewActive);
    for (const auto& Shot : PreviewShots)
    {
        if (auto* Projectile = Shot.Get())
        {
            Projectile->ProjectileMovement->TickComponent(2.f, LEVELTICK_All, nullptr);
        }
    }
    ++GFrameCounter;
    WorldGraphs->Tick(0.f);
    TestEqual(TEXT("Closed effects never produce later contact payload"), LateHits, 0);
    FPureMassView View;
    if (TestTrue(TEXT("Original potential victim remains alive"), Mass->Resolve(Target, View)))
    {
        TestEqual(TEXT("Closing preview prevents actual later hit damage"), View.Health, 200.f);
    }
    TestEqual(TEXT("Only unrelated active world effects remain"), WorldGraphs->GetActiveExecutionCount(), 2);
    return true;
}
#endif
