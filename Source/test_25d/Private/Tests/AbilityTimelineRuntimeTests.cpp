// 调用链：Automation -> 临时World/真实ASC/AnimInstance -> Timeline任务 -> 蒙太奇Notify -> 测试GA。
// 不写资产，不启动AI或用户存档；结束销毁World，所有任务和监听随ASC清理。
#include "AbilityTimelineTestAbility.h"
#include "AbilitySystem/Timeline/AnimNotify_AbilityTimelineEvent.h"
#include "Misc/AutomationTest.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Player/StanderWeaponActionConfig.h"
#include "UObject/Script.h"
#include "Character/StanderPlayer.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/WeaponSetConfig.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Tests/EquipmentTestEffects.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"

UAbilityTimelineTestAbility::UAbilityTimelineTestAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UAbilityTimelineTestAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
                                                  const FGameplayAbilityActorInfo* Info,
                                                  FGameplayAbilityActivationInfo Activation,
                                                  const FGameplayEventData* Data)
{
    Hits = 0;
    TailHits = 0;
    Finishes = 0;
    Elapsed = 0;
    HitTime = -1;
    LastSourcePosition = 0;
    Task = UAbilityTask_RunTimeline::RunAbilityTimeline(this, TestMontage, TestDefinition);
    Task->OnEvent.AddDynamic(this, &ThisClass::Receive);
    Task->OnFinished.AddDynamic(this, &ThisClass::Finish);
    Task->OnPosition.AddDynamic(this, &ThisClass::Position);
    Task->ReadyForActivation();
}

void UAbilityTimelineTestAbility::Receive(FGameplayTag Tag, FGameplayEventData Payload)
{
    if (Tag == FGameplayTag::RequestGameplayTag(TEXT("Test.AbilityTimeline.Tail")))
    {
        ++TailHits;
        return;
    }
    ++Hits;
    HitTime = Elapsed;
}

void UAbilityTimelineTestAbility::Position(float SourcePosition)
{
    LastSourcePosition = SourcePosition;
}

void UAbilityTimelineTestAbility::Finish(bool bInterrupted)
{
    ++Finishes;
    bWasInterrupted = bInterrupted;
    if (!bKeepAbilityActive)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bInterrupted);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineRuntimeNotify, "test_25d.AbilityTimeline.RealMontageNotifyAndCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 在真实蒙太奇连续播放时检查变速后的通知时刻、取消清理与下一次施法。 */
bool FTimelineRuntimeNotify::RunTest(const FString&)
{
    auto* Config = LoadObject<UStanderWeaponActionConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_Attack1.DA_Attack1"));
    auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander"));
    if (!TestNotNull(TEXT("Existing weapon config"), Config) || !TestNotNull(TEXT("Existing Stander mesh"), MeshAsset))
        return false;

    FEditorScriptExecutionGuard ScriptGuard;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor = World->SpawnActor<AEnemyCharacter>();
    auto* ASC = Actor->GetAbilitySystemComponent();
    auto* Mesh = Actor->GetMesh();
    Mesh->SetSkeletalMeshAsset(MeshAsset);
    Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
    ASC->InitAbilityActorInfo(Actor, Actor);
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityTimelineTestAbility::StaticClass(), 1));
    auto* Ability = Cast<UAbilityTimelineTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    Ability->TestMontage = DuplicateObject<UAnimMontage>(Config->Montage, GetTransientPackage());
    auto* Montage = Ability->TestMontage.Get();
    Montage->Notifies.Reset();
    Montage->BlendIn.SetBlendTime(0);
    Montage->BlendOut.SetBlendTime(.1f);
    const float Length = Montage->GetPlayLength();
    Ability->TestDefinition = NewObject<UAbilityTimelineDefinition>(Ability);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto& Phase = Ability->TestDefinition->Phases.AddDefaulted_GetRef();
        Phase.Name = FName(*FString::FromInt(Index));
        Phase.SourceEnd = Index == 0 ? Length * .25f : (Index == 1 ? Length * .5f : Length);
        Phase.Duration = Index == 2 ? .4f : .2f;
    }
    const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("Test.AbilityTimeline.Hit"));
    auto& Window = Ability->TestDefinition->Events.AddDefaulted_GetRef();
    Window.EventTag = Tag;
    Window.SourceStart = Length * .25f - .001f;
    Window.SourceEnd = Length * .25f + .001f;
    auto* Notify = NewObject<UAnimNotify_AbilityTimelineEvent>(Montage);
    Notify->EventTag = Tag;
    FAnimNotifyEvent Event;
    Event.Notify = Notify;
    Event.Link(Montage, Length * .25f);
    Montage->Notifies.Add(Event);
    const auto TailTag = FGameplayTag::RequestGameplayTag(TEXT("Test.AbilityTimeline.Tail"));
    auto& TailWindow = Ability->TestDefinition->Events.AddDefaulted_GetRef();
    TailWindow.EventTag = TailTag;
    TailWindow.SourceStart = Length - .002f;
    TailWindow.SourceEnd = Length;
    auto* TailNotify = NewObject<UAnimNotify_AbilityTimelineEvent>(Montage);
    TailNotify->EventTag = TailTag;
    FAnimNotifyEvent TailEvent;
    TailEvent.Notify = TailNotify;
    TailEvent.Link(Montage, Length - .001f);
    Montage->Notifies.Add(TailEvent);

    auto Step = [&](int32 Frames)
    {
        for (int32 Index = 0; Index < Frames && Ability->IsActive(); ++Index)
        {
            Ability->Elapsed += .01f;
            Mesh->TickAnimation(.01f, false);
            Mesh->GetAnimInstance()->DispatchQueuedAnimEvents();
            Ability->Task->TickTask(.01f);
        }
    };
    TestTrue(TEXT("Real ASC activation"), ASC->TryActivateAbility(Handle));
    Step(100);
    TestEqual(TEXT("Notify emits exactly one gameplay event"), Ability->Hits, 1);
    TestEqual(TEXT("Queued notify in the final animation frame is delivered"), Ability->TailHits, 1);
    TestTrue(TEXT("Notify follows changed windup duration"), FMath::IsNearlyEqual(Ability->HitTime, .2f, .025f));
    TestEqual(TEXT("Natural finish emitted once"), Ability->Finishes, 1);
    TestFalse(TEXT("Natural completion is not interruption"), Ability->bWasInterrupted);
    TestEqual(TEXT("Natural finish delivers endpoint for table events and movement"), Ability->LastSourcePosition,
              Length);
    TestFalse(TEXT("Ability releases active state"), Ability->IsActive());
    TestFalse(TEXT("ASC event listener removed"), ASC->GenericGameplayEventCallbacks.FindOrAdd(Tag).IsBound());

    TestTrue(TEXT("Same GA can activate again"), ASC->TryActivateAbility(Handle));
    Step(5);
    const auto PreviousCast = Ability->Task->GetSnapshot().CastId;
    ASC->CancelAbilityHandle(Handle);
    TestTrue(TEXT("Cancellation closes event gate"), Ability->Task->GetSnapshot().bClosed);
    TestEqual(TEXT("No early damage during cancelled windup"), Ability->Hits, 0);
    TestFalse(TEXT("Cancellation unbinds ASC event"), ASC->GenericGameplayEventCallbacks.FindOrAdd(Tag).IsBound());
    TestTrue(TEXT("Activation after cancel"), ASC->TryActivateAbility(Handle));
    TestTrue(TEXT("Fresh cast identity"), Ability->Task->GetSnapshot().CastId != PreviousCast);
    Step(100);
    TestEqual(TEXT("Reactivation hits once"), Ability->Hits, 1);
    Ability->bKeepAbilityActive = true;
    TestTrue(TEXT("GA may wait for projectiles after montage"), ASC->TryActivateAbility(Handle));
    Step(100);
    TestNull(TEXT("Finished task releases ASC montage ownership even while GA continues"), ASC->GetAnimatingAbility());
    ASC->CancelAbilityHandle(Handle);
    ASC->ClearAbility(Handle);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineSamples, "test_25d.AbilityTimeline.AuthoredSamples",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 独立进程读回三个生产配置，确保资产已接入且没有第二条Notify伤害路径。 */
bool FTimelineSamples::RunTest(const FString&)
{
    for (const TCHAR* Relative :
         {TEXT("Sword_Shield/DA_Attack1"), TEXT("Sword_Shield/DA_Block"), TEXT("Magic/DA_Attack1")})
    {
        const FString Path = FString(TEXT("/Game/BluePrints/Characters/Stander/Weapons/")) + Relative;
        auto* Config = LoadObject<UStanderWeaponActionConfig>(nullptr, *Path);
        if (!TestNotNull(*Path, Config) || !TestNotNull(TEXT("Timeline assigned"), Config->Timeline.Get()))
            continue;
        FAbilityTimelineSnapshot Snapshot;
        FString Error;
        const bool bResolved = Config->Timeline->ResolveMontage(Config->Montage, Snapshot, Error);
        if (!TestTrue(*FString::Printf(TEXT("Resolved sample %s: %s"), Relative, *Error), bResolved))
        {
            continue;
        }
        AddInfo(Path + TEXT(" validation: ") + Error);
        TestTrue(TEXT("Action accepts authored timeline before commit"), Config->Validate());
        TestTrue(TEXT("No parallel notify damage source"), Config->Timeline->Events.IsEmpty());
        // 首次制作时可保持原速度，但后续策划允许改秒数。验证当前 DA/表配置被消费，
        // 而不是把原蒙太奇长度/旧 PlayRate 固定为永远不能修改的验收值。
        float ExpectedDuration = 0.f;
        for (const auto& Phase : Config->Timeline->Phases)
        {
            const auto* Row = Phase.Timing.DataTable ? Phase.Timing.DataTable->FindRow<FAbilityTimelineTimingRow>(
                                                           Phase.Timing.RowName, TEXT("TimelineSampleTest"), false)
                                                     : nullptr;
            ExpectedDuration += Row ? Row->Duration : Phase.Duration;
        }
        TestTrue(TEXT("Sample duration follows current authored phase seconds"),
                 FMath::IsNearlyEqual(Snapshot.ElapsedAt(Config->Montage->GetPlayLength()), ExpectedDuration, .001f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineGuard, "test_25d.AbilityTimeline.StanderGuardWindow",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 装备真实剑盾并激活生产格挡GA；慢放窗口由源位置控制，结束/取消均移除GE与Busy。 */
bool FTimelineGuard::RunTest(const FString&)
{
    auto* GuardClass = LoadClass<UStanderWeaponActionAbility>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/GA_Block.GA_Block_C"));
    auto* SourceSet = LoadObject<UWeaponSetConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_WeaponSet.DA_WeaponSet"));
    if (!TestNotNull(TEXT("Existing guard GA"), GuardClass) || !TestNotNull(TEXT("Existing weapon"), SourceSet))
        return false;
    FEditorScriptExecutionGuard ScriptGuard;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* State = World->SpawnActor<AMyPlayerState>();
    auto* Pawn = World->SpawnActor<AAbilityTimelineTestStander>();
    Pawn->AttachState(State);
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    auto* Equipment = State->GetEquipmentComponent();
    auto* Backpack = State->GetBackpackComponent();
    Pawn->GetMesh()->SetSkeletalMeshAsset(
        LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander")));
    TestEqual(TEXT("Fixture Avatar exposes its actual PlayerState ASC"), Pawn->GetAbilitySystemComponent(),
              State->GetAbilitySystemComponent());
    FAbilitySourceToken FixtureSource;
    FString SourceError;
    const bool bValidFixtureSource = AbilityExecutionIdentity::CaptureSource(Pawn, FixtureSource, SourceError);
    TestTrue(*FString::Printf(TEXT("Fixture captures a valid source life: %s"), *SourceError), bValidFixtureSource);
    ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000);
    const auto Slot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Weapon"));
    Equipment->AvailableSlots.AddTag(Slot);
    auto* Catalog = NewObject<UBackpackItemDefinitions>(State);
    auto& Item = Catalog->Items.AddDefaulted_GetRef();
    Item.ItemDefinitionId = FPrimaryAssetId(TEXT("BackpackItem"), TEXT("TimelineTestWeapon"));
    Item.bEquippable = true;
    Item.bStackable = false;
    Item.MaxStack = 1;
    Item.AllowedEquipmentSlots.AddTag(Slot);
    Item.EquipEffect = UEquipmentTestArmor::StaticClass();
    Item.WeaponSet = SourceSet;
    Backpack->SetItemDefinitions(Catalog);
    Backpack->Initialize();
    Backpack->SetCapacity(1);
    Pawn->DefaultWeaponSet = SourceSet;
    TestTrue(TEXT("Default Sword_Shield loads without an equipped item"), Equipment->ReconcileEffects());
    TestTrue(TEXT("Default loadout leaves equipment slots empty"), Equipment->Slots.IsEmpty());
    const auto* DefaultGuard = ASC->FindAbilitySpecFromClass(GuardClass);
    const auto DefaultHandle = DefaultGuard ? DefaultGuard->Handle : FGameplayAbilitySpecHandle();
    TestTrue(TEXT("Default set grants the real sword guard"), DefaultHandle.IsValid());
    Backpack->TryAddItem(Item.ItemDefinitionId, 1);
    const bool bEquipped = Equipment->TryEquip(0, Slot, Backpack->GetInventoryModel()->GetSlots()[0].ItemInstanceId,
                                               Backpack->GetRevision(), Equipment->Revision);
    TestTrue(TEXT("Equip real sword and shield"), bEquipped);
    TestNotNull(TEXT("Same default handle survives equipping the matching set"),
                ASC->FindAbilitySpecFromHandle(DefaultHandle));
    if (bEquipped)
    {
        const auto EquippedInstance = Equipment->Slots[0].ItemInstanceId;
        TestTrue(TEXT("Unequip the matching set"), Equipment->TryUnequip(
            Slot, EquippedInstance, Backpack->GetRevision(), Equipment->Revision));
        TestTrue(TEXT("After unequip, inventory equipment is empty"), Equipment->Slots.IsEmpty());
        TestTrue(TEXT("After unequip, same Sword_Shield set remains active"), Equipment->GetWeaponSet() == SourceSet);
        TestNotNull(TEXT("Same default handle survives unequip"), ASC->FindAbilitySpecFromHandle(DefaultHandle));
    }
    // Exercise every authored action, including the six real default input slots.
    TestEqual(TEXT("Real Sword_Shield set exposes all 17 actions"), SourceSet->Abilities.Num(), 17);
    for (const auto& Entry : SourceSet->Abilities)
    {
        auto* EquippedSpec = Entry.InputTag.IsValid() ? ASC->GetSpecWithSlot(Entry.InputTag)
                                                     : ASC->FindAbilitySpecFromClass(Entry.Ability);
        const FString Label = Entry.InputTag.ToString() + TEXT(" -> ") + Entry.Ability->GetName();
        if (!TestTrue(Label + TEXT(" has expected weapon GA"),
                      EquippedSpec && EquippedSpec->Ability->GetClass() == Entry.Ability))
        {
            continue;
        }
        auto* Action = CastChecked<UStanderWeaponActionAbility>(EquippedSpec->GetPrimaryInstance());
        TestTrue(Label + TEXT(" action config valid"), Action->ActionConfig->Validate());
        const auto Handle = EquippedSpec->Handle;
        bool bEndedNormally = false;
        const auto EndHandle = ASC->OnAbilityEnded.AddLambda(
            [&](const FAbilityEndedData& Ended)
            {
                if (Ended.AbilitySpecHandle == Handle)
                {
                    bEndedNormally = !Ended.bWasCancelled;
                }
            });
        Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000);
        if (Entry.InputTag.IsValid())
        {
            ASC->AbilityInputTagHeld(Entry.InputTag);
        }
        else
        {
            ASC->TryActivateAbility(Handle);
        }
        TestTrue(Label + TEXT(" starts"), Action->IsActive());
        TestTrue(Label + TEXT(" plays configured montage"),
                 ASC->GetCurrentMontage() == Action->ActionConfig->Montage);
        for (int32 Frame = 0; Frame < 2000 && Action->IsActive(); ++Frame)
        {
            Pawn->GetMesh()->TickAnimation(.01f, false);
            Pawn->GetMesh()->GetAnimInstance()->DispatchQueuedAnimEvents();
            if (auto* Task = Action->GetActiveTimelineTask())
            {
                Task->TickTask(.01f);
            }
            World->GetTimerManager().Tick(.01f);
        }
        ASC->OnAbilityEnded.Remove(EndHandle);
        TestTrue(Label + TEXT(" completes without cancellation"), bEndedNormally);
        TestFalse(Label + TEXT(" is no longer active"), Action->IsActive());
        TestFalse(Label + TEXT(" releases Busy"),
                  ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stander.Busy"))));
        ASC->CancelAbilityHandle(Handle);
        if (Action->ActionConfig->CooldownTag.IsValid())
        {
            ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Action->ActionConfig->CooldownTag));
        }
    }
    auto* Spec = ASC->FindAbilitySpecFromClass(GuardClass);
    auto* Ability = Spec ? Cast<UStanderWeaponActionAbility>(Spec->GetPrimaryInstance()) : nullptr;
    if (TestNotNull(TEXT("Equipment grants guard"), Ability))
    {
        // 独立配置只在测试世界持有，不更改CDO或用户调整过的DA。
        Ability->ActionConfig = DuplicateObject<UStanderWeaponActionConfig>(Ability->ActionConfig, Ability);
        auto* Config = Ability->ActionConfig.Get();
        if (TestNotNull(TEXT("Authored guard timeline"), Config->Timeline.Get()))
        {
            Config->Timeline = DuplicateObject<UAbilityTimelineDefinition>(Config->Timeline, Config);
            // 运行时回归使用独立、确定的 0.5 倍速夹具。直接将用户调好的秒数乘二，
            // 不保证结果慢于源动画；保留 Timing 表引用还可能完全覆盖该倍率修改。
            Config->Timeline->Phases.Reset();
            auto& SlowPhase = Config->Timeline->Phases.AddDefaulted_GetRef();
            SlowPhase.Name = TEXT("SlowGuardFixture");
            SlowPhase.SourceEnd = Config->Montage->GetPlayLength();
            SlowPhase.Duration = SlowPhase.SourceEnd * 2.f;
            Config->Cooldown = 0;
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            TestTrue(TEXT("Activate production timeline GA"), ASC->TryActivateAbility(Spec->Handle));
            const auto* GuardEvent = Config->Events.FindByPredicate(
                [](const auto& Event)
                {
                    return Event.Kind == EStanderWeaponActionKind::Guard;
                });
            bool bSawGuard = false;
            bool bSawExtendedGuard = false;
            float GuardStartedAt = -1.f;
            float Elapsed = 0;
            for (int32 Frame = 0; Frame < 2000 && Ability->IsActive(); ++Frame)
            {
                Elapsed += .01f;
                Pawn->GetMesh()->TickAnimation(.01f, false);
                Pawn->GetMesh()->GetAnimInstance()->DispatchQueuedAnimEvents();
                if (auto* Task = Ability->GetActiveTimelineTask())
                    Task->TickTask(.01f);
                World->GetTimerManager().Tick(.01f);
                const bool bGuarding = ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag());
                if (bGuarding && GuardStartedAt < 0.f)
                {
                    GuardStartedAt = Elapsed;
                }
                bSawGuard |= bGuarding;
                bSawExtendedGuard |= bGuarding && GuardEvent && Elapsed - GuardStartedAt > GuardEvent->Duration + .02f;
            }
            TestTrue(TEXT("Production guard opens"), bSawGuard);
            TestTrue(TEXT("Slow guard survives old fixed-seconds expiry"), bSawExtendedGuard);
            TestFalse(TEXT("Production GA completes"), Ability->IsActive());
            TestFalse(TEXT("Guard removed after completion"),
                      ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
            TestFalse(TEXT("Busy removed after completion"),
                      ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stander.Busy"))));
            TestNull(TEXT("Task reference released"), Ability->GetActiveTimelineTask());
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            TestTrue(TEXT("Guard reactivation"), ASC->TryActivateAbility(Spec->Handle));
            for (int32 Frame = 0;
                 Frame < 100 && Ability->IsActive() && !ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag());
                 ++Frame)
            {
                Pawn->GetMesh()->TickAnimation(.01f, false);
                Pawn->GetMesh()->GetAnimInstance()->DispatchQueuedAnimEvents();
                if (auto* Task = Ability->GetActiveTimelineTask())
                    Task->TickTask(.01f);
            }
            ASC->CancelAbilityHandle(Spec->Handle);
            TestFalse(TEXT("Cancelled guard removes GE"), ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
            TestNull(TEXT("Cancelled guard releases task"), Ability->GetActiveTimelineTask());
        }
    }
    ASC->CancelAllAbilities();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
