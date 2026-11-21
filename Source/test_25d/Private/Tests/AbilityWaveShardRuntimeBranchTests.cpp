// 测试链：真实目录预览/可信配方事务 -> 冻结Wave图或原Shards确认/通知 -> 独立状态、原生命和资源清理。
// Wave控制其已接受snapshot的空间步进；Shards保留原输入/目标/动画通知前端，不直接调用私有执行或状态服务。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Ability/MyArcaneShardAbility.h"
#include "PureMass/PureMassAbility.h"
#include "PureMass/PureMassSubsystem.h"
#include "Actor/PointCollection.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/AutomationTest.h"

namespace AbilityWaveShardRuntimeBranchTests
{
    /** 新规则通过同一开发事务提交，不能直接改变实际目录/源资产中的图。 */
    bool Edit(FAbilityActualMageFrontendFixture& F, const FAbilityRuntimeDeveloperDefinition& Row, int32 Index,
              FName Modifier, float Value, FString& Error, FName Fragment = NAME_None, FName Port = NAME_None)
    {
        FAbilityRuntimeDeveloperEdit Request;
        Request.DefinitionId = Row.DefinitionId;
        Request.ModifierId = Modifier;
        Request.NodeIndex = Index;
        Request.Value = Value;
        Request.FragmentId = Fragment;
        Request.OutputPortId = Port;
        Request.ExpectedBaseHash = Row.BaseHash;
        Request.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
        return F.Developer->ApplyEdit(Request, Error);
    }

    /** 选真实原型根Wave，不把手工缩小的新基图伪装成正式目录技能。 */
    const FAbilityRuntimeDeveloperDefinition* Wave(FAbilityActualMageFrontendFixture& F, FString& Error)
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
                [&](const auto& Node)
                {
                    return Node.NodeId == Row.Base.DefaultGraph->EntryNodeId;
                });
            const auto* Scope = Entry ? Cast<UAbilityPrototypeScopeNodeConfig>(Entry->Config) : nullptr;
            if (Scope && Scope->Parameters && Scope->Parameters->Kind == EAbilityPrototypeKind::Wave)
            {
                return &Row;
            }
        }
        Error = TEXT("Actual catalogs contain no selectable root Wave definition");
        return nullptr;
    }

    /** 只枚举活点集合Actor；它由原GA/Clock拥有，测试不能以修改指针代替销毁证据。 */
    int32 Collections(UWorld* World)
    {
        int32 Count = 0;
        for (TActorIterator<APointCollection> It(World); It; ++It)
        {
            Count += !It->IsActorBeingDestroyed() ? 1 : 0;
        }
        return Count;
    }

    int32 Mass(FAbilityActualMageFrontendFixture& F)
    {
        auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
        Sim->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = 200.f;
        Type->Radius = 20.f;
        Type->HalfHeight = 40.f;
        Type->XPReward = 0;
        Type->Speed = 0.f;
        return Sim->SpawnMonster(Type, FVector(630, 0, 0));
    }
} // namespace AbilityWaveShardRuntimeBranchTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWavePreviewRuntimeStatus,
                                 "test_25d.DynamicMage.RuntimeBranches.ActualWavePreviewSnapshotAddsOriginalLifeState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWavePreviewRuntimeStatus::RunTest(const FString&)
{
    using namespace AbilityWaveShardRuntimeBranchTests;
    for (const bool bRetire : {false, true})
    {
        FAbilityActualMageFrontendFixture F;
        FString Error;
        const auto* Row = Wave(F, Error);
        if (!TestNotNull(TEXT("Actual current Wave directory is available"), Row) ||
            !TestTrue(TEXT("Actual Wave preview starts without manual base registration"),
                      F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            AddError(Error);
            return false;
        }
        const int32 RootIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [&](const auto& Node)
            {
                return Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
            });
        const int32 WaveIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const auto& Node)
            {
                return Node.TypeId == TEXT("Spatial.PrototypeWave");
            });
        if (!TestTrue(TEXT("Actual default has separate geometry and scope axes"),
                      RootIndex != INDEX_NONE && WaveIndex != INDEX_NONE) ||
            !TestTrue(TEXT("Actual Wave radius config is accepted"),
                      Edit(F, *Row, RootIndex, TEXT("Set.Radius"), 300.f, Error)) ||
            !TestTrue(TEXT("Actual Wave speed config is accepted"),
                      Edit(F, *Row, RootIndex, TEXT("Set.Speed"), 100.f, Error)) ||
            !TestTrue(TEXT("Actual Wave lifetime config is accepted"),
                      Edit(F, *Row, RootIndex, TEXT("Set.Duration"), 3.f, Error)) ||
            !TestTrue(TEXT("Trusted parser adds an independent state to the existing target-found port"),
                      Edit(F, *Row, WaveIndex, TEXT("Append.Registered"), 1.f, Error, TEXT("Target.Petrify"),
                           TEXT("TargetFound"))))
        {
            AddError(Error);
            return false;
        }
        FAbilityRuntimeRecipeSnapshot Accepted;
        if (!TestTrue(TEXT("Real preview returns its accepted immutable cast snapshot"),
                      F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Accepted, Error)))
        {
            AddError(Error);
            return false;
        }
        TestEqual(TEXT("Runtime graph records the actual accepted revision/hash"), Accepted.ContentHash,
                  F.Developer->GetDeveloperState().ContentHash);
        auto* Context = NewObject<UPrototypeCastContext>(F.World);
        Context->Caster = F.Pawn;
        Context->CastId = FGuid::NewGuid();
        Context->Level = 1;
        Context->DamageParams.SourceAbilitySystemComponent = F.PlayerASC;
        const auto* Defaults = Row->Base.AbilityClass->GetDefaultObject<UPrototypeAbility>();
        const auto* DamageClass = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("DamageEffectClass"));
        Context->DamageParams.DamageGameplayEffectClass =
            DamageClass ? Cast<UClass>(DamageClass->GetObjectPropertyValue_InContainer(Defaults)) : nullptr;
        if (!TestTrue(TEXT("Actual PlayerState source life is captured"), Context->CaptureSourceLife(Error)))
        {
            AddError(Error);
            return false;
        }
        FAbilityNodeInput Input;
        Input.SourceToken = Context->GetSourceLife();
        Input.Context = Context;
        Input.Direction = FVector::ForwardVector;
        Input.DamageParams = Context->DamageParams;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Accepted.Graph->CompatibilityProfile;
        Options.BudgetPolicy = Accepted.Graph->BudgetPolicy;
        if (!TestTrue(TEXT("Accepted actual Wave snapshot executes its independent runtime status branch"),
                      Run->StartWithInput(Accepted.Graph, Input, Options, Error)) ||
            (Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed))
        {
            AddError(Error);
            return false;
        }
        ++GFrameCounter;
        Run->Advance(.5f);
        TestFalse(TEXT("The added status cannot bypass the actual slow wavefront"), F.IsPetrified());
        if (bRetire)
        {
            F.Target->EnableDungeonPooling();
        }
        ++GFrameCounter;
        Run->Advance(1.01f);
        TestEqual(TEXT("Only the observed original target life can receive the added state when the front arrives"),
                  F.IsPetrified(), !bRetire);
        Run->Cancel();
        const auto Events = Run->GetAcceptedEventCount();
        Run->Advance(5.f);
        TestEqual(TEXT("Cancelled dynamic Wave cannot publish another state branch"), Run->GetAcceptedEventCount(),
                  Events);
        TestEqual(TEXT("Actual source base definition remains unchanged"),
                  AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph), Row->BaseHash);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShardPreviewRuntimeReplacement,
                                 "test_25d.DynamicMage.RuntimeBranches.ActualShardPreviewReplacesBatchDamageWithState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShardPreviewRuntimeReplacement::RunTest(const FString&)
{
    using namespace AbilityWaveShardRuntimeBranchTests;
    const FString Path =
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneChard/GA_ArcaneShards.GA_ArcaneShards_C");
    for (const bool bCancel : {false, true})
    {
        FAbilityActualMageFrontendFixture F;
        F.Target->SetActorLocation(FVector(600, 0, 0));
        const int32 Neighbor = Mass(F);
        FString Error;
        const auto* Row = F.Definition(Path, Error);
        if (!TestNotNull(TEXT("Exact current Shards directory exists"), Row) ||
            !TestTrue(TEXT("Actual native Shards preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            AddError(Error);
            return false;
        }
        F.Handle = F.Developer->GetPreviewHandle();
        const auto* ClockNode = Row->Base.DefaultGraph->Nodes.FindByPredicate(
            [](const auto& Node)
            {
                return Node.TypeId == TEXT("Control.ShardPoints");
            });
        const auto* ClockSettings = ClockNode ? Cast<UAbilityArcaneShardNodeConfig>(ClockNode->Config) : nullptr;
        if (!TestNotNull(TEXT("Actual original point query declares its target representations"), ClockSettings))
        {
            return false;
        }
        const int32 Sink = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const auto& Node)
            {
                return Node.TypeId == TEXT("Effect.ShardDamageBatch");
            });
        if (!TestTrue(TEXT("Actual default exposes one independent batch damage owner"), Sink != INDEX_NONE) ||
            !TestTrue(TEXT("Trusted parser replaces TargetSet damage with a status batch"),
                      Edit(F, *Row, Sink, TEXT("Replace.Status.Petrify"), 2.f, Error)))
        {
            AddError(Error);
            return false;
        }
        auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assets/Characters/Aura/SKM_Aura.SKM_Aura"));
        const auto* Defaults = Row->Base.AbilityClass->GetDefaultObject<UMyArcaneShardAbility>();
        if (!TestNotNull(TEXT("Actual Aura source mesh for original montage loads"), Mesh) ||
            !TestNotNull(TEXT("Actual original Shards montage is configured"), Defaults->ShardMontage.Get()))
        {
            return false;
        }
        F.Pawn->GetMesh()->SetSkeletalMeshAsset(Mesh);
        F.Pawn->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        F.Pawn->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
        F.PlayerASC->RefreshAbilityActorInfo();
        F.PlayerASC->AbilityActorInfo->PlayerController = F.Controller;
        F.Key(IE_Pressed);
        auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        if (!TestNotNull(TEXT("Original T input selects its exact temporary native spec"), Spec))
        {
            return false;
        }
        FPredictionKey Prediction;
        Prediction.Current = bCancel ? 3182 : 3181;
        F.PlayerASC->CallServerTryActivateAbility(F.Handle, Spec->InputPressed, Prediction);
        UMyArcaneShardAbility* Active = nullptr;
        for (auto* Instance : Spec->GetAbilityInstances())
        {
            if (Instance && Instance->IsActive())
            {
                Active = Cast<UMyArcaneShardAbility>(Instance);
                break;
            }
        }
        if (!TestNotNull(TEXT("Actual original per-execution Shards frontend waits for confirmation"), Active))
        {
            return false;
        }
        const TStrongObjectPtr<UMyArcaneShardAbility> KeepAbility(Active);
        F.Key(IE_Released);
        F.Key(IE_Pressed);
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Location = Hit->HitResult.ImpactPoint = FVector(600, 0, 0);
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        FMath::RandInit(902117);
        F.PlayerASC->ServerSetReplicatedTargetData(F.Handle,
                                                   Active->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                                   Data, FGameplayTag(), FPredictionKey());
        if (!TestTrue(TEXT("Original confirmation/TargetData starts the actual Shards montage"),
                      F.Pawn->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Defaults->ShardMontage)))
        {
            return false;
        }
        auto* Property = FindFProperty<FObjectPropertyBase>(Active->GetClass(), TEXT("PreparedPoints"));
        auto* Prepared =
            Property ? Cast<UAbilityArcaneShardContext>(Property->GetObjectPropertyValue_InContainer(Active)) : nullptr;
        if (!TestNotNull(TEXT("Original front prepares its actual owned point collection"), Prepared))
        {
            return false;
        }
        TestTrue(TEXT("The actual original point actor exists before Notify"), IsValid(Prepared->PointCollection));
        TestFalse(TEXT("The new payload never executes before original Notify"), F.IsPetrified());
        const auto Notify = FGameplayTag::RequestGameplayTag(TEXT("Event.Montage.AracaneShards"));
        FGameplayEventData Event;
        Event.EventTag = Notify;
        F.PlayerASC->HandleGameplayEvent(Notify, &Event);
        TestTrue(TEXT("Original Notify actually applies the replacement Actor state"), F.IsPetrified());
        FPureMassView View;
        if (TestTrue(TEXT("Actual nearby Mass is queryable after the same point"),
                     F.World->GetSubsystem<UPureMassSubsystem>()->Resolve(Neighbor, View)))
        {
            TestEqual(TEXT("Replacing payload preserves the original query's explicit Mass support"), View.bPetrified,
                      ClockSettings->bIncludeMass);
            TestEqual(TEXT("No removed native shard damage leaks into Mass"), View.Health, 200.f);
        }
        TestEqual(TEXT("The removed original batch damage does not also reduce Actor HP"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  10000.f);
        TestTrue(TEXT("Replacement preserves the original first-point cost boundary"),
                 F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < 10000.f);
        if (bCancel)
        {
            const auto Transform = F.Target->GetActorTransform();
            F.Target->EnableDungeonPooling();
            const int32 OldLease = F.Target->GetDungeonPoolLease();
            // Enable只登记池管理；真实Sleep移除旧GE/状态，Wake才开始下一生命。TimeOnly夹具不替代该流程。
            F.Target->SleepForDungeonPool(false);
            TestTrue(TEXT("Actual pool sleep retires the old lease"), F.Target->IsDungeonPoolSleeping());
            TestTrue(TEXT("Actual pool wake begins a new target life"),
                     F.Target->WakeFromDungeonPool(Transform, NAME_None));
            TestTrue(TEXT("The woken target has a new positive pool lease"),
                     F.Target->GetDungeonPoolLease() > OldLease);
            TestFalse(TEXT("Actual pool reset clears state from the old target life"), F.IsPetrified());
            TestTrue(TEXT("Public preview close cancels only its owned point loop"), F.Developer->EndPreview(Error));
            F.Step(2.f);
            TestFalse(TEXT("Cancelled old point timers never apply state to the new pool life"), F.IsPetrified());
        }
        else
        {
            for (int32 Step = 0;
                 Step < 1200 && (Active->IsActive() ||
                                 F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount() > 0);
                 ++Step)
            {
                F.Step(.01f);
            }
            TestFalse(TEXT("Actual unchanged point/tail lifecycle finishes naturally"), Active->IsActive());
        }
        TestEqual(TEXT("Original owned PointCollection is released after normal or cancelled execution"),
                  Collections(F.World), 0);
        TestEqual(TEXT("Replacement loop leaves no active world execution"),
                  F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount(), 0);
    }
    return true;
}
#endif
