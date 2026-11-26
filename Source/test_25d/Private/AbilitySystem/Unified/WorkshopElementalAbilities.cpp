// 调用链：默认图装配 -> Compile 克隆 -> Ground TargetData/Commit -> 既有计时与查询 -> 分别施加状态和表现。
// 原生标签只表示技能身份、冷却。元素、时序及查询尺寸由图节点配置定义。
#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SoakingRain, "Abilities.Mage.SoakingRain");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SoakingRainCooldown, "Cooldown.Mage.SoakingRain");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CinderBlast, "Abilities.Mage.CinderBlast");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CinderBlastCooldown, "Cooldown.Mage.CinderBlast");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ThunderImpact, "Abilities.Mage.ThunderImpact");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ThunderImpactCooldown, "Cooldown.Mage.ThunderImpact");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CrystalBurst, "Abilities.Mage.CrystalBurst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CrystalBurstCooldown, "Cooldown.Mage.CrystalBurst");

UWorkshopElementalAreaAbility::UWorkshopElementalAreaAbility()
{
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    WorkshopDamageEffect = TSoftClassPtr<UGameplayEffect>(
        FSoftObjectPath(TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C")));
}

void UWorkshopElementalAreaAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
                                                    const FGameplayAbilityActorInfo* Info,
                                                    FGameplayAbilityActivationInfo Activation,
                                                    const FGameplayEventData* Event)
{
    if (Info && Info->IsNetAuthority() && !DamageEffectClass)
    {
        DamageEffectClass = WorkshopDamageEffect.LoadSynchronous();
        if (!DamageEffectClass)
        {
            EndAbility(Handle, Info, Activation, true, true);
            return;
        }
    }
    Super::ActivateAbility(Handle, Info, Activation, Event);
}

void UWorkshopElementalAreaAbility::ConfigureArea(float Radius, float DamageAmount, EAbilityElement Element,
                                                  const TCHAR* VisualPath)
{
    // 和护盾默认图一样，配置的 Outer 必须是图，保证编译、预览与施法快照均深拷贝。
    const auto& Initializer = FObjectInitializer::Get();
    auto* Query = Initializer.CreateDefaultSubobject<UAbilityQueryNodeConfig>(Graph, TEXT("ElementQuery"));
    Query->Query.Radius = Radius;
    Query->Query.MaxTargets = 8;

    const FGuid QueryId(0, 0, 0, 1);
    const FGuid EffectId(0, 0, 0, 2);
    const FGuid VisualId(0, 0, 0, 3);
    Graph->EntryNodeId = QueryId;
    Graph->Nodes.Add({QueryId, TEXT("Query.Shape"), Query});
    if (DamageAmount > 0.f)
    {
        auto* DamageNode = Initializer.CreateDefaultSubobject<UAbilityDamageNodeConfig>(Graph, TEXT("ElementDamage"));
        DamageNode->Damage = DamageAmount;
        DamageNode->DamagePerLevel = 0.f;
        DamageNode->ElementOverride = Element;
        Graph->Nodes.Add({EffectId, TEXT("Effect.Damage"), DamageNode});
    }
    else
    {
        auto* Exposure = Initializer.CreateDefaultSubobject<UAbilityApplyExposureConfig>(Graph, TEXT("ElementStatus"));
        Exposure->Element = Element;
        Graph->Nodes.Add({EffectId, TEXT("Status.ApplyExposure"), Exposure});
    }

    auto* Visual = Initializer.CreateDefaultSubobject<UAbilityElementPulseVisualConfig>(Graph, TEXT("ElementVisual"));
    Visual->System = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(VisualPath));
    Graph->Nodes.Add({VisualId, TEXT("Presentation.ElementPulse"), Visual});
    Graph->Edges = {{QueryId, EffectId, TEXT("TargetFound")}, {QueryId, VisualId, TEXT("Completed")}};
}

void UWorkshopElementalAreaAbility::PrependTiming(FName Type, UAbilityNodeConfig* Config, FName QueryPort)
{
    const FGuid TimingId(0, 0, 0, 4);
    Graph->Nodes.Add({TimingId, Type, Config});
    Graph->Edges.Add({TimingId, Graph->EntryNodeId, QueryPort});
    Graph->EntryNodeId = TimingId;
}

UWorkshopSoakingRain::UWorkshopSoakingRain()
{
    SetAssetTags(FGameplayTagContainer(TAG_SoakingRain));
    CooldownTags.AddTag(TAG_SoakingRainCooldown);
    ManaCost = 12.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "SoakingRain", "润泽雨幕");
    SpellSummary = NSLOCTEXT("SkillCards", "SoakingRainSummary", "润湿落点 3.2 米内的敌人，连续扫描 3 次。");
    ConfigureArea(320.f, 0.f, EAbilityElement::Water,
                  TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Air.NS_Free_Spells_Buff_Air"));
    auto* Repeat =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilityRepeatNodeConfig>(Graph, TEXT("RainCadence"));
    Repeat->bEmitImmediately = true;
    Repeat->Period = .75f;
    // 允许频率卡独立修改脉冲间隔。由 Count 决定正常完成，Duration 只是硬截止。
    Repeat->Duration = 1.6f;
    PrependTiming(TEXT("Control.Repeat"), Repeat, TEXT("Pulse"));
}

UWorkshopCinderBlast::UWorkshopCinderBlast()
{
    SetAssetTags(FGameplayTagContainer(TAG_CinderBlast));
    CooldownTags.AddTag(TAG_CinderBlastCooldown);
    ManaCost = 20.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "CinderBlast", "炽焰涌爆");
    SpellSummary = NSLOCTEXT("SkillCards", "CinderBlastSummary", "0.45 秒后在落点 2.8 米内造成基础 36 点火伤。");
    ConfigureArea(280.f, 36.f, EAbilityElement::Fire,
                  TEXT("/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion.NS_FireExplosion"));
    auto* Delay = FObjectInitializer::Get().CreateDefaultSubobject<UAbilityDelayNodeConfig>(Graph, TEXT("Ignition"));
    Delay->DelaySeconds = .45f;
    PrependTiming(TEXT("Control.DelayOrigin"), Delay, TEXT("Completed"));
}

UWorkshopThunderImpact::UWorkshopThunderImpact()
{
    SetAssetTags(FGameplayTagContainer(TAG_ThunderImpact));
    CooldownTags.AddTag(TAG_ThunderImpactCooldown);
    ManaCost = 19.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "ThunderImpact", "雷鸣骤击");
    SpellSummary = NSLOCTEXT("SkillCards", "ThunderImpactSummary", "对落点 3 米内的敌人造成基础 28 点雷伤。");
    ConfigureArea(300.f, 28.f, EAbilityElement::Thunder,
                  TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Lightning.NS_Free_Spells_Buff_Lightning"));
}

UWorkshopCrystalBurst::UWorkshopCrystalBurst()
{
    SetAssetTags(FGameplayTagContainer(TAG_CrystalBurst));
    CooldownTags.AddTag(TAG_CrystalBurstCooldown);
    ManaCost = 17.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "CrystalBurst", "寒晶爆发");
    SpellSummary = NSLOCTEXT("SkillCards", "CrystalBurstSummary", "对落点 2.5 米内的敌人造成基础 24 点冰伤。");
    ConfigureArea(250.f, 24.f, EAbilityElement::Ice,
                  TEXT("/Game/Art/CompositionEffects/NS_Frost_Impact.NS_Frost_Impact"));
}
