/** 调用链与单例生命周期：UMyAssetManager::StartInitialLoading -> InitializeNativeGameplayTags -> UGameplayTagsManager注册 -> 各模块Get只读访问。GameplayTags是静态C++值实例，不是UObject，不由GC或对象池管理；存活于模块静态生命周期，不随地图重建。各进程独立注册同名标签，不能把静态实例地址当成网络身份。 */
// Fill out your copyright notice in the Description page of Project Settings.

#include "MyGameplayTags.h"
#include "GameplayTagsManager.h"

FMyGameplayTags FMyGameplayTags::GameplayTags;

void FMyGameplayTags::InitializeNativeGameplayTags()
{
    GameplayTags.Attributes_Primary_Strength =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Primary.Strength"));

    GameplayTags.Attributes_Primary_Intelligence =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Primary.Intelligence"));

    GameplayTags.Attributes_Primary_Resilience =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Primary.Resilience"));
    GameplayTags.Attributes_Primary_Vigor =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Primary.Vigor"));

    GameplayTags.Attributes_Secondary_Armor =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.Armor"));
    GameplayTags.Attributes_Secondary_MaxHealth =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.MaxHealth"));

    GameplayTags.Attributes_Secondary_MaxMana =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.MaxMana"));
    GameplayTags.Attributes_Secondary_ArmorPene =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.ArmorPene"));

    GameplayTags.Attributes_Secondary_BlockChance =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.BlockChance"));

    GameplayTags.Attributes_Secondary_CriticalHitChance =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.CriticalHitChance"));
    GameplayTags.Attributes_Secondary_CriticalHitDamage =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.CriticalHitDamage"));

    GameplayTags.Attributes_Secondary_CriticalHitResistence =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.CriticalHitResistence"));

    GameplayTags.Attributes_Secondary_HealthRegeneration =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.HealthRegeneration"));
    GameplayTags.Attributes_Secondary_ManaRegeneration =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Secondary.ManaRegeneration"));

    GameplayTags.Attributes_Meta_IncomingXP =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Meta.IncomingXP"));

    GameplayTags.InputTag_LMB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.LMB"));
    GameplayTags.InputTag_RMB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.RMB"));
    GameplayTags.InputTag_1 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.1"));
    GameplayTags.InputTag_2 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.2"));
    GameplayTags.InputTag_3 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.3"));
    GameplayTags.InputTag_4 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.4"));
    GameplayTags.InputTag_Backpack = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.Backpack"));
    GameplayTags.InputTag_Pickup = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.Pickup"));

    GameplayTags.InputTag_Passive_1 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.Passive.1"));
    GameplayTags.InputTag_Passive_2 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("InputTag.Passive.2"));

    GameplayTags.Damage = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Damage"));

    GameplayTags.Damage_Physics = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Damage.Physics"));
    GameplayTags.Damage_Magic = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Damage.Magic"));
    GameplayTags.Damage_Pure = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Damage.Pure"));
    GameplayTags.Damage_Fire = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Damage.Fire"));
    GameplayTags.Damage_Thunder = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Damage.Thunder"));
    GameplayTags.Damage_Water = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Damage.Water"));

    GameplayTags.Attributes_Resistance_Fire =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Resistance.Fire"));
    GameplayTags.Attributes_Resistance_Water =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Resistance.Water"));
    GameplayTags.Attributes_Resistance_Thunder =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Resistance.Thunder"));
    GameplayTags.Attributes_Resistance_Magic =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Resistance.Magic"));
    GameplayTags.Attributes_Resistance_Physics =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Resistance.Physics"));
    GameplayTags.Attributes_Resistance_Pure =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Attributes.Resistance.Pure"));

    GameplayTags.Effects_HitReact = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Effects.HitReact"));

    /* 将属性和抗性标签对应 */
    GameplayTags.DamageTypesToResistance.Add(GameplayTags.Damage_Fire, GameplayTags.Attributes_Resistance_Fire);
    GameplayTags.DamageTypesToResistance.Add(GameplayTags.Damage_Thunder, GameplayTags.Attributes_Resistance_Thunder);
    GameplayTags.DamageTypesToResistance.Add(GameplayTags.Damage_Magic, GameplayTags.Attributes_Resistance_Magic);
    GameplayTags.DamageTypesToResistance.Add(GameplayTags.Damage_Physics, GameplayTags.Attributes_Resistance_Physics);
    GameplayTags.DamageTypesToResistance.Add(GameplayTags.Damage_Water, GameplayTags.Attributes_Resistance_Water);
    GameplayTags.DamageTypesToResistance.Add(GameplayTags.Damage_Pure, GameplayTags.Attributes_Resistance_Pure);

    //

    //abilities
    GameplayTags.Abilities_None = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.None"));

    GameplayTags.Abilities_Attack = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Attack"));
    GameplayTags.Abilities_Summon = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Summon"));
    GameplayTags.Abilities_HitReact = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.HitReact"));

    GameplayTags.Abilities_Status_Locked =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Status.Locked"));
    GameplayTags.Abilities_Status_Eligible =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Status.Eligible"));
    GameplayTags.Abilities_Status_Unlocked =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Status.Unlocked"));
    GameplayTags.Abilities_Status_Equipped =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Status.Equipped"));

    GameplayTags.Abilities_Type_Offensive =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Type.Offensive"));
    GameplayTags.Abilities_Type_Passive =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Type.Passive"));
    GameplayTags.Abilities_Type_None = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Type.None"));
    //
    GameplayTags.Abilities_Passive_HaloOfProtection =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Passive.HaloOfProtection"));
    GameplayTags.Abilities_Passive_LifeSiphon =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Passive.LifeSiphon"));
    GameplayTags.Abilities_Passive_ManaSiphon =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Passive.ManaSiphon"));
    GameplayTags.Abilities_Passive_OrbitingFireBolt =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Passive.OrbitingFireBolt"));

    GameplayTags.Abilities_Passive_ListenForEvent =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Passive.ListenForEvent"));

    //

    GameplayTags.Abilities_Fire_FireBolt =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Fire.FireBolt"));

    GameplayTags.Abilities_Fire_FireBlast =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Fire.FireBlast"));
    GameplayTags.Abilities_Thunder_Electrocute =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Thunder.Electrocute"));
    GameplayTags.Abilities_Arcane_ArcaneShards =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Arcane.ArcaneShards"));
    GameplayTags.Abilities_Arcane_ArcaneNova =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Abilities.Arcane.ArcaneNova"));

    //cooldown

    GameplayTags.Cooldown_Fire_FireBolt =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Cooldown.Fire.FireBolt"));
    GameplayTags.Cooldown_Thunder_Electrocute =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Cooldown.Thunder.Electrocute"));
    GameplayTags.Cooldown_Arcane_ArcaneShards =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Cooldown.Arcane.ArcaneShards"));
    GameplayTags.Cooldown_Arcane_ArcaneNova =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Cooldown.Arcane.ArcaneNova"));

    //debuff
    GameplayTags.Debuff_Burn = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Debuff.Burn"));
    GameplayTags.Debuff_Stun = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Debuff.Stun"));
    GameplayTags.Debuff_Arcane = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Debuff.Arcane"));
    GameplayTags.Debuff_Physical = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Debuff.Physical"));

    GameplayTags.Debuff_Chance = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Debuff.Chance"));
    GameplayTags.Debuff_Damage = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Debuff.Damage"));
    GameplayTags.Debuff_Duration = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Debuff.Duration"));
    GameplayTags.Debuff_Frequency = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Debuff.Frequency"));

    GameplayTags.DamageTypesToDebuffs.Add(GameplayTags.Damage_Fire, GameplayTags.Debuff_Burn);
    GameplayTags.DamageTypesToDebuffs.Add(GameplayTags.Damage_Thunder, GameplayTags.Debuff_Stun);
    GameplayTags.DamageTypesToDebuffs.Add(GameplayTags.Damage_Magic, GameplayTags.Debuff_Arcane);
    GameplayTags.DamageTypesToDebuffs.Add(GameplayTags.Damage_Physics, GameplayTags.Debuff_Physical);

    //

    GameplayTags.Player_Block_InputPressed =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Player.Block.InputPressed"));
    GameplayTags.Player_Block_InputHeld =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Player.Block.InputHeld"));
    GameplayTags.Player_Block_InputReleased =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Player.Block.InputReleased"));
    GameplayTags.Player_Block_CursorTrace =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Player.Block.CursorTrace"));

    //

    GameplayTags.CombatSocket_Weapon = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("CombatSocket.Weapon"));
    GameplayTags.CombatSocket_RightHand =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("CombatSocket.RightHand"));
    GameplayTags.CombatSocket_LeftHand =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("CombatSocket.LeftHand"));
    GameplayTags.CombatSocket_Tail = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("CombatSocket.Tail"));

    GameplayTags.Montage_Attack_1 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Montage.Attack.1"));
    GameplayTags.Montage_Attack_2 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Montage.Attack.2"));
    GameplayTags.Montage_Attack_3 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Montage.Attack.3"));
    GameplayTags.Montage_Attack_4 = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Montage.Attack.4"));

    GameplayTags.GameplayCue_FireBlast =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("GameplayCue.FireBlast"));
    GameplayTags.GameplayCue_ArcaneNova =
        UGameplayTagsManager::Get().AddNativeGameplayTag(FName("GameplayCue.ArcaneNova"));
}
