// 调用链：AssetManager 注册原生标签 -> BuildTagSnapshot -> GAS 伤害、输入和表现。
// Fill out your copyright notice in the Description page of Project Settings.

#include "MyGameplayTags.h"
#include "GameplayTagsManager.h"

FMyGameplayTags FMyGameplayTags::GameplayTags;

void FMyGameplayTags::InitializeNativeGameplayTags()
{
    GameplayTags = BuildTagSnapshot(true);
}

const FMyGameplayTags& FMyGameplayTags::Get()
{
#if WITH_EDITOR
    if (GameplayTags.DamageTypesToResistance.IsEmpty())
    {
        // 热重载重置模块静态快照，但保留引擎已注册的标签，也不会重跑 AssetManager 的启动加载。
        // 使用旧模块注册的末段标签判断标签树是否已就绪，冷启动仍走标准注册入口。
        const auto* Manager = UGameplayTagsManager::GetIfAllocated();
        static const FName ReadyTag(TEXT("CombatSocket.Tail"));
        if (Manager && Manager->RequestGameplayTag(ReadyTag, false).IsValid())
        {
            static const FMyGameplayTags RecoveredTags = BuildTagSnapshot(false);
            return RecoveredTags;
        }
    }
#endif
    return GameplayTags;
}

FMyGameplayTags FMyGameplayTags::BuildTagSnapshot(bool bRegisterNativeTags)
{
    FMyGameplayTags Snapshot;
    auto& Manager = UGameplayTagsManager::Get();
    const auto ResolveTag = [&Manager, bRegisterNativeTags](const TCHAR* Name)
    {
        const FName TagName(Name);
        return bRegisterNativeTags ? Manager.AddNativeGameplayTag(TagName) : Manager.RequestGameplayTag(TagName, false);
    };

    Snapshot.Attributes_Primary_Strength =
        ResolveTag(TEXT("Attributes.Primary.Strength"));

    Snapshot.Attributes_Primary_Intelligence =
        ResolveTag(TEXT("Attributes.Primary.Intelligence"));

    Snapshot.Attributes_Primary_Resilience =
        ResolveTag(TEXT("Attributes.Primary.Resilience"));
    Snapshot.Attributes_Primary_Vigor =
        ResolveTag(TEXT("Attributes.Primary.Vigor"));

    Snapshot.Attributes_Secondary_Armor =
        ResolveTag(TEXT("Attributes.Secondary.Armor"));
    Snapshot.Attributes_Secondary_MaxHealth =
        ResolveTag(TEXT("Attributes.Secondary.MaxHealth"));

    Snapshot.Attributes_Secondary_MaxMana =
        ResolveTag(TEXT("Attributes.Secondary.MaxMana"));
    Snapshot.Attributes_Secondary_ArmorPene =
        ResolveTag(TEXT("Attributes.Secondary.ArmorPene"));

    Snapshot.Attributes_Secondary_BlockChance =
        ResolveTag(TEXT("Attributes.Secondary.BlockChance"));

    Snapshot.Attributes_Secondary_CriticalHitChance =
        ResolveTag(TEXT("Attributes.Secondary.CriticalHitChance"));
    Snapshot.Attributes_Secondary_CriticalHitDamage =
        ResolveTag(TEXT("Attributes.Secondary.CriticalHitDamage"));

    Snapshot.Attributes_Secondary_CriticalHitResistence =
        ResolveTag(TEXT("Attributes.Secondary.CriticalHitResistence"));

    Snapshot.Attributes_Secondary_HealthRegeneration =
        ResolveTag(TEXT("Attributes.Secondary.HealthRegeneration"));
    Snapshot.Attributes_Secondary_ManaRegeneration =
        ResolveTag(TEXT("Attributes.Secondary.ManaRegeneration"));

    Snapshot.Attributes_Meta_IncomingXP =
        ResolveTag(TEXT("Attributes.Meta.IncomingXP"));

    Snapshot.InputTag_LMB = ResolveTag(TEXT("InputTag.LMB"));
    Snapshot.InputTag_RMB = ResolveTag(TEXT("InputTag.RMB"));
    Snapshot.InputTag_1 = ResolveTag(TEXT("InputTag.1"));
    Snapshot.InputTag_2 = ResolveTag(TEXT("InputTag.2"));
    Snapshot.InputTag_3 = ResolveTag(TEXT("InputTag.3"));
    Snapshot.InputTag_4 = ResolveTag(TEXT("InputTag.4"));
    Snapshot.InputTag_Backpack = ResolveTag(TEXT("InputTag.Backpack"));
    Snapshot.InputTag_Pickup = ResolveTag(TEXT("InputTag.Pickup"));
    Snapshot.InputTag_Dodge = ResolveTag(TEXT("InputTag.Dodge"));
    Snapshot.InputTag_DeveloperRecipe = ResolveTag(TEXT("InputTag.DeveloperRecipe"));

    Snapshot.InputTag_Passive_1 = ResolveTag(TEXT("InputTag.Passive.1"));
    Snapshot.InputTag_Passive_2 = ResolveTag(TEXT("InputTag.Passive.2"));

    Snapshot.Damage = ResolveTag(TEXT("Damage"));

    Snapshot.Damage_Physics = ResolveTag(TEXT("Damage.Physics"));
    Snapshot.Damage_Magic = ResolveTag(TEXT("Damage.Magic"));
    Snapshot.Damage_Pure = ResolveTag(TEXT("Damage.Pure"));
    Snapshot.Damage_Fire = ResolveTag(TEXT("Damage.Fire"));
    Snapshot.Damage_Thunder = ResolveTag(TEXT("Damage.Thunder"));
    Snapshot.Damage_Water = ResolveTag(TEXT("Damage.Water"));
    Snapshot.Damage_Ice = ResolveTag(TEXT("Damage.Ice"));

    Snapshot.Attributes_Resistance_Fire =
        ResolveTag(TEXT("Attributes.Resistance.Fire"));
    Snapshot.Attributes_Resistance_Water =
        ResolveTag(TEXT("Attributes.Resistance.Water"));
    Snapshot.Attributes_Resistance_Thunder =
        ResolveTag(TEXT("Attributes.Resistance.Thunder"));
    Snapshot.Attributes_Resistance_Magic =
        ResolveTag(TEXT("Attributes.Resistance.Magic"));
    Snapshot.Attributes_Resistance_Physics =
        ResolveTag(TEXT("Attributes.Resistance.Physics"));
    Snapshot.Attributes_Resistance_Pure =
        ResolveTag(TEXT("Attributes.Resistance.Pure"));

    Snapshot.Effects_HitReact = ResolveTag(TEXT("Effects.HitReact"));

    /* 将属性和抗性标签对应 */
    Snapshot.DamageTypesToResistance.Add(Snapshot.Damage_Fire, Snapshot.Attributes_Resistance_Fire);
    Snapshot.DamageTypesToResistance.Add(Snapshot.Damage_Thunder, Snapshot.Attributes_Resistance_Thunder);
    Snapshot.DamageTypesToResistance.Add(Snapshot.Damage_Magic, Snapshot.Attributes_Resistance_Magic);
    Snapshot.DamageTypesToResistance.Add(Snapshot.Damage_Physics, Snapshot.Attributes_Resistance_Physics);
    Snapshot.DamageTypesToResistance.Add(Snapshot.Damage_Water, Snapshot.Attributes_Resistance_Water);
    Snapshot.DamageTypesToResistance.Add(Snapshot.Damage_Ice, Snapshot.Attributes_Resistance_Water);
    Snapshot.DamageTypesToResistance.Add(Snapshot.Damage_Pure, Snapshot.Attributes_Resistance_Pure);

    //

    //abilities
    Snapshot.Abilities_None = ResolveTag(TEXT("Abilities.None"));

    Snapshot.Abilities_Attack = ResolveTag(TEXT("Abilities.Attack"));
    Snapshot.Abilities_Summon = ResolveTag(TEXT("Abilities.Summon"));
    Snapshot.Abilities_HitReact = ResolveTag(TEXT("Abilities.HitReact"));

    Snapshot.Abilities_Status_Locked =
        ResolveTag(TEXT("Abilities.Status.Locked"));
    Snapshot.Abilities_Status_Eligible =
        ResolveTag(TEXT("Abilities.Status.Eligible"));
    Snapshot.Abilities_Status_Unlocked =
        ResolveTag(TEXT("Abilities.Status.Unlocked"));
    Snapshot.Abilities_Status_Equipped =
        ResolveTag(TEXT("Abilities.Status.Equipped"));

    Snapshot.Abilities_Type_Offensive =
        ResolveTag(TEXT("Abilities.Type.Offensive"));
    Snapshot.Abilities_Type_Passive =
        ResolveTag(TEXT("Abilities.Type.Passive"));
    Snapshot.Abilities_Type_None = ResolveTag(TEXT("Abilities.Type.None"));
    //
    Snapshot.Abilities_Passive_HaloOfProtection =
        ResolveTag(TEXT("Abilities.Passive.HaloOfProtection"));
    Snapshot.Abilities_Passive_LifeSiphon =
        ResolveTag(TEXT("Abilities.Passive.LifeSiphon"));
    Snapshot.Abilities_Passive_ManaSiphon =
        ResolveTag(TEXT("Abilities.Passive.ManaSiphon"));
    Snapshot.Abilities_Passive_OrbitingFireBolt =
        ResolveTag(TEXT("Abilities.Passive.OrbitingFireBolt"));

    Snapshot.Abilities_Passive_ListenForEvent =
        ResolveTag(TEXT("Abilities.Passive.ListenForEvent"));

    //

    Snapshot.Abilities_Fire_FireBolt =
        ResolveTag(TEXT("Abilities.Fire.FireBolt"));

    Snapshot.Abilities_Fire_FireBlast =
        ResolveTag(TEXT("Abilities.Fire.FireBlast"));
    Snapshot.Abilities_Thunder_Electrocute =
        ResolveTag(TEXT("Abilities.Thunder.Electrocute"));
    Snapshot.Abilities_Arcane_ArcaneShards =
        ResolveTag(TEXT("Abilities.Arcane.ArcaneShards"));
    Snapshot.Abilities_Arcane_ArcaneNova =
        ResolveTag(TEXT("Abilities.Arcane.ArcaneNova"));

    //cooldown

    Snapshot.Cooldown_Fire_FireBolt =
        ResolveTag(TEXT("Cooldown.Fire.FireBolt"));
    Snapshot.Cooldown_Thunder_Electrocute =
        ResolveTag(TEXT("Cooldown.Thunder.Electrocute"));
    Snapshot.Cooldown_Arcane_ArcaneShards =
        ResolveTag(TEXT("Cooldown.Arcane.ArcaneShards"));
    Snapshot.Cooldown_Arcane_ArcaneNova =
        ResolveTag(TEXT("Cooldown.Arcane.ArcaneNova"));

    //debuff
    Snapshot.Debuff_Burn = ResolveTag(TEXT("Debuff.Burn"));
    Snapshot.Debuff_Stun = ResolveTag(TEXT("Debuff.Stun"));
    Snapshot.Debuff_Arcane = ResolveTag(TEXT("Debuff.Arcane"));
    Snapshot.Debuff_Physical = ResolveTag(TEXT("Debuff.Physical"));

    Snapshot.Debuff_Chance = ResolveTag(TEXT("Debuff.Chance"));
    Snapshot.Debuff_Damage = ResolveTag(TEXT("Debuff.Damage"));
    Snapshot.Debuff_Duration = ResolveTag(TEXT("Debuff.Duration"));
    Snapshot.Debuff_Frequency = ResolveTag(TEXT("Debuff.Frequency"));

    Snapshot.DamageTypesToDebuffs.Add(Snapshot.Damage_Fire, Snapshot.Debuff_Burn);
    Snapshot.DamageTypesToDebuffs.Add(Snapshot.Damage_Thunder, Snapshot.Debuff_Stun);
    Snapshot.DamageTypesToDebuffs.Add(Snapshot.Damage_Magic, Snapshot.Debuff_Arcane);
    Snapshot.DamageTypesToDebuffs.Add(Snapshot.Damage_Physics, Snapshot.Debuff_Physical);

    //

    Snapshot.Player_Block_InputPressed =
        ResolveTag(TEXT("Player.Block.InputPressed"));
    Snapshot.Player_Block_InputHeld =
        ResolveTag(TEXT("Player.Block.InputHeld"));
    Snapshot.Player_Block_InputReleased =
        ResolveTag(TEXT("Player.Block.InputReleased"));
    Snapshot.Player_Block_CursorTrace =
        ResolveTag(TEXT("Player.Block.CursorTrace"));

    //

    Snapshot.CombatSocket_Weapon = ResolveTag(TEXT("CombatSocket.Weapon"));
    Snapshot.CombatSocket_RightHand =
        ResolveTag(TEXT("CombatSocket.RightHand"));
    Snapshot.CombatSocket_LeftHand =
        ResolveTag(TEXT("CombatSocket.LeftHand"));
    Snapshot.CombatSocket_Tail = ResolveTag(TEXT("CombatSocket.Tail"));

    Snapshot.Montage_Attack_1 = ResolveTag(TEXT("Montage.Attack.1"));
    Snapshot.Montage_Attack_2 = ResolveTag(TEXT("Montage.Attack.2"));
    Snapshot.Montage_Attack_3 = ResolveTag(TEXT("Montage.Attack.3"));
    Snapshot.Montage_Attack_4 = ResolveTag(TEXT("Montage.Attack.4"));

    Snapshot.GameplayCue_FireBlast =
        ResolveTag(TEXT("GameplayCue.FireBlast"));
    Snapshot.GameplayCue_ArcaneNova =
        ResolveTag(TEXT("GameplayCue.ArcaneNova"));
    return Snapshot;
}
