// 主要调用链：GAS PreActivate → 独立执行会话 → 输入/注册动作/一次Commit → 派生GA收尾与会话关闭；描述查询供技能UI复用。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "Character/EnemyCharacter.h"

#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /** 费用可以同步结束/重激活 GA，先复制纯值身份，不能在提交后读取下一次施法成员。 */
    struct FRuntimeCostObservation
    {
        FAbilitySourceToken Source;
        FAbilityRuntimeDeveloperCost Cost;
        bool bEnabled = false;
    };

    /** 只捕获真正运行动态配方的服务器施法，不改变无修正的原有费用路径。 */
    FRuntimeCostObservation CaptureRuntimeCost(const UMyGameplayAbility& Ability, const FGameplayAbilityActorInfo* Info,
                                               FName Stage)
    {
        FRuntimeCostObservation Result;
        const auto& Recipe = Ability.GetRuntimeRecipeSnapshot();
        const auto Session = Ability.GetExecutionSession();
        FString Error;
        if (Info && Info->IsNetAuthority() && Session && Recipe.Graph &&
            AbilityExecutionIdentity::CaptureSource(Info->AvatarActor.Get(), Result.Source, Error) &&
            Result.Source.ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetManaAttribute()))
        {
            Result.bEnabled = true;
            Result.Cost.RootCastId = Session->GetId();
            Result.Cost.AbilityTag = Recipe.AbilityTag;
            Result.Cost.Revision = Recipe.Revision;
            Result.Cost.FrozenHash = Recipe.ContentHash;
            Result.Cost.Stage = Stage;
            Result.Cost.ResourceBefore = Result.Source.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        }
        return Result;
    }

    /** 回执失败不传播为游戏提交失败，也不从效果完成事件推断费用。 */
    void CompleteRuntimeCost(FRuntimeCostObservation Observation, bool bSuccess)
    {
        auto* ASC = Observation.Source.ASC.Get();
        AActor* RecipeOwner = ASC ? ASC->GetOwnerActor() : nullptr;
        auto* Developer = IsValid(RecipeOwner)
                              ? RecipeOwner->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>()
                              : nullptr;
        if (!Observation.bEnabled || !Developer || !ASC)
        {
            return;
        }
        Observation.Cost.bSuccess = bSuccess;
        Observation.Cost.ResourceAfter = ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        FString Error;
        Developer->RecordCost(Observation.Source, Observation.Cost, Error);
    }
} // namespace

void UMyGameplayAbility::ObserveFrozenRuntimeGraph(UAbilityGraphInstance* Run, const FAbilitySourceToken& Source)
{
    AActor* RecipeOwner = Source.ASC.IsValid() ? Source.ASC->GetOwnerActor() : nullptr;
    auto* Developer =
        IsValid(RecipeOwner) ? RecipeOwner->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>() : nullptr;
    const auto Session = ExecutionSession;
    if (RuntimeRecipeSnapshot.Graph && Session && Developer)
    {
        FString Error;
        Developer->ObserveExecution(Run, Source, RuntimeRecipeSnapshot.AbilityTag, RuntimeRecipeSnapshot.Revision,
                                    RuntimeRecipeSnapshot.ContentHash, Error, Session->GetId());
    }
}

void UMyGameplayAbility::ForgetFrozenRuntimeGraph(UAbilityGraphInstance* Run, const FAbilitySourceToken& Source)
{
    AActor* RecipeOwner = Source.ASC.IsValid() ? Source.ASC->GetOwnerActor() : nullptr;
    if (auto* Developer = IsValid(RecipeOwner)
                              ? RecipeOwner->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>()
                              : nullptr)
    {
        Developer->ForgetExecution(Run);
    }
}

UAbilityGraphDefinition* UMyGameplayAbility::BuildRuntimeBaseGraph(UObject* Outer, FString& Error) const
{
    Error = TEXT("This frontend has no runtime behavior definition adapter.");
    return nullptr;
}

UAbilityGraphInstance* UMyGameplayAbility::StartFrozenRuntimeRecipe(const FAbilityNodeInput& Input,
                                                                    EAbilityExecutionLifetime Lifetime, FString& Error)
{
    Error = RuntimeRecipeError;
    const auto Session = ExecutionSession;
    UWorld* World = Input.SourceToken.World.Get();
    auto* Owner = World ? World->GetSubsystem<UAbilityGraphWorldSubsystem>() : nullptr;
    if (!Error.IsEmpty() || bRuntimeRecipeStarted || !RuntimeRecipeSnapshot.Graph || !IsActive() || !CurrentActorInfo ||
        !CurrentActorInfo->IsNetAuthority() || !Session || !Session->IsSourceCurrent() || !Owner)
    {
        if (Error.IsEmpty())
        {
            Error = TEXT("A frozen recipe requires its active authoritative frontend and original source world.");
        }
        return nullptr;
    }
    // 在任何用户回调/节点执行之前记账；同步 Notify 重入不能再创建一张独立根图。
    bRuntimeRecipeStarted = true;
    // Start 中的伤害/事件可以同步 EndAbility；本地强引用保持配置及执行对象直到移交完成。
    const TStrongObjectPtr<UAbilityGraphDefinition> Definition(RuntimeRecipeSnapshot.Graph.Get());
    const TStrongObjectPtr<UAbilityGraphInstance> Run(NewObject<UAbilityGraphInstance>(World));
    ObserveFrozenRuntimeGraph(Run.Get(), Input.SourceToken);
    const TWeakObjectPtr<UAbilityGraphInstance> WeakRun(Run.Get());
    Session->OnSignal.AddLambda(
        [WeakRun, Lifetime](FName Signal, FGameplayTag)
        {
            if (auto* Instance = WeakRun.Get();
                Instance && (Signal == TEXT("Cancelled") ||
                             (Signal == TEXT("Completed") && Lifetime == EAbilityExecutionLifetime::BoundToAbility)))
            {
                Instance->Cancel();
            }
        });
    FAbilityExecutionOptions Options;
    Options.CompatibilityProfile = Definition->CompatibilityProfile;
    Options.BudgetPolicy = Definition->BudgetPolicy;
    Options.Lifetime = Lifetime;
    Options.OwnerLease = RuntimeExecutionOwnerLease;
    if (!Run->StartWithInput(Definition.Get(), Input, Options, Error) || !Owner->Adopt(Run.Get(), Error))
    {
        ForgetFrozenRuntimeGraph(Run.Get(), Input.SourceToken);
        Run->Cancel(EAbilityGraphEndReason::Failed);
        return nullptr;
    }
    if (Run->IsFinished() && Run->GetEndReason() != EAbilityGraphEndReason::Completed &&
        Run->GetEndReason() != EAbilityGraphEndReason::Expired)
    {
        Error = TEXT("Frozen recipe stopped before its action could complete.");
        return nullptr;
    }
    return Run.Get();
}

// PreActivate由GAS统一调用，原生GA不调用父Activate或蓝图入口同样拥有隔离会话。
bool UMyGameplayAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo,
                                            const FGameplayTagContainer* SourceTags,
                                            const FGameplayTagContainer* TargetTags,
                                            FGameplayTagContainer* OptionalRelevantTags) const
{
    const auto* Enemy = ActorInfo ? Cast<AEnemyCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
    if (Enemy && Enemy->IsIceFrozen())
    {
        return false;
    }
    if (ActorInfo && ActorInfo->IsNetAuthority() && ActorInfo->AbilitySystemComponent.IsValid())
    {
        const auto* Spec = ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle);
        const auto* Developer =
            Spec ? Cast<UAbilityRuntimeRecipeDeveloperComponent>(Spec->SourceObject.Get()) : nullptr;
        if (Developer && !Developer->GetPreviewOwnerLease(Handle).IsValid())
        {
            return false;
        }
    }
    return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UMyGameplayAbility::PreActivate(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                     FGameplayAbilityActivationInfo ActivationInfo,
                                     FOnGameplayAbilityEnded::FDelegate* EndedDelegate,
                                     const FGameplayEventData* TriggerEventData)
{
    // 非实例GA的CDO不能保存施法状态；当前资产审计没有此类入口。
    if (IsInstantiated())
    {
        CloseExecutionSession(true);
        RuntimeRecipeSnapshot = {};
        RuntimeRecipeError.Reset();
        bRuntimeRecipeStarted = false;
        RuntimeExecutionOwnerLease.Invalidate();
        // 在目标等待/动画之前冻结本次版本。组件属于当前 ASC Owner，不跟随共享 CDO 或其它玩家。
        if (ActorInfo && ActorInfo->IsNetAuthority() && ActorInfo->AbilitySystemComponent.IsValid())
        {
            const auto* Spec = ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle);
            const auto* Developer =
                Spec ? Cast<UAbilityRuntimeRecipeDeveloperComponent>(Spec->SourceObject.Get()) : nullptr;
            if (Developer)
            {
                RuntimeExecutionOwnerLease = Developer->GetPreviewOwnerLease(Handle);
                if (!RuntimeExecutionOwnerLease.IsValid())
                {
                    RuntimeRecipeError = TEXT("This temporary preview has already closed.");
                }
            }
            AActor* Owner = ActorInfo->AbilitySystemComponent->GetOwnerActor();
            auto* Recipes = IsValid(Owner) ? Owner->FindComponentByClass<UAbilityRuntimeRecipeComponent>() : nullptr;
            const auto* OwnerDeveloper = Cast<UAbilityRuntimeRecipeDeveloperComponent>(Recipes);
            if (Recipes && RuntimeRecipeError.IsEmpty())
            {
                for (const FGameplayTag& Identity : GetAssetTags())
                {
                    // 准确的临时 Spec 冻结预览图，正式 Spec 只收集本玩家当前的外部来源。
                    const bool bPreviewDefault = RuntimeExecutionOwnerLease.IsValid() && Developer &&
                                                 Developer->GetDeveloperState().AbilityTag == Identity;
                    FAbilityRuntimeRecipeSnapshot Candidate;
                    bool bHasRecipe = false;
                    bool bAccepted = false;
                    if (OwnerDeveloper && !Developer)
                    {
                        bAccepted = OwnerDeveloper->CreateFormalCastSnapshot(Identity, this, this, Candidate,
                                                                            bHasRecipe, RuntimeRecipeError);
                    }
                    else
                    {
                        bHasRecipe = bPreviewDefault || Recipes->HasContributions(Identity);
                        bAccepted = !bHasRecipe ||
                                    Recipes->CreateCastSnapshot(Identity, this, Candidate, RuntimeRecipeError);
                    }
                    if (!bAccepted)
                    {
                        RuntimeRecipeSnapshot = {};
                        break;
                    }
                    if (!bHasRecipe)
                    {
                        continue;
                    }
                    if (RuntimeRecipeSnapshot.Graph)
                    {
                        RuntimeRecipeSnapshot = {};
                        RuntimeRecipeError = TEXT("One activation cannot resolve multiple base ability recipes.");
                        break;
                    }
                    RuntimeRecipeSnapshot = MoveTemp(Candidate);
                }
            }
        }
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
    if (!RuntimeRecipeError.IsEmpty())
    {
        // 冻结失败不能通过忽略蓝图 Commit 返回值而退回旧伤害入口；动作桥也会独立检查。
        return false;
    }
    const auto Session = ExecutionSession;
    auto Commit = [&]()
    {
        const auto Observation = CaptureRuntimeCost(*this, ActorInfo, TEXT("CommitAbility"));
        const bool bSuccess = Super::CommitAbility(Handle, ActorInfo, ActivationInfo, OptionalRelevantTags);
        CompleteRuntimeCost(Observation, bSuccess);
        return bSuccess;
    };
    return Session ? Session->TryCommit(Commit) : Commit();
}

bool UMyGameplayAbility::CommitAbilityCost(FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo,
                                           FGameplayAbilityActivationInfo ActivationInfo,
                                           FGameplayTagContainer* OptionalRelevantTags)
{
    if (!RuntimeRecipeError.IsEmpty())
    {
        return false;
    }
    const auto Observation = CaptureRuntimeCost(*this, ActorInfo, TEXT("CommitAbilityCost"));
    const bool bSuccess = Super::CommitAbilityCost(Handle, ActorInfo, ActivationInfo, OptionalRelevantTags);
    CompleteRuntimeCost(Observation, bSuccess);
    return bSuccess;
}

bool UMyGameplayAbility::CommitAbilityCooldown(FGameplayAbilitySpecHandle Handle,
                                               const FGameplayAbilityActorInfo* ActorInfo,
                                               FGameplayAbilityActivationInfo ActivationInfo, bool ForceCooldown,
                                               FGameplayTagContainer* OptionalRelevantTags)
{
    if (!RuntimeRecipeError.IsEmpty())
    {
        return false;
    }
    const auto Observation = CaptureRuntimeCost(*this, ActorInfo, TEXT("CommitAbilityCooldown"));
    const bool bSuccess =
        Super::CommitAbilityCooldown(Handle, ActorInfo, ActivationInfo, ForceCooldown, OptionalRelevantTags);
    CompleteRuntimeCost(Observation, bSuccess);
    return bSuccess;
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
    const auto EndingSession = ExecutionSession;
    CloseExecutionSession(bWasCancelled);
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
    // 结束委托可能同步重激活同一个 InstancedPerActor 对象；旧尾部只能释放旧会话的快照。
    if (ExecutionSession == EndingSession)
    {
        RuntimeRecipeSnapshot = {};
        RuntimeRecipeError.Reset();
    }
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
