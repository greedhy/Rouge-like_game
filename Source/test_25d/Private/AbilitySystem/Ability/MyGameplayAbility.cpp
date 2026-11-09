// 主要调用链：GAS PreActivate → 独立执行会话 → 输入/注册动作/一次Commit → 派生GA收尾与会话关闭；描述查询供技能UI复用。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Ability/MyGameplayAbility.h"

#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"

// PreActivate由GAS统一调用，原生GA不调用父Activate或蓝图入口同样拥有隔离会话。
void UMyGameplayAbility::PreActivate(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                     FGameplayAbilityActivationInfo ActivationInfo,
                                     FOnGameplayAbilityEnded::FDelegate* EndedDelegate,
                                     const FGameplayEventData* TriggerEventData)
{
    // 非实例GA的CDO不能保存施法状态；当前资产审计没有此类入口。
    if (IsInstantiated())
    {
        CloseExecutionSession(true);
        FAbilitySourceToken Source;
        FString Error;
        const bool bAuthority = ActorInfo && ActorInfo->IsNetAuthority();
        const bool bCaptured =
            bAuthority && AbilityExecutionIdentity::CaptureSource(ActorInfo->AvatarActor.Get(), Source, Error);
        const TWeakObjectPtr<UAbilitySystemComponent> ASC =
            ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
        const TWeakObjectPtr<AActor> Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
        const bool bBoundToRun = IsExecutionSessionBoundToRun();
        // 值捕获本次激活的来源与寿命；重入激活不能通过修改派生类成员让旧会话获得新身份。
        ExecutionSession = MakeShared<FAbilityExecutionSession>(
            [Source, bAuthority, bCaptured, ASC, Avatar, bBoundToRun]()
            {
                if (bAuthority)
                {
                    AActor* Resolved = nullptr;
                    return bCaptured &&
                           (bBoundToRun ? AbilityExecutionIdentity::ResolveSource(Source, Resolved)
                                        : AbilityExecutionIdentity::ResolveSourceAvatarLifetime(Source, Resolved));
                }
                return ASC.IsValid() && Avatar.IsValid() && ASC->GetAvatarActor() == Avatar.Get();
            });
    }
    Super::PreActivate(Handle, ActorInfo, ActivationInfo, EndedDelegate, TriggerEventData);
}

bool UMyGameplayAbility::IsExecutionSessionBoundToRun() const
{
    return true;
}

bool UMyGameplayAbility::CommitAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                       FGameplayAbilityActivationInfo ActivationInfo,
                                       FGameplayTagContainer* OptionalRelevantTags)
{
    const auto Session = ExecutionSession;
    if (!Session)
    {
        return Super::CommitAbility(Handle, ActorInfo, ActivationInfo, OptionalRelevantTags);
    }
    return Session->TryCommit(
        [&]()
        {
            return Super::CommitAbility(Handle, ActorInfo, ActivationInfo, OptionalRelevantTags);
        });
}

bool UMyGameplayAbility::DispatchExecutionAction(FName Action, int32 Sequence, TFunction<void(int32)> Execute)
{
    const auto Session = ExecutionSession;
    // 预测客户端仍可使用会话输入/表现与GAS预测Commit，但不能执行权威伤害/状态动作。
    if (!Session || !IsActive() || !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
    {
        return false;
    }
    Session->RegisterAction(Action, MoveTemp(Execute));
    return Session->ExecuteAction(Action, Sequence);
}

void UMyGameplayAbility::CloseExecutionSession(bool bCancelled)
{
    // 回调可以同步进入下一次激活；本地引用保证清理的是原会话。
    const auto Session = ExecutionSession;
    if (Session)
    {
        Session->Close(bCancelled);
    }
}

void UMyGameplayAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                                    bool bWasCancelled)
{
    CloseExecutionSession(bWasCancelled);
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// 生成当前等级的默认富文本描述，供派生技能覆盖。
FString UMyGameplayAbility::GetDescription(int32 Level)
{
    return FString::Printf(
        TEXT("<Default>%s, </><Level>%d</>"),
        L"Default Ability Name - LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum",
        Level);
}

// 生成下一等级的默认描述，供派生技能覆盖。
FString UMyGameplayAbility::GetNextLevelDescription(int32 Level)
{
    return FString::Printf(TEXT("<Default>Next Level: </><Level>%d</> \n<Default>Causes much more damage. </>"), Level);
}

// 生成包含解锁等级的提示文本。
FString UMyGameplayAbility::GetLockedDescription(int32 Level)
{
    return FString::Printf(TEXT("<Default>Spell Locked Until Level: %d</>"), Level);
}

// 从费用 GE 的法力修饰器尝试求指定等级静态消耗。
float UMyGameplayAbility::GetManaCost(float InLevel) const
{
    float ManaCost = 0.f;
    if (const UGameplayEffect* CostEffect = GetCostGameplayEffect())
    {
        for (FGameplayModifierInfo Mod : CostEffect->Modifiers)
        {
            if (Mod.Attribute == UMyAttributeSet::GetManaAttribute())
            {
                Mod.ModifierMagnitude.GetStaticMagnitudeIfPossible(InLevel, ManaCost);
                break;
            }
        }
    }
    return ManaCost;
}

// 从冷却 GE 尝试求指定等级静态持续时间。
float UMyGameplayAbility::GetCooldown(float InLevel) const
{
    float Cooldown = 0.f;
    if (const UGameplayEffect* CooldownEffect = GetCooldownGameplayEffect())
    {
        CooldownEffect->DurationMagnitude.GetStaticMagnitudeIfPossible(InLevel, Cooldown);
    }
    return Cooldown;
}
// 调用父类并为活动技能转发带预测键的按下事件。
void UMyGameplayAbility::InputPressed(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo,
                                      const FGameplayAbilityActivationInfo ActivationInfo)
{
    Super::InputPressed(Handle, ActorInfo, ActivationInfo);
    if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
    {
        return;
    }
    //通过句柄获取技能实例
    FGameplayAbilitySpec* AbilitySpec = ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle);
    //技能实例在激活状态，触发输入事件
    if (AbilitySpec && AbilitySpec->IsActive())
    {
        const auto Session = ExecutionSession;
        if (Session && !Session->PublishSignal(TEXT("InputPressed")))
        {
            return;
        }
        //将按下事件复制到服务器和所有相关的客户端
        ActorInfo->AbilitySystemComponent->InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Handle,
                                                                 ActivationInfo.GetActivationPredictionKey());
    }
}

// 调用父类并转发带预测键的松开事件。
void UMyGameplayAbility::InputReleased(const FGameplayAbilitySpecHandle Handle,
                                       const FGameplayAbilityActorInfo* ActorInfo,
                                       const FGameplayAbilityActivationInfo ActivationInfo)
{
    Super::InputReleased(Handle, ActorInfo, ActivationInfo);
    if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
    {
        return;
    }
    const auto Session = ExecutionSession;
    if (Session && !Session->PublishSignal(TEXT("InputReleased")))
    {
        return;
    }

    //将抬起事件复制到服务器和所有相关的客户端
    ActorInfo->AbilitySystemComponent->InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Handle,
                                                             ActivationInfo.GetActivationPredictionKey());
}
