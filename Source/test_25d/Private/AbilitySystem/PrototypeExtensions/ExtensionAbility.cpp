/**
 * 扩展 GA 输入层：ASC 激活 -> ActivateAbility -> UPureMassTargetData.Ready 异步回调 -> TargetReady -> Execute/Commit -> AExtensionRuntime::Spawn。正常结束释放 GA 引用，但子图和子系统记录继续持有 Context；取消标记 Context。费用由服务器结算，客户端只提交目标请求。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/PrototypeExtensions/ExtensionAbility.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "PureMass/PureMassAbility.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassTargetData.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
/** GAS 激活检查：要求扩展节点 ValidateGraph 成功，再执行父类费用/冷却等检查。 */
bool UExtensionAbility::CanActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                           const FGameplayTagContainer* S, const FGameplayTagContainer* T,
                                           FGameplayTagContainer* R) const
{
    const auto* N = Cast<UExtensionNode>(Prototype);
    return N && N->ValidateGraph() && Super::CanActivateAbility(H, I, S, T, R);
}
/** CommitAbility 回调：服务器通过 UPureMassAbility::SpendMana 应用稳定原生扣蓝 GE。 */
void UExtensionAbility::ApplyCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                  FGameplayAbilityActivationInfo) const
{
    if (I && I->IsNetAuthority() && Prototype)
        UPureMassAbility::SpendMana(I->AvatarActor.Get(), Prototype->ManaCost);
}
/** 重置每次施法状态；自身目标直接执行，否则创建混合目标任务并设置三秒超时；不直接结算伤害。 */
void UExtensionAbility::ActivateAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                        FGameplayAbilityActivationInfo, const FGameplayEventData*)
{
    bConsumed = false;
    CastContext = nullptr;
    auto* N = Cast<UExtensionNode>(Prototype);
    if (!I || !N || !N->ValidateGraph())
    {
        Abort();
        return;
    }
    if (N->bTargetSelf)
    {
        if (I->IsNetAuthority())
            Execute(GetAvatarActorFromActorInfo()->GetActorLocation(), {});
        return;
    }
    GetWorld()->GetTimerManager().SetTimer(Timeout, this, &ThisClass::Abort, 3.f, false);
    auto* Task = UPureMassTargetData::Create(this);
    Task->Ready.AddDynamic(this, &ThisClass::TargetReady);
    Task->ReadyForActivation();
}
/** 目标任务异步回调：拒绝 Actor/实体 ID 混装数据；服务器校验射程、遮挡与 Attach/Chain 必需目标后调用 Execute。 */
void UExtensionAbility::TargetReady(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bConsumed || !CurrentActorInfo)
        return;
    if (!CurrentActorInfo->IsNetAuthority())
    {
        bConsumed = true;
        GetWorld()->GetTimerManager().ClearTimer(Timeout);
        return;
    }
    const FHitResult* Hit = Data.Num() == 1 && Data.Get(0) ? Data.Get(0)->GetHitResult() : nullptr;
    if (!Hit || !Hit->bBlockingHit || (Hit->GetActor() && Hit->Item != INDEX_NONE))
    {
        Abort();
        return;
    }
    auto T = Hit->GetActor() ? FPureMassCombatTarget::FromActor(Hit->GetActor()) : FPureMassCombatTarget(Hit->Item);
    FVector P;
    auto* N = Cast<UExtensionNode>(Prototype);
    if (!UPureMassAbility::ValidateCombatTarget(GetAvatarActorFromActorInfo(), N, T, Hit->ImpactPoint, P) ||
        ((N->Behavior == EExtensionKind::Attach || N->Behavior == EExtensionKind::Chain) && !T.IsSet()))
    {
        Abort();
        return;
    }
    Execute(P, T);
}
/** 去重并提交施法，创建扩展 Context，按行为计算起点，生成根节点并通知攻击目标；成功后正常结束 GA。 */
void UExtensionAbility::Execute(FVector P, FPureMassCombatTarget T)
{
    if (bConsumed || !IsActive() || !CurrentActorInfo->IsNetAuthority())
        return;
    bConsumed = true;
    GetWorld()->GetTimerManager().ClearTimer(Timeout);
    auto* N = CastChecked<UExtensionNode>(Prototype);
    auto* Avatar = GetAvatarActorFromActorInfo();
    SetOffensiveAttackTarget(T.Actor.Get());
    if (!CanActivateAbility(CurrentSpecHandle, CurrentActorInfo) ||
        !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
    {
        Abort();
        return;
    }
    CastContext = NewObject<UExtensionCastContext>(Avatar);
    CastContext->Caster = Avatar;
    CastContext->CastId = FGuid::NewGuid();
    CastContext->Level = GetAbilityLevel();
    CastContext->DamageParams = MakeDamageEffectParamsFromClassDefaults();
    FVector Origin = P,
            Direction = (P - Avatar->GetActorLocation()).GetSafeNormal(SMALL_NUMBER, Avatar->GetActorForwardVector());
    if (N->Behavior == EExtensionKind::Projectile)
    {
        Origin = Avatar->GetActorLocation() + FVector(0, 0, 30);
        Direction = (P + (!T.IsSet() ? FVector(0, 0, 30) : FVector::ZeroVector) - Origin)
                        .GetSafeNormal(SMALL_NUMBER, Direction);
    }
    if (N->Behavior == EExtensionKind::Chain)
        Origin = Avatar->GetActorLocation();
    if (!AExtensionRuntime::Spawn(CastContext, N, Origin, Direction, T, 0, Origin))
    {
        Abort();
        return;
    }
    APureMassRuntime::NotifyCommittedAttack(Avatar, this, T);
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
/** 超时或校验失败入口：仅对仍激活的 GA 调用取消式 EndAbility。 */
void UExtensionAbility::Abort()
{
    if (IsActive())
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
/** 清除目标超时；取消才标记 Context，正常结束不撤销已生成的持久效果；清空本地引用并调用父类。 */
void UExtensionAbility::EndAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                   FGameplayAbilityActivationInfo A, bool Rep, bool Cancel)
{
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(Timeout);
    if (Cancel && CastContext)
        CastContext->bCancelled = true;
    CastContext = nullptr;
    Super::EndAbility(H, I, A, Rep, Cancel);
}
/** UI 查询当前扩展配置名称、等级、费用、冷却和 Summary；不运行子图。 */
FString UExtensionAbility::GetDescription(int32 Level)
{
    if (!Prototype)
        return TEXT("");
    return FString::Printf(TEXT("<Title>%s</>\n<Default>Lv.%d | MP %.0f | CD %.1fs</>\n<Default>%s</>"),
                           *Prototype->DisplayName.ToString(), Level, Prototype->ManaCost, Prototype->Cooldown,
                           *Prototype->Summary.ToString());
}
