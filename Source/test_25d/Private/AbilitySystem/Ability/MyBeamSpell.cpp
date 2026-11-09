// 调用链：原生鼠标/Notify/松开任务 -> 注册 Channel.Electrocute；EndAbility 关闭图、最短时长定时器和本地预测表现。

#include "AbilitySystem/Ability/MyBeamSpell.h"

#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "AbilitySystem/AbilityTask/TargetDataUnderMouse.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

UMyBeamSpell::UMyBeamSpell()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UMyBeamSpell::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                   FGameplayAbilityActivationInfo ActivationInfo,
                                   const FGameplayEventData* TriggerEventData)
{
    bTargetConfirmed = bChannelStarted = bReleaseReceived = bPredictedMovementLocked = bCooldownCommitted = false;
    ElectrocuteInput = {};
    if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || !ElectrocuteMontage ||
        !ActorInfo->AvatarActor->Implements<UCombatInterface>() || !FMath::IsFinite(MinimumElectrocuteTime) ||
        MinimumElectrocuteTime < 0.f || !FMath::IsFinite(ElectrocuteDamageInterval) || ElectrocuteDamageInterval <= 0.f)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    auto* Mouse = UTargetDataUnderMouse::CreateTargetDataUnderMouse(this);
    Mouse->ValidData.AddDynamic(this, &ThisClass::OnElectrocuteTargetData);
    Mouse->ReadyForActivation();
}

void UMyBeamSpell::OnElectrocuteTargetData(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bTargetConfirmed || Data.Num() == 0 || !Data.Get(0) || !Data.Get(0)->GetHitResult())
    {
        return;
    }
    const auto Session = GetExecutionSession();
    if (!Session || !Session->IsSourceCurrent())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    const auto& Hit = *Data.Get(0)->GetHitResult();
    if (!Hit.bBlockingHit || Hit.ImpactPoint.ContainsNaN())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    bTargetConfirmed = true;
    StoreMouseDataInfo(Hit);
    StoreOwnerVariables();
    ElectrocuteInput.Origin = MouseHitLocation;
    ElectrocuteInput.Level = GetAbilityLevel();
    if (CurrentActorInfo->IsNetAuthority())
    {
        FString Error;
        if (!AbilityExecutionIdentity::CaptureSource(GetAvatarActorFromActorInfo(), ElectrocuteInput.SourceToken,
                                                     Error))
        {
            EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
            return;
        }
        FAbilityTargetToken TargetLife;
        if (MouseHitActor && MouseHitActor->Implements<UCombatInterface>() &&
            AbilityExecutionIdentity::CaptureTarget(ElectrocuteInput.SourceToken,
                                                    FPureMassCombatTarget::FromActor(MouseHitActor), TargetLife, Error))
        {
            ElectrocuteInput.LiveTargetToken = TargetLife;
        }
    }
    if (OwnerPlayerController)
    {
        OwnerPlayerController->bShowMouseCursor = false;
    }
    ICombatInterface::Execute_UpdateFacingTarget(GetAvatarActorFromActorInfo(), MouseHitLocation);
    GetAbilitySystemComponentFromActorInfo()->ExecuteGameplayCue(
        FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.ShockBurst")));
    auto* Notify = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
        this, FGameplayTag::RequestGameplayTag(TEXT("Event.Montage.Electrocute")), nullptr, true, true);
    Notify->EventReceived.AddDynamic(this, &ThisClass::OnElectrocuteNotify);
    Notify->ReadyForActivation();
    auto* Montage = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, ElectrocuteMontage,
                                                                                   1.f, NAME_None, false);
    Montage->OnCancelled.AddDynamic(this, &ThisClass::OnElectrocuteMontageInterrupted);
    Montage->OnInterrupted.AddDynamic(this, &ThisClass::OnElectrocuteMontageInterrupted);
    Montage->OnCompleted.AddDynamic(this, &ThisClass::OnElectrocuteMontageCompleted);
    Montage->ReadyForActivation();
    if (!IsActive())
    {
        return;
    }
    // 与蓝图一致：最短时间从目标数据返回后创建的 WaitInputRelease 开始计量。
    auto* Release = UAbilityTask_WaitInputRelease::WaitInputRelease(this, false);
    Release->OnRelease.AddDynamic(this, &ThisClass::OnElectrocuteReleased);
    Release->ReadyForActivation();
}

void UMyBeamSpell::OnElectrocuteNotify(FGameplayEventData Payload)
{
    if (!IsActive() || bChannelStarted || !bTargetConfirmed)
    {
        return;
    }
    const auto Session = GetExecutionSession();
    if (!Session || !Session->IsSourceCurrent())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    bChannelStarted = true;
    if (!CurrentActorInfo->IsNetAuthority())
    {
        ICombatInterface::Execute_SetInShockLoop(GetAvatarActorFromActorInfo(), true);
        if (OwnerCharacter)
        {
            OwnerCharacter->GetCharacterMovement()->DisableMovement();
            bPredictedMovementLocked = true;
        }
        return;
    }
    auto* Graph = NewObject<UAbilityGraphDefinition>(this);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("LegacyActor.Channel.v1");
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Channel.Electrocute");
    auto* Config = NewObject<UAbilityElectrocuteNodeConfig>(Graph);
    Config->DamageInterval = ElectrocuteDamageInterval;
    Config->MaxAdditionalTargets = MaxNumShockTargets;
    Config->LoopCue = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.ShockLoop"));
    Node.Config = Config;
    Graph->Nodes.Add(Node);
    Graph->EntryNodeId = Node.NodeId;
    ElectrocuteContext = NewObject<UAbilityElectrocuteContext>(this);
    ElectrocuteContext->AimActor = MouseHitActor;
    const TWeakObjectPtr<UMyBeamSpell> WeakThis(this);
    ElectrocuteContext->CommitPeriodCost = [WeakThis]()
    {
        auto* Self = WeakThis.Get();
        return Self && Self->IsActive() &&
               Self->CommitAbilityCost(Self->CurrentSpecHandle, Self->CurrentActorInfo, Self->CurrentActivationInfo);
    };
    ElectrocuteContext->CommitEndCooldown = [WeakThis]()
    {
        if (auto* Self = WeakThis.Get(); Self && Self->IsActive() && !Self->bCooldownCommitted)
        {
            Self->bCooldownCommitted = true;
            Self->CommitAbilityCooldown(Self->CurrentSpecHandle, Self->CurrentActorInfo, Self->CurrentActivationInfo,
                                        false);
        }
    };
    ElectrocuteInput.Context = ElectrocuteContext;
    ElectrocuteInput.DamageParams = MakeDamageEffectParamsFromClassDefaults();
    ElectrocuteGraph = NewObject<UAbilityGraphInstance>(GetWorld());
    GraphFinishedHandle = ElectrocuteGraph->OnFinished.AddLambda(
        [WeakThis](EAbilityGraphEndReason Reason)
        {
            if (auto* Self = WeakThis.Get(); Self && Self->IsActive())
            {
                Self->EndAbility(Self->CurrentSpecHandle, Self->CurrentActorInfo, Self->CurrentActivationInfo, true,
                                 Reason != EAbilityGraphEndReason::Completed);
            }
        });
    FAbilityExecutionOptions Options;
    Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    FString Error;
    const TStrongObjectPtr<UAbilityGraphInstance> Starting(ElectrocuteGraph.Get());
    auto* WorldOwner = GetWorld()->GetSubsystem<UAbilityGraphWorldSubsystem>();
    if (!Starting->StartWithInput(Graph, ElectrocuteInput, Options, Error) || !WorldOwner ||
        !WorldOwner->Adopt(Starting.Get(), Error))
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}

void UMyBeamSpell::OnElectrocuteReleased(float TimeHeld)
{
    if (!IsActive() || bReleaseReceived)
    {
        return;
    }
    bReleaseReceived = true;
    const float Remaining = MinimumElectrocuteTime - TimeHeld;
    if (CurrentActorInfo->IsNetAuthority() && Remaining > 0.f)
    {
        GetWorld()->GetTimerManager().SetTimer(MinimumHoldTimer, this, &ThisClass::FinishElectrocute, Remaining, false);
    }
    else if (!CurrentActorInfo->IsNetAuthority() && Remaining > 0.f)
    {
        // 原客户端提前恢复表现；服务器仍保持最短时间，最终 EndAbility 经 GAS 复制。
        CleanupPredictedPresentation();
    }
    else
    {
        FinishElectrocute();
    }
}

void UMyBeamSpell::FinishElectrocute()
{
    if (!IsActive())
    {
        return;
    }
    const auto Session = GetExecutionSession();
    if (!Session || !Session->IsSourceCurrent())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    if (CurrentActorInfo->IsNetAuthority() && ElectrocuteContext && ElectrocuteContext->Execution.IsValid())
    {
        ElectrocuteContext->RequestCompletion();
        return;
    }
    if (!bCooldownCommitted)
    {
        bCooldownCommitted = true;
        CommitAbilityCooldown(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false);
    }
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UMyBeamSpell::CleanupPredictedPresentation()
{
    if (OwnerPlayerController)
    {
        OwnerPlayerController->bShowMouseCursor = true;
    }
    if (bPredictedMovementLocked && OwnerCharacter)
    {
        bPredictedMovementLocked = false;
        ICombatInterface::Execute_SetInShockLoop(OwnerCharacter, false);
        OwnerCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
}

void UMyBeamSpell::OnElectrocuteMontageInterrupted()
{
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}

void UMyBeamSpell::OnElectrocuteMontageCompleted()
{
    if (IsActive() && !bChannelStarted)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}

void UMyBeamSpell::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                              FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                              bool bWasCancelled)
{
    CloseExecutionSession(bWasCancelled);
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(MinimumHoldTimer);
    }
    if (ElectrocuteGraph)
    {
        ElectrocuteGraph->OnFinished.Remove(GraphFinishedHandle);
        ElectrocuteGraph->Cancel();
        ElectrocuteGraph = nullptr;
    }
    CleanupPredictedPresentation();
    ElectrocuteContext = nullptr;
    MouseHitActor = nullptr;
    OwnerCharacter = nullptr;
    OwnerPlayerController = nullptr;
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UMyBeamSpell::StoreMouseDataInfo(const FHitResult& HitResult)
{
    if (HitResult.bBlockingHit)
    {
        MouseHitLocation = HitResult.ImpactPoint;
        MouseHitActor = HitResult.GetActor();
        SetOffensiveAttackTarget(MouseHitActor);
    }
    else
    {
        CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
        //EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true,false);
    }
}

void UMyBeamSpell::StoreOwnerVariables()
{
    if (CurrentActorInfo)
    {
        OwnerPlayerController = CurrentActorInfo->PlayerController.Get();
        OwnerCharacter = Cast<ACharacter>(CurrentActorInfo->AvatarActor);
    }
}

void UMyBeamSpell::TraceFirstTarget(const FVector& BeamTargetLocation)
{
    check(OwnerCharacter);
    if (OwnerCharacter->Implements<UCombatInterface>())
    {
        if (USkeletalMeshComponent* Weapon = ICombatInterface::Execute_GetWeapon(OwnerCharacter))
        {
            TArray<AActor*> ActorsToIgnore;
            ActorsToIgnore.Add(OwnerCharacter);
            FHitResult HitResult;
            const FVector SocketLocation = Weapon->GetSocketLocation(FName("TipSocket"));
            UKismetSystemLibrary::SphereTraceSingle(OwnerCharacter, SocketLocation, BeamTargetLocation, 10.f,
                                                    TraceTypeQuery1, false, ActorsToIgnore, EDrawDebugTrace::None,
                                                    HitResult, true);

            if (HitResult.bBlockingHit)
            {
                MouseHitLocation = HitResult.ImpactPoint;
                MouseHitActor = HitResult.GetActor();
            }
        }
    }
    if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(MouseHitActor))
    {
        ReportOffensiveAttack(MouseHitActor);
        if (!CombatInterface->GetOnDeathDelegate().IsAlreadyBound(this, &UMyBeamSpell::PrimaryTargetDied))
        {
            CombatInterface->GetOnDeathDelegate().AddDynamic(this, &UMyBeamSpell::PrimaryTargetDied);
        }
    }
}

void UMyBeamSpell::StoreAdditionalTargets(TArray<AActor*>& OutAdditionalTargets)
{
    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(GetAvatarActorFromActorInfo());
    ActorsToIgnore.Add(MouseHitActor);

    TArray<AActor*> OverlappingActors;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(GetAvatarActorFromActorInfo(), OverlappingActors,
                                                        ActorsToIgnore, 850.f, MouseHitActor->GetActorLocation());

    int32 NumAdditionalTargets = FMath::Min(GetAbilityLevel() + 1, MaxNumShockTargets);
    //int32 NumAdditionalTargets = 5;

    UMyAbilitySystemLibrary::GetClosestTargets(NumAdditionalTargets, OverlappingActors, OutAdditionalTargets,
                                               MouseHitActor->GetActorLocation());

    for (AActor* Target : OutAdditionalTargets)
    {
        if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(Target))
        {
            if (!CombatInterface->GetOnDeathDelegate().IsAlreadyBound(this, &UMyBeamSpell::AdditionalTargetDied))
            {
                CombatInterface->GetOnDeathDelegate().AddDynamic(this, &UMyBeamSpell::AdditionalTargetDied);
            }
        }
    }
}
void UMyBeamSpell::OnEndAbility(TArray<AActor*> AdditionalTargets)
{
    //取消主要目标的死亡回调
    if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(MouseHitActor))
    {
        CombatInterface->GetOnDeathDelegate().RemoveDynamic(this, &UMyBeamSpell::PrimaryTargetDied);
    }

    //取消额外目标的死亡回调
    for (AActor* TargetActor : AdditionalTargets)
    {
        if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(TargetActor))
        {
            CombatInterface->GetOnDeathDelegate().RemoveDynamic(this, &UMyBeamSpell::AdditionalTargetDied);
        }
    }
}
