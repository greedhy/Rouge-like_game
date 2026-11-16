// 调用链：独立GA前端 -> UAbilityTask_RunGraph -> 注册Time.WorldSlow；取消不影响其他调用者的时间租约。
#include "AbilitySystem/Ability/WorldSlowAbility.h"
#include "AbilitySystem/Unified/AbilityTask_RunGraph.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorldTimeNodes.h"
#include "NativeGameplayTags.h"
#include "Engine/World.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "AbilitySystemComponent.h"
#include "Misc/ScopeExit.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorldSlowAbility, "Abilities.Time.WorldSlow");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorldSlowCooldown, "Cooldown.Time.WorldSlow");

/** 每次激活保存独立启动事务；同步取消并重入时旧栈帧只能清理旧GE。 */
struct FWorldSlowActivationState
{
    TSharedPtr<FAbilityExecutionSession> Session;
    TWeakObjectPtr<UAbilitySystemComponent> ASC;
    FActiveGameplayEffectHandle Cooldown;
    bool bStarting = true;
};

UWorldSlowCooldownEffect::UWorldSlowCooldownEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(3.f);
    auto* Component = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("WorldSlowCooldownTags"));
    GEComponents.Add(Component);
    FInheritedTagContainer Tags;
    Tags.AddTag(TAG_WorldSlowCooldown);
    Component->SetAndApplyTargetTagChanges(Tags);
}

UWorldSlowAbility::UWorldSlowAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    SetAssetTags(FGameplayTagContainer(TAG_WorldSlowAbility));
    CooldownGameplayEffectClass = UWorldSlowCooldownEffect::StaticClass();
}

bool UWorldSlowAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                           const FGameplayTagContainer* SourceTags,
                                           const FGameplayTagContainer* TargetTags,
                                           FGameplayTagContainer* OptionalRelevantTags) const
{
    FString Error;
    FAbilitySourceToken Source;
    return Info && Info->AvatarActor.IsValid() && Info->AvatarActor->GetWorld() &&
           AbilityExecutionIdentity::CaptureSource(Info->AvatarActor.Get(), Source, Error) &&
           Info->AvatarActor->GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>() &&
           Info->AvatarActor->GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>()->CanAcquire(Source, Settings, this,
                                                                                                 Error) &&
           Super::CanActivateAbility(Handle, Info, SourceTags, TargetTags, OptionalRelevantTags);
}

void UWorldSlowAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                        FGameplayAbilityActivationInfo Activation,
                                        const FGameplayEventData* TriggerEventData)
{
    const auto Starting = MakeShared<FWorldSlowActivationState>();
    Starting->Session = GetExecutionSession();
    Starting->ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
    ActivationState = Starting;
    ON_SCOPE_EXIT
    {
        Starting->bStarting = false;
    };
    FAbilityNodeInput Input;
    FString Error;
    const bool bCommitted =
        Info && Info->IsNetAuthority() && CanActivateAbility(Handle, Info) &&
        AbilityExecutionIdentity::CaptureSource(Info->AvatarActor.Get(), Input.SourceToken, Error) &&
        CommitAbility(Handle, Info, Activation);
    if (!bCommitted || !IsActive() || GetExecutionSession() != Starting->Session)
    {
        RollbackStartupCooldown(Starting);
        if (GetExecutionSession() == Starting->Session && IsActive())
        {
            EndAbility(Handle, Info, Activation, true, true);
        }
        return;
    }
    Input.Origin = Info->AvatarActor->GetActorLocation();
    auto* Graph = NewObject<UAbilityGraphDefinition>(this);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Time.WorldSlow");
    auto* Config = NewObject<UAbilityWorldSlowNodeConfig>(Graph);
    Config->Settings = Settings;
    Node.Config = Config;
    Graph->Nodes.Add(Node);
    Graph->EntryNodeId = Node.NodeId;
    Task = UAbilityTask_RunGraph::RunAbilityGraphWithInput(this, Graph, Input, {});
    Task->OnFinished.AddDynamic(this, &ThisClass::GraphFinished);
    Task->ReadyForActivation();
}

void UWorldSlowAbility::ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                      FGameplayAbilityActivationInfo Activation) const
{
    const auto Starting = ActivationState;
    if (const auto* Effect = GetCooldownGameplayEffect())
    {
        const auto Applied =
            ApplyGameplayEffectToOwner(Handle, Info, Activation, Effect, GetAbilityLevel(Handle, Info));
        if (Starting)
        {
            Starting->Cooldown = Applied;
            if (Starting->Session != GetExecutionSession() || !Starting->Session->IsSourceCurrent())
            {
                RollbackStartupCooldown(Starting);
            }
        }
    }
}

void UWorldSlowAbility::RollbackStartupCooldown(const TSharedPtr<FWorldSlowActivationState>& State) const
{
    // Commit委托可能同步换生命/改世界倍率；只回收本次启动的GE，不按Tag删除其他来源。
    if (State && State->Cooldown.IsValid() && State->ASC.IsValid())
    {
        const auto Cooldown = State->Cooldown;
        State->Cooldown.Invalidate();
        State->ASC->RemoveActiveGameplayEffect(Cooldown);
    }
}

void UWorldSlowAbility::GraphFinished(bool bCancelled)
{
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
    }
}

void UWorldSlowAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                   FGameplayAbilityActivationInfo Activation, bool bReplicateEndAbility,
                                   bool bWasCancelled)
{
    const auto Ending = ActivationState;
    auto* EndingTask = Task.Get();
    Task = nullptr;
    // 先断开旧任务/会话再触发GE移除委托；委托可完成旧End并同步进入新激活。
    if (Ending && Ending->Session)
    {
        Ending->Session->Close(bWasCancelled);
    }
    if (EndingTask)
    {
        EndingTask->OnFinished.RemoveAll(this);
        EndingTask->EndTask();
    }
    if (Ending && Ending->bStarting && bWasCancelled)
    {
        RollbackStartupCooldown(Ending);
    }
    if (!Ending || GetExecutionSession() == Ending->Session)
    {
        Super::EndAbility(Handle, Info, Activation, bReplicateEndAbility, bWasCancelled);
    }
}
