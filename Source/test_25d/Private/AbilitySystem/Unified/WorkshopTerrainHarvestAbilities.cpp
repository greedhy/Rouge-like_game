// 调用链：实际地面目标 -> 服务器 Commit -> 同层地表消费 -> Removed 成功端口 -> 独立收益。
// 护盾由玩家 Avatar 持有及复制。沼息恢复的时钟归本次 GA 图，取消后停止恢复。
#include "AbilitySystem/Unified/WorkshopTerrainHarvestAbilities.h"

#include "AbilitySystem/Unified/AbilityBarrier.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/WorkshopDispelAbilities.h"
#include "AbilitySystem/Unified/WorkshopRejuvenationAbilities.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostHarvest, "Abilities.Mage.FrostHarvest");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostHarvestCooldown, "Cooldown.Mage.FrostHarvest");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BogHarvest, "Abilities.Mage.BogHarvest");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BogHarvestCooldown, "Cooldown.Mage.BogHarvest");

namespace
{
    /** 使用已经注册的地表移除节点，租约归属与成功载荷沿用原行为。 */
    void ConfigureHarvest(UAbilityGraphDefinition& Graph, ETerrainSurfaceType Kind)
    {
        auto* Acquire = FObjectInitializer::Get().CreateDefaultSubobject<UAbilityDispelSurfaceNodeConfig>(
            &Graph, TEXT("HarvestTerrain"));
        Acquire->Kind = Kind;
        Acquire->bFilterByKind = true;
        Acquire->MaxTargets = 1;

        Graph.EntryNodeId = FGuid(0, 0, 0, 1);
        Graph.Nodes = {{Graph.EntryNodeId, TEXT("World.DispelSurface"), Acquire}};
    }
} // namespace

UWorkshopFrostHarvest::UWorkshopFrostHarvest()
{
    SetAssetTags(FGameplayTagContainer(TAG_FrostHarvest));
    CooldownTags.AddTag(TAG_FrostHarvestCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 14.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "FrostHarvest", "霜壳回收");
    SpellSummary = NSLOCTEXT("SkillCards", "FrostHarvestSummary",
                             "消耗落点 3.6 米内最近的一块自有霜地，获得 40 点护盾，持续 5 秒。");

    ConfigureHarvest(*Graph, ETerrainSurfaceType::Frost);
    auto* Barrier =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilityBarrierNodeConfig>(Graph, TEXT("HarvestBarrier"));
    Barrier->Capacity = 40.f;
    Barrier->Duration = 5.f;

    const FGuid BarrierId(0, 0, 0, 2);
    Graph->Nodes.Add({BarrierId, TEXT("Status.Barrier"), Barrier});
    Graph->Edges.Add({Graph->EntryNodeId, BarrierId, TEXT("Removed")});
}

UWorkshopBogHarvest::UWorkshopBogHarvest()
{
    SetAssetTags(FGameplayTagContainer(TAG_BogHarvest));
    CooldownTags.AddTag(TAG_BogHarvestCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 12.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "BogHarvest", "沼息回收");
    SpellSummary = NSLOCTEXT("SkillCards", "BogHarvestSummary",
                             "消耗落点 3.6 米内最近的一块自有毒地，恢复自身生命 3 次，每次 8 点，间隔 0.6 秒。");

    ConfigureHarvest(*Graph, ETerrainSurfaceType::Toxic);
    const auto& Initializer = FObjectInitializer::Get();
    auto* Repeat = Initializer.CreateDefaultSubobject<UAbilityRepeatNodeConfig>(Graph, TEXT("HarvestPulse"));
    Repeat->Count = 3;
    Repeat->Period = .6f;
    Repeat->Duration = 2.f;
    Repeat->bEmitImmediately = true;
    auto* Restore = Initializer.CreateDefaultSubobject<UAbilityRestoreSelfNodeConfig>(Graph, TEXT("HarvestRestore"));
    Restore->Amount = 8.f;
    auto* Anchor = Initializer.CreateDefaultSubobject<UAbilityAnchorNodeConfig>(Graph, TEXT("HarvestCasterAnchor"));
    Anchor->Anchor = EAbilitySpatialAnchor::Source;
    auto* Visual = Initializer.CreateDefaultSubobject<UAbilityElementPulseVisualConfig>(Graph, TEXT("HarvestVisual"));
    Visual->System = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Healing.NS_Free_Spells_Buff_Healing")));

    // 恢复先读取原施法者身份，再从施法者当前坐标播放表现；回收位置仍可供其它分支使用。
    const FGuid RepeatId(0, 0, 0, 2);
    const FGuid RestoreId(0, 0, 0, 3);
    const FGuid AnchorId(0, 0, 0, 4);
    const FGuid VisualId(0, 0, 0, 5);
    Graph->Nodes.Append({{RepeatId, TEXT("Control.Repeat"), Repeat},
                         {RestoreId, TEXT("Effect.RestoreSelf"), Restore},
                         {AnchorId, TEXT("Spatial.Anchor"), Anchor},
                         {VisualId, TEXT("Presentation.ElementPulse"), Visual}});
    Graph->Edges = {{Graph->EntryNodeId, RepeatId, TEXT("Removed")},
                    {RepeatId, RestoreId, TEXT("Pulse")},
                    {RestoreId, AnchorId, TEXT("Restored")},
                    {AnchorId, VisualId, TEXT("Completed")}};
}
