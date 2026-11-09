// 调用链：装备事务或效果对账 → 准备武器能力 → 提交输入槽切换 → 角色武器外观刷新；失败准备由事务退出清理。
#include "Equipment/EquipmentComponent.h"
#include "Equipment/WeaponSetConfig.h"
#include "Backpack/ItemDefinition.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Character/StanderPlayer.h"
#include "Player/MyPlayerState.h"
#include "MyGameplayTags.h"
#include "Abilities/GameplayAbility.h"

// Find the same avatar for authority-side grants and client-side visuals.
AStanderPlayer* UEquipmentComponent::FindStanderAvatar() const
{
    const auto* State = Cast<AMyPlayerState>(GetOwner());
    if (auto* Player = State ? Cast<AStanderPlayer>(State->GetPawn()) : nullptr)
    {
        return Player;
    }
    // A restored PlayerState can be available before its pawn link or ActorInfo.
    const auto* AbilitySystem = ASC();
    return AbilitySystem && AbilitySystem->AbilityActorInfo.IsValid()
               ? Cast<AStanderPlayer>(AbilitySystem->GetAvatarActor())
               : nullptr;
}

// Inventory weapon sets override the avatar's default action set.
UWeaponSetConfig* UEquipmentComponent::WeaponSetFor(const TArray<FEquipmentSlot>& Records, bool bIncludeDefault) const
{
    for (const auto& Record : Records)
        if (const auto* Def = Definition(Record.ItemDefinitionId); Def && Def->WeaponSet)
            return Def->WeaponSet;
    const auto* Player = bIncludeDefault ? FindStanderAvatar() : nullptr;
    return Player ? Player->DefaultWeaponSet.Get() : nullptr;
}
// 返回当前装备或角色默认的武器动作组。
UWeaponSetConfig* UEquipmentComponent::GetWeaponSet() const
{
    return WeaponSetFor(Slots);
}
// 按授予句柄判断能力是否属于本组件武器组。
bool UEquipmentComponent::IsWeaponAbility(FGameplayAbilitySpecHandle Handle) const
{
    return WeaponHandles.Contains(Handle);
}
// 读取能力当前输入槽；被武器挤出时回退到保存的原槽位。
FGameplayTag UEquipmentComponent::GetSavedInputSlot(const FGameplayAbilitySpec& Spec) const
{
    if (!UMyAbilitySystemComponent::AbilityHasAnySlot(Spec))
        if (const auto* Saved = DisplacedSlots.Find(Spec.Handle))
            return *Saved;
    return UMyAbilitySystemComponent::GetInputTagFromSpec(Spec);
}
// 约束武器能力可用输入槽，并保护当前武器配置占用的槽位。
bool UEquipmentComponent::CanAssignWeaponSlot(FGameplayAbilitySpecHandle Handle, FGameplayTag Slot) const
{
    if (IsWeaponAbility(Handle) && !UWeaponSetConfig::IsWeaponInputSlot(Slot))
        return false;
    // A weapon loadout owns its configured slots; its extra actions may replace those actions.
    if (const auto* Set = GetWeaponSet())
        for (const auto& Entry : Set->Abilities)
            if (Entry.InputTag == Slot)
                return IsWeaponAbility(Handle);
    return true;
}
// 撤销尚未提交的临时武器能力。
void UEquipmentComponent::DiscardWeaponSet(const TArray<FGameplayAbilitySpecHandle>& Prepared)
{
    if (auto* A = ASC())
        for (auto Handle : Prepared)
            A->ClearAbility(Handle);
}
// 验证武器组唯一性、可取消性和能力标签，预授予新能力供事务提交。
bool UEquipmentComponent::PrepareWeaponSet(const TArray<FEquipmentSlot>& Records,
                                           TArray<FGameplayAbilitySpecHandle>& Prepared)
{
    auto* A = Cast<UMyAbilitySystemComponent>(ASC());
    if (!A)
        return WeaponSetFor(Records) == nullptr && WeaponHandles.IsEmpty();
    auto* Set = WeaponSetFor(Records);
    int32 Count = 0;
    for (const auto& R : Records)
        if (const auto* D = Definition(R.ItemDefinitionId); D && D->WeaponSet)
            ++Count;
    if (Count > 1 || (Set && (!Set->Validate() || !Cast<AStanderPlayer>(A->GetAvatarActor()))))
        return false;
    if (Set == AppliedWeaponSet.Get() && WeaponASC.Get() == A)
    {
        bool Complete = true;
        for (auto H : WeaponHandles)
            Complete &= A->FindAbilitySpecFromHandle(H) != nullptr;
        if (Complete && (!Set || WeaponHandles.Num() == Set->Abilities.Num()))
            return true;
    }
    // Cancellation must be possible before any old loadout state is changed.
    for (const auto& Spec : A->GetActivatableAbilities())
    {
        bool Displaced = WeaponHandles.Contains(Spec.Handle);
        if (Set)
            for (const auto& E : Set->Abilities)
                Displaced |= E.InputTag.IsValid() && UMyAbilitySystemComponent::AbilityHasSlot(Spec, E.InputTag);
        if (Displaced && Spec.IsActive())
        {
            const auto Instances = Spec.GetAbilityInstances();
            if (Instances.IsEmpty() && Spec.Ability && !Spec.Ability->CanBeCanceled())
                return false;
            for (auto* AbilityInstance : Instances)
                if (AbilityInstance && AbilityInstance->IsActive() && !AbilityInstance->CanBeCanceled())
                    return false;
        }
    }
    if (!Set)
        return true;
    // Ability-tag lookup underlies the HUD/RPCs; never introduce ambiguous identities.
    for (const auto& Entry : Set->Abilities)
        for (const auto& Existing : A->GetActivatableAbilities())
            if (!WeaponHandles.Contains(Existing.Handle) &&
                Entry.Ability.GetDefaultObject()->GetAssetTags().HasTagExact(A->GetAbilityTagFromSpec(Existing)))
                return false;
    for (const auto& Entry : Set->Abilities)
    {
        FGameplayAbilitySpec Spec(Entry.Ability, Entry.Level, INDEX_NONE, this);
        Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Unlocked);
        auto H = A->GiveAbility(Spec);
        if (!H.IsValid() || !A->FindAbilitySpecFromHandle(H))
        {
            DiscardWeaponSet(Prepared);
            Prepared.Reset();
            return false;
        }
        Prepared.Add(H);
    }
    return true;
}
// 撤销旧武器能力、恢复被挤出能力，再为新武器分配输入槽并刷新外观。
void UEquipmentComponent::CommitWeaponSet(const TArray<FEquipmentSlot>& Records,
                                          const TArray<FGameplayAbilitySpecHandle>& Prepared, bool bIncludeDefault)
{
    auto* A = Cast<UMyAbilitySystemComponent>(ASC());
    if (!A)
        return;
    auto* Set = WeaponSetFor(Records, bIncludeDefault);
    FGuid Instance;
    for (const auto& R : Records)
        if (const auto* D = Definition(R.ItemDefinitionId); D && D->WeaponSet)
        {
            Instance = R.ItemInstanceId;
            break;
        }
    // Equipping the physical version of the same set keeps its existing handles and user slot assignments.
    if (Prepared.IsEmpty() && Set == AppliedWeaponSet.Get() && WeaponASC.Get() == A)
    {
        if (Instance != AppliedWeaponInstance)
        {
            ++WeaponGeneration;
            AppliedWeaponInstance = Instance;
        }
        return;
    }
    // 世代推进使旧武器上下文失效，随后清理旧能力并恢复被挤出的普通技能。
    ++WeaponGeneration;
    const auto& Tags = FMyGameplayTags::Get();
    if (auto* Old = Cast<UMyAbilitySystemComponent>(WeaponASC.Get()))
    {
        for (auto H : WeaponHandles)
            if (auto* S = Old->FindAbilitySpecFromHandle(H))
            {
                const auto Tag = Old->GetAbilityTagFromSpec(*S);
                const auto Slot = Old->GetInputTagFromSpec(*S);
                Old->CancelAbilityHandle(H);
                Old->ClearAbility(H);
                Old->ClientEquipAbility(Tag, Tags.Abilities_Status_Unlocked, FGameplayTag(), Slot);
            }
        for (const auto& Pair : DisplacedSlots)
            if (auto* S = Old->FindAbilitySpecFromHandle(Pair.Key);
                S && !Old->AbilityHasAnySlot(*S) && Old->SlotIsEmpty(Pair.Value))
            {
                Old->AssignSlotToAbility(*S, Pair.Value);
                S->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Unlocked);
                S->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Equipped);
                Old->MarkAbilitySpecDirty(*S, true);
                Old->ClientEquipAbility(Old->GetAbilityTagFromSpec(*S), Tags.Abilities_Status_Equipped, Pair.Value,
                                        FGameplayTag());
            }
    }
    // 旧组清理完毕后接管预授予句柄，再覆盖新配置要求的输入槽。
    WeaponHandles = Prepared;
    DisplacedSlots.Reset();
    WeaponASC = A;
    AppliedWeaponSet = Set;
    AppliedWeaponInstance = Instance;
    if (Set)
        for (int32 I = 0; I < Prepared.Num(); ++I)
        {
            const auto Slot = Set->Abilities[I].InputTag;
            if (Slot.IsValid())
                if (auto* Old = A->GetSpecWithSlot(Slot))
                {
                    const auto H = Old->Handle;
                    const auto Tag = A->GetAbilityTagFromSpec(*Old);
                    DisplacedSlots.Add(H, Slot);
                    A->CancelAbilityHandle(H);
                    if (auto* S = A->FindAbilitySpecFromHandle(H))
                    {
                        A->ClearSlot(S);
                        S->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Equipped);
                        S->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Unlocked);
                        A->MarkAbilitySpecDirty(*S, true);
                    }
                    A->ClientEquipAbility(Tag, Tags.Abilities_Status_Unlocked, FGameplayTag(), Slot);
                }
            if (auto* S = A->FindAbilitySpecFromHandle(Prepared[I]))
            {
                if (Slot.IsValid())
                {
                    A->AssignSlotToAbility(*S, Slot);
                    S->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Unlocked);
                    S->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Equipped);
                }
                A->MarkAbilitySpecDirty(*S, true);
                A->ClientEquipAbility(A->GetAbilityTagFromSpec(*S), A->GetStatusFromSpec(*S), Slot, FGameplayTag());
            }
        }
    if (bIncludeDefault)
    {
        RefreshWeaponVisuals();
    }
}
// 定位玩家状态当前角色并应用装备武器外观。
void UEquipmentComponent::RefreshWeaponVisuals()
{
    auto* Player = FindStanderAvatar();
    if (Player)
    {
        Player->ApplyWeaponSetVisuals(GetWeaponSet());
    }
}

// 记录武器能力挤出的普通能力输入槽，供卸装后恢复。
void UEquipmentComponent::RememberWeaponSlotDisplacement(FGameplayAbilitySpecHandle Incoming,
                                                         const FGameplayAbilitySpec& Displaced)
{
    if (GetOwner()->HasAuthority() && IsWeaponAbility(Incoming) && !IsWeaponAbility(Displaced.Handle))
        DisplacedSlots.FindOrAdd(Displaced.Handle) = UMyAbilitySystemComponent::GetInputTagFromSpec(Displaced);
}
