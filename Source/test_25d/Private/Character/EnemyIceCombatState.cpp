// 调用链：GAS 承伤 → SetIceCombatState → 服务器取消行动和计时 → RepNotify 更新速度、姿势和冰霜表现。
// 到期、死亡和池回收共用 Clear；状态与租约一起复制，客户端只呈现当前生命。
#include "Character/EnemyCharacter.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BrainComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

EAbilityExposure AEnemyCharacter::GetActiveIceCombatState() const
{
    const bool bLiveLease = !bDead && !IsDungeonPoolSleeping() && IceCombatState.Lease == GetDungeonPoolLease();
    return bLiveLease ? IceCombatState.Exposure : EAbilityExposure::None;
}

bool AEnemyCharacter::IsIceFrozen() const
{
    return GetActiveIceCombatState() == EAbilityExposure::Frozen;
}

float AEnemyCharacter::GetIceAttackRate() const
{
    return GetActiveIceCombatState() == EAbilityExposure::Chilled ? AbilityElements::ChillAttackRate : 1.f;
}

float AEnemyCharacter::GetIceMovementRate() const
{
    return GetActiveIceCombatState() == EAbilityExposure::Chilled ? AbilityElements::ChillMoveRate : 1.f;
}

void AEnemyCharacter::SetIceCombatState(EAbilityExposure Exposure, float Duration)
{
    if (!HasAuthority() || bDead || IsDungeonPoolSleeping())
    {
        return;
    }
    if ((Exposure != EAbilityExposure::Chilled && Exposure != EAbilityExposure::Frozen) || !FMath::IsFinite(Duration) ||
        Duration <= 0.f)
    {
        if (IceCombatState.Exposure != EAbilityExposure::None)
        {
            ClearIceCombatState();
        }
        return;
    }
    const bool bEnteringFreeze = Exposure == EAbilityExposure::Frozen && !IsIceFrozen();
    const int32 Lease = GetDungeonPoolLease();
    IceCombatState = {Exposure, Lease};
    GetWorldTimerManager().SetTimer(IceExpiry, this, &AEnemyCharacter::ClearIceCombatState, Duration, false);
    OnRep_IceCombatState();
    if (bEnteringFreeze)
    {
        // 先让可取消 GA 正常收尾并撤销移动声明，再作废旧攻击代次及由 AI 发起的移动。
        AbilitySystemComponent->CancelAllAbilities();
        if (!IsValid(this) || Lease != GetDungeonPoolLease() || !IsIceFrozen())
        {
            return;
        }
        InvalidateEnemyAttack();
        auto* Movement = GetCharacterMovement();
        PreFreezeMovementMode.Emplace(Movement->MovementMode, Movement->CustomMovementMode);
        Movement->StopMovementImmediately();
        Movement->ClearAccumulatedForces();
        Movement->DisableMovement();
        if (auto* AI = Cast<AAIController>(GetController()))
        {
            AI->StopMovement();
            AI->ClearFocus(EAIFocusPriority::Gameplay);
            if (auto* Brain = AI->GetBrainComponent(); Brain && Brain->IsRunning() && !Brain->IsPaused())
            {
                IcePausedBrain = Brain;
                Brain->PauseLogic(TEXT("Ice freeze"));
            }
        }
    }
    ForceNetUpdate();
}

void AEnemyCharacter::ClearIceCombatState()
{
    if (HasAuthority())
    {
        GetWorldTimerManager().ClearTimer(IceExpiry);
        IceCombatState = {};
        const bool bAlive = !bDead && !IsDungeonPoolSleeping() && !IsActorBeingDestroyed();
        if (PreFreezeMovementMode && bAlive && GetCharacterMovement()->MovementMode == MOVE_None)
        {
            GetCharacterMovement()->SetMovementMode(PreFreezeMovementMode->Key, PreFreezeMovementMode->Value);
        }
        PreFreezeMovementMode.Reset();
        auto* AI = Cast<AAIController>(GetController());
        if (auto* Brain = IcePausedBrain.Get(); bAlive && AI && AI->GetBrainComponent() == Brain && Brain->IsPaused())
        {
            Brain->ResumeLogic(TEXT("Ice thaw"));
        }
        IcePausedBrain.Reset();
        ForceNetUpdate();
    }
    OnRep_IceCombatState();
}

void AEnemyCharacter::OnRep_IceCombatState()
{
    RefreshMovementSpeed();
    const auto State = GetActiveIceCombatState();
    auto* EnemyMesh = GetMesh();
    if (State == EAbilityExposure::Frozen && !PreFreezeAnimRate)
    {
        // 石化单独持有 bPauseAnims，解冻只恢复冰系自己修改的参数。
        PreFreezeAnimRate = EnemyMesh->GlobalAnimRateScale;
        EnemyMesh->GlobalAnimRateScale = 0.f;
    }
    else if (State != EAbilityExposure::Frozen && PreFreezeAnimRate)
    {
        EnemyMesh->GlobalAnimRateScale = PreFreezeAnimRate.GetValue();
        PreFreezeAnimRate.Reset();
    }
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    if (State == EAbilityExposure::None)
    {
        if (IceVisual)
        {
            IceVisual->DestroyComponent();
            IceVisual = nullptr;
        }
        return;
    }
    if (!IceVisual)
    {
        if (auto* System = AbilityElements::GetFrostVisual())
        {
            IceVisual = UNiagaraFunctionLibrary::SpawnSystemAttached(System, GetRootComponent(), NAME_None,
                                                                     FVector::ZeroVector, FRotator::ZeroRotator,
                                                                     EAttachLocation::KeepRelativeOffset, false);
        }
    }
    if (IceVisual)
    {
        IceVisual->SetRelativeScale3D(FVector(State == EAbilityExposure::Frozen ? 1.5f : 1.f));
    }
}
