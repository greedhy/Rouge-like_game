// 调用链：生效阶段 → 共享角色表现；护甲 GE 创建/移除 → 租约复制 → 本地护甲光效。
#include "Character/GruxEnemyCharacter.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Character/CombatPresentationComponent.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GruxArmor, "Data.Grux.RoarArmor");

namespace
{
    const FName AttackSlot(TEXT("Attack"));
    const FName RoarArmorSlot(TEXT("RoarArmor"));
} // namespace

UGruxRoarArmorEffect::UGruxRoarArmorEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(6);
    FGameplayModifierInfo Modifier;
    Modifier.Attribute = UMyAttributeSet::GetArmorAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    FSetByCallerFloat Magnitude;
    Magnitude.DataTag = TAG_GruxArmor;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
    Modifiers.Add(Modifier);
}

// 蓝图类定义资源档位。实例只在有表现的世界中随角色建立一次。
void AGruxEnemyCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    if (CombatPresentationClass && GetNetMode() != NM_DedicatedServer)
    {
        CombatPresentation = NewObject<UCombatPresentationComponent>(this, CombatPresentationClass);
        AddInstanceComponent(CombatPresentation);
        CombatPresentation->RegisterComponent();
    }
}

void AGruxEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AGruxEnemyCharacter, RoarPresentationLease);
}

bool AGruxEnemyCharacter::HasRoarArmor() const
{
    return RoarArmor.IsValid() && AbilitySystemComponent &&
           AbilitySystemComponent->GetActiveGameplayEffect(RoarArmor) != nullptr;
}

// 只有成功创建且仍属于当前生命的 GE 才开启护甲表现。
FActiveGameplayEffectHandle AGruxEnemyCharacter::ApplyRoarArmor(float Armor, float Duration)
{
    if (!HasAuthority() || bDead || IsDungeonPoolSleeping() || HasRoarArmor() || !FMath::IsFinite(Armor) ||
        !FMath::IsFinite(Duration) || Armor <= 0 || Duration <= 0 || Duration > 30)
    {
        return {};
    }
    auto Spec = AbilitySystemComponent->MakeOutgoingSpec(UGruxRoarArmorEffect::StaticClass(), 1,
                                                         AbilitySystemComponent->MakeEffectContext());
    if (!Spec.IsValid())
    {
        return {};
    }
    Spec.Data->SetDuration(Duration, true);
    Spec.Data->SetSetByCallerMagnitude(TAG_GruxArmor, Armor);
    const int32 Lease = GetDungeonPoolLease();
    const auto Applied = AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    if (bDead || IsDungeonPoolSleeping() || GetDungeonPoolLease() != Lease)
    {
        AbilitySystemComponent->RemoveActiveGameplayEffect(Applied);
        return {};
    }
    auto* RemovalDelegate = AbilitySystemComponent->OnGameplayEffectRemoved_InfoDelegate(Applied);
    if (!RemovalDelegate)
    {
        AbilitySystemComponent->RemoveActiveGameplayEffect(Applied);
        return {};
    }
    RoarArmor = Applied;
    RemovalDelegate->AddUObject(this, &ThisClass::OnRoarArmorRemoved, Applied);
    SetRoarPresentationLease(Lease);
    return RoarArmor;
}

// 预先清空旧句柄，让同步删除委托无法撤销之后建立的状态。
void AGruxEnemyCharacter::RemoveRoarArmor(FActiveGameplayEffectHandle Expected)
{
    if (!HasAuthority() || !Expected.IsValid() || Expected != RoarArmor)
    {
        return;
    }
    const auto Removed = RoarArmor;
    RoarArmor.Invalidate();
    SetRoarPresentationLease(INDEX_NONE);
    AbilitySystemComponent->RemoveActiveGameplayEffect(Removed);
}

// 同一个出口处理自然到期和 ASC 外部移除。
void AGruxEnemyCharacter::OnRoarArmorRemoved(const FGameplayEffectRemovalInfo&, FActiveGameplayEffectHandle Expected)
{
    if (RoarArmor == Expected)
    {
        RoarArmor.Invalidate();
        SetRoarPresentationLease(INDEX_NONE);
    }
}

void AGruxEnemyCharacter::SetRoarPresentationLease(int32 Lease)
{
    RoarPresentationLease = Lease;
    OnRep_RoarPresentation();
    ForceNetUpdate();
}

// 远端可能先收到 GE 表现租约，后收到池生命。池回调可重新应用同一状态。
void AGruxEnemyCharacter::OnRep_RoarPresentation()
{
    if (!CombatPresentation)
    {
        return;
    }
    if (bDead || IsDungeonPoolSleeping() || RoarPresentationLease == INDEX_NONE ||
        RoarPresentationLease != GetDungeonPoolLease())
    {
        CombatPresentation->StopPresentation(RoarArmorSlot);
    }
    else if (!CombatPresentation->HasPresentation(RoarArmorSlot))
    {
        CombatPresentation->PlayPresentation(RoarArmorSlot, RoarArmorSlot, GetActorTransform(), GetMesh());
    }
}

void AGruxEnemyCharacter::ReconcileEnemyPresentation()
{
    Super::ReconcileEnemyPresentation();
    OnRep_RoarPresentation();
}

bool AGruxEnemyCharacter::PlayAttackPresentation(const FStoneGolemPresentation& State)
{
    const FTransform Pose(FRotator(0.f, State.Yaw, 0.f), State.Impact);
    return CombatPresentation &&
           CombatPresentation->PlayPresentation(AttackSlot, State.Tuning.ImpactPresentationPreset, Pose, GetMesh());
}

void AGruxEnemyCharacter::StopAttackPresentation()
{
    if (CombatPresentation)
    {
        CombatPresentation->StopPresentation(AttackSlot);
    }
}

// 死亡、打断及休眠从现有攻击生命周期进入，主动停止本条命的本地组件。
void AGruxEnemyCharacter::InvalidateEnemyAttack()
{
    RemoveRoarArmor(RoarArmor);
    if (CombatPresentation)
    {
        CombatPresentation->StopAllPresentations();
    }
    Super::InvalidateEnemyAttack();
}

void AGruxEnemyCharacter::EndPlay(EEndPlayReason::Type Reason)
{
    RemoveRoarArmor(RoarArmor);
    if (CombatPresentation)
    {
        CombatPresentation->StopAllPresentations();
    }
    Super::EndPlay(Reason);
}
