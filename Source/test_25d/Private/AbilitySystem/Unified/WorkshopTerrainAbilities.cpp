// 调用链：默认图由配方编译器冻结 -> Ground 施放 -> World 地表节点 -> Terrain 接触服务。
#include "AbilitySystem/Unified/WorkshopTerrainAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VenomMire, "Abilities.Mage.VenomMire");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VenomMireCooldown, "Cooldown.Mage.VenomMire");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MireIgnition, "Abilities.Mage.MireIgnition");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MireIgnitionCooldown, "Cooldown.Mage.MireIgnition");

UWorkshopVenomMire::UWorkshopVenomMire()
{
    SetAssetTags(FGameplayTagContainer(TAG_VenomMire));
    CooldownTags.AddTag(TAG_VenomMireCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 18.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "VenomMire", "沼蚀毒床");
    SpellSummary = NSLOCTEXT("SkillCards", "VenomMireSummary", "在落点生成持续 8 秒的毒床，可由施法者引燃。");

    auto* Surface =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilityElementSurfaceNodeConfig>(Graph, TEXT("MireSurface"));
    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("World.ElementSurface"), Surface}};
}

UWorkshopMireIgnition::UWorkshopMireIgnition()
{
    SetAssetTags(FGameplayTagContainer(TAG_MireIgnition));
    CooldownTags.AddTag(TAG_MireIgnitionCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 12.f;
    Cooldown = 5.f;
    SpellName = NSLOCTEXT("SkillCards", "MireIgnition", "熔沼引燃");
    SpellSummary =
        NSLOCTEXT("SkillCards", "MireIgnitionSummary", "引燃落点 3.6 米内最近的己方毒床，沿用原范围与剩余寿命。");

    auto* Ignite =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilityIgniteSurfaceNodeConfig>(Graph, TEXT("IgniteMire"));
    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("World.IgniteSurface"), Ignite}};
}
