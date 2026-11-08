// 主要调用链：GameplayEffect执行/复制 → 属性预处理与PostGameplayEffectExecute → 伤害、死亡、经验及UI属性委托；玩家ASC由PlayerState持有。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/MyAttributeSet.h"
#include <Net/UnrealNetwork.h>
#include <GameFramework/Character.h>
#include "AbilitySystemBlueprintLibrary.h"
#include "MyAbilityTypes.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Interface/CombatInterface.h"
#include "Interface/PlayerInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MyPlayerController.h"
#include "test_25d/MyLogChannels.h"
#include "Terrain/TerrainStatusComponent.h"

UMyAttributeSet::UMyAttributeSet()
{

    const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();
    TagsToAttributes.Add(GameplayTags.Attributes_Primary_Strength, GetStrengthAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Primary_Intelligence, GetIntelligenceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Primary_Resilience, GetResilienceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Primary_Vigor, GetVigorAttribute);

    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_Armor, GetArmorAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_ArmorPene, GetArmorPeneAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_BlockChance, GetBlockChanceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitChance, GetCriticalHitChanceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitDamage, GetCriticalHitDamageAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitResistence, GetCriticalHitResistenceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_HealthRegeneration, GetHealthRegenerationAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_ManaRegeneration, GetManaRegenerationAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_MaxHealth, GetMaxHealthAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Secondary_MaxMana, GetMaxManaAttribute);

    TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Fire, GetFireResistanceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Water, GetWaterResistanceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Thunder, GetThunderResistanceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Magic, GetMagicResistanceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Physics, GetPhysicsResistanceAttribute);
    TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Pure, GetPhysicsResistanceAttribute);
}
// 在属性修改前限制生命和法力到当前最大值。
void UMyAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{

    Super::PreAttributeChange(Attribute, NewValue);

    if (Attribute == GetHealthAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
    }
    if (Attribute == GetManaAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana());
    }
}

// 解析效果来源目标，过滤死亡目标，再分派资源、伤害与经验处理。
void UMyAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);

    FEffectProperties Props;
    SetEffectProperties(Data, Props);

    if (Props.TargetCharacter->Implements<UCombatInterface>())
    {
        if (ICombatInterface::Execute_IsDead(Props.TargetCharacter))
        {
            return;
        }
    }

    if (Data.EvaluatedData.Attribute == GetHealthAttribute())
    {
        //如果Health被修改了,就调用PostAttributeChange
        SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
    }
    if (Data.EvaluatedData.Attribute == GetManaAttribute())
    {
        //如果Mana被修改了,就调用PostAttributeChange
        SetMana(FMath::Clamp(GetMana(), 0.f, GetMaxMana()));
    }
    if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
    {
        HandleIncomingDamage(Props);
    }
    if (Data.EvaluatedData.Attribute == GetIncomingXPAttribute())
    {
        HandleIncomingXP(Props);
    }
}

// 解析效果上下文与 ActorInfo，汇总双方 ASC、控制器及角色。
void UMyAttributeSet::SetEffectProperties(const struct FGameplayEffectModCallbackData& Data,
                                          FEffectProperties& Props) const
{
    Props.EffectContextHandle = Data.EffectSpec.GetContext();
    Props.SourceASC = Props.EffectContextHandle.GetOriginalInstigatorAbilitySystemComponent();
    if (IsValid(Props.SourceASC) && Props.SourceASC->AbilityActorInfo.IsValid() &&
        Props.SourceASC->AbilityActorInfo->AvatarActor.IsValid())
    {
        Props.SourceAvatarActor = Props.SourceASC->AbilityActorInfo->AvatarActor.Get();
        Props.SourceController = Props.SourceASC->AbilityActorInfo->PlayerController.Get();
        //如果GetPlayerController为null,可能该Controller不是PlayerController
        if (Props.SourceController == nullptr && Props.SourceAvatarActor != nullptr)
        {
            if (const APawn* Pawn = Cast<APawn>(Props.SourceAvatarActor))
            {
                Props.SourceController = Pawn->GetController();
            }
        }
        if (Props.SourceController)
        {
            Props.SourceCharacter = Cast<ACharacter>(Props.SourceController->GetPawn());
        }
    }

    if (Data.Target.AbilityActorInfo.IsValid() && Data.Target.AbilityActorInfo->AvatarActor.IsValid())
    {
        Props.TargetAvatarActor = Data.Target.AbilityActorInfo->AvatarActor.Get();
        Props.TargetController = Data.Target.AbilityActorInfo->PlayerController.Get();
        Props.TargetCharacter = Cast<ACharacter>(Props.TargetAvatarActor);
        Props.TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Props.TargetAvatarActor);
    }
}

// 最大资源变化后消费升级补满标记，装备事务中跳过补满。
void UMyAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
    Super::PostAttributeChange(Attribute, OldValue, NewValue);
    if (bEquipmentChanging)
        return;

    if (Attribute == GetMaxHealthAttribute() && bTopOffHealth)
    {
        SetHealth(GetMaxHealth());
        bTopOffHealth = false;
    }
    if (Attribute == GetMaxManaAttribute() && bTopOffMana)
    {
        SetMana(GetMaxMana());
        bTopOffMana = false;
    }
}

// 收到 FireResistance 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_FireResistance(const FGameplayAttributeData& OldFireResistance) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, FireResistance, OldFireResistance);
}
// 收到 PureResistance 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_PureResistance(const FGameplayAttributeData& OldPureResistance) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, PureResistance, OldPureResistance);
}
// 收到 ThunderResistance 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_ThunderResistance(const FGameplayAttributeData& OldThunderResistance) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, ThunderResistance, OldThunderResistance);
}

// 收到 WaterResistance 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_WaterResistance(const FGameplayAttributeData& OldWaterResistance) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, WaterResistance, OldWaterResistance);
}

// 收到 MagicResistance 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_MagicResistance(const FGameplayAttributeData& OldMagicResistance) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, MagicResistance, OldMagicResistance);
}

// 收到 PhysicsResistance 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_PhysicsResistance(const FGameplayAttributeData& OldPhysicsResistance) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, PhysicsResistance, OldPhysicsResistance);
}

// 收到 Strength 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_Strength(const FGameplayAttributeData& OldStrength) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Strength, OldStrength);
}

// 收到 Intelligence 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_Intelligence(const FGameplayAttributeData& OldIntelligence) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Intelligence, OldIntelligence);
}

// 收到 Resilience 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_Resilience(const FGameplayAttributeData& OldResilience) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Resilience, OldResilience);
}

// 收到 Vigor 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_Vigor(const FGameplayAttributeData& OldVigor) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Vigor, OldVigor);
}

// 收到 Armor 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_Armor(const FGameplayAttributeData& OldArmor) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Armor, OldArmor);
}

// 收到 ArmorPene 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_ArmorPene(const FGameplayAttributeData& OldArmorPene) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, ArmorPene, OldArmorPene);
}

// 收到 BlockChance 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_BlockChance(const FGameplayAttributeData& OldBlockChance) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, BlockChance, OldBlockChance);
}

// 收到 CriticalHitChance 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_CriticalHitChance(const FGameplayAttributeData& OldCriticalHitChance) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, CriticalHitChance, OldCriticalHitChance);
}

// 收到 CriticalHitDamage 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_CriticalHitDamage(const FGameplayAttributeData& OldCriticalHitDamage) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, CriticalHitDamage, OldCriticalHitDamage);
}

// 收到 CriticalHitResistence 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_CriticalHitResistence(const FGameplayAttributeData& OldCriticalHitResistence) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, CriticalHitResistence, OldCriticalHitResistence);
}

// 收到 HealthRegeneration 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_HealthRegeneration(const FGameplayAttributeData& OldHealthRegeneration) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, HealthRegeneration, OldHealthRegeneration);
}

// 收到 ManaRegeneration 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_ManaRegeneration(const FGameplayAttributeData& OldManaRegeneration) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, ManaRegeneration, OldManaRegeneration);
}

// 登记属性复制，始终触发 GAS 属性复制通知。
void UMyAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Mana, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Strength, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Intelligence, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Resilience, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Vigor, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Armor, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, ArmorPene, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, BlockChance, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, CriticalHitChance, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, CriticalHitDamage, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, CriticalHitResistence, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, HealthRegeneration, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, ManaRegeneration, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, FireResistance, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, ThunderResistance, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, WaterResistance, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, MagicResistance, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, PhysicsResistance, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, PureResistance, COND_None, REPNOTIFY_Always);
}

// 收到 Health 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Health, OldHealth);
}

// 收到 MaxHealth 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, MaxHealth, OldMaxHealth);
}

// 收到 Mana 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_Mana(const FGameplayAttributeData& OldMana) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Mana, OldMana);
}

// 收到 MaxMana 属性复制后，通过 GAS 宏通知属性聚合器和监听者。
void UMyAttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana) const
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, MaxMana, OldMaxMana);
}

// 优先通知来源玩家显示伤害数字，否则通知目标玩家。
void UMyAttributeSet::ShowFloatingText(const FEffectProperties& Props, float Damage, bool bBlockHit,
                                       bool bCriticalHit) const
{
    if (Props.SourceCharacter != Props.TargetCharacter)
    {

        if (AMyPlayerController* PC =
                Props.SourceCharacter ? Cast<AMyPlayerController>(Props.SourceCharacter->Controller) : nullptr)
        {
            PC->ShowDamageNumber(Damage, Props.TargetCharacter, bBlockHit, bCriticalHit);
            return;
        }
        if (AMyPlayerController* PC =
                Props.TargetCharacter ? Cast<AMyPlayerController>(Props.TargetCharacter->Controller) : nullptr)
        {
            PC->ShowDamageNumber(Damage, Props.TargetCharacter, bBlockHit, bCriticalHit);
        }
    }
}
// 消费并清零伤害元属性，扣血后结算死亡或受击，再显示数字与施加减益。
void UMyAttributeSet::HandleIncomingDamage(const FEffectProperties& Props)
{

    //如果IncomingDamage被修改了,就调用PostAttributeChange
    const float LocalIncomingDamage = GetIncomingDamage();
    SetIncomingDamage(0.f);
    if (LocalIncomingDamage > 0.f)
    {
        const float NewHealth = GetHealth() - LocalIncomingDamage;
        SetHealth(FMath::Clamp(NewHealth, 0.f, GetMaxHealth()));

        const bool bFatal = NewHealth <= 0.f;
        if (bFatal)
        {
            ICombatInterface* CombatInterface = Cast<ICombatInterface>(Props.TargetAvatarActor);
            if (CombatInterface)
            {
                FVector Impulse = UMyAbilitySystemLibrary::GetDeathImpulse(Props.EffectContextHandle);

                CombatInterface->Die(UMyAbilitySystemLibrary::GetDeathImpulse(Props.EffectContextHandle));
            }
            SendXPEvent(Props);
        }
        else
        {
            if (!Cast<UTerrainStatusComponent>(Props.EffectContextHandle.GetSourceObject()) &&
                Props.TargetCharacter->Implements<UCombatInterface>() &&
                !ICombatInterface::Execute_IsBeingShocked(Props.TargetCharacter))
            {
                FGameplayTagContainer TagContainer;
                TagContainer.AddTag(FMyGameplayTags::Get().Effects_HitReact);
                Props.TargetASC->TryActivateAbilitiesByTag(TagContainer);
            }

            const FVector& KnockbackForce = UMyAbilitySystemLibrary::GetKnockbackForce(Props.EffectContextHandle);
            if (!KnockbackForce.IsNearlyZero(1.f))
            {
                Props.TargetCharacter->LaunchCharacter(KnockbackForce, true, true);
            }
        }

        const bool bBlock = UMyAbilitySystemLibrary::IsBlockedHit(Props.EffectContextHandle);
        const bool bCriticalHit = UMyAbilitySystemLibrary::IsCriticalHit(Props.EffectContextHandle);
        ShowFloatingText(Props, LocalIncomingDamage, bBlock, bCriticalHit);
        if (UMyAbilitySystemLibrary::IsSuccessfulDebuff(Props.EffectContextHandle))
        {
            Debuff(Props);
        }
    }
}
// 消费经验元属性，结算跨级奖励并设置升级资源补满标记。
void UMyAttributeSet::HandleIncomingXP(const FEffectProperties& Props)
{
    const float LocalIncomingXP = GetIncomingXP();
    SetIncomingXP(0.f);

    // Source Character is the owner, since GA_ListenForEvents applies GE_EventBasedEffect, adding to IncomingXP
    if (Props.SourceCharacter->Implements<UPlayerInterface>() && Props.SourceCharacter->Implements<UCombatInterface>())
    {
        const int32 CurrentLevel = ICombatInterface::Execute_GetPlayerLevel(Props.SourceCharacter);
        const int32 CurrentXP = IPlayerInterface::Execute_GetXP(Props.SourceCharacter);

        const int32 NewLevel =
            IPlayerInterface::Execute_FindLevelForXP(Props.SourceCharacter, CurrentXP + LocalIncomingXP);
        const int32 NumLevelUps = NewLevel - CurrentLevel;
        if (NumLevelUps > 0)
        {
            IPlayerInterface::Execute_AddToPlayerLevel(Props.SourceCharacter, NumLevelUps);

            int32 AttributePointsReward = 0;
            int32 SpellPointsReward = 0;

            for (int32 i = 0; i < NumLevelUps; ++i)
            {
                SpellPointsReward +=
                    IPlayerInterface::Execute_GetSpellPointsReward(Props.SourceCharacter, CurrentLevel + i);
                AttributePointsReward +=
                    IPlayerInterface::Execute_GetAttributePointsReward(Props.SourceCharacter, CurrentLevel + i);
            }

            IPlayerInterface::Execute_AddToAttributePoints(Props.SourceCharacter, AttributePointsReward);
            IPlayerInterface::Execute_AddToSpellPoints(Props.SourceCharacter, SpellPointsReward);

            bTopOffHealth = true;
            bTopOffMana = true;

            IPlayerInterface::Execute_LevelUp(Props.SourceCharacter);
        }

        IPlayerInterface::Execute_AddToXP(Props.SourceCharacter, LocalIncomingXP);
    }
}
// 构造限时周期伤害 GE，附加状态标签并施加给目标。
void UMyAttributeSet::Debuff(const FEffectProperties& Props)
{
    const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();
    FGameplayEffectContextHandle EffectContext = Props.SourceASC->MakeEffectContext();
    EffectContext.AddSourceObject(Props.SourceAvatarActor);

    const FGameplayTag DamageType = UMyAbilitySystemLibrary::GetDamageType(Props.EffectContextHandle);
    const float DebuffDamage = UMyAbilitySystemLibrary::GetDebuffDamage(Props.EffectContextHandle);
    const float DebuffDuration = UMyAbilitySystemLibrary::GetDebuffDuration(Props.EffectContextHandle);
    const float DebuffFrequency = UMyAbilitySystemLibrary::GetDebuffFrequency(Props.EffectContextHandle);

    FString DebuffName = FString::Printf(TEXT("DynamicDebuff_%s"), *DamageType.ToString());
    UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage(), FName(DebuffName));

    Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
    Effect->Period = DebuffFrequency;
    Effect->DurationMagnitude = FScalableFloat(DebuffDuration);

    const FGameplayTag DebuffTag = GameplayTags.DamageTypesToDebuffs[DamageType];

    FInheritedTagContainer TagContainer;
    TagContainer.AddTag(DebuffTag);
    UTargetTagsGameplayEffectComponent* TargetTagsComponent = NewObject<UTargetTagsGameplayEffectComponent>(Effect);

    //5.3弃用InheritableOwnedTagsContainer，InheritableOwnedTagsContainer无法添加tag.但是不知道为什么还是会掉血，但是不会触发niagara
    //初始化FInheritedTagContainer，addtag ，SetAndApplyTargetTagChanges

    //Effect->InheritableOwnedTagsContainer.AddTag(DebuffTag);  已失效
    if (DebuffTag.MatchesTagExact(GameplayTags.Debuff_Stun))
    {
        TagContainer.AddTag(GameplayTags.Player_Block_CursorTrace);
        TagContainer.AddTag(GameplayTags.Player_Block_InputHeld);
        TagContainer.AddTag(GameplayTags.Player_Block_InputPressed);
        TagContainer.AddTag(GameplayTags.Player_Block_InputReleased);
    }
    TargetTagsComponent->SetAndApplyTargetTagChanges(TagContainer);

    Effect->StackingType = EGameplayEffectStackingType::AggregateBySource;
    Effect->StackLimitCount = 1;

    const int32 Index = Effect->Modifiers.Num();
    Effect->Modifiers.Add(FGameplayModifierInfo());
    FGameplayModifierInfo& ModifierInfo = Effect->Modifiers[Index];

    ModifierInfo.ModifierMagnitude = FScalableFloat(DebuffDamage);
    ModifierInfo.ModifierOp = EGameplayModOp::Additive;
    ModifierInfo.Attribute = UMyAttributeSet::GetIncomingDamageAttribute();

    if (FGameplayEffectSpec* MutableSpec = new FGameplayEffectSpec(Effect, EffectContext, 1.f))
    {
        FMyGameplayEffectContext* MyContext = static_cast<FMyGameplayEffectContext*>(MutableSpec->GetContext().Get());
        TSharedPtr<FGameplayTag> DebuffDamageType = MakeShareable(new FGameplayTag(DamageType));
        MyContext->SetDamageType(DebuffDamageType);

        Props.TargetASC->ApplyGameplayEffectSpecToSelf(*MutableSpec);
    }
}
// 排除无效来源与自杀，按目标职业等级向来源发送经验事件。
void UMyAttributeSet::SendXPEvent(const FEffectProperties& Props)
{
    // Environmental damage has no credited player; never award self-kill XP.
    if (!IsValid(Props.SourceCharacter) || Props.SourceCharacter == Props.TargetCharacter ||
        !IsValid(Props.TargetCharacter))
        return;
    if (Props.TargetCharacter->Implements<UCombatInterface>())
    {

        const int32 TargetLevel = ICombatInterface::Execute_GetPlayerLevel(Props.TargetCharacter);

        const ECharacterClass TargetClass = ICombatInterface::Execute_GetCharacterClass(Props.TargetCharacter);
        const int32 XPReward =
            UMyAbilitySystemLibrary::GetXPRewardForClassAndLevel(Props.TargetCharacter, TargetClass, TargetLevel);

        const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();
        FGameplayEventData Payload;
        Payload.EventTag = GameplayTags.Attributes_Meta_IncomingXP;
        Payload.EventMagnitude = XPReward;
        UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Props.SourceCharacter,
                                                                 GameplayTags.Attributes_Meta_IncomingXP, Payload);
    }
}
