// 调用链：GAS受击激活 -> 原GE与原受击蒙太奇 -> BlendOut/结束/打断 -> 撤销所属GE和任务。
#include "AbilitySystem/Ability/MyHitReactAbility.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystem/Unified/AbilityExecutionSession.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "GameplayEffect.h"
#include "Interface/CombatInterface.h"
#include "Roguelike/DungeonRunDirector.h"

UMyHitReactAbility::UMyHitReactAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UMyHitReactAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                         FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    bEndingReact = false;
    const auto ActivationSession = GetExecutionSession();
    auto* Avatar = Info ? Info->AvatarActor.Get() : nullptr;
    if (!Avatar || !HitReactEffectClass || !Avatar->Implements<UCombatInterface>())
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    if (Info->IsNetAuthority())
    {
        FAbilitySourceToken Source;
        FString Error;
        if (!AbilityExecutionIdentity::CaptureSource(Avatar, Source, Error))
        {
            EndAbility(Handle, Info, Activation, true, true);
            return;
        }
        // 只接受不与他人合并句柄的持久GE；本前端按原蒙太奇回调控制所属时长。
        auto* Settings = NewObject<UAbilityOwnedEffectNodeConfig>(this);
        Settings->EffectClass = HitReactEffectClass;
        Settings->EffectLevel = HitReactEffectLevel;
        auto Applied = AbilityGameplayEffectActions::Apply(Source, *Settings);
        // GE回调可以同步取消或回收来源；EndAbility先于Apply返回时，当前栈负责撤销新句柄。
        AActor* Current = nullptr;
        if (!IsActive() || bEndingReact || !AbilityExecutionIdentity::ResolveSource(Source, Current))
        {
            AbilityGameplayEffectActions::Remove(Applied);
            if (IsActive() && !bEndingReact && GetExecutionSession() == ActivationSession)
            {
                EndAbility(Handle, Info, Activation, true, true);
            }
            return;
        }
        OwnedReact = Applied;
        if (!OwnedReact.bApplied)
        {
            EndAbility(Handle, Info, Activation, true, true);
            return;
        }
    }
    else if (HasAuthorityOrPredictionKey(Info, &Activation))
    {
        // 原蓝图的LocalPredicted自身GE保留引擎预测键路径；图节点的服务器权限契约不因此放宽。
        const auto Session = GetExecutionSession();
        const TWeakObjectPtr<AActor> CapturedAvatar = Avatar;
        const TWeakObjectPtr<UAbilitySystemComponent> CapturedASC = Info->AbilitySystemComponent;
        const auto* Enemy = Cast<AEnemyCharacter>(Avatar);
        const int32 Lease = Enemy ? Enemy->GetDungeonPoolLease() : 0;
        const TWeakObjectPtr<ADungeonRunDirector> Director = ADungeonRunDirector::Find(Avatar->GetWorld());
        const FGuid RunId = Director.IsValid() ? Director->RunId : FGuid();
        const auto IsPredictionSourceCurrent = [&]()
        {
            auto* CurrentAvatar = CapturedAvatar.Get();
            auto* CurrentASC = CapturedASC.Get();
            if (!Session || !Session->IsSourceCurrent() || !IsValid(CurrentAvatar) ||
                CurrentAvatar->IsActorBeingDestroyed() || ICombatInterface::Execute_IsDead(CurrentAvatar) ||
                !IsValid(CurrentASC) || CurrentASC->GetAvatarActor() != CurrentAvatar)
            {
                return false;
            }
            const auto* CurrentEnemy = Cast<AEnemyCharacter>(CurrentAvatar);
            const auto* CurrentDirector = ADungeonRunDirector::Find(CurrentAvatar->GetWorld());
            return (!CurrentEnemy ||
                    (!CurrentEnemy->IsDungeonPoolSleeping() && CurrentEnemy->GetDungeonPoolLease() == Lease)) &&
                   CurrentDirector == Director.Get() && (!CurrentDirector || CurrentDirector->RunId == RunId);
        };
        auto* Settings = NewObject<UAbilityOwnedEffectNodeConfig>(this);
        Settings->EffectClass = HitReactEffectClass;
        Settings->EffectLevel = HitReactEffectLevel;
        FString Error;
        if (!Settings->Validate(Error) || !IsPredictionSourceCurrent())
        {
            EndAbility(Handle, Info, Activation, true, true);
            return;
        }
        FAbilityGameplayEffectApplication Applied;
        Applied.TargetASC = CapturedASC;
        // ApplyGameplayEffectToOwner内部使用ASC GetPredictionKeyForNewAction，等同原BP节点。
        Applied.Handle = ApplyGameplayEffectToOwner(
            Handle, Info, Activation, HitReactEffectClass->GetDefaultObject<UGameplayEffect>(), HitReactEffectLevel, 1);
        Applied.bApplied = Applied.Handle.WasSuccessfullyApplied();
        if (!IsActive() || bEndingReact || !IsPredictionSourceCurrent())
        {
            AbilityGameplayEffectActions::Remove(Applied);
            if (IsActive() && !bEndingReact && GetExecutionSession() == ActivationSession)
            {
                EndAbility(Handle, Info, Activation, true, true);
            }
            return;
        }
        OwnedReact = Applied;
        if (!OwnedReact.bApplied)
        {
            EndAbility(Handle, Info, Activation, true, true);
            return;
        }
    }
    auto* Montage = ICombatInterface::Execute_GetHitReactMontage(Avatar);
    if (!Montage)
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    auto* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, 1.f);
    // 原图在BlendOut就移除GE，不能改为等待完整Montage结束。
    Task->OnBlendOut.AddDynamic(this, &ThisClass::ReactFinished);
    Task->OnCompleted.AddDynamic(this, &ThisClass::ReactFinished);
    Task->OnInterrupted.AddDynamic(this, &ThisClass::ReactFinished);
    Task->OnCancelled.AddDynamic(this, &ThisClass::ReactFinished);
    Task->ReadyForActivation();
}

void UMyHitReactAbility::ReactFinished()
{
    if (IsActive() && !bEndingReact)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    }
}

void UMyHitReactAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (bEndingReact)
    {
        return;
    }
    bEndingReact = true;
    CloseExecutionSession(bCancelled);
    AbilityGameplayEffectActions::Remove(OwnedReact);
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
