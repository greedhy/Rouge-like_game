// 调用链：保留旧被动身份的原生激活 -> WaitGameplayEvent -> 同一GE动作；旧K2激活图保留但不执行。
#include "AbilitySystem/Ability/MyAttributeEventAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/World.h"
#include "Interface/CombatInterface.h"

UMyAttributeEventAbility::UMyAttributeEventAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

bool UMyAttributeEventAbility::IsExecutionSessionBoundToRun() const
{
    return false;
}

void UMyAttributeEventAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                               FGameplayAbilityActivationInfo Activation,
                                               const FGameplayEventData* Event)
{
    FString Error;
    if (!Info || !ListenTag.IsValid() || !EventEffectClass || !FMath::IsFinite(EventEffectLevel) ||
        EventEffectLevel < 0.f)
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    EventSource = {};
    bAuthoritySource = Info->IsNetAuthority();
    auto* Avatar = Info->AvatarActor.Get();
    if (bAuthoritySource)
    {
        if (!AbilityExecutionIdentity::CaptureSource(Avatar, EventSource, Error))
        {
            EndAbility(Handle, Info, Activation, true, true);
            return;
        }
    }
    else if (IsValid(Avatar))
    {
        // 原BP在客户端也创建Wait任务。这里保存前端生命身份，效果应用仍由原ASC API决定权限。
        EventSource.World = Avatar->GetWorld();
        EventSource.Avatar = Avatar;
        EventSource.ASC = Info->AbilitySystemComponent;
        if (const auto* Enemy = Cast<AEnemyCharacter>(Avatar); Enemy && Enemy->GetDungeonPoolLease() > 0)
        {
            EventSource.EnemyLease = Enemy->GetDungeonPoolLease();
        }
    }
    if (!IsEventSourceCurrent())
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    // 只启用原图实际连到Activate的监听器；ListenForEvent中悬空XP分支不能额外注册。
    auto* Wait = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, ListenTag, nullptr, false, bMatchEventExactly);
    Wait->EventReceived.AddDynamic(this, &ThisClass::ReceiveAttributeEvent);
    Wait->ReadyForActivation();
}

bool UMyAttributeEventAbility::IsEventSourceCurrent() const
{
    AActor* Avatar = EventSource.Avatar.Get();
    if (bAuthoritySource)
    {
        return AbilityExecutionIdentity::ResolveSourceAvatarLifetime(EventSource, Avatar);
    }
    auto* World = EventSource.World.Get();
    auto* ASC = EventSource.ASC.Get();
    if (!IsValid(Avatar) || Avatar->IsActorBeingDestroyed() || !World || World->bIsTearingDown ||
        Avatar->GetWorld() != World || !IsValid(ASC) || ASC->GetAvatarActor() != Avatar ||
        (Avatar->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Avatar)))
    {
        return false;
    }
    if (const auto* Enemy = Cast<AEnemyCharacter>(Avatar))
    {
        if (Enemy->IsDungeonPoolSleeping() || Enemy->GetDungeonPoolLease() != EventSource.EnemyLease.Get(0))
        {
            return false;
        }
    }
    return true;
}

void UMyAttributeEventAbility::ReceiveAttributeEvent(FGameplayEventData Payload)
{
    const auto Session = GetExecutionSession();
    if (!IsActive() || !Session)
    {
        return;
    }
    if (!Session->IsSourceCurrent() || !IsEventSourceCurrent())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    auto* Settings = NewObject<UAbilityGameplayEffectNodeConfig>(this);
    Settings->EffectClass = EventEffectClass;
    Settings->EffectLevel = EventEffectLevel;
    if (!AbilityGameplayEffectActions::BuildEventMagnitudes(Payload.EventTag, Payload.EventMagnitude,
                                                            InitializedEventTags, bInitializeListedTags,
                                                            Settings->SetByCallerMagnitudes))
    {
        return;
    }
    // 每个原始GameplayEvent独立施加一次，不额外Commit，也不把事件转换成敌对伤害参数。
    if (bAuthoritySource)
    {
        // 订阅可能先于 RunId 初始化。每条新事件只为当前这次 GE 捕获严格来源，不刷新任何旧施法令牌。
        // 这验证当前执行许可；事件是否应发放经验仍由既有 Actor/Mass 击杀生产方负责。
        FAbilitySourceToken CurrentEventSource;
        FString Error;
        if (!AbilityExecutionIdentity::CaptureSource(EventSource.Avatar.Get(), CurrentEventSource, Error))
        {
            EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
            return;
        }
        AbilityGameplayEffectActions::Apply(CurrentEventSource, *Settings);
    }
    else
    {
        auto* ASC = EventSource.ASC.Get();
        auto Spec = ASC->MakeOutgoingSpec(Settings->EffectClass, Settings->EffectLevel, ASC->MakeEffectContext());
        if (Spec.IsValid())
        {
            for (const auto& Pair : Settings->SetByCallerMagnitudes)
            {
                Spec.Data->SetSetByCallerMagnitude(Pair.Key, Pair.Value);
            }
            // 原ASC蓝图方法不传预测键；保留引擎对该客户端调用的原权限判断，不增加预测属性效果。
            ASC->BP_ApplyGameplayEffectSpecToSelf(Spec);
        }
    }
    if (IsActive() && GetExecutionSession() == Session && (!Session->IsSourceCurrent() || !IsEventSourceCurrent()))
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}

void UMyAttributeEventAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                          FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    CloseExecutionSession(bCancelled);
    EventSource = {};
    bAuthoritySource = false;
    // GAS结束拥有的WaitGameplayEvent任务并解绑ASC，旧激活不会继续接收新事件。
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
