// 调用链：Ground 输入通过 GAS Commit → World.ElementSurface → 使用现有地表过滤和有限租约。
#include "AbilitySystem/Unified/WorkshopWaterShallowsAbility.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WaterShallows, "Abilities.Mage.WaterShallows");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WaterShallowsCooldown, "Cooldown.Mage.WaterShallows");

UWorkshopWaterShallows::UWorkshopWaterShallows()
{
    SetAssetTags(FGameplayTagContainer(TAG_WaterShallows));
    CooldownTags.AddTag(TAG_WaterShallowsCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 13.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "WaterShallows", "水纹浅滩");
    SpellSummary = NSLOCTEXT("SkillCards", "WaterShallowsSummary", "在落点生成持续 6 秒的水地，使踏入的敌人潮湿。");

    auto* Surface = FObjectInitializer::Get().CreateDefaultSubobject<UAbilityElementSurfaceNodeConfig>(
        Graph, TEXT("ShallowsSurface"));
    Surface->Kind = ETerrainSurfaceType::Water;
    Surface->Radius = 240.f;
    Surface->Duration = 6.f;
    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("World.ElementSurface"), Surface}};
}
