// 调用链：Automation -> ASC TryActivateAbility -> 生产 TargetData 的服务器委托 -> 生产 GA Commit/RunGraph。
// 本夹具不是双端联网证明；网络传输和远端视觉仍需 PIE 验收。
#include "AbilityGraphTestAbility.h"
#include "AbilitySystem/Unified/AbilityTask_RunGraph.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UAbilityGraphTestCost::UAbilityGraphTestCost()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo Modifier;
    Modifier.Attribute = UMyAttributeSet::GetManaAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(-10.f);
    Modifiers.Add(Modifier);
}

UAbilityGraphTestCooldown::UAbilityGraphTestCooldown()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(2.f);
    auto* Component = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTags"));
    GEComponents.Add(Component);
    FInheritedTagContainer Tags;
    Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Unified.DelayedPetrify")));
    Component->SetAndApplyTargetTagChanges(Tags);
}

UAbilityGraphTestAbility::UAbilityGraphTestAbility()
{
    CostGameplayEffectClass = UAbilityGraphTestCost::StaticClass();
    CooldownGameplayEffectClass = UAbilityGraphTestCooldown::StaticClass();
}

void UAbilityGraphTestAbility::StepGraph(float Delta)
{
    if (GraphTask)
    {
        GraphTask->TickTask(Delta);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "TimerManager.h"
#include "Roguelike/DungeonEnemyPool.h"
#include "Components/BoxComponent.h"
#include "UObject/Script.h"
#include "UObject/StrongObjectPtr.h"
#include "DungeonPlayerRunTestTypes.h"

namespace AbilityGraphIntegrationTests
{
    /** 每个测试授予真实 GA，模拟远程玩家的服务器 ActorInfo，不依赖鼠标窗口。 */
    struct FGraphIntegrationFixture : FAbilityGraphTestFixture
    {
        FGameplayAbilitySpecHandle Handle;
        UAbilityGraphTestAbility* Ability = nullptr;
        bool bCancelBeforeDestroy = true;
        int16 RequestSequence = 100;

        FGraphIntegrationFixture()
        {
            ASC->AbilityActorInfo->PlayerController = World->SpawnActor<AAbilityGraphRemoteTestController>();
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityGraphTestAbility::StaticClass(), 1));
            Ability = Cast<UAbilityGraphTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
            Ability->Graph = Graph;
        }

        /** 模拟服务器收到客户端预测激活请求；普通 TryActivate 会向远端发指令，不能作为收包测试。 */
        bool ActivateFromClientRequest()
        {
            FPredictionKey RequestKey;
            RequestKey.Current = ++RequestSequence;
            ASC->CallServerTryActivateAbility(Handle, false, RequestKey);
            return Ability->IsActive();
        }

        /** 引擎会限制单帧 Delta 且计时器每帧只推进一次；以实际小帧模拟游戏时间。 */
        void AdvanceWorld(float Seconds)
        {
            for (float Elapsed = 0; Elapsed < Seconds; Elapsed += .05f)
            {
                ++GFrameCounter;
                World->Tick(LEVELTICK_All, .05f);
            }
        }

        /** 通过真实 ASC 目标缓存和委托投递；不直接调用 GA 的 TargetReady。 */
        void SendTarget(const FPureMassCombatTarget& TargetData)
        {
            FVector Location;
            PureMassCombat::Resolve(World, Source, TargetData, Location);
            auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
            if (TargetData.IsActor())
            {
                Hit->HitResult =
                    FHitResult(TargetData.Actor.Get(), Cast<UPrimitiveComponent>(TargetData.Actor->GetRootComponent()),
                               Location, FVector::UpVector);
            }
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.Item = TargetData.EntityId;
            Hit->HitResult.ImpactPoint = Location;
            FGameplayAbilityTargetDataHandle Data;
            Data.Add(Hit);
            SendData(Data);
        }

        /** 允许投递畸形/伪造载荷，仍走生产 ASC 缓存和目标任务委托。 */
        void SendData(const FGameplayAbilityTargetDataHandle& Data)
        {
            ASC->ServerSetReplicatedTargetData(Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                               Data, FGameplayTag(), FPredictionKey());
        }

        /** 测试不留下活跃 AbilityTask，尤其覆盖失败断言提前返回时的清理。 */
        ~FGraphIntegrationFixture()
        {
            if (bCancelBeforeDestroy)
            {
                ASC->CancelAllAbilities();
            }
        }
    };
} // namespace AbilityGraphIntegrationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphIntegrationActor, "test_25d.UnifiedGA.Integration.ActorCostAndLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 单次费用/冷却、延迟生效和到期恢复同时检查真实 ASC 与目标组件。 */
bool FAbilityGraphIntegrationActor::RunTest(const FString&)
{
    AbilityGraphIntegrationTests::FGraphIntegrationFixture F;
    TestTrue(TEXT("ASC activates production entry"), F.ActivateFromClientRequest());
    TestTrue(TEXT("Waiting for replicated target"), F.Ability->IsActive());
    TestEqual(TEXT("No cost before validated target"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              100.f);
    F.SendTarget(FPureMassCombatTarget::FromActor(F.Target));
    F.SendTarget(FPureMassCombatTarget::FromActor(F.Target));
    TestEqual(TEXT("Repeated target cannot commit twice"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
    TestEqual(TEXT("Exactly one cooldown GE"), F.ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 1);
    F.Ability->StepGraph(.24f);
    TestFalse(TEXT("No early control"), F.IsPetrified());
    F.Ability->StepGraph(.011f);
    TestTrue(TEXT("Actor control after delay"), F.IsPetrified());
    TestFalse(TEXT("GA ended normally"), F.Ability->IsActive());
    TestFalse(TEXT("Tasks and target timeout cleared"), F.Ability->HasPendingGraphWork());
    TestFalse(TEXT("Cooldown prevents second cast"), F.ActivateFromClientRequest());
    F.AdvanceWorld(1.1f);
    auto* Control = F.Target->FindComponentByClass<UBatch6PetrifyComponent>();
    if (TestNotNull(TEXT("Real state component"), Control))
    {
        Control->TickComponent(1.01f, LEVELTICK_All, nullptr);
    }
    TestFalse(TEXT("Target control expires independently of finished GA"), F.IsPetrified());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphIntegrationCancel,
                                 "test_25d.UnifiedGA.Integration.CancelTimeoutAndAvatar",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 取消、超时、来源替换均必须释放真实 GAS 任务并阻止后续效果。 */
bool FAbilityGraphIntegrationCancel::RunTest(const FString&)
{
    {
        AbilityGraphIntegrationTests::FGraphIntegrationFixture F;
        F.ActivateFromClientRequest();
        F.SendTarget(FPureMassCombatTarget::FromActor(F.Target));
        F.ASC->CancelAbilityHandle(F.Handle);
        F.Ability->StepGraph(1.f);
        TestFalse(TEXT("Cancelled cast no control"), F.IsPetrified());
        TestFalse(TEXT("Cancellation cleared all work"), F.Ability->HasPendingGraphWork());
    }
    {
        AbilityGraphIntegrationTests::FGraphIntegrationFixture F;
        F.ActivateFromClientRequest();
        F.AdvanceWorld(3.2f);
        TestFalse(TEXT("Missing data times out"), F.Ability->IsActive());
        TestFalse(TEXT("Timeout removes target listener/task"), F.Ability->HasPendingGraphWork());
        TestEqual(TEXT("No cost on target timeout"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                  100.f);
    }
    {
        AbilityGraphIntegrationTests::FGraphIntegrationFixture F;
        F.ActivateFromClientRequest();
        F.SendTarget(FPureMassCombatTarget::FromActor(F.Target));
        F.ASC->InitAbilityActorInfo(F.Source, F.World->SpawnActor<AEnemyCharacter>());
        F.Ability->StepGraph(1.f);
        TestFalse(TEXT("Old avatar cast cannot apply"), F.IsPetrified());
        TestFalse(TEXT("Avatar replacement ends graph task"), F.Ability->HasPendingGraphWork());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphIntegrationMass,
                                 "test_25d.UnifiedGA.Integration.MassApplyExpireAndRemoval",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Mass 使用同一个 GAS 入口，真实实体状态到期；移除后不允许延迟控制。 */
bool FAbilityGraphIntegrationMass::RunTest(const FString&)
{
    for (bool bRemove : {false, true})
    {
        AbilityGraphIntegrationTests::FGraphIntegrationFixture F;
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = 100;
        Type->XPReward = 0;
        int32 Id = Mass->SpawnMonster(Type, FVector(150, 100, 0));
        TestTrue(TEXT("ASC starts mixed target entry"), F.ActivateFromClientRequest());
        F.SendTarget(FPureMassCombatTarget(Id));
        if (bRemove)
        {
            Mass->RemoveMonster(Id);
        }
        F.Ability->StepGraph(.251f);
        FPureMassView View;
        if (!bRemove)
        {
            TestTrue(TEXT("Mass target resolves"), Mass->Resolve(Id, View));
            TestTrue(TEXT("Real entity petrified"), View.bPetrified);
            auto& Manager = F.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
            auto& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Id));
            for (int32 Step = 0; Step < 5; ++Step)
            {
                Mass->BeginUpdate(.25f);
                Mass->UpdateMonster(Fragment);
                Mass->FinishUpdate();
            }
            Mass->Resolve(Id, View);
            TestFalse(TEXT("Mass status expires"), View.bPetrified);
        }
        else
        {
            TestFalse(TEXT("Removed entity stays absent"), Mass->Resolve(Id, View));
        }
        TestFalse(TEXT("Mass path releases GA tasks"), F.Ability->HasPendingGraphWork());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphIntegrationPool,
                                 "test_25d.UnifiedGA.Integration.ActorPoolReuseRejectsOldCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 真实 ASC 目标路径捕获池生命；归还并在后续房间借出后，旧延迟不得影响同一个 Actor 的新生命。 */
bool FAbilityGraphIntegrationPool::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    AbilityGraphIntegrationTests::FGraphIntegrationFixture F;
    FDungeonEnemyPool Pool;
    F.Target->EnableDungeonPooling(0.f);
    TestTrue(TEXT("Pool adopts first life"), Pool.Adopt(F.Target, TEXT("FirstRoom"), 1));
    const int32 FirstLease = F.Target->GetDungeonPoolLease();
    TestTrue(TEXT("First life has a real positive lease"), FirstLease > 0);
    TestTrue(TEXT("ASC activates before return"), F.ActivateFromClientRequest());
    F.SendTarget(FPureMassCombatTarget::FromActor(F.Target));
    TestEqual(TEXT("Target accepted and committed once"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
    F.Ability->StepGraph(.1f);
    F.Target->SleepForDungeonPool();
    TestTrue(TEXT("Returned target retires lease"), F.Target->IsDungeonPoolSleeping());
    TestTrue(TEXT("Pool ledger retires returned actor"), Pool.Retire(F.Target));
    TestNull(TEXT("Same room quarantine remains enforced"), Pool.Acquire(F.Target->GetClass(), TEXT("FirstRoom")));
    AEnemyCharacter* Reused = Pool.Acquire(F.Target->GetClass(), TEXT("NextRoom"));
    if (!TestTrue(TEXT("Later room reuses the same UObject"), Reused == F.Target))
    {
        return false;
    }
    TestTrue(TEXT("New life wakes"), Reused->WakeFromDungeonPool(FTransform(FVector(150, 0, 0)), TEXT("NextRoom")));
    TestTrue(TEXT("New life has a distinct positive lease"),
             Reused->GetDungeonPoolLease() > 0 && Reused->GetDungeonPoolLease() != FirstLease);
    F.Ability->StepGraph(.2f);
    TestFalse(TEXT("Old target data cannot petrify reused actor"), F.IsPetrified());
    TestFalse(TEXT("Invalidated cast ends its GA"), F.Ability->IsActive());
    TestFalse(TEXT("Lease rejection releases graph and target tasks"), F.Ability->HasPendingGraphWork());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphIntegrationWorldExit,
                                 "test_25d.UnifiedGA.Integration.WorldExitReleasesPendingWork",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 不预先取消 GA：由真实 DestroyWorld/ClearWorldComponents/ASC OnUnregister 收尾等待目标或延迟中的任务。 */
bool FAbilityGraphIntegrationWorldExit::RunTest(const FString&)
{
    const int32 InitialContexts = GEngine->GetWorldContexts().Num();
    for (bool bSendTarget : {false, true})
    {
        TStrongObjectPtr<UAbilityGraphTestAbility> RetainedAbility;
        TStrongObjectPtr<AEnemyCharacter> RetainedTarget;
        {
            AbilityGraphIntegrationTests::FGraphIntegrationFixture F;
            TestTrue(TEXT("Activate pending cast before world exit"), F.ActivateFromClientRequest());
            if (bSendTarget)
            {
                F.SendTarget(FPureMassCombatTarget::FromActor(F.Target));
                F.Ability->StepGraph(.1f);
            }
            TestTrue(TEXT("Fixture has pending work before cleanup"), F.Ability->HasPendingGraphWork());
            RetainedAbility.Reset(F.Ability);
            RetainedTarget.Reset(F.Target);
            // Retain only for assertions, disable the fixture's safety cancel so it cannot mask exit behavior.
            F.bCancelBeforeDestroy = false;
            F.World->BeginTearingDown();
        }
        TestEqual(TEXT("WorldContext removed by real fixture world destruction"), GEngine->GetWorldContexts().Num(),
                  InitialContexts);
        TestFalse(TEXT("World cleanup ends active GA without a manual cancel"), RetainedAbility->IsActive());
        TestFalse(TEXT("World cleanup releases target timeout and graph/task references"),
                  RetainedAbility->HasPendingGraphWork());
        RetainedAbility->StepGraph(1.f);
        TestFalse(TEXT("No delayed petrification after world exit"), RetainedTarget->GetMesh()->bPauseAnims);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphIntegrationRejectedPayload,
                                 "test_25d.UnifiedGA.Integration.RejectedTargetsDoNotCommit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 经真实目标任务投递无效数据，逐项验证服务端拒绝且费用/冷却/控制/异步占用均不产生。 */
bool FAbilityGraphIntegrationRejectedPayload::RunTest(const FString&)
{
    const TArray<FName> Cases = {TEXT("Empty"),        TEXT("Multiple"),        TEXT("NonBlocking"),
                                 TEXT("ActorAndMass"), TEXT("SpoofedLocation"), TEXT("OutOfRange"),
                                 TEXT("Friendly"),     TEXT("InvalidGraph"),    TEXT("Occluded")};
    for (FName Case : Cases)
    {
        AddInfo(FString::Printf(TEXT("Rejected payload case: %s"), *Case.ToString()));
        AbilityGraphIntegrationTests::FGraphIntegrationFixture F;
        if (Case == TEXT("OutOfRange"))
        {
            F.Target->SetActorLocation(FVector(1500, 0, 0));
        }
        if (Case == TEXT("Friendly"))
        {
            F.Target->Tags.Remove(TEXT("Enemy"));
            F.Target->Tags.AddUnique(TEXT("Player"));
        }
        if (Case == TEXT("InvalidGraph"))
        {
            F.Graph->Nodes[0].TypeId = TEXT("Unregistered.InvalidNode");
        }
        if (Case == TEXT("Occluded"))
        {
            AActor* Wall = F.World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Wall);
            Wall->AddInstanceComponent(Box);
            Wall->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(10, 150, 150));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionResponseToAllChannels(ECR_Ignore);
            Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            Box->RegisterComponent();
            Wall->SetActorLocation(FVector(75, 0, 0));
            TestFalse(TEXT("Fixture wall actually blocks production LOS"),
                      PureMassCombat::Visible(F.World, F.Source, F.Source->GetActorLocation() + FVector(0, 0, 30),
                                              FPureMassCombatTarget::FromActor(F.Target)));
        }
        TestTrue(TEXT("ASC starts target wait"), F.ActivateFromClientRequest());
        FGameplayAbilityTargetDataHandle Data;
        if (Case != TEXT("Empty"))
        {
            auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
            Hit->HitResult = FHitResult(F.Target, F.Target->GetMesh(), F.Target->GetActorLocation(), FVector::UpVector);
            Hit->HitResult.bBlockingHit = Case != TEXT("NonBlocking");
            Hit->HitResult.Item = Case == TEXT("ActorAndMass") ? 123 : INDEX_NONE;
            Hit->HitResult.ImpactPoint = F.Target->GetActorLocation();
            if (Case == TEXT("SpoofedLocation"))
            {
                Hit->HitResult.ImpactPoint += FVector(0, 1000, 0);
            }
            Data.Add(Hit);
            if (Case == TEXT("Multiple"))
            {
                Data.Add(new FGameplayAbilityTargetData_SingleTargetHit(*Hit));
            }
        }
        F.SendData(Data);
        F.Ability->StepGraph(1.f);
        TestEqual(TEXT("Rejected request leaves Mana unchanged"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
        TestEqual(TEXT("Rejected request creates no cooldown GE"),
                  F.ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);
        TestFalse(TEXT("Rejected request never petrifies"), F.IsPetrified());
        TestFalse(TEXT("Rejected request ends GA"), F.Ability->IsActive());
        TestFalse(TEXT("Rejected request clears target and graph work"), F.Ability->HasPendingGraphWork());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphIntegrationWaitingAvatar,
                                 "test_25d.UnifiedGA.Integration.AvatarChangeBeforeTargetData",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** PlayerState ASC 在等待目标期间换 Avatar，旧激活请求不能借新 Avatar 身份扣费施加控制。 */
bool FAbilityGraphIntegrationWaitingAvatar::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* State = F.World->SpawnActor<AMyPlayerState>();
    auto* Original = F.World->SpawnActor<ADungeonPlayerRunTestPawn>(FVector(-200, 0, 0), FRotator::ZeroRotator, Params);
    Original->AttachState(State);
    Original->Tags = {TEXT("Player")};
    auto* ASC = Original->GetAbilitySystemComponent();
    ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(State));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
    ASC->AbilityActorInfo->PlayerController = F.World->SpawnActor<AAbilityGraphRemoteTestController>();
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityGraphTestAbility::StaticClass(), 1));
    auto* Ability = Cast<UAbilityGraphTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    Ability->Graph = F.Graph;
    FPredictionKey Key;
    Key.Current = 221;
    ASC->CallServerTryActivateAbility(Handle, false, Key);
    TestTrue(TEXT("Original avatar is actually waiting for target"), Ability->IsActive());
    auto* Replacement =
        F.World->SpawnActor<ADungeonPlayerRunTestPawn>(FVector(-100, 100, 0), FRotator::ZeroRotator, Params);
    Replacement->AttachState(State);
    Replacement->Tags = {TEXT("Player")};
    TestTrue(TEXT("Both avatars share the real persistent ASC"), Replacement->GetAbilitySystemComponent() == ASC);
    TestTrue(TEXT("ASC now binds new avatar"), ASC->GetAvatarActor() == Replacement);
    auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
    Hit->HitResult = FHitResult(F.Target, F.Target->GetMesh(), F.Target->GetActorLocation(), FVector::UpVector);
    Hit->HitResult.bBlockingHit = true;
    Hit->HitResult.Item = INDEX_NONE;
    Hit->HitResult.ImpactPoint = F.Target->GetActorLocation();
    FGameplayAbilityTargetDataHandle Data;
    Data.Add(Hit);
    ASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), Key);
    Ability->StepGraph(.3f);
    TestEqual(TEXT("Old request never charges replacement avatar"),
              ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
    TestFalse(TEXT("Old request never controls target using new source"), F.IsPetrified());
    TestFalse(TEXT("Rejected old request closes all pending work"), Ability->HasPendingGraphWork());
    ASC->CancelAllAbilities();
    return true;
}
#endif
