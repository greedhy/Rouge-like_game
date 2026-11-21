// 调用链：合法 Extension 数据图 -> 原兼容/新细节点编译 -> 同一 GraphInstance -> 真实 Mass/共享状态/位移。
// 每例先运行旧基线，再以独立世界复原相同行为；不把运行内部宏改名视作成功拆分。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityAuthoredGraphFineCompiler.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "NiagaraComponent.h"
#include "Terrain/TerrainSurface.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeFineExtensionTests
{
    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        TArray<UAbilityGraphInstance*> Runs;

        FWorld()
        {
            Target->Destroy();
            // 夹具只测试场景地面/位移，源敌人自带的世界空间 Widget/Mesh 不是障碍物。
            // 尤其 Widget 的 WorldDynamic 平面会令 Y 轴目标开始于穿透状态；保留 Pawn Capsule。
            TArray<UPrimitiveComponent*> Parts;
            Source->GetComponents(Parts);
            for (auto* Part : Parts)
            {
                if (Part != Source->GetCapsuleComponent())
                {
                    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                }
            }
        }

        ~FWorld()
        {
            for (auto* Run : Runs)
            {
                Run->Cancel();
            }
        }

        int32 Enemy(FVector Position, float StartingHealth = 200.f)
        {
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = StartingHealth;
            Type->Radius = 20.f;
            Type->HalfHeight = 40.f;
            Type->XPReward = 0;
            return Mass->SpawnMonster(Type, Position);
        }

        float Health(int32 Id) const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View) ? View.Health : -1.f;
        }

        FPureMassFragment& Fragment(int32 Id)
        {
            return World->GetSubsystem<UMassEntitySubsystem>()
                ->GetMutableEntityManager()
                .GetFragmentDataChecked<FPureMassFragment>(World->GetSubsystem<UPureMassSubsystem>()->GetEntity(Id));
        }

        /** 正式 Mass 处理器在位移后发布查询快照；夹具只更新指定实体，保持同一原生命。 */
        void Publish(const TArray<int32>& Ids)
        {
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            if (Mass->BeginUpdate(.001f))
            {
                for (int32 Id : Ids)
                {
                    Mass->UpdateMonster(Fragment(Id));
                }
                Mass->FinishUpdate();
            }
        }

        UExtensionNode* Node(EExtensionKind Behavior, float Damage = 0.f, float Radius = 40.f)
        {
            auto* Result = NewObject<UExtensionNode>(World);
            Result->Behavior = Behavior;
            Result->Damage = Damage;
            Result->DamagePerLevel = 0.f;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Cooldown = 0.f;
            Result->Radius = Radius;
            Result->Period = .1f;
            Result->Duration = 1.f;
            return Result;
        }

        /** 编译前明确验证原资产合同；新版不得靠拒绝合法配置绕过复原要求。 */
        UAbilityGraphDefinition* Compile(FAutomationTestBase& Test, UExtensionNode* Root, bool bFine)
        {
            if (!Test.TestTrue(TEXT("Synthetic extension graph satisfies the actual old contract"),
                               Root->ValidateGraph()))
            {
                return nullptr;
            }
            FString Error;
            auto* Result = bFine ? AbilityPrototypeFineCompiler::Compile(World, Root, TEXT("LegacyExtension.v1"), Error)
                                 : UAbilityLegacyGraphCompiler::Compile(World, Root, TEXT("LegacyExtension.v1"), Error);
            if (!Test.TestNotNull(FString(bFine ? TEXT("Fine extension: ") : TEXT("Legacy baseline: ")) + Error,
                                  Result))
            {
                return nullptr;
            }
            if (bFine)
            {
                for (const auto& Node : Result->Nodes)
                {
                    Test.TestFalse(TEXT("Extension default contains no complete old runtime macro"),
                                   Node.TypeId.ToString().StartsWith(TEXT("Legacy.")));
                    Test.TestFalse(TEXT("A renamed old execution config is not a fine primitive"),
                                   Node.Config->IsA<UAbilityLegacyNodeConfig>());
                }
            }
            return Result;
        }

        /** 捕获本次来源和指定目标的原生命；所有后续节点继续共享原 Context 的逻辑预算。 */
        UAbilityGraphInstance* Start(UAbilityGraphDefinition* Definition, int32 TargetId, FString& Error,
                                     UExtensionCastContext*& OutContext, FVector Origin = FVector::ZeroVector,
                                     int32 InitialSpawned = 0, int32 InitialSurfaces = 0)
        {
            auto* Context = NewObject<UExtensionCastContext>(World);
            OutContext = Context;
            Context->Caster = Source;
            Context->CastId = FGuid::NewGuid();
            Context->Level = 1;
            Context->Spawned = InitialSpawned;
            Context->Surfaces = InitialSurfaces;
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            if (!Context->CaptureSourceLife(Error))
            {
                return nullptr;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Context->GetSourceLife();
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            Input.Direction = FVector::ForwardVector;
            Input.Origin = Input.EventCenter = Origin;
            if (TargetId != INDEX_NONE)
            {
                FAbilityTargetToken Token;
                if (!AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, FPureMassCombatTarget(TargetId), Token,
                                                             Error))
                {
                    return nullptr;
                }
                Input.LiveTargetToken = Token;
            }
            auto* Run = NewObject<UAbilityGraphInstance>(World);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            Runs.Add(Run);
            return Run->StartWithInput(Definition, Input, Options, Error) ? Run : nullptr;
        }

        /** 模拟一个真实新帧，只推进图一次，不同时让 World subsystem 重复 tick 图。 */
        static void Advance(UAbilityGraphInstance& Run, float Delta)
        {
            ++GFrameCounter;
            Run.Advance(Delta);
        }

        void Floor()
        {
            auto* Actor = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(2000, 2000, 1));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Actor->SetActorLocation(FVector(0, 0, -51));
        }
    };

    /** 使用当前真实Mage目录/DA；不拿合成对象或Saved审计JSON替代资产验收。 */
    UExtensionNode* ActualRoot(FAutomationTestBase& Test, const TCHAR* Name)
    {
        const FString Path = FString::Printf(TEXT("/Game/PrototypeExtensions/Batch5/%s.%s"), Name, Name);
        auto* Root = LoadObject<UExtensionNode>(nullptr, *Path);
        if (!Test.TestNotNull(TEXT("Actual imported Extension asset: ") + Path, Root))
        {
            return nullptr;
        }
        const auto* Catalog =
            LoadObject<UAbilityInfo>(nullptr, TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo"));
        bool bReferenced = false;
        if (Catalog)
        {
            for (const auto& Row : Catalog->AbilityInformation)
            {
                const auto* Ability = Row.Ability ? Cast<UPrototypeAbility>(Row.Ability->GetDefaultObject()) : nullptr;
                bReferenced |=
                    Ability && Row.RequiredProfession == EPlayerProfession::Mage && Ability->Prototype == Root;
            }
        }
        return Test.TestTrue(TEXT("Real mage combat directory references this exact root DA"), bReferenced) ? Root
                                                                                                            : nullptr;
    }

    AExtensionRuntime* VisualRoot(FWorld& F, const UExtensionNode& Root, bool bActiveOnly = false)
    {
        for (TActorIterator<AExtensionRuntime> It(F.World); It; ++It)
        {
            if ((!bActiveOnly || !It->bFinished) && It->GetOwner() == F.Source &&
                (It->Node == &Root || It->GetPresentationSnapshot().StableReference == &Root))
            {
                if (!It->HasActorBegunPlay())
                {
                    It->DispatchBeginPlay();
                }
                return *It;
            }
        }
        return nullptr;
    }

    /** 真实 Actor GE 的调用数和 SetByCaller debuff值，不以无效伤害参数跳过结算。 */
    bool ActorPayload(FAutomationTestBase& Test, bool bZero)
    {
        for (const bool bFine : {false, true})
        {
            FEditorScriptExecutionGuard Guard;
            FAbilityGraphTestFixture F;
            auto* TargetASC = F.Target->GetAbilitySystemComponent();
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            F.Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            F.Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            F.Target->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
            F.Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
            auto* Root = NewObject<UExtensionNode>(F.World);
            Root->Behavior = EExtensionKind::Burst;
            Root->Radius = 350.f;
            Root->Damage = bZero ? 0.f : 1.f;
            Root->DamagePerLevel = 0.f;
            Root->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Root->Cooldown = 0.f;
            FString Error;
            auto* Definition =
                bFine ? AbilityPrototypeFineCompiler::Compile(F.World, Root, TEXT("LegacyExtension.v1"), Error)
                      : UAbilityLegacyGraphCompiler::Compile(F.World, Root, TEXT("LegacyExtension.v1"), Error);
            if (!Test.TestNotNull(FString(bFine ? TEXT("Fine Actor payload: ") : TEXT("Old Actor payload: ")) + Error,
                                  Definition))
            {
                return false;
            }
            auto* Context = NewObject<UExtensionCastContext>(F.World);
            Context->Caster = F.Source;
            Context->Level = 1;
            Context->CastId = FGuid::NewGuid();
            Context->DamageParams.SourceAbilitySystemComponent = F.ASC;
            Context->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            Context->DamageParams.DebuffChance = 100.f;
            Context->CaptureSourceLife(Error);
            int32 Calls = 0;
            float DebuffChance = -1.f;
            const auto Handle = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                {
                    if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                    {
                        ++Calls;
                        DebuffChance = Spec.GetSetByCallerMagnitude(FMyGameplayTags::Get().Debuff_Chance, false, -1.f);
                    }
                });
            ON_SCOPE_EXIT
            {
                TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
            };
            FAbilityNodeInput Input;
            Input.SourceToken = Context->GetSourceLife();
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            Input.Direction = FVector::ForwardVector;
            auto* Run = NewObject<UAbilityGraphInstance>(F.World);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            if (!Test.TestTrue(TEXT("Actual Actor extension burst starts: ") + Error,
                               Run->StartWithInput(Definition, Input, Options, Error)))
            {
                return false;
            }
            Test.TestEqual(TEXT("Extension zero damage does not invoke an Actor GE"), Calls, bZero ? 0 : 1);
            Test.TestEqual(TEXT("Actual constant test GE exposes any erroneous zero-damage application"),
                           TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), bZero ? 100.f : 90.f);
            if (!bZero)
            {
                Test.TestEqual(TEXT("Extension's actual Actor payload explicitly disables the random root debuff"),
                               DebuffChance, 0.f);
            }
            Test.TestEqual(TEXT("Local payload profile never mutates the root damage template"),
                           Context->DamageParams.DebuffChance, 100.f);
            Run->Cancel();
        }
        return true;
    }
} // namespace AbilityPrototypeFineExtensionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineExtensionZeroActorDamage,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ZeroActorDamagePreservesNoGameplayEffectCall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineExtensionZeroActorDamage::RunTest(const FString&)
{
    return AbilityPrototypeFineExtensionTests::ActorPayload(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineExtensionActorDebuffProfile,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ActorPayloadDisablesDebuffWithoutMutatingRoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineExtensionActorDebuffProfile::RunTest(const FString&)
{
    return AbilityPrototypeFineExtensionTests::ActorPayload(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineExtensionBurstDelayHooks,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.BurstHitDelayCompletionUsesRealChildFragments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineExtensionBurstDelayHooks::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        const int32 Victim = F.Enemy(FVector(25, 0, 0));
        auto* Root = F.Node(EExtensionKind::Burst, 1.f);
        Root->HitChild = F.Node(EExtensionKind::Delay);
        Root->HitChild->ArmDelay = .2f;
        Root->HitChild->CompleteChild = F.Node(EExtensionKind::Burst, 7.f);
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, INDEX_NONE, Error, Context);
        if (!TestNotNull(TEXT("Actual burst→delay→burst starts: ") + Error, Run))
        {
            return false;
        }
        TestEqual(TEXT("Initial burst applies exactly its root damage"), F.Health(Victim), 199.f);
        FWorld::Advance(*Run, .19f);
        TestEqual(TEXT("Delay child does not release early"), F.Health(Victim), 199.f);
        FWorld::Advance(*Run, .02f);
        TestEqual(TEXT("Delayed completion runs the separately configured burst"), F.Health(Victim), 192.f);
        TestEqual(TEXT("Only three actual logical fragments consume the old spawn budget"), Context->Spawned, 3);
        TestTrue(TEXT("Immediate extension burst and delayed child fully release the graph"), Run->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineExtensionAttachTransfers,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.AttachPeriodTransfersOriginalAnchorAndCancelsExpiry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineExtensionAttachTransfers::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        const int32 Anchor = F.Enemy(FVector(25, 0, 0));
        const int32 Next = F.Enemy(FVector(200, 0, 0));
        auto* Root = F.Node(EExtensionKind::Attach);
        Root->Duration = .6f;
        Root->TransferRadius = 300.f;
        Root->MaxTransfers = 1;
        Root->PeriodChild = F.Node(EExtensionKind::Burst, 2.f, 20.f);
        Root->CompleteChild = F.Node(EExtensionKind::Burst, 30.f, 20.f);
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, Anchor, Error, Context);
        if (!TestNotNull(TEXT("Actual extension anchor starts: ") + Error, Run))
        {
            return false;
        }
        FWorld::Advance(*Run, .11f);
        TestEqual(TEXT("First period's separate burst follows the real anchor"), F.Health(Anchor), 198.f);
        FPureMassDamage Kill;
        Kill.Source = F.Source;
        Kill.Amount = 1000.f;
        Kill.DamageType = FMyGameplayTags::Get().Damage_Magic;
        F.World->GetSubsystem<UPureMassSubsystem>()->ApplyDamage(Anchor, Kill);
        FWorld::Advance(*Run, .11f);
        TestEqual(TEXT("Loss of the old anchor transfers and pulses at another actual Mass life"), F.Health(Next),
                  198.f);
        Run->Cancel();
        FWorld::Advance(*Run, 1.f);
        TestEqual(TEXT("Cancellation never substitutes a normal CompleteChild burst"), F.Health(Next), 198.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineExtensionStackSharedThreshold,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.CrossCastStackThresholdPreservesLatestContextAndSingleDetonation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineExtensionStackSharedThreshold::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        const int32 Victim = F.Enemy(FVector(25, 0, 0));
        auto* Root = F.Node(EExtensionKind::Stack);
        Root->EffectId = TEXT("FineExtensionThreshold");
        Root->StackThreshold = 3;
        Root->DetonationDelay = .1f;
        Root->CompleteChild = F.Node(EExtensionKind::Burst, 11.f);
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        TArray<UAbilityGraphInstance*> Runs;
        TArray<UExtensionCastContext*> Contexts;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            UExtensionCastContext* Context = nullptr;
            auto* Run = F.Start(Definition, Victim, Error, Context);
            if (!TestNotNull(TEXT("Independent cast joins the shared stack key: ") + Error, Run))
            {
                return false;
            }
            Runs.Add(Run);
            Contexts.Add(Context);
        }
        Runs[0]->Cancel();
        auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
        TestEqual(TEXT("Old caller cleanup cannot cancel the latest three layers"),
                  State->LayersFor(F.Source, Root->EffectId, FPureMassCombatTarget(Victim)), 3);
        State->Tick(.11f);
        FWorld::Advance(*Runs[2], 0.f);
        TestEqual(TEXT("Exactly one shared threshold executes actual CompleteChild damage"), F.Health(Victim), 189.f);
        TestEqual(TEXT("Latest caster context owns the threshold's actual child generation"), Contexts[2]->Spawned, 2);
        State->Tick(1.f);
        FWorld::Advance(*Runs[2], 1.f);
        TestEqual(TEXT("Detached state cannot detonate again"), F.Health(Victim), 189.f);
        TestEqual(TEXT("Shared stack state is consumed once"), State->StackCount(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineExtensionDisplaceCompletion,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.DisplaceCompletionOriginUsesActualMovedTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineExtensionDisplaceCompletion::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Floor();
        const int32 Victim = F.Enemy(FVector(200, 0, 0));
        const int32 Neighbor = F.Enemy(FVector(90, 0, 0));
        auto* Root = F.Node(EExtensionKind::Displace);
        Root->Duration = .5f;
        Root->bSingleTarget = true;
        Root->MoveDistance = 180.f;
        Root->SafeDistance = 70.f;
        Root->bPull = true;
        Root->CompleteChild = F.Node(EExtensionKind::Burst, 17.f, 40.f);
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, Victim, Error, Context);
        if (!TestNotNull(TEXT("Actual extension single displacement starts: ") + Error, Run))
        {
            return false;
        }
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        auto& Fragment = F.World->GetSubsystem<UMassEntitySubsystem>()
                             ->GetMutableEntityManager()
                             .GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Victim));
        auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
        State->MoveMass(Fragment, .5f);
        if (Mass->BeginUpdate(.001f))
        {
            Mass->UpdateMonster(Fragment);
            Mass->FinishUpdate();
        }
        State->Tick(.5f);
        FWorld::Advance(*Run, .5f);
        TestTrue(TEXT("Original movement service reaches the authored safe distance"),
                 FMath::IsNearlyEqual(Fragment.Location.X, 70.0, 1.));
        TestEqual(TEXT("CompleteChild burst centers on the actually moved target"), F.Health(Victim), 183.f);
        TestEqual(TEXT("Moved endpoint area reaches a distinct nearby target"), F.Health(Neighbor), 183.f);
        TestTrue(TEXT("Displacement and immediate burst finish their graph"), Run->IsFinished());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineActualExtensionAttachVisual,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ActualAttachAssetOwnsAnchorVisualAndPeriodPayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineActualExtensionAttachVisual::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Root = ActualRoot(*this, TEXT("DA_Prototype_Attach"));
    if (!Root || !TestNotNull(TEXT("Actual Attach root has a real loop effect"), Root->LoopVFX.Get()) ||
        !TestNotNull(TEXT("Actual Attach has its own period payload"), Root->PeriodChild.Get()))
    {
        return false;
    }
    const float OriginalRadius = Root->Radius;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        const int32 Victim = F.Enemy(FVector(200, 0, 0));
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, Victim, Error, Context);
        if (!TestNotNull(TEXT("Actual asset starts through the selected default path: ") + Error, Run))
        {
            return false;
        }
        auto* Host = VisualRoot(F, *Root);
        if (!TestNotNull(TEXT("Actual Attach owns the existing Extension renderer class"), Host))
        {
            return false;
        }
        auto* Visual = Host->FindComponentByClass<UNiagaraComponent>();
        TestTrue(TEXT("Actual stable resource is playing at the original head offset"),
                 Visual && Visual->GetAsset() == Root->LoopVFX &&
                     Visual->GetRelativeLocation().Equals(FVector(0, 0, 85)));
        F.Fragment(Victim).Location = FVector(250, 0, 0);
        F.Publish({Victim});
        FWorld::Advance(*Run, Root->Period + .01f);
        TestTrue(TEXT("Renderer follows the current position of its original anchor life"),
                 Host->GetActorLocation().Equals(FVector(250, 0, 0), .01));
        TestEqual(TEXT("Period uses the actual configured independent child damage"), F.Health(Victim),
                  200.f - Root->PeriodChild->DamageAtLevel(1));
        Run->Cancel();
        TestTrue(TEXT("Cancelled state also closes its exact actual visual"), Host->bFinished);
        TestTrue(TEXT("Cancelled loop is inactive"), Visual && !Visual->IsActive());
        FWorld::Advance(*Run, Root->Period * 2.f);
        TestEqual(TEXT("Cancellation never releases another period payload"), F.Health(Victim),
                  200.f - Root->PeriodChild->DamageAtLevel(1));
    }
    TestEqual(TEXT("Actual root DA is never rewritten"), Root->Radius, OriginalRadius);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineActualExtensionAreaDisplace,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ActualAreaDisplaceOwnsParallelMovesAndRadiusVisual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineActualExtensionAreaDisplace::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Root = ActualRoot(*this, TEXT("DA_Prototype_Displace"));
    if (!Root || !TestFalse(TEXT("Actual Displace is a complete area query"), Root->bSingleTarget))
    {
        return false;
    }
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Floor();
        const int32 A = F.Enemy(FVector(200, 0, 0));
        const int32 B = F.Enemy(FVector(0, 200, 0));
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, INDEX_NONE, Error, Context);
        if (!TestNotNull(TEXT("Actual multi-target displacement starts: ") + Error, Run))
        {
            return false;
        }
        auto* Host = VisualRoot(F, *Root);
        auto* Visual = Host ? Host->FindComponentByClass<UNiagaraComponent>() : nullptr;
        TestTrue(TEXT("Real radius visual matches the actual move-query radius"),
                 Visual && Visual->GetAsset() == Root->LoopVFX &&
                     Visual->GetRelativeScale3D().Equals(FVector(Root->Radius / 100.f, Root->Radius / 100.f, .3f)));
        auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
        State->MoveMass(F.Fragment(A), Root->Duration);
        State->MoveMass(F.Fragment(B), Root->Duration);
        F.Publish({A, B});
        State->Tick(Root->Duration);
        FWorld::Advance(*Run, Root->Duration);
        TestTrue(TEXT("First actual movement command reaches the same safe distance"),
                 F.Fragment(A).Location.Equals(FVector(Root->SafeDistance, 0, -9), 1.));
        TestTrue(TEXT("Second target owns a different simultaneous movement command"),
                 F.Fragment(B).Location.Equals(FVector(0, Root->SafeDistance, -9), 1.));
        TestEqual(TEXT("Internal area movement commands do not consume extra logical prototype spawns"),
                  Context->Spawned, 1);
        TestTrue(TEXT("Area lifetime and owned visual finish at the authored duration"),
                 Run->IsFinished() && Host && Host->bFinished);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineExtensionHistoricalMoveCenter,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.HitChildPushPreservesOffAxisRootEventCenter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineExtensionHistoricalMoveCenter::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Floor();
        const int32 Victim = F.Enemy(FVector(0, 200, 0));
        const int32 Neighbor = F.Enemy(FVector(0, 350, 0));
        auto* Root = F.Node(EExtensionKind::Burst, 0.f, 300.f);
        auto* Move = F.Node(EExtensionKind::Displace);
        Move->Duration = .5f;
        Move->MoveDistance = 120.f;
        Move->SafeDistance = 0.f;
        Move->bPull = false;
        Move->bSingleTarget = true;
        Move->CompleteChild = F.Node(EExtensionKind::Burst, 17.f, 40.f);
        Root->HitChild = Move;
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, INDEX_NONE, Error, Context);
        if (!TestNotNull(TEXT("Actual zero-damage contact admits its independent movement child: ") + Error, Run))
        {
            return false;
        }
        auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
        State->MoveMass(F.Fragment(Victim), .5f);
        F.Publish({Victim, Neighbor});
        State->Tick(.5f);
        FWorld::Advance(*Run, .5f);
        TestTrue(TEXT("Push uses historical root center, not impact origin or arbitrary cast direction"),
                 F.Fragment(Victim).Location.Equals(FVector(0, 320, -9), 1.));
        TestEqual(TEXT("Moved target receives the actual completion payload"), F.Health(Victim), 183.f);
        TestEqual(TEXT("Off-axis completion area reaches a distinct endpoint neighbor"), F.Health(Neighbor), 183.f);
        TestEqual(TEXT("Root, actual movement child and completion child reserve exactly three logical spawns"),
                  Context->Spawned, 3);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineActualEmberBrandClosure,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ActualEmberProjectileHitsSharedStackAndSingleBlast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineActualEmberBrandClosure::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Root = ActualRoot(*this, TEXT("DA_EmberBrand"));
    if (!Root || !Root->HitChild || !Root->HitChild->CompleteChild)
    {
        return false;
    }
    const auto* Mark = Root->HitChild.Get();
    for (const bool bFine : {false, true})
    {
        FWorld F;
        const int32 Victim = F.Enemy(FVector(200, 0, 0));
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Latest = nullptr;
        UAbilityGraphInstance* LatestRun = nullptr;
        for (int32 Index = 0; Index < Mark->StackThreshold; ++Index)
        {
            LatestRun = F.Start(Definition, INDEX_NONE, Error, Latest);
            if (!TestNotNull(TEXT("Actual EmberBrand projectile starts: ") + Error, LatestRun))
            {
                return false;
            }
            auto* Host = VisualRoot(F, *Root, true);
            TestTrue(TEXT("Actual projectile owns its stable fireball loop"),
                     Host && Host->FindComponentByClass<UNiagaraComponent>() &&
                         Host->FindComponentByClass<UNiagaraComponent>()->GetAsset() == Root->LoopVFX);
            FWorld::Advance(*LatestRun, .3f);
            TestEqual(TEXT("Each real projectile applies its own root damage once"), F.Health(Victim),
                      200.f - Root->DamageAtLevel(1) * (Index + 1));
            TestTrue(TEXT("Projectile contact terminates its own flight visual"), Host && Host->bFinished);
            TestEqual(TEXT("Actual contact joins the same shared stack key"),
                      F.World->GetSubsystem<UExtensionWorldSubsystem>()->LayersFor(F.Source, Mark->EffectId,
                                                                                   FPureMassCombatTarget(Victim)),
                      Index + 1);
        }
        F.World->GetSubsystem<UExtensionWorldSubsystem>()->Tick(Mark->DetonationDelay + .01f);
        FWorld::Advance(*LatestRun, 0.f);
        TestEqual(TEXT("Threshold applies exactly one actual configured blast"), F.Health(Victim),
                  200.f - Root->DamageAtLevel(1) * Mark->StackThreshold - Mark->CompleteChild->DamageAtLevel(1));
        TestEqual(TEXT("Latest cast owns projectile, mark and one threshold child"), Latest->Spawned, 3);
        TestEqual(TEXT("Consumed actual mark is not retained"),
                  F.World->GetSubsystem<UExtensionWorldSubsystem>()->StackCount(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineActualStormConvergenceClosure,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ActualStormChainStartsImmediatelyAndMarksEachOriginalLife",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineActualStormConvergenceClosure::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Root = ActualRoot(*this, TEXT("DA_StormConvergence"));
    if (!Root || !Root->HitChild)
    {
        return false;
    }
    for (const bool bFine : {false, true})
    {
        FWorld F;
        TArray<int32> Victims = {F.Enemy(FVector(200, 0, 0)), F.Enemy(FVector(300, 0, 0)), F.Enemy(FVector(400, 0, 0))};
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, Victims[0], Error, Context);
        if (!TestNotNull(TEXT("Actual Extension chain starts: ") + Error, Run))
        {
            return false;
        }
        TestEqual(TEXT("Extension first contact occurs during Start without Advance"), F.Health(Victims[0]),
                  200.f - Root->DamageAtLevel(1));
        auto* Host = VisualRoot(F, *Root);
        FWorld::Advance(*Run, Root->Period + .01f);
        FWorld::Advance(*Run, Root->Period + .01f);
        for (int32 Index = 0; Index < Victims.Num(); ++Index)
        {
            TestEqual(TEXT("Chain applies the actual independent per-hop scalar once"), F.Health(Victims[Index]),
                      200.f - Root->DamageAtLevel(1) * FMath::Pow(Root->DamageFalloff, Index));
            TestEqual(TEXT("Each original life receives exactly one real shared mark"),
                      F.World->GetSubsystem<UExtensionWorldSubsystem>()->LayersFor(
                          F.Source, Root->HitChild->EffectId, FPureMassCombatTarget(Victims[Index])),
                      1);
        }
        TestTrue(TEXT("Actual chain visual closes on its final hit frame"), Host && Host->bFinished);
        TestTrue(TEXT("Actual beam endpoint is the final target snapshot"),
                 Host && Host->BeamEnd.Equals(FVector(400, 0, 0), .01));
        TestEqual(TEXT("One root plus three actual mark fragments reserve logically"), Context->Spawned, 4);
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineActualStackPresentationOwnership,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ActualEmberMarkVisualTransfersLeaseAndClosesAtThreshold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineActualStackPresentationOwnership::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Parent = ActualRoot(*this, TEXT("DA_EmberBrand"));
    auto* Root = Parent ? Parent->HitChild.Get() : nullptr;
    if (!TestNotNull(TEXT("Actual projectile's shared mark DA"), Root))
    {
        return false;
    }
    for (const bool bFine : {false, true})
    {
        FWorld F;
        const int32 Victim = F.Enemy(FVector(200, 0, 0));
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Latest = nullptr;
        UAbilityGraphInstance* Run = nullptr;
        TArray<AExtensionRuntime*> Hosts;
        for (int32 Index = 0; Index < Root->StackThreshold; ++Index)
        {
            Run = F.Start(Definition, Victim, Error, Latest, FVector(200, 0, 0));
            if (!TestNotNull(TEXT("Actual mark application starts: ") + Error, Run))
            {
                return false;
            }
            auto* Host = VisualRoot(F, *Root, true);
            if (!TestNotNull(FString::Printf(TEXT("%s mark application %d owns a distinct active renderer"),
                                             bFine ? TEXT("Fine") : TEXT("Original"), Index + 1),
                             Host))
            {
                return false;
            }
            Hosts.Add(Host);
            TestTrue(TEXT("Actual mark loop uses the stable configured resource"),
                     Host->FindComponentByClass<UNiagaraComponent>() &&
                         Host->FindComponentByClass<UNiagaraComponent>()->GetAsset() == Root->LoopVFX);
            for (int32 Old = 0; Old < Index; ++Old)
            {
                TestTrue(TEXT("Ownership transfer closes previous exact visual waiters"), Hosts[Old]->bFinished);
            }
        }
        F.World->GetSubsystem<UExtensionWorldSubsystem>()->Tick(Root->DetonationDelay + .01f);
        FWorld::Advance(*Run, 0.f);
        TestTrue(TEXT("The triggering lease's actual visual also closes"), Hosts.Last()->bFinished);
        TestEqual(TEXT("Only actual configured threshold child deals damage"), F.Health(Victim),
                  200.f - Root->CompleteChild->DamageAtLevel(1));
        TestEqual(TEXT("Only latest shared context owns its threshold child"), Latest->Spawned, 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineActualExtensionSurfaceOwnership,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ActualFrostChildOwnsConfiguredTerrainAndRemovesOnlyItsSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineActualExtensionSurfaceOwnership::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Parent = ActualRoot(*this, TEXT("DA_FrostParasite"));
    auto* Root = Parent ? Parent->CompleteChild.Get() : nullptr;
    if (!TestNotNull(TEXT("Actual FrostParasite expiry surface DA"), Root) ||
        !TestNotNull(TEXT("Actual configured terrain Blueprint class"), Root->SurfaceClass.Get()))
    {
        return false;
    }
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Floor();
        auto* Definition = F.Compile(*this, Root, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, INDEX_NONE, Error, Context);
        if (!TestNotNull(TEXT("Actual configured terrain scope starts: ") + Error, Run))
        {
            return false;
        }
        ATerrainSurface* Surface = nullptr;
        int32 Count = 0;
        for (TActorIterator<ATerrainSurface> It(F.World); It; ++It)
        {
            if (It->IsA(Root->SurfaceClass) && It->GetOwner() == F.Source)
            {
                Surface = *It;
                ++Count;
            }
        }
        if (!TestEqual(TEXT("Exactly one real configured terrain actor is owned"), Count, 1) ||
            !TestNotNull(TEXT("Actual terrain instance"), Surface))
        {
            return false;
        }
        TestTrue(TEXT("Actual terrain uses frozen radius, not a substitute effect actor"),
                 Surface->SurfaceSize.Equals(FVector2D(Root->Radius * 2.f)));
        TestTrue(TEXT("Actual terrain retains original caster-enemy filters"),
                 Surface->bOnlyAffectCasterEnemies && Surface->bAffectEnemies && !Surface->bAffectPlayers);
        TestEqual(TEXT("One terrain quota is reserved in original shared context"), Context->Surfaces, 1);
        TestEqual(TEXT("Terrain scope is one logical prototype spawn"), Context->Spawned, 1);
        TWeakObjectPtr<ATerrainSurface> WeakSurface(Surface);
        Run->Cancel();
        TestFalse(TEXT("Scope cancellation destroys its exact owned terrain"), WeakSurface.IsValid());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineAllNineActualExtensionClosures,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.AllNineActualRootsKeepCompleteDefaultTrace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineAllNineActualExtensionClosures::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    for (const TCHAR* Name : {TEXT("DA_Prototype_Attach"), TEXT("DA_Prototype_Stack"), TEXT("DA_Prototype_Displace"),
                              TEXT("DA_StormBrand"), TEXT("DA_FrostParasite"), TEXT("DA_EmberBrand"),
                              TEXT("DA_StormConvergence"), TEXT("DA_GravityWell"), TEXT("DA_BrambleRepulse")})
    {
        auto* Root = ActualRoot(*this, Name);
        if (!Root)
        {
            return false;
        }
        TArray<float> OriginalTrace;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Floor();
            const TArray<int32> Victims = {F.Enemy(FVector(200, 0, 0), 2000.f), F.Enemy(FVector(270, 0, 0), 2000.f),
                                           F.Enemy(FVector(330, 0, 0), 2000.f)};
            auto* Definition = F.Compile(*this, Root, bFine);
            if (!Definition)
            {
                return false;
            }
            auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
            TArray<UExtensionCastContext*> Contexts;
            bool bSawTerrain = false;
            auto CastRoot = [&]()
            {
                UExtensionCastContext* Context = nullptr;
                FString Error;
                auto* Run = F.Start(Definition, Victims[0], Error, Context);
                Contexts.Add(Context);
                return TestNotNull(FString::Printf(TEXT("%s %s actual root starts: %s"), Name,
                                                   bFine ? TEXT("Fine") : TEXT("Original"), *Error),
                                   Run);
            };
            if (!CastRoot())
            {
                return false;
            }
            TArray<float> Trace;
            for (int32 Step = 0; Step < 100; ++Step)
            {
                if (Root->Behavior == EExtensionKind::Projectile && (Step == 4 || Step == 8) && !CastRoot())
                {
                    return false;
                }
                for (int32 Victim : Victims)
                {
                    State->MoveMass(F.Fragment(Victim), .1f);
                }
                F.Publish(Victims);
                State->Tick(.1f);
                ++GFrameCounter;
                // 所有根本帧各推进一次；World并未另外tick这些图，不重复消费任何施法时钟。
                const auto Runs = F.Runs;
                for (auto* Run : Runs)
                {
                    Run->Advance(.1f);
                }
                for (TActorIterator<AExtensionRuntime> It(F.World); It; ++It)
                {
                    if (It->GetOwner() == F.Source && !It->HasActorBegunPlay())
                    {
                        It->DispatchBeginPlay();
                    }
                }
                for (TActorIterator<ATerrainSurface> It(F.World); It; ++It)
                {
                    bSawTerrain |= It->GetOwner() == F.Source;
                }
                for (int32 Victim : Victims)
                {
                    Trace.Add(F.Health(Victim));
                    const FVector Position = F.Fragment(Victim).Location;
                    Trace.Add(Position.X);
                    Trace.Add(Position.Y);
                    Trace.Add(Position.Z);
                }
                for (auto* Context : Contexts)
                {
                    Trace.Add(Context->Spawned);
                    Trace.Add(Context->Surfaces);
                }
            }
            for (auto* Run : F.Runs)
            {
                TestTrue(FString::Printf(TEXT("%s all default child lifetimes finish"), Name), Run->IsFinished());
                TestTrue(FString::Printf(TEXT("%s completes without hidden default failure"), Name),
                         Run->GetEndReason() == EAbilityGraphEndReason::Completed);
            }
            TestEqual(TEXT("All real shared marks expire or detonate by the full default horizon"), State->StackCount(),
                      0);
            if (Root->Behavior == EExtensionKind::Displace)
            {
                TestFalse(TEXT("Actual displacement defaults really move a captured target"),
                          F.Fragment(Victims[0]).Location.Equals(FVector(200, 0, 0), 1.f));
            }
            else
            {
                TestTrue(TEXT("Actual composed default performs its real damage, rather than only closing"),
                         F.Health(Victims[0]) < 2000.f);
            }
            if (FString(Name) == TEXT("DA_FrostParasite") || FString(Name) == TEXT("DA_BrambleRepulse"))
            {
                TestTrue(TEXT("The actual full root reaches its configured terrain child"), bSawTerrain);
            }
            for (TActorIterator<ATerrainSurface> It(F.World); It; ++It)
            {
                TestTrue(TEXT("A completed root retains no owned terrain actor"), It->GetOwner() != F.Source);
            }
            if (!bFine)
            {
                OriginalTrace = MoveTemp(Trace);
            }
            else if (TestEqual(FString(Name) + TEXT(" complete trace shape matches"), Trace.Num(), OriginalTrace.Num()))
            {
                for (int32 Index = 0; Index < Trace.Num(); ++Index)
                {
                    if (!FMath::IsNearlyEqual(Trace[Index], OriginalTrace[Index], .01f))
                    {
                        AddError(FString::Printf(TEXT("%s default trace differs at value %d: original %.4f, fine %.4f"),
                                                 Name, Index, OriginalTrace[Index], Trace[Index]));
                        break;
                    }
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineActualSurfaceQuotaFailure,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.SurfaceAdmissionRetainsQuotaBeforeLogicalFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineActualSurfaceQuotaFailure::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Parent = ActualRoot(*this, TEXT("DA_FrostParasite"));
    auto* Root = Parent ? Parent->CompleteChild.Get() : nullptr;
    if (!Root)
    {
        return false;
    }
    for (const bool bLogicalLimit : {false, true})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Floor();
            auto* Definition = F.Compile(*this, Root, bFine);
            if (!Definition)
            {
                return false;
            }
            FString Error;
            UExtensionCastContext* Context = nullptr;
            auto* Run = F.Start(Definition, INDEX_NONE, Error, Context, FVector::ZeroVector, bLogicalLimit ? 64 : 0,
                                bLogicalLimit ? 0 : 8);
            TestNotNull(FString::Printf(TEXT("%s accepts a valid compiled graph before %s runtime quota failure"),
                                        bFine ? TEXT("Fine") : TEXT("Original"),
                                        bLogicalLimit ? TEXT("logical") : TEXT("surface")),
                        Run);
            TestTrue(TEXT("Actual graph resource-acquisition failure retains Failed end reason"),
                     F.Runs.Last()->GetEndReason() == EAbilityGraphEndReason::Failed);
            TestTrue(TEXT("Actual failed execution closes the shared legacy context"), Context->bCancelled);
            TestEqual(TEXT("Surface quota/projection precedes the logical reservation"), Context->Surfaces,
                      bLogicalLimit ? 1 : 8);
            TestEqual(TEXT("Rejected admission does not reserve another logical spawn"), Context->Spawned,
                      bLogicalLimit ? 64 : 0);
            int32 TerrainCount = 0;
            for (TActorIterator<ATerrainSurface> It(F.World); It; ++It)
            {
                ++TerrainCount;
            }
            TestEqual(TEXT("Rejected surface admission creates no terrain"), TerrainCount, 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredMovingCompletedKeepsOriginalInput,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.MovingStateCompletedExtraEdgeRetainsOriginalInvocationOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredMovingCompletedKeepsOriginalInput::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Root = ActualRoot(*this, TEXT("DA_BrambleRepulse"));
    auto* Move = Root ? Root->HitChild.Get() : nullptr;
    if (!Root || !Move)
    {
        return false;
    }
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Floor();
        const int32 Victim = F.Enemy(FVector(200, 0, 0));
        const int32 OriginalOriginNeighbor = F.Enemy(FVector(0, 10, 0));
        auto* Actual = NewObject<UAbilityGraphDefinition>(F.World);
        Actual->SchemaVersion = 2;
        Actual->CompatibilityProfile = TEXT("LegacyExtension.v1");
        Actual->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
        auto Add = [&](UExtensionNode& Asset, FName Type, int32 Depth)
        {
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = Type;
            auto* Config = NewObject<UAbilityLegacyNodeConfig>(Actual);
            Config->Profile = Actual->CompatibilityProfile;
            Config->LegacyDepth = Depth;
            Config->PresentationParameters = &Asset;
            Config->Parameters = DuplicateObject(&Asset, Config);
            Node.Config = Config;
            Actual->Nodes.Add(Node);
            return Node.NodeId;
        };
        const FGuid Entry = Add(*Move, TEXT("Legacy.Extension.Displace"), 0);
        const FGuid Extra = Add(*Root, TEXT("Legacy.Extension.Burst"), 1);
        auto* ExtraConfig = CastChecked<UAbilityLegacyNodeConfig>(Actual->Nodes.Last().Config);
        ExtraConfig->Parameters->Damage = 17.f;
        ExtraConfig->Parameters->DamagePerLevel = 0.f;
        ExtraConfig->Parameters->Radius = 20.f;
        Actual->EntryNodeId = Entry;
        FAbilityGraphEdge Edge;
        Edge.From = Entry;
        Edge.OutputPort = TEXT("Completed");
        Edge.To = Extra;
        Actual->Edges.Add(Edge);
        FString Error;
        auto* Definition = bFine ? AbilityAuthoredGraphFineCompiler::Compile(F.World, Actual, Error) : Actual;
        if (!TestNotNull(TEXT("Actual moving extra-edge graph compiles: ") + Error, Definition))
        {
            return false;
        }
        UExtensionCastContext* Context = nullptr;
        auto* Run = F.Start(Definition, Victim, Error, Context);
        if (!TestNotNull(TEXT("Moving state and original completion edge start: ") + Error, Run))
        {
            return false;
        }
        auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
        State->MoveMass(F.Fragment(Victim), .5f);
        F.Publish({Victim, OriginalOriginNeighbor});
        State->Tick(.5f);
        FWorld::Advance(*Run, .5f);
        TestTrue(TEXT("The real original displacement actually reaches its distinct endpoint"),
                 F.Fragment(Victim).Location.Equals(FVector(350, 0, -9), 1.f));
        TestEqual(TEXT("Legacy Completed extra edge consumes original invocation origin"),
                  F.Health(OriginalOriginNeighbor), 183.f);
        TestEqual(TEXT("Completed does not silently become the final-position OnExpire port"), F.Health(Victim), 200.f);
        TestEqual(TEXT("Disconnected original DA surface child is not resurrected"), Context->Surfaces, 0);
        TestEqual(TEXT("Only the actual moving root and graph-only completion payload reserve"), Context->Spawned, 2);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeDisplacementRejectionIsBranchLocal,
    "test_25d.UnifiedGA.DynamicMage.FineExtension.ActualBrambleActorMoveRejectionOnlyClosesItsBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeDisplacementRejectionIsBranchLocal::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineExtensionTests;
    auto* Root = ActualRoot(*this, TEXT("DA_BrambleRepulse"));
    auto* Move = Root ? Root->HitChild.Get() : nullptr;
    if (!Move)
    {
        return false;
    }
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Floor();
        auto* Actor = F.World->SpawnActor<AEnemyCharacter>();
        Actor->Tags.AddUnique(TEXT("Enemy"));
        Actor->SetActorLocation(FVector(300, 0, 0));
        auto* TargetASC = Actor->GetAbilitySystemComponent();
        TargetASC->InitAbilityActorInfo(Actor, Actor);
        TargetASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 200.f);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 200.f);
        for (auto* Part : TInlineComponentArray<UPrimitiveComponent*>(Actor))
        {
            Part->SetCollisionEnabled(Part == Actor->GetCapsuleComponent() ? ECollisionEnabled::QueryOnly
                                                                           : ECollisionEnabled::NoCollision);
        }
        Actor->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
        Actor->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        FVector Rejected;
        TestFalse(TEXT("The original shared geometry actually rejects this floor-penetrating capsule"),
                  UExtensionWorldSubsystem::ConstrainMove(F.World, Actor, Actor->GetActorLocation(),
                                                          Actor->GetActorLocation() + FVector(Move->MoveDistance, 0, 0),
                                                          Actor->GetCapsuleComponent()->GetScaledCapsuleRadius(),
                                                          Actor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
                                                          Rejected));
        auto* Definition = F.Compile(*this, Move, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        auto* Context = NewObject<UExtensionCastContext>(F.World);
        Context->Caster = F.Source;
        Context->Level = 1;
        Context->DamageParams.SourceAbilitySystemComponent = F.ASC;
        Context->CaptureSourceLife(Error);
        FAbilityTargetToken Target;
        if (!TestTrue(TEXT("Actual actor retains its original hostile life"),
                      AbilityExecutionIdentity::CaptureTarget(Context->GetSourceLife(),
                                                              FPureMassCombatTarget::FromActor(Actor), Target, Error)))
        {
            return false;
        }
        FAbilityNodeInput Input;
        Input.SourceToken = Context->GetSourceLife();
        Input.Context = Context;
        Input.DamageParams = Context->DamageParams;
        Input.LiveTargetToken = Target;
        Input.Origin = Actor->GetActorLocation();
        Input.Direction = FVector::ForwardVector;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        F.Runs.Add(Run);
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Definition->CompatibilityProfile;
        Options.BudgetPolicy = Definition->BudgetPolicy;
        TestTrue(TEXT("Valid original/fine configuration is accepted"),
                 Run->StartWithInput(Definition, Input, Options, Error));
        FWorld::Advance(*Run, .05f);
        TestTrue(TEXT("Rejected move closes this original branch immediately"), Run->IsFinished());
        TestEqual(TEXT("Original local movement rejection does not fail the entire cast"), Run->GetEndReason(),
                  EAbilityGraphEndReason::Completed);
        TestFalse(TEXT("Original rejected movement does not cancel its siblings' shared context"), Context->bCancelled);
        TestEqual(TEXT("Rejected movement admits no normal completion terrain child"), Context->Surfaces, 0);
        TestEqual(TEXT("Only the attempted original movement fragment reserves logically"), Context->Spawned, 1);
    }
    return true;
}
#endif
