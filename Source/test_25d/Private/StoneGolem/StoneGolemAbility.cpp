#include "StoneGolem/StoneGolemAbility.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SGHammer, "Abilities.StoneGolem.HeavyHammer");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SGQuake, "Abilities.StoneGolem.Quake");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SGShield, "Abilities.StoneGolem.ShieldBash");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SGHammerCD, "Cooldown.StoneGolem.HeavyHammer");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SGQuakeCD, "Cooldown.StoneGolem.Quake");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SGShieldCD, "Cooldown.StoneGolem.ShieldBash");
UStoneGolemHeavyHammer::UStoneGolemHeavyHammer()
{
    SetAssetTags(FGameplayTagContainer(TAG_SGHammer));
    CooldownIdentity = TAG_SGHammerCD;
}
UStoneGolemQuake::UStoneGolemQuake()
{
    SetAssetTags(FGameplayTagContainer(TAG_SGQuake));
    CooldownIdentity = TAG_SGQuakeCD;
}
UStoneGolemShieldBash::UStoneGolemShieldBash()
{
    SetAssetTags(FGameplayTagContainer(TAG_SGShield));
    CooldownIdentity = TAG_SGShieldCD;
}
