// 主要链路：地下城死亡改为可逆倒地；保存被动能力句柄，复制倒地状态并封锁行动，复活验证能力归属后恢复资源与被动能力。
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Roguelike/DungeonRunDirector.h"
#include "AbilitySystem/Ability/MyPassiveAbility.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MyPlayerState.h"
#include "Terrain/TerrainStatusComponent.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

namespace
{
    // 构造倒地期间需要封锁的输入标签集合。
    FGameplayTagContainer DungeonDownedInputTags()
    {
        const auto& Tags = FMyGameplayTags::Get();
        FGameplayTagContainer Result;
        Result.AddTag(Tags.Player_Block_InputPressed);
        Result.AddTag(Tags.Player_Block_InputHeld);
        Result.AddTag(Tags.Player_Block_InputReleased);
        Result.AddTag(Tags.Player_Block_CursorTrace);
        return Result;
    }
    // 识别需要暂停恢复的被动能力，排除输入切换型能力。
    bool IsDungeonPassive(const FGameplayAbilitySpec& Spec)
    {
        const auto* Ability = Cast<UMyGameplayAbility>(Spec.Ability);
        if (Ability && Ability->IsInputToggleAbility())
            return false;
        return Spec.Ability &&
               (Spec.Ability->IsA<UMyPassiveAbility>() ||
                Spec.Ability->GetAssetTags().HasTag(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Passive"))));
    }
    // 构造倒地期间统一封锁的能力根标签。
    FGameplayTagContainer DungeonDownedAbilityTags()
    {
        return FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Abilities")));
    }
    // 只移除负面效果，保留基础属性、有益增益和冷却。
    void ClearDungeonDebuffs(UAbilitySystemComponent* ASC)
    {
        if (!ASC)
            return;
        // Preserve attribute initialization, beneficial buffs and cooldowns; only project debuffs expire on down.
        const FGameplayTagContainer Debuffs(FGameplayTag::RequestGameplayTag(TEXT("Debuff")));
        ASC->RemoveActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Debuffs));
    }
} // namespace

// 服务器将死亡转为倒地，缓存活动被动能力并通知局内导演。
void ADungeonPlayerCharacter::Die(const FVector& DeathImpulse)
{
    if (!HasAuthority() || GetNetMode() == NM_Client || bDungeonDowned || bDead)
        return;
    PausedPassiveAbilities.Reset();
    if (AbilitySystemComponent)
    {
        FScopedAbilityListLock Lock(*AbilitySystemComponent);
        for (const auto& Spec : AbilitySystemComponent->GetActivatableAbilities())
            if (Spec.IsActive() && IsDungeonPassive(Spec))
                PausedPassiveAbilities.Add(Spec.Handle);
    }
    // Never enter either inherited death path: both permanently dismantle the pawn or its loadout.
    GetWorldTimerManager().ClearTimer(DeathTimer);
    bDungeonDowned = true;
    OnRep_Downed();
    ForceNetUpdate();
    if (auto* Director = ADungeonRunDirector::Find(GetWorld()))
        Director->NotifyPlayerDown(this);
}

// 服务器验证复活位置、比例及能力归属，清除残留伤害并恢复资源与已装备被动。
bool ADungeonPlayerCharacter::ReviveForDungeon(FVector SafeLocation, float Fraction)
{
    if (!HasAuthority() || GetNetMode() == NM_Client || !bDungeonDowned || !FMath::IsFinite(Fraction) ||
        Fraction <= 0.f || Fraction > 1.f || SafeLocation.ContainsNaN())
        return false;
    auto* State = GetPlayerState<AMyPlayerState>();
    auto* ASC = GetAbilitySystemComponent();
    if (!State || !ASC || ASC != State->GetAbilitySystemComponent() || ASC->GetOwnerActor() != State ||
        ASC->GetAvatarActor() != this)
        return false;
    const float MaxHealth = ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute());
    const float MaxMana = ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute());
    if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.f || !FMath::IsFinite(MaxMana) || MaxMana < 0.f)
        return false;
    if (!SetActorLocation(SafeLocation, false, nullptr, ETeleportType::TeleportPhysics))
        return false;
    // A fatal hit can add its debuff after Die returns. Also discard any instant/periodic
    // IncomingDamage left by the shared AttributeSet's dead-target early return, while still dead.
    ClearDungeonDebuffs(ASC);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetIncomingDamageAttribute(), 0.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),
                                 FMath::Clamp(MaxHealth * Fraction, 0.f, MaxHealth));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), FMath::Clamp(MaxMana * Fraction, 0.f, MaxMana));
    bDungeonDowned = false;
    OnRep_Downed();
    // 只恢复倒地前活动且仍处于装备状态的被动能力，避免误激活其他技能。
    const auto Resume = MoveTemp(PausedPassiveAbilities);
    for (const auto Handle : Resume)
        if (const auto* Spec = ASC->FindAbilitySpecFromHandle(Handle);
            Spec && IsDungeonPassive(*Spec) && !Spec->IsActive() &&
            Spec->DynamicAbilityTags.HasTagExact(FMyGameplayTags::Get().Abilities_Status_Equipped))
            ASC->TryActivateAbility(Handle);
    ForceNetUpdate();
    return true;
}

// 成对迁移倒地输入标签和能力封锁，避免旧能力系统残留门禁。
void ADungeonPlayerCharacter::UpdateDownedInputBlock()
{
    UAbilitySystemComponent* Desired = bDungeonDowned ? AbilitySystemComponent.Get() : nullptr;
    if (DownedInputASC.Get() == Desired)
        return;
    if (auto* Previous = DownedInputASC.Get())
    {
        Previous->RemoveLooseGameplayTags(DungeonDownedInputTags());
        Previous->UnBlockAbilitiesWithTags(DungeonDownedAbilityTags());
    }
    DownedInputASC = Desired;
    if (Desired)
    {
        Desired->AddLooseGameplayTags(DungeonDownedInputTags());
        // Equip/UI RPCs and direct GAS activation must obey the same downed gate as keyboard input.
        Desired->BlockAbilitiesWithTags(DungeonDownedAbilityTags());
    }
}

// 幂等应用倒地碰撞、移动和可见性；倒地取消能力，起身恢复行走。
void ADungeonPlayerCharacter::OnRep_Downed()
{
    bDead = bDungeonDowned;
    UpdateDownedInputBlock();
    // 输入绑定可晚于状态复制；先补门禁，再跳过重复的碰撞与表现切换。
    if (bAppliedDowned == bDungeonDowned)
        return;
    bAppliedDowned = bDungeonDowned;
    if (bDungeonDowned)
    {
        LivingCapsuleCollision = GetCapsuleComponent()->GetCollisionEnabled();
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        if (AbilitySystemComponent)
            AbilitySystemComponent->CancelAllAbilities();
        if (TerrainStatus)
            TerrainStatus->ClearTerrainEffects();
        if (HasAuthority())
            ClearDungeonDebuffs(AbilitySystemComponent);
    }
    else
    {
        GetCapsuleComponent()->SetCollisionEnabled(LivingCapsuleCollision);
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        RefreshMovementSpeed();
    }
    GetMesh()->SetVisibility(!bDungeonDowned, true);
    if (Weapon)
        Weapon->SetVisibility(!bDungeonDowned, true);
    if (bDungeonDowned)
        FOnDeath.Broadcast(this);
}

// 完成父类绑定后补应用倒地门禁，兼容复制先于能力系统就绪。
void ADungeonPlayerCharacter::InitAbilityActorInfo()
{
    Super::InitAbilityActorInfo();
    // Downed replication can arrive before the owning PlayerState/ASC.
    UpdateDownedInputBlock();
    if (bDungeonDowned && AbilitySystemComponent)
        AbilitySystemComponent->CancelAllAbilities();
}

// 移除自身持有的倒地标签与能力封锁，再执行父类清理。
void ADungeonPlayerCharacter::EndPlay(EEndPlayReason::Type Reason)
{
    if (auto* ASC = DownedInputASC.Get())
    {
        ASC->RemoveLooseGameplayTags(DungeonDownedInputTags());
        ASC->UnBlockAbilitiesWithTags(DungeonDownedAbilityTags());
    }
    DownedInputASC.Reset();
    Super::EndPlay(Reason);
}
// 注册本类需要复制的状态，同时保留父类复制字段。
void ADungeonPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADungeonPlayerCharacter, bDungeonDowned);
}
