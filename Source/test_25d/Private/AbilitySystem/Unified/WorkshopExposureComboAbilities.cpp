// 调用链：Query.SelectTargets -> Status.ConsumeExposure -> Spatial.TargetOrigin -> 局部选敌 -> 共享伤害与表现。
#include "AbilitySystem/Unified/WorkshopExposureComboAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/WorkshopEmberAbilities.h"
#include "AbilitySystem/Unified/WorkshopShatterAbilities.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EmberRupture, "Abilities.Mage.EmberRupture");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EmberRuptureCooldown, "Cooldown.Mage.EmberRupture");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ConductiveRelay, "Abilities.Mage.ConductiveRelay");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ConductiveRelayCooldown, "Cooldown.Mage.ConductiveRelay");

namespace
{
    /** 将种子查询和精确消耗接入既有区域入口，空放只结束，不产生下游动作。 */
    void PrependExposureSeed(UAbilityGraphDefinition& Graph, EAbilityExposure Expected, bool bRelay)
    {
        const auto& Initializer = FObjectInitializer::Get();
        auto* Seed = Initializer.CreateDefaultSubobject<UAbilitySelectTargetsConfig>(&Graph, TEXT("ComboSeed"));
        Seed->Radius = 450.f;
        Seed->MaxTargets = 1;
        Seed->TargetSelection = EAbilityTargetSelectionPolicy::Nearest;
        Seed->TargetFilter = 3 + static_cast<uint8>(Expected);
        auto* Consume =
            Initializer.CreateDefaultSubobject<UAbilityConsumeExposureNodeConfig>(&Graph, TEXT("ComboSpend"));
        Consume->Expected = Expected;
        auto* Anchor = Initializer.CreateDefaultSubobject<UAbilityTargetOriginConfig>(&Graph, TEXT("ComboAnchor"));
        Anchor->bRememberExcludedTarget = bRelay;

        const FGuid EffectQueryId = Graph.EntryNodeId;
        const FGuid SeedId(0, 0, 0, 4);
        const FGuid ConsumeId(0, 0, 0, 5);
        const FGuid AnchorId(0, 0, 0, 6);
        Graph.EntryNodeId = SeedId;
        Graph.Nodes.Append({{SeedId, TEXT("Query.SelectTargets"), Seed},
                            {ConsumeId, TEXT("Status.ConsumeExposure"), Consume},
                            {AnchorId, TEXT("Spatial.TargetOrigin"), Anchor}});
        Graph.Edges.Append({{SeedId, ConsumeId, TEXT("TargetFound")}, {ConsumeId, AnchorId, TEXT("Consumed")}});
        if (bRelay)
        {
            auto* Fuse = Initializer.CreateDefaultSubobject<UAbilityDelayNodeConfig>(&Graph, TEXT("RelayFuse"));
            Fuse->DelaySeconds = .22f;
            const FGuid FuseId(0, 0, 0, 7);
            Graph.Nodes.Add({FuseId, TEXT("Control.DelayOrigin"), Fuse});
            Graph.Edges.Append({{AnchorId, FuseId, TEXT("Located")}, {FuseId, EffectQueryId, TEXT("Completed")}});
        }
        else
        {
            Graph.Edges.Add({AnchorId, EffectQueryId, TEXT("Located")});
        }
    }
} // namespace

UWorkshopEmberRupture::UWorkshopEmberRupture()
{
    SetAssetTags(FGameplayTagContainer(TAG_EmberRupture));
    CooldownTags.AddTag(TAG_EmberRuptureCooldown);
    ManaCost = 20.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "EmberRupture", "熔印裂爆");
    SpellSummary =
        NSLOCTEXT("SkillCards", "EmberRuptureSummary",
                  "消耗落点 4.5 米内最近敌人的灼热，以它为中心对 2.35 米内最多 5 名敌人造成基础奥术伤害 32。");
    ConfigureArea(235.f, 32.f, EAbilityElement::Arcane,
                  TEXT("/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion.NS_FireExplosion"));
    CastChecked<UAbilityQueryNodeConfig>(Graph->Nodes[0].Config)->Query.MaxTargets = 5;
    PrependExposureSeed(*Graph, EAbilityExposure::Burning, false);
}

UWorkshopConductiveRelay::UWorkshopConductiveRelay()
{
    SetAssetTags(FGameplayTagContainer(TAG_ConductiveRelay));
    CooldownTags.AddTag(TAG_ConductiveRelayCooldown);
    ManaCost = 18.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "ConductiveRelay", "雷印传递");
    SpellSummary =
        NSLOCTEXT("SkillCards", "ConductiveRelaySummary",
                  "消耗落点 4.5 米内最近敌人的导电，0.22 秒后从其原位置打击 3.3 米内最近的最多 3 名其他敌人。");
    ConfigureArea(330.f, 21.f, EAbilityElement::Thunder,
                  TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Lightning.NS_Free_Spells_Aura_Lightning"));
    // 原区域后继只需要同类型的 TargetFound 和 Completed 端口，换成共享的有序邻近查询。
    auto* Neighbors =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilitySelectTargetsConfig>(Graph, TEXT("RelayNeighbors"));
    Neighbors->Radius = 330.f;
    Neighbors->MaxTargets = 3;
    Neighbors->TargetSelection = EAbilityTargetSelectionPolicy::Nearest;
    Neighbors->bExcludeAnchorTarget = true;
    Graph->Nodes[0].TypeId = TEXT("Query.SelectTargets");
    Graph->Nodes[0].Config = Neighbors;
    PrependExposureSeed(*Graph, EAbilityExposure::Conductive, true);
}
