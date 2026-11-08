// 调用链：装备武器套装前调用 Validate → 检查表现资源、技能身份与输入槽唯一性；IsWeaponInputSlot 限制武器可占用输入。
#include "Equipment/WeaponSetConfig.h"
#include "Animation/AnimInstance.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "Abilities/GameplayAbility.h"
// 判定槽位是否属于鼠标双键或数字 1–4 的武器输入白名单。
bool UWeaponSetConfig::IsWeaponInputSlot(FGameplayTag Slot)
{
    static const TCHAR* Names[] = {TEXT("InputTag.LMB"), TEXT("InputTag.RMB"), TEXT("InputTag.1"),
                                   TEXT("InputTag.2"),   TEXT("InputTag.3"),   TEXT("InputTag.4")};
    for (const auto* Name : Names)
        if (Slot == FGameplayTag::RequestGameplayTag(Name))
            return true;
    return false;
}
// 检查武器表现配置与授予技能的完整性，拒绝重复类、技能身份和输入占位。
bool UWeaponSetConfig::Validate() const
{
    if (Family.IsNone() || Abilities.IsEmpty() || !AnimClass || (MainMesh && MainSocket.IsNone()) ||
        (OffhandMesh && OffhandSocket.IsNone()) || MainTransform.ContainsNaN() || OffhandTransform.ContainsNaN())
        return false;
    // 分别记录技能类、输入槽与技能身份，防止不同配置项发生冲突。
    TSet<UClass*> Classes;
    TSet<FGameplayTag> Slots;
    TSet<FGameplayTag> Identities;
    for (const auto& Entry : Abilities)
    {
        if (!Entry.Ability || !Entry.Ability->IsChildOf(UMyGameplayAbility::StaticClass()) || Entry.Level < 1 ||
            Classes.Contains(Entry.Ability.Get()))
            return false;
        Classes.Add(Entry.Ability.Get());
        const auto* CDO = Entry.Ability.GetDefaultObject();
        bool HasIdentity = false;
        for (const auto& Tag : CDO->GetAssetTags())
            if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Abilities"))))
            {
                if (Identities.Contains(Tag))
                    return false;
                Identities.Add(Tag);
                HasIdentity = true;
            }
        if (!HasIdentity)
            return false;
        if (Entry.InputTag.IsValid())
        {
            if (!IsWeaponInputSlot(Entry.InputTag) || Slots.Contains(Entry.InputTag))
                return false;
            Slots.Add(Entry.InputTag);
        }
    }
    return true;
}
