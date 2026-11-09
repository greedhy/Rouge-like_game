// 主要调用链：公共预警/接触窗口 → ResolveHit 启动孢子区域 → Advance 按脉冲时钟结算 → EndAbility 清理委托。
#include "AbilitySystem/Ability/ShroomEnemyAbility.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "NativeGameplayTags.h"
#include "Engine/World.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShroomSlam, "Abilities.Shroom.Slam");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShroomSpore, "Abilities.Shroom.Spore");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShroomSlamCD, "Cooldown.Shroom.Slam");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShroomSporeCD, "Cooldown.Shroom.Spore");
UShroomSlamAbility::UShroomSlamAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_ShroomSlam));
    CooldownIdentity = TAG_ShroomSlamCD;
}
UShroomSporeAbility::UShroomSporeAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_ShroomSpore));
    CooldownIdentity = TAG_ShroomSporeCD;
}
// 校验可见区域、持续时长、脉冲周期及次数上限。
bool UShroomSporeConfig::ValidateSpore(FString& Error) const
{
    if (!Tuning.Validate(Error))
        return false;
    if (!FMath::IsFinite(Duration) || !FMath::IsFinite(Period) || Period < .1f || Duration < Period || Duration > 10 ||
        Duration / Period > 20 || !Tuning.bDrawFootprint)
    {
        Error = TEXT("Spore requires a visible field, valid duration and bounded pulse count");
        return false;
    }
    return true;
}
// 要求孢子配置有效后复用公共敌人激活检查。
bool UShroomSporeAbility::CanActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                             const FGameplayTagContainer* S, const FGameplayTagContainer* T,
                                             FGameplayTagContainer* R) const
{
    const auto* Config = Cast<UShroomSporeConfig>(AttackConfig);
    FString Error;
    return Config && Config->ValidateSpore(Error) && Super::CanActivateAbility(H, I, S, T, R);
}
// 重置区域状态并缓存持续时间、周期，再开始公共预警。
void UShroomSporeAbility::ActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                          FGameplayAbilityActivationInfo A, const FGameplayEventData* E)
{
    bFieldActive = false;
    FieldAction.Reset();
    if (const auto* Config = Cast<UShroomSporeConfig>(AttackConfig))
    {
        FieldDuration = Config->Duration;
        FieldPeriod = Config->Period;
    }
    Super::ActivateAbility(H, I, A, E);
}
// 把预警切为持续危险区域，绑定中断回调并尝试消费首个脉冲。
void UShroomSporeAbility::ResolveHit()
{
    if (!Golem.IsValid() || !Golem->IsAttackCurrent(Serial, Lease))
    {
        Abort();
        return;
    }
    bFieldActive = true;
    Golem->ShowActiveFootprint(Serial, &Center);
    if (!IsActive() || !Golem.IsValid() || !Golem->IsAttackCurrent(Serial, Lease))
    {
        return;
    }
    if (auto* Anim = Golem->GetMesh()->GetAnimInstance())
    {
        const int32 OwnedSerial = Serial, OwnedLease = Lease;
        auto Interrupted = [this, OwnedSerial, OwnedLease](UAnimMontage*, bool bInterrupted)
        {
            if (bInterrupted && IsActive() && Serial == OwnedSerial && Lease == OwnedLease && Golem.IsValid() &&
                Golem->IsAttackCurrent(OwnedSerial, OwnedLease))
                Abort();
        };
        FOnMontageBlendingOutStarted Blend;
        Blend.BindWeakLambda(this, Interrupted);
        Anim->Montage_SetBlendingOutDelegate(Blend, Snapshot.Montage);
        FOnMontageEnded End;
        End.BindWeakLambda(this, Interrupted);
        Anim->Montage_SetEndDelegate(End, Snapshot.Montage);
    }
    FAbilityNodeInput Input;
    FString Error;
    if (!AbilityExecutionIdentity::CaptureSource(Golem.Get(), Input.SourceToken, Error))
    {
        Abort();
        return;
    }
    Input.Origin = Center;
    Input.Direction = FRotator(0, Yaw, 0).Vector();
    Input.DamageParams = MakeDamageEffectParamsFromClassDefaults();
    Input.DamageParams.DamageGameplayEffectClass = SnapshotDamageEffect;
    Input.DamageParams.DamageType = SnapshotDamageType;
    Input.DamageParams.BaseDamage = SnapshotDamage;
    if (SelectedTarget.IsValid())
    {
        FAbilityTargetToken Target;
        if (AbilityExecutionIdentity::CaptureTarget(
                Input.SourceToken, FPureMassCombatTarget::FromActor(SelectedTarget.Get()), Target, Error))
        {
            Input.LiveTargetToken = Target;
        }
    }
    auto* Settings = NewObject<UAbilityEnemySpecialNodeConfig>(this);
    Settings->Kind = EAbilityEnemySpecialKind::Spore;
    Settings->Shape = Snapshot;
    Settings->LockedSurface = LockedSurface;
    Settings->LockedTriangles = LockedTriangles;
    Settings->Duration = FieldDuration;
    Settings->Period = FieldPeriod;
    const int32 ActionSerial = Serial, ActionLease = Lease;
    const TWeakObjectPtr<UShroomSporeAbility> WeakThis(this);
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.IsCurrent = [WeakThis, ActionSerial, ActionLease]()
    {
        auto* Self = WeakThis.Get();
        return Self && Self->IsActive() && Self->Golem.IsValid() &&
               Self->Golem->IsAttackCurrent(ActionSerial, ActionLease) && !Self->Golem->bIsStunned;
    };
    Callbacks.BeforeActorDamage = [WeakThis](AActor* Target)
    {
        if (auto* Self = WeakThis.Get())
        {
            Self->ReportOffensiveAttack(Target);
        }
    };
    FieldAction = MakeShared<FAbilityEnemyAreaAction>();
    if (!FieldAction->Start(Input, *Settings, MoveTemp(Callbacks), GetWorld()->GetTimeSeconds(), Error))
    {
        Abort();
        return;
    }
    const auto Running = FieldAction;
    if (Running->Advance(GetWorld()->GetTimeSeconds()) == EAbilityEnemyActionState::Failed && IsActive())
    {
        Abort();
    }
}
// 区域生效前复用父类时序，生效后检查租约并按时钟结算或到期结束。
void UShroomSporeAbility::Advance()
{
    if (!IsActive())
        return;
    if (!bFieldActive)
    {
        Super::Advance();
        return;
    }
    if (!Golem.IsValid() || !Golem->IsAttackCurrent(Serial, Lease) || Golem->bIsStunned)
    {
        Abort();
        return;
    }
    const auto Running = FieldAction;
    const auto Result = Running ? Running->Advance(GetWorld()->GetTimeSeconds()) : EAbilityEnemyActionState::Failed;
    if (!IsActive())
    {
        return;
    }
    if (Result == EAbilityEnemyActionState::Completed)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    }
    else if (Result == EAbilityEnemyActionState::Failed)
    {
        Abort();
    }
}
// 解除持续区域绑定的动画委托，再清理公共攻击状态。
void UShroomSporeAbility::EndAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                     FGameplayAbilityActivationInfo A, bool R, bool C)
{
    CloseExecutionSession(C);
    if (FieldAction)
    {
        FieldAction->Stop();
        FieldAction.Reset();
    }
    if (bFieldActive && Golem.IsValid() && Golem->IsAttackCurrent(Serial, Lease))
        if (auto* Anim = Golem->GetMesh()->GetAnimInstance())
        {
            FOnMontageBlendingOutStarted Blend;
            FOnMontageEnded End;
            Anim->Montage_SetBlendingOutDelegate(Blend, Snapshot.Montage);
            Anim->Montage_SetEndDelegate(End, Snapshot.Montage);
        }
    bFieldActive = false;
    Super::EndAbility(H, I, A, R, C);
}
