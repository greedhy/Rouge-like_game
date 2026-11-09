// 调用链：ASC -> 固定来源/上下文 -> Enemy/Ground目标任务或Self -> 校验图/一次Commit -> Task或世界持有图。
// 客户端只提交目标请求；服务器解析目标、费用和效果，独立图正常移交后不再归GA任务清理。
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityTask_RunGraph.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "PureMass/PureMassTargetData.h"
#include "PureMass/PureMassCombatTarget.h"
#include "NativeGameplayTags.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UnifiedDelayedPetrify, "Abilities.Unified.DelayedPetrify");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UnifiedDelayedPetrifyCooldown, "Cooldown.Unified.DelayedPetrify");

UUnifiedGraphAbility::UUnifiedGraphAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    DebuffChance = 0.f;
    KnockbackChance = 0.f;
}

void UUnifiedGraphAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                           FGameplayAbilityActivationInfo Activation,
                                           const FGameplayEventData* TriggerEventData)
{
    bEnding = false;
    bTargetConsumed = false;
    bIndependentTransferred = false;
    ActivationSource = {};
    CastContext = nullptr;
    IndependentExecution = nullptr;
    if (!Info || !Info->AvatarActor.IsValid() || !GetWorld() || !Graph || !FMath::IsFinite(CastRange) ||
        CastRange <= 0.f ||
        (TargetMode != EUnifiedAbilityTargetMode::Enemy && TargetMode != EUnifiedAbilityTargetMode::Ground &&
         TargetMode != EUnifiedAbilityTargetMode::Self) ||
        (ExecutionLifetime != EAbilityExecutionLifetime::BoundToAbility &&
         ExecutionLifetime != EAbilityExecutionLifetime::IndependentCastEffect) ||
        ((bStopOnInputRelease || bToggleInput) && ExecutionLifetime != EAbilityExecutionLifetime::BoundToAbility) ||
        (bStopOnInputRelease && bToggleInput))
    {
        AbortCast();
        return;
    }
    // 原型与新节点共享同一份上下文，在异步等待前固定来源和等级；不在目标到达后重新认领新Avatar。
    if (Info->IsNetAuthority())
    {
        CastContext = NewObject<UPrototypeCastContext>(GetWorld());
        CastContext->Caster = Info->AvatarActor.Get();
        CastContext->CastId = FGuid::NewGuid();
        CastContext->Level = GetAbilityLevel();
        CastContext->DamageParams = MakeDamageEffectParamsFromClassDefaults();
        FString Error;
        if (!CastContext->CaptureSourceLife(Error))
        {
            AbortCast();
            return;
        }
        ActivationSource = CastContext->GetSourceLife();
    }
    if (bStopOnInputRelease)
    {
        // 同时在预测端和权威端建立监听；标准任务负责ServerSetReplicatedEvent，不能仅在服务器重写虚函数。
        ReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, false);
        ReleaseTask->OnRelease.AddDynamic(this, &ThisClass::ChannelReleased);
        ReleaseTask->ReadyForActivation();
        if (!IsActive() || bEnding)
        {
            return;
        }
    }
    if (TargetMode == EUnifiedAbilityTargetMode::Self)
    {
        bTargetConsumed = true;
        if (Info->IsNetAuthority())
        {
            FAbilityNodeInput Input;
            Input.Origin = Info->AvatarActor->GetActorLocation();
            Input.Direction = Info->AvatarActor->GetActorForwardVector();
            ExecutePreparedInput(Input);
        }
        return;
    }
    GetWorld()->GetTimerManager().SetTimer(TargetTimeout, this, &ThisClass::AbortCast, 3.f);
    TargetTask = UPureMassTargetData::Create(this);
    TargetTask->Ready.AddDynamic(this, &ThisClass::TargetReady);
    TargetTask->ReadyForActivation();
}

void UUnifiedGraphAbility::TargetReady(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bEnding || bTargetConsumed || !CurrentActorInfo)
    {
        return;
    }
    bTargetConsumed = true;
    GetWorld()->GetTimerManager().ClearTimer(TargetTimeout);
    if (!CurrentActorInfo->IsNetAuthority())
    {
        return;
    }
    const FHitResult* Hit = Data.Num() == 1 && Data.Get(0) ? Data.Get(0)->GetHitResult() : nullptr;
    AActor* Avatar = nullptr;
    if (!Hit || !Hit->bBlockingHit || Hit->ImpactPoint.ContainsNaN() || (Hit->GetActor() && Hit->Item != INDEX_NONE) ||
        (!Hit->GetActor() && Hit->Item != INDEX_NONE && Hit->Item <= 0) ||
        !AbilityExecutionIdentity::ResolveSource(ActivationSource, Avatar))
    {
        AbortCast();
        return;
    }
    const FPureMassCombatTarget Requested =
        Hit->GetActor() ? FPureMassCombatTarget::FromActor(Hit->GetActor()) : FPureMassCombatTarget(Hit->Item);
    FAbilityNodeInput Input;
    FAbilityTargetToken Target;
    FVector Location;
    FString Error;
    // Ground可以使用鼠标命中的敌人位置瞄准，但任何随请求携带的身份仍须服务器校验；不会传给图活目标端口。
    if (TargetMode == EUnifiedAbilityTargetMode::Enemy || Requested.IsSet())
    {
        if (!AbilityExecutionIdentity::CaptureTarget(ActivationSource, Requested, Target, Error) ||
            !AbilityExecutionIdentity::ResolveTarget(ActivationSource, Target, Location) ||
            FVector::DistSquared(Location, Hit->ImpactPoint) > FMath::Square(250.f))
        {
            AbortCast();
            return;
        }
    }
    const FVector SourcePosition = Avatar->GetActorLocation();
    if (TargetMode == EUnifiedAbilityTargetMode::Enemy)
    {
        if (FVector::DistSquared(Location, SourcePosition) > FMath::Square(CastRange) ||
            !PureMassCombat::Visible(GetWorld(), Avatar, SourcePosition + FVector(0, 0, 30), Requested))
        {
            AbortCast();
            return;
        }
        Input.LiveTargetToken = Target;
        // Schema1保留旧样例目标位置；Schema2交付族从施法者出发，目标身份单独携带。
        Input.Origin = Graph && Graph->SchemaVersion == 1 ? Location : SourcePosition;
        Input.Direction = (Location - SourcePosition).GetSafeNormal();
    }
    else if (TargetMode == EUnifiedAbilityTargetMode::Ground)
    {
        if (FVector::DistSquared(Hit->ImpactPoint, SourcePosition) > FMath::Square(CastRange) ||
            !PureMassCombat::GroundVisible(GetWorld(), Avatar, SourcePosition + FVector(0, 0, 30), Hit->ImpactPoint))
        {
            AbortCast();
            return;
        }
        Input.Origin = Hit->ImpactPoint;
        Input.Direction = (Input.Origin - SourcePosition).GetSafeNormal();
    }
    else
    {
        AbortCast();
        return;
    }
    if (Input.Direction.IsNearlyZero())
    {
        Input.Direction = Avatar->GetActorForwardVector();
    }
    ExecutePreparedInput(Input);
}

void UUnifiedGraphAbility::ExecutePreparedInput(FAbilityNodeInput Input)
{
    AActor* Avatar = nullptr;
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    TArray<FString> Errors;
    if (!IsActive() || bEnding || !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority() || !Graph || !CastContext ||
        !CastContext->ValidateSourceLife() || !AbilityExecutionIdentity::ResolveSource(ActivationSource, Avatar))
    {
        AbortCast();
        return;
    }
    // 费用回调可能同步触发其它业务；先冻结并校验图，运行时不读修改后的共享资产。
    const TStrongObjectPtr<UAbilityGraphDefinition> Prepared(DuplicateObject<UAbilityGraphDefinition>(Graph, this));
    if (!Prepared->Validate(Registry, Errors))
    {
        AbortCast();
        return;
    }
    const auto* Entry = Prepared->Nodes.FindByPredicate(
        [&](const FAbilityGraphNode& Node)
        {
            return Node.NodeId == Prepared->EntryNodeId;
        });
    const auto* Descriptor = Entry ? Registry.Find(Entry->TypeId) : nullptr;
    const EAbilityPortPayload Required = Prepared->SchemaVersion == 1 ? EAbilityPortPayload::LiveTarget
                                         : Descriptor                 ? Descriptor->InputPayload
                                                                      : EAbilityPortPayload::DamageResult;
    if (!Descriptor || (Required != EAbilityPortPayload::Origin && Required != EAbilityPortPayload::LiveTarget) ||
        (Required == EAbilityPortPayload::LiveTarget && !Input.LiveTargetToken.IsSet()) || Input.Origin.ContainsNaN() ||
        Input.Direction.ContainsNaN())
    {
        AbortCast();
        return;
    }
    Input.SourceToken = ActivationSource;
    Input.Context = CastContext;
    Input.Level = CastContext->Level;
    Input.DamageParams = CastContext->DamageParams;
    Input.EventCenter = Input.Origin;
    FAbilityExecutionOptions Options;
    Options.CompatibilityProfile = Prepared->CompatibilityProfile;
    Options.BudgetPolicy = Prepared->BudgetPolicy;
    Options.Lifetime = ExecutionLifetime;
    if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo) || !IsActive() || bEnding ||
        !AbilityExecutionIdentity::ResolveSource(ActivationSource, Avatar))
    {
        AbortCast();
        return;
    }
    if (ExecutionLifetime == EAbilityExecutionLifetime::BoundToAbility)
    {
        GraphTask = UAbilityTask_RunGraph::RunAbilityGraphWithInput(this, Prepared.Get(), Input, Options);
        GraphTask->OnFinished.AddDynamic(this, &ThisClass::GraphFinished);
        GraphTask->ReadyForActivation();
        return;
    }
    auto* WorldGraphs = GetWorld()->GetSubsystem<UAbilityGraphWorldSubsystem>();
    if (!WorldGraphs)
    {
        AbortCast();
        return;
    }
    IndependentExecution = NewObject<UAbilityGraphInstance>(GetWorld());
    const TStrongObjectPtr<UAbilityGraphInstance> Execution(IndependentExecution.Get());
    FString Error;
    if (!Execution->StartWithInput(Prepared.Get(), Input, Options, Registry, Error) || !IsActive() || bEnding ||
        (Execution->IsFinished() && Execution->GetEndReason() != EAbilityGraphEndReason::Completed &&
         Execution->GetEndReason() != EAbilityGraphEndReason::Expired) ||
        !WorldGraphs->Adopt(Execution.Get(), Error))
    {
        Execution->Cancel();
        AbortCast();
        return;
    }
    bIndependentTransferred = true;
    GraphFinished(false);
}

void UUnifiedGraphAbility::GraphFinished(bool bCancelled)
{
    if (IsActive() && !bEnding)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
    }
}
void UUnifiedGraphAbility::InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                         FGameplayAbilityActivationInfo Activation)
{
    Super::InputReleased(Handle, Info, Activation);
    if (bStopOnInputRelease && Info && Info->IsNetAuthority())
    {
        GraphFinished(true);
    }
}
void UUnifiedGraphAbility::ChannelReleased(float HeldSeconds)
{
    if (bStopOnInputRelease && CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
    {
        GraphFinished(true);
    }
}
void UUnifiedGraphAbility::AbortCast()
{
    GraphFinished(true);
}
bool UUnifiedGraphAbility::HasPendingGraphWork() const
{
    return TargetTask || GraphTask || IndependentExecution || ReleaseTask ||
           (GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(TargetTimeout));
}
void UUnifiedGraphAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                      FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (bEnding)
    {
        return;
    }
    bEnding = true;
    CloseExecutionSession(bCancelled);
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(TargetTimeout);
    }
    if (TargetTask)
    {
        TargetTask->Ready.RemoveAll(this);
        TargetTask->EndTask();
        TargetTask = nullptr;
    }
    if (GraphTask)
    {
        GraphTask->OnFinished.RemoveAll(this);
        GraphTask->EndTask();
        GraphTask = nullptr;
    }
    if (ReleaseTask)
    {
        ReleaseTask->OnRelease.RemoveAll(this);
        if (Info && Info->AbilitySystemComponent.IsValid())
        {
            // 标准WaitInputRelease只在收到松键时移除原生监听；提前取消也须撤销本任务的ASC委托。
            Info->AbilitySystemComponent
                ->AbilityReplicatedEventDelegate(EAbilityGenericReplicatedEvent::InputReleased, Handle,
                                                 Activation.GetActivationPredictionKey())
                .RemoveAll(ReleaseTask.Get());
        }
        ReleaseTask->EndTask();
        ReleaseTask = nullptr;
    }
    if (IndependentExecution && (bCancelled || !bIndependentTransferred))
    {
        IndependentExecution->Cancel();
    }
    IndependentExecution = nullptr;
    CastContext = nullptr;
    ActivationSource = {};
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
