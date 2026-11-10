/**
 * 调用链：控制器输入 -> AbilityInputTagPressed/Held/Released -> GAS激活/输入事件；技能菜单 -> ServerEquipAbility/ServerSpendSpellPoint -> 修改Spec -> 标记复制并向所属客户端发UI通知。
 * 同步：技能Spec由GAS管理复制，ClientEquipAbility/ClientUpdateAbilityStatus是UI事件通知，不能替代Spec复制；OnRep_ActivateAbilities等待能力类引用可用后广播就绪。ServerOnly能力不能用客户端Spec.IsActive来决定服务端是否执行切换。
 * 生命周期：组件属于PlayerState，InitAbilityActorInfo把当前Pawn绑定为Avatar。永久学习、装备临时授予、持续激活是不同生命周期；装备能力只按其授予句柄回收，旅行存档不应永久记录装备专属Spec。回调绑定与ActorInfo重建必须由角色初始化入口协调，避免重生重复订阅。
 */
// 主要调用链：玩家控制器输入/技能菜单RPC/角色初始化 → ASC授予、激活、换槽与恢复 → GAS能力执行及UI委托；装备专属能力由EquipmentComponent管理。
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/MyPlayerState.h"
// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystemBlueprintLibrary.h"
#include "MyAbilityTypes.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Interface/CombatInterface.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "Game/LoadScreenSaveGame.h"
#include "Interface/PlayerInterface.h"
#include "test_25d/MyLogChannels.h"
#include "AbilitySystem/Ability/StanderAbility.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Net/UnrealNetwork.h"

// Simulated proxies still receive GAS's normal montage state. This is the missing owner view.
void UMyAbilitySystemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UMyAbilitySystemComponent, StanderOwnerMontage, COND_OwnerOnly);
}

// Mirror the authoritative montage clock, including timeline rate changes and interrupted stops.
void UMyAbilitySystemComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
    Super::PreReplication(ChangedPropertyTracker);
    const auto* Ability = GetAnimatingAbility();
    const auto& Rep = GetRepAnimMontageInfo();
    auto* Montage = Cast<UAnimMontage>(Rep.Animation);
    if (Ability && Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::ServerOnly &&
        (Ability->IsA<UStanderWeaponActionAbility>() || Ability->IsA<UStanderAbility>()) && Montage &&
        GetAvatarActor())
    {
        StanderOwnerMontage.Avatar = GetAvatarActor();
        StanderOwnerMontage.Montage = Montage;
        StanderOwnerMontage.PlayInstanceId = Rep.PlayInstanceId;
        StanderOwnerMontage.Position = Rep.Position;
        StanderOwnerMontage.Rate = Rep.PlayRate;
        StanderOwnerMontage.BlendTime = Rep.BlendTime;
        StanderOwnerMontage.bStopped = Rep.IsStopped;
    }
    else if (!StanderOwnerMontage.bStopped)
    {
        // GAS may clear AnimatingAbility as part of ending the GA before this replication frame.
        StanderOwnerMontage.bStopped = true;
        StanderOwnerMontage.BlendTime = Rep.Animation == StanderOwnerMontage.Montage && Rep.IsStopped
                                           ? Rep.BlendTime
                                           : .08f;
    }
}

void UMyAbilitySystemComponent::OnRep_StanderOwnerMontage()
{
    RefreshStanderOwnerMontage();
}

// Presentation only: no ability activation, ASC montage ownership, cost, or gameplay dispatch.
void UMyAbilitySystemComponent::RefreshStanderOwnerMontage()
{
    if (!AbilityActorInfo.IsValid() || AbilityActorInfo->IsNetAuthority() ||
        !AbilityActorInfo->IsLocallyControlled())
    {
        return;
    }
    const auto& State = StanderOwnerMontage;
    auto* Anim = AbilityActorInfo->GetAnimInstance();
    auto* PreviousAnim = OwnerPlaybackAnim.Get();
    auto* PreviousMontage = OwnerPlaybackMontage.Get();
    const auto* PreviousInstance = PreviousAnim && PreviousMontage
                                       ? PreviousAnim->GetActiveInstanceForMontage(PreviousMontage)
                                       : nullptr;
    const bool bOwnsPrevious = PreviousInstance && PreviousInstance->GetInstanceID() == OwnerPlaybackInstance;
    if (State.bStopped || State.Avatar != GetAvatarActor() || !Anim || !State.Montage)
    {
        if (bOwnsPrevious)
        {
            PreviousAnim->Montage_Stop(FMath::Max(0.f, State.BlendTime), PreviousMontage);
        }
        OwnerPlaybackInstance = INDEX_NONE;
        OwnerPlaybackAnim.Reset();
        OwnerPlaybackMontage.Reset();
        return;
    }

    const bool bNewPlay = PreviousAnim != Anim || PreviousMontage != State.Montage ||
                          OwnerPlaybackId != State.PlayInstanceId;
    if (bNewPlay)
    {
        if (bOwnsPrevious && (PreviousAnim != Anim || PreviousMontage != State.Montage))
        {
            PreviousAnim->Montage_Stop(.08f, PreviousMontage);
        }
        if (Anim->Montage_Play(State.Montage, State.Rate, EMontagePlayReturnType::MontageLength,
                               State.Position, false) <= 0.f)
        {
            return;
        }
        const auto* Instance = Anim->GetActiveInstanceForMontage(State.Montage);
        OwnerPlaybackAnim = Anim;
        OwnerPlaybackMontage = State.Montage;
        OwnerPlaybackId = State.PlayInstanceId;
        OwnerPlaybackInstance = Instance ? Instance->GetInstanceID() : INDEX_NONE;
        return;
    }
    if (bOwnsPrevious)
    {
        Anim->Montage_SetPlayRate(State.Montage, State.Rate);
        if (FMath::Abs(Anim->Montage_GetPosition(State.Montage) - State.Position) > .1f)
        {
            Anim->Montage_SetPosition(State.Montage, State.Position);
        }
    }
}


// 绑定自身效果应用委托，交给客户端回调提取资产标签。
void UMyAbilitySystemComponent::AbilityActorInfoSet()
{
    OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &UMyAbilitySystemComponent::ClientEffectApplied);
}

// 提取效果资产标签并广播给订阅者。
void UMyAbilitySystemComponent::ClientEffectApplied_Implementation(UAbilitySystemComponent* AbilitySystemComponent,
                                                                   const FGameplayEffectSpec& EffectSpec,
                                                                   FActiveGameplayEffectHandle ActiveEffectHandle)
{
    FGameplayTagContainer TagContainer;
    EffectSpec.GetAllAssetTags(TagContainer);

    EffectAssectTags.Broadcast(TagContainer);
}
// 授予初始主动技能，写入输入与装备标签，最后广播就绪。
void UMyAbilitySystemComponent::AddCharacterAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupAbilities)
{
    for (const TSubclassOf<UGameplayAbility> AbilityClass : StartupAbilities)
    {
        FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, 1);
        if (const UMyGameplayAbility* MyAbility = Cast<UMyGameplayAbility>(AbilitySpec.Ability))
        {
            AbilitySpec.DynamicAbilityTags.AddTag(MyAbility->StartupInputTag);
            AbilitySpec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
            GiveAbility(AbilitySpec);
        }
    }
    bStartupAbilityGiven = true;
    AbilitiesGiven.Broadcast();
}

// 授予初始被动技能并立即尝试激活。
void UMyAbilitySystemComponent::AddCharacterPassiveAbilities(
    const TArray<TSubclassOf<UGameplayAbility>>& StartupPassiveAbilities)
{
    for (const TSubclassOf<UGameplayAbility> AbilityClass : StartupPassiveAbilities)
    {
        FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, 1);
        AbilitySpec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
        const FGameplayAbilitySpecHandle Handle = GiveAbility(AbilitySpec);
        TryActivateAbility(Handle);
    }
}

// 跳过开关技能，对匹配输入标签的普通技能尝试激活。
void UMyAbilitySystemComponent::AbilityInputTagHeld(const FGameplayTag& InputTag)
{
    if (!InputTag.IsValid())
        return;
    FScopedAbilityListLock ActiveScopeLoc(*this);
    for (auto& AbilitySpec : GetActivatableAbilities())
    {
        if (AbilitySpec.DynamicAbilityTags.HasTagExact(InputTag))
        {
            const auto* Ability = Cast<UMyGameplayAbility>(AbilitySpec.Ability);
            if (Ability && Ability->IsInputToggleAbility())
                continue;
            AbilitySpecInputPressed(AbilitySpec);
            if (!AbilitySpec.IsActive())
            {
                TryActivateAbility(AbilitySpec.Handle);
            }
        }
    }
}

// 锁定技能列表并执行外部遍历委托。
void UMyAbilitySystemComponent::ForEachAbility(const FForEachAbility& Delegate)
{
    FScopedAbilityListLock ActiveScopeLock(*this);
    for (const FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
    {
        if (!Delegate.ExecuteIfBound(AbilitySpec))
        {
            UE_LOG(LogMyChannel, Error, TEXT("Failed to execute delegate in %hs"), __FUNCTION__);
        }
    }
}
FGameplayTag UMyAbilitySystemComponent::GetAbilityTagFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
    if (AbilitySpec.Ability)
    {
        for (FGameplayTag Tag :
             AbilitySpec.Ability.Get()
                 ->AbilityTags) //假定只含有一个包含Abilities的标签，目前游戏中包含3个 攻击、召唤、法术
        {
            if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities"))))
            {
                return Tag;
            }
        }
    }
    return FGameplayTag();
}

FGameplayTag UMyAbilitySystemComponent::GetInputTagFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
    for (FGameplayTag Tag : AbilitySpec.DynamicAbilityTags)
    {
        if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("InputTag"))))
        {
            return Tag;
        }
    }
    return FGameplayTag();
}

FGameplayTag UMyAbilitySystemComponent::GetStatusFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
    for (FGameplayTag StatusTag : AbilitySpec.DynamicAbilityTags)
    {
        if (StatusTag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities.Status"))))
        {
            return StatusTag;
        }
    }
    return FGameplayTag();
}

void UMyAbilitySystemComponent::UpgradeAttribute(const FGameplayTag& AttributeTag)
{
    if (GetAvatarActor()->Implements<UPlayerInterface>())
    {
        if (IPlayerInterface::Execute_GetAttributePoints(GetAvatarActor()) > 0)
        {
            ServerUpgradeAttribute(AttributeTag);
        }
    }
}

void UMyAbilitySystemComponent::ServerUpgradeAttribute_Implementation(const FGameplayTag& AttributeTag)
{
    FGameplayEventData Payload;
    Payload.EventTag = AttributeTag;
    Payload.EventMagnitude = 1.f;

    UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(GetAvatarActor(), AttributeTag, Payload);

    if (GetAvatarActor()->Implements<UPlayerInterface>())
    {
        IPlayerInterface::Execute_AddToAttributePoints(GetAvatarActor(), -1);
    }
}

void UMyAbilitySystemComponent::OnRep_ActivateAbilities()
{
    Super::OnRep_ActivateAbilities();
    // GAS retries OnRep when a spec's ability reference is not mapped yet.
    // Do not mark that incomplete list ready, or suppress the later retry/update.
    for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
    {
        if (!Spec.Ability)
            return;
    }
    bStartupAbilityGiven = true;
    AbilitiesGiven.Broadcast();
}
FGameplayAbilitySpec* UMyAbilitySystemComponent::GetSpecFromAbilityTag(const FGameplayTag& AbilityTag)
{
    FScopedAbilityListLock ActiveScopeLoc(*this);
    for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
    {
        for (FGameplayTag Tag : AbilitySpec.Ability.Get()->AbilityTags)
        {
            if (Tag.MatchesTag(AbilityTag))
            {
                return &AbilitySpec;
            }
        }
    }
    return nullptr;
}
void UMyAbilitySystemComponent::ClientUpdateAbilityStatus_Implementation(const FGameplayTag& AbilityTag,
                                                                         const FGameplayTag& StatusTag,
                                                                         int32 AbilityLevel)
{
    if (FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(AbilityTag))
    {
        // ✅ 同步等级
        Spec->Level = AbilityLevel;

        // ✅ 同步状态标签
        const FMyGameplayTags& Tags = FMyGameplayTags::Get();
        Spec->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Eligible);
        Spec->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Unlocked);
        Spec->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Equipped);
        Spec->DynamicAbilityTags.AddTag(StatusTag);
    }
    AbilityStatusChanged.Broadcast(AbilityTag, StatusTag, AbilityLevel);
}
void UMyAbilitySystemComponent::UpdateAbilityStatuses(int32 Level)
{
    UAbilityInfo* AbilityInfo = UMyAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
    for (const FMyAbilityInfo& Info : AbilityInfo->AbilityInformation)
    {
        if (!Info.AbilityTag.IsValid())
            continue;
        const auto* CatalogAbility = Info.Ability ? Cast<UMyGameplayAbility>(Info.Ability.GetDefaultObject()) : nullptr;
        if (CatalogAbility && CatalogAbility->IsEquipmentGrantedOnly())
            continue;
        if (Level < Info.LevelRequirement)
            continue;
        if (GetSpecFromAbilityTag(Info.AbilityTag) == nullptr)
        {
            FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(Info.Ability, 1);
            AbilitySpec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Eligible);
            GiveAbility(AbilitySpec);
            MarkAbilitySpecDirty(AbilitySpec);
            ClientUpdateAbilityStatus(Info.AbilityTag, FMyGameplayTags::Get().Abilities_Status_Eligible, 1);
        }
    }
}
// 排除装备专属技能，消耗点数解锁或升级并同步等级状态。
void UMyAbilitySystemComponent::ServerSpendSpellPoint_Implementation(const FGameplayTag& AbilityTag)
{
    if (auto* State = Cast<AMyPlayerState>(GetOwnerActor()))
        if (auto* Spec = GetSpecFromAbilityTag(AbilityTag);
            Spec && State->GetEquipmentComponent()->IsWeaponAbility(Spec->Handle))
            return;
    if (FGameplayAbilitySpec* AbilitySpec = GetSpecFromAbilityTag(AbilityTag))
    {
        if (const auto* Ability = Cast<UMyGameplayAbility>(AbilitySpec->Ability);
            Ability && Ability->IsEquipmentGrantedOnly())
            return;
        if (GetAvatarActor()->Implements<UPlayerInterface>())
        {
            IPlayerInterface::Execute_AddToSpellPoints(GetAvatarActor(), -1);
        }

        const FMyGameplayTags GameplayTags = FMyGameplayTags::Get();
        FGameplayTag Status = GetStatusFromSpec(*AbilitySpec);
        if (Status.MatchesTagExact(GameplayTags.Abilities_Status_Eligible))
        {
            AbilitySpec->DynamicAbilityTags.RemoveTag(GameplayTags.Abilities_Status_Eligible);
            AbilitySpec->DynamicAbilityTags.AddTag(GameplayTags.Abilities_Status_Unlocked);
            Status = GameplayTags.Abilities_Status_Unlocked;
        }
        else if (Status.MatchesTagExact(GameplayTags.Abilities_Status_Equipped) ||
                 Status.MatchesTagExact(GameplayTags.Abilities_Status_Unlocked))
        {
            AbilitySpec->Level += 1;
        }
        ClientUpdateAbilityStatus(AbilityTag, Status, AbilitySpec->Level);
        // Metadata must reach the owner even for ServerOnly abilities; the default
        // dirty path skips their existing specs as an activation-state optimization.
        MarkAbilitySpecDirty(*AbilitySpec, true);
    }
}
bool UMyAbilitySystemComponent::GetDescriptionsByAbilityTag(const FGameplayTag& AbilityTag, FString& OutDescription,
                                                            FString& OutNextLevelDescription)
{
    if (const FGameplayAbilitySpec* AbilitySpec = GetSpecFromAbilityTag(AbilityTag))
    {
        if (UMyGameplayAbility* MyAbility = Cast<UMyGameplayAbility>(AbilitySpec->Ability))
        {
            OutDescription = MyAbility->GetDescription(AbilitySpec->Level);
            OutNextLevelDescription = MyAbility->GetNextLevelDescription(AbilitySpec->Level + 1);
            return true;
        }
    }
    const UAbilityInfo* AbilityInfo = UMyAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
    if (!AbilityTag.IsValid() || AbilityTag.MatchesTagExact(FMyGameplayTags::Get().Abilities_None))
    {
        OutDescription = FString();
    }
    else
    {
        OutDescription =
            UMyGameplayAbility::GetLockedDescription(AbilityInfo->FindAbilityInfoForTag(AbilityTag).LevelRequirement);
    }
    OutNextLevelDescription = FString();
    return false;
}
FGameplayTag UMyAbilitySystemComponent::GetStatusFromAbilityTag(const FGameplayTag& AbilityTag)
{
    if (const FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(AbilityTag))
    {
        return GetStatusFromSpec(*Spec);
    }
    return FGameplayTag();
}
FGameplayTag UMyAbilitySystemComponent::GetSlotFromAbilityTag(const FGameplayTag& AbilityTag)
{
    if (const FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(AbilityTag))
    {
        return GetInputTagFromSpec(*Spec);
    }
    return FGameplayTag();
}
void UMyAbilitySystemComponent::ClearSlot(FGameplayAbilitySpec* Spec)
{
    const FGameplayTag Slot = GetInputTagFromSpec(*Spec);
    Spec->DynamicAbilityTags.RemoveTag(Slot);
}
bool UMyAbilitySystemComponent::AbilityHasSlot(const FGameplayAbilitySpec& Spec, const FGameplayTag& Slot)
{
    return Spec.DynamicAbilityTags.HasTagExact(Slot);
}
bool UMyAbilitySystemComponent::AbilityHasSlot(FGameplayAbilitySpec* Spec, const FGameplayTag& Slot)
{
    for (FGameplayTag Tag : Spec->DynamicAbilityTags)
    {
        if (Tag.MatchesTagExact(Slot))
        {
            return true;
        }
    }
    return false;
}
bool UMyAbilitySystemComponent::SlotIsEmpty(const FGameplayTag& Slot)
{
    FScopedAbilityListLock ActiveScopeLoc(*this);
    for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
    {
        if (AbilityHasSlot(AbilitySpec, Slot))
        {
            return false;
        }
    }
    return true;
}
void UMyAbilitySystemComponent::ClearAbilitiesOfSlot(const FGameplayTag& Slot)
{
    FScopedAbilityListLock ActiveScopeLock(*this);
    for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
    {
        if (AbilityHasSlot(&Spec, Slot))
        {
            ClearSlot(&Spec);
        }
    }
}
FGameplayAbilitySpec* UMyAbilitySystemComponent::GetSpecWithSlot(const FGameplayTag& Slot)
{
    FScopedAbilityListLock ActiveScopeLock(*this);
    for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
    {
        if (AbilitySpec.DynamicAbilityTags.HasTagExact(Slot))
        {
            return &AbilitySpec;
        }
    }
    return nullptr;
}
void UMyAbilitySystemComponent::ClientEquipAbility_Implementation(const FGameplayTag& AbilityTag,
                                                                  const FGameplayTag& Status, const FGameplayTag& Slot,
                                                                  const FGameplayTag& PreviousSlot)
{
    AbilityEquipped.Broadcast(AbilityTag, Status, Slot, PreviousSlot);
}
// 校验槽位资格，处理原占用技能与被动激活后同步装备结果。
void UMyAbilitySystemComponent::ServerEquipAbility_Implementation(const FGameplayTag& AbilityTag,
                                                                  const FGameplayTag& Slot)
{
    FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(AbilityTag);
    if (!Spec || !Slot.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag"))))
        return;
    if (auto* State = Cast<AMyPlayerState>(GetOwnerActor());
        State && !State->GetEquipmentComponent()->CanAssignWeaponSlot(Spec->Handle, Slot))
        return;
    const FMyGameplayTags& Tags = FMyGameplayTags::Get();
    const FGameplayTag Status = GetStatusFromSpec(*Spec);
    if (Status != Tags.Abilities_Status_Equipped && Status != Tags.Abilities_Status_Unlocked)
        return;
    const bool bPassive = IsPassiveAbility(*Spec);
    if (bPassive != Slot.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.Passive"))))
        return;
    const auto* EquippingAbility = Cast<UMyGameplayAbility>(Spec->Ability);
    if (EquippingAbility && EquippingAbility->ShouldCancelOnEquipChange() && Spec->IsActive())
        CancelAbilityHandle(Spec->Handle);
    const FGameplayTag PreviousSlot = GetInputTagFromSpec(*Spec);
    if (PreviousSlot == Slot && Spec->IsActive())
    {
        ClientEquipAbility(AbilityTag, Tags.Abilities_Status_Equipped, Slot, PreviousSlot);
        return;
    }
    // Reject a failed passive activation before displacing the existing slot occupant.
    if (bPassive && !Spec->IsActive() && !TryActivateAbility(Spec->Handle))
        return;
    if (FGameplayAbilitySpec* OldSpec = GetSpecWithSlot(Slot); OldSpec && OldSpec != Spec)
    {
        if (auto* State = Cast<AMyPlayerState>(GetOwnerActor()))
            State->GetEquipmentComponent()->RememberWeaponSlotDisplacement(Spec->Handle, *OldSpec);
        const FGameplayTag OldTag = GetAbilityTagFromSpec(*OldSpec);
        if (IsPassiveAbility(*OldSpec))
        {
            DeactivatePassiveAbility.Broadcast(OldTag);
            MulticastActivatePassiveEffect(OldTag, false);
        }
        else if (const auto* OldAbility = Cast<UMyGameplayAbility>(OldSpec->Ability);
                 OldAbility && OldAbility->ShouldCancelOnEquipChange())
        {
            CancelAbilityHandle(OldSpec->Handle);
        }
        ClearSlot(OldSpec);
        OldSpec->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Equipped);
        OldSpec->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Unlocked);
        MarkAbilitySpecDirty(*OldSpec, true);
        ClientUpdateAbilityStatus(OldTag, Tags.Abilities_Status_Unlocked, OldSpec->Level);
    }
    AssignSlotToAbility(*Spec, Slot);
    Spec->DynamicAbilityTags.RemoveTag(Status);
    Spec->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Equipped);
    MarkAbilitySpecDirty(*Spec, true);
    if (bPassive)
        MulticastActivatePassiveEffect(AbilityTag, true);
    ClientEquipAbility(AbilityTag, Tags.Abilities_Status_Equipped, Slot, PreviousSlot);
}
bool UMyAbilitySystemComponent::IsPassiveAbility(const FGameplayAbilitySpec& Spec) const
{
    const UAbilityInfo* AbilityInfo = UMyAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
    const FGameplayTag AbilityTag = GetAbilityTagFromSpec(Spec);
    if (!AbilityInfo)
        return false;
    const FMyAbilityInfo& Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);
    const FGameplayTag AbilityType = Info.AbilityType;
    return AbilityType.MatchesTagExact(FMyGameplayTags::Get().Abilities_Type_Passive);
}
void UMyAbilitySystemComponent::MulticastActivatePassiveEffect_Implementation(const FGameplayTag& AbilityTag,
                                                                              bool bActivate)
{
    ActivatePassiveEffect.Broadcast(AbilityTag, bActivate);
}
bool UMyAbilitySystemComponent::AbilityHasAnySlot(const FGameplayAbilitySpec& Spec)
{
    return Spec.DynamicAbilityTags.HasTag(FGameplayTag::RequestGameplayTag(FName("InputTag")));
}
void UMyAbilitySystemComponent::AssignSlotToAbility(FGameplayAbilitySpec& Spec, const FGameplayTag& Slot)
{
    ClearSlot(&Spec);
    Spec.DynamicAbilityTags.AddTag(Slot);
}
// 开关技能提交服务器切换，普通活动技能转发输入事件。
void UMyAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
    if (!InputTag.IsValid())
        return;
    FScopedAbilityListLock ActiveScopeLoc(*this);
    for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
    {
        if (AbilitySpec.DynamicAbilityTags.HasTagExact(InputTag))
        {
            const auto* Ability = Cast<UMyGameplayAbility>(AbilitySpec.Ability);
            if (Ability && Ability->IsInputToggleAbility())
            {
                // Do not rely on client Spec.IsActive for a ServerOnly ability.
                ServerToggleAbility(AbilitySpec.Handle, InputTag);
                continue;
            }
            if (AbilitySpec.IsActive())
            {
                AbilitySpecInputPressed(AbilitySpec);
                InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, AbilitySpec.Handle,
                                      AbilitySpec.ActivationInfo.GetActivationPredictionKey());
            }
        }
    }
}

// 服务器校验输入槽及技能类型，再取消或激活开关技能。
void UMyAbilitySystemComponent::ServerToggleAbility_Implementation(FGameplayAbilitySpecHandle AbilityHandle,
                                                                   FGameplayTag InputTag)
{
    FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(AbilityHandle);
    const auto* Ability = Spec ? Cast<UMyGameplayAbility>(Spec->Ability) : nullptr;
    const auto& Tags = FMyGameplayTags::Get();
    if (!Ability || !Ability->IsInputToggleAbility() || !InputTag.IsValid() ||
        !Spec->DynamicAbilityTags.HasTagExact(InputTag) || GetStatusFromSpec(*Spec) != Tags.Abilities_Status_Equipped ||
        !InputTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag"))) ||
        InputTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.Passive"))))
        return;
    if (Spec->IsActive())
        CancelAbilityHandle(AbilityHandle);
    else if (!HasMatchingGameplayTag(Tags.Player_Block_InputPressed))
        TryActivateAbility(AbilityHandle);
}

// 跳过开关技能，为普通活动技能转发松开事件。
void UMyAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
    if (!InputTag.IsValid())
        return;
    FScopedAbilityListLock ActiveScopeLoc(*this);

    for (auto& AbilitySpec : GetActivatableAbilities())
    {
        if (AbilitySpec.DynamicAbilityTags.HasTagExact(InputTag))
        {
            const auto* Ability = Cast<UMyGameplayAbility>(AbilitySpec.Ability);
            if (Ability && Ability->IsInputToggleAbility())
                continue;
            if (AbilitySpec.IsActive())
            {

                AbilitySpecInputReleased(AbilitySpec);
                InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, AbilitySpec.Handle,
                                      AbilitySpec.ActivationInfo.GetActivationPredictionKey());
            }
        }
    }
}
// 恢复存档等级槽位，兼容旧开关技能并恢复已装备被动。
void UMyAbilitySystemComponent::AddCharacterAbilitiesFromSaveData(ULoadScreenSaveGame* SaveData)
{
    for (const FSavedAbility& Data : SaveData->SavedAbilities)
    {
        const TSubclassOf<UGameplayAbility> LoadedAbilityClass = Data.GameplayAbility;
        const auto* SavedAbility =
            LoadedAbilityClass ? Cast<UMyGameplayAbility>(LoadedAbilityClass.GetDefaultObject()) : nullptr;
        if (SavedAbility && SavedAbility->IsEquipmentGrantedOnly())
            continue;

        FGameplayAbilitySpec LoadedAbilitySpec = FGameplayAbilitySpec(LoadedAbilityClass, Data.AbilityLevel);

        LoadedAbilitySpec.DynamicAbilityTags.AddTag(Data.AbilitySlot);
        LoadedAbilitySpec.DynamicAbilityTags.AddTag(Data.AbilityStatus);
        const auto* LoadedAbility = Cast<UMyGameplayAbility>(LoadedAbilitySpec.Ability);
        if (LoadedAbility && LoadedAbility->IsInputToggleAbility())
        {
            // Legacy saves used a passive slot. Keep the learned level, release that
            // incompatible slot, and never restore a running toggle from a save.
            if (Data.AbilitySlot.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.Passive"))))
            {
                LoadedAbilitySpec.DynamicAbilityTags.RemoveTag(Data.AbilitySlot);
                if (Data.AbilityStatus == FMyGameplayTags::Get().Abilities_Status_Equipped)
                {
                    LoadedAbilitySpec.DynamicAbilityTags.RemoveTag(Data.AbilityStatus);
                    LoadedAbilitySpec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Unlocked);
                }
            }
            GiveAbility(LoadedAbilitySpec);
        }
        else if (Data.AbilityType == FMyGameplayTags::Get().Abilities_Type_Offensive)
        {
            GiveAbility(LoadedAbilitySpec);
        }
        else if (Data.AbilityType == FMyGameplayTags::Get().Abilities_Type_Passive)
        {
            if (Data.AbilityStatus.MatchesTagExact(FMyGameplayTags::Get().Abilities_Status_Equipped))
            {
                const FGameplayAbilitySpecHandle Handle = GiveAbility(LoadedAbilitySpec);
                if (TryActivateAbility(Handle))
                    MulticastActivatePassiveEffect(Data.AbilityTag, true);
            }
            else
            {
                GiveAbility(LoadedAbilitySpec);
            }
        }
    }
    // Save/travel restoration must notify the same consumers as startup grants
    // and owner-client replication.
    bStartupAbilityGiven = true;
    AbilitiesGiven.Broadcast();
}
// 校验服务器权限、攻击提交及敌对存活目标，广播有效攻击事件。
void UMyAbilitySystemComponent::NotifyOffensiveAttack(UGameplayAbility* SourceAbility, AActor* Target)
{
    AActor* Avatar = GetAvatarActor();
    const auto* Attack = Cast<UMyDamageGameplayAbility>(SourceAbility);
    if (!IsValid(Avatar) || !Avatar->HasAuthority() || !IsValid(Target) || !Attack ||
        !Attack->HasCommittedOffensiveAttack() || !SourceAbility->IsActive() ||
        SourceAbility->GetAbilitySystemComponentFromActorInfo() != this ||
        SourceAbility->GetAssetTags().HasTag(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Passive"))) ||
        !Target->Implements<UCombatInterface>() || ICombatInterface::Execute_IsDead(Target) ||
        !UMyAbilitySystemLibrary::IsNotFriend(Avatar, Target))
        return;
    OffensiveAttackTargeted.Broadcast(SourceAbility, Target);
}
