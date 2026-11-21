// 调用链：资产管理器注册标签 -> 模块提供只读快照；编辑器重载后可从引擎标签树重建快照。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
/**
 * 
 */
struct FMyGameplayTags
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    friend class FGameplayTagModuleReloadTest;
#endif

public:
    static const FMyGameplayTags& Get();
    static void InitializeNativeGameplayTags();
    FGameplayTag Attributes_Primary_Strength;

    FGameplayTag Attributes_Primary_Intelligence;

    FGameplayTag Attributes_Primary_Resilience;

    FGameplayTag Attributes_Primary_Vigor;

    FGameplayTag Attributes_Secondary_Armor;
    FGameplayTag Attributes_Secondary_MaxHealth;

    FGameplayTag Attributes_Secondary_MaxMana;

    FGameplayTag Attributes_Secondary_ArmorPene;

    FGameplayTag Attributes_Secondary_BlockChance;

    FGameplayTag Attributes_Secondary_CriticalHitChance;

    FGameplayTag Attributes_Secondary_CriticalHitDamage;

    FGameplayTag Attributes_Secondary_CriticalHitResistence;

    FGameplayTag Attributes_Secondary_HealthRegeneration;

    FGameplayTag Attributes_Secondary_ManaRegeneration;
    //

    FGameplayTag Attributes_Meta_IncomingXP;

    //
    FGameplayTag InputTag_LMB;
    FGameplayTag InputTag_RMB;
    FGameplayTag InputTag_1;
    FGameplayTag InputTag_2;
    FGameplayTag InputTag_3;
    FGameplayTag InputTag_4;
    FGameplayTag InputTag_Backpack;
    FGameplayTag InputTag_Pickup;
    FGameplayTag InputTag_Dodge;
    /** 开发预览独占槽，普通存档和正式技能槽不使用。 */
    FGameplayTag InputTag_DeveloperRecipe;

    FGameplayTag InputTag_Passive_1;
    FGameplayTag InputTag_Passive_2;

    //

    FGameplayTag Abilities_None;

    FGameplayTag Abilities_Attack;
    FGameplayTag Abilities_Summon;

    FGameplayTag Abilities_Fire_FireBolt;
    FGameplayTag Abilities_Fire_FireBlast;
    FGameplayTag Abilities_Thunder_Electrocute;
    FGameplayTag Abilities_Arcane_ArcaneShards;
    FGameplayTag Abilities_Arcane_ArcaneNova;

    FGameplayTag Abilities_HitReact;

    FGameplayTag Abilities_Status_Locked;
    FGameplayTag Abilities_Status_Eligible;
    FGameplayTag Abilities_Status_Unlocked;
    FGameplayTag Abilities_Status_Equipped;

    FGameplayTag Abilities_Type_Offensive;
    FGameplayTag Abilities_Type_Passive;
    FGameplayTag Abilities_Type_None;

    FGameplayTag Abilities_Passive_HaloOfProtection;
    FGameplayTag Abilities_Passive_LifeSiphon;
    FGameplayTag Abilities_Passive_ManaSiphon;
    FGameplayTag Abilities_Passive_OrbitingFireBolt;

    FGameplayTag Abilities_Passive_ListenForEvent;
    //
    FGameplayTag Cooldown_Fire_FireBolt;
    FGameplayTag Cooldown_Thunder_Electrocute;
    FGameplayTag Cooldown_Arcane_ArcaneShards;
    FGameplayTag Cooldown_Arcane_ArcaneNova;

    //

    FGameplayTag CombatSocket_Weapon;    //使用武器攻击蒙太奇标签
    FGameplayTag CombatSocket_RightHand; //右手攻击蒙太奇标签
    FGameplayTag CombatSocket_LeftHand;  //左手攻击蒙太奇标签
    FGameplayTag CombatSocket_Tail;

    FGameplayTag Montage_Attack_1;
    FGameplayTag Montage_Attack_2;
    FGameplayTag Montage_Attack_3;
    FGameplayTag Montage_Attack_4;

    //
    FGameplayTag Damage;

    FGameplayTag Damage_Physics;
    FGameplayTag Damage_Magic;
    FGameplayTag Damage_Pure;

    FGameplayTag Damage_Fire;
    FGameplayTag Damage_Thunder;
    FGameplayTag Damage_Water;
    //属性伤害抗性
    FGameplayTag Attributes_Resistance_Fire;    //火属性伤害抵抗 标签
    FGameplayTag Attributes_Resistance_Water;   //水属性伤害抵抗 标签
    FGameplayTag Attributes_Resistance_Thunder; //雷属性伤害抵抗 标签
    FGameplayTag Attributes_Resistance_Magic;   //魔法伤害抵抗 标签
    FGameplayTag Attributes_Resistance_Physics; //物理伤害抵抗 标签
    FGameplayTag Attributes_Resistance_Pure;    //物理伤害抵抗 标签

    FGameplayTag Debuff_Burn;
    FGameplayTag Debuff_Stun;
    FGameplayTag Debuff_Arcane;
    FGameplayTag Debuff_Physical;

    FGameplayTag Debuff_Chance;
    FGameplayTag Debuff_Damage;
    FGameplayTag Debuff_Duration;
    FGameplayTag Debuff_Frequency;

    //TArray<FGameplayTag> DamageTypes;
    TMap<FGameplayTag, FGameplayTag> DamageTypesToResistance; //属性伤害标签对应属性抵抗标签
    TMap<FGameplayTag, FGameplayTag> DamageTypesToDebuffs;

    //
    FGameplayTag Effects_HitReact;

    FGameplayTag Player_Block_InputPressed;
    FGameplayTag Player_Block_InputHeld;
    FGameplayTag Player_Block_InputReleased;
    FGameplayTag Player_Block_CursorTrace;
    //
    FGameplayTag GameplayCue_FireBlast;
    FGameplayTag GameplayCue_ArcaneNova;

private:
    static FMyGameplayTags BuildTagSnapshot(bool bRegisterNativeTags);
    static FMyGameplayTags GameplayTags;
};
