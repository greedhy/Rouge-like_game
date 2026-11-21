// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Data/AbilityInfo.h"

#include "test_25d/MyLogChannels.h"
#include "Character/SwordShieldPlayer.h"
#include "Player/MyPlayerState.h"

// 玩家化身同时供服务器和客户端识别职业，兼容旧试验地图尚未设置职业的 PlayerState。
EPlayerProfession UAbilityInfo::ResolveProfession(const AActor* Actor)
{
    const auto* Pawn = Cast<APawn>(Actor);
    const auto* State = Pawn ? Pawn->GetPlayerState<AMyPlayerState>() : Cast<AMyPlayerState>(Actor);
    if (State && State->Profession != EPlayerProfession::Unselected)
    {
        return State->Profession;
    }
    const APawn* Avatar = Pawn ? Pawn : (State ? State->GetPawn() : nullptr);
    return Avatar && Avatar->IsA<ASwordShieldPlayer>() ? EPlayerProfession::Stander : EPlayerProfession::Mage;
}

// 成员资格和职业必须同时满足，空 Tag 与未知目录项不会获得学习资格。
bool UAbilityInfo::CanLearn(const FGameplayTag& AbilityTag, const AActor* Actor) const
{
    const auto* Row = AbilityInformation.FindByPredicate([&](const FMyAbilityInfo& Info)
    {
        return Info.AbilityTag == AbilityTag && Info.Ability;
    });
    return Row && Actor && Row->RequiredProfession == ResolveProfession(Actor);
}

FMyAbilityInfo UAbilityInfo::FindAbilityInfoForTag(const FGameplayTag& AbilityTag, bool bLogNotFound) const
{
    for (const FMyAbilityInfo& Info : AbilityInformation)
    {
        if (Info.AbilityTag == AbilityTag)
        {
            return Info;
        }
    }

    if (bLogNotFound)
    {
        UE_LOG(LogMyChannel, Error, TEXT("Can't find info for AbilityTag [%s] on AbilityInfo [%s]"),
               *AbilityTag.ToString(), *GetNameSafe(this));
    }

    return FMyAbilityInfo();
}
