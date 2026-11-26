// 调用链：GAS Commit → Query.SelectTargets → Status.ConsumeExposure → 元素反应或资源恢复。
// 图拥有默认配置；目标生命拥有元素状态；恢复的单次限额由本次图执行实例持有。
#include "AbilitySystem/Unified/WorkshopReactionShiftAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/WorkshopEmberAbilities.h"
#include "AbilitySystem/Unified/WorkshopRejuvenationAbilities.h"
#include "AbilitySystem/Unified/WorkshopShatterAbilities.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SteamCondensation, "Abilities.Mage.SteamCondensation");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SteamCondensationCooldown, "Cooldown.Mage.SteamCondensation");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ConductiveRecharge, "Abilities.Mage.ConductiveRecharge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ConductiveRechargeCooldown, "Cooldown.Mage.ConductiveRecharge");

namespace
{
    const FGuid SelectId(0, 0, 0, 1);
    const FGuid ConsumeId(0, 0, 0, 2);
    const FGuid PulseId(0, 0, 0, 3);
    const FGuid ResultId(0, 0, 0, 4);

    /** 同一半径驱动选敌和落点表现，所有不同结果都依赖实际消耗成功。 */
    void BuildExposureQuery(UAbilityGraphDefinition& Graph, EAbilityExposure Status, int32 MaxTargets,
                            const TCHAR* VisualPath)
    {
        const auto& Initializer = FObjectInitializer::Get();
        auto* Query = Initializer.CreateDefaultSubobject<UAbilitySelectTargetsConfig>(&Graph, TEXT("ReactionQuery"));
        Query->Radius = 420.f;
        Query->MaxTargets = MaxTargets;
        Query->TargetFilter = 3 + static_cast<uint8>(Status);
        auto* Consume =
            Initializer.CreateDefaultSubobject<UAbilityConsumeExposureNodeConfig>(&Graph, TEXT("ConsumeReaction"));
        Consume->Expected = Status;
        auto* Pulse =
            Initializer.CreateDefaultSubobject<UAbilityElementPulseVisualConfig>(&Graph, TEXT("ReactionVisual"));
        Pulse->System = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(VisualPath));

        Graph.EntryNodeId = SelectId;
        Graph.Nodes = {{SelectId, TEXT("Query.SelectTargets"), Query},
                       {ConsumeId, TEXT("Status.ConsumeExposure"), Consume},
                       {PulseId, TEXT("Presentation.ElementPulse"), Pulse}};
        Graph.Edges = {{SelectId, ConsumeId, TEXT("TargetFound")}, {SelectId, PulseId, TEXT("Completed")}};
    }
} // namespace

UWorkshopSteamCondensation::UWorkshopSteamCondensation()
{
    SetAssetTags(FGameplayTagContainer(TAG_SteamCondensation));
    CooldownTags.AddTag(TAG_SteamCondensationCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 12.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "SteamCondensation", "蒸汽冷凝");
    SpellSummary = NSLOCTEXT("SkillCards", "SteamCondensationSummary", "选中蒸汽状态的敌人，消耗蒸汽并施加潮湿。");

    BuildExposureQuery(*Graph, EAbilityExposure::Steam, 5,
                       TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Air.NS_Free_Spells_Buff_Air"));
    auto* Apply =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilityApplyExposureConfig>(Graph, TEXT("CondenseReaction"));
    Graph->Nodes.Add({ResultId, TEXT("Status.ApplyExposure"), Apply});
    Graph->Edges.Add({ConsumeId, ResultId, TEXT("Consumed")});
}

UWorkshopConductiveRecharge::UWorkshopConductiveRecharge()
{
    SetAssetTags(FGameplayTagContainer(TAG_ConductiveRecharge));
    CooldownTags.AddTag(TAG_ConductiveRechargeCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 5.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "ConductiveRecharge", "导电回充");
    SpellSummary = NSLOCTEXT("SkillCards", "ConductiveRechargeSummary", "消耗一名敌人的导电，恢复自身 9 点法力。");

    BuildExposureQuery(
        *Graph, EAbilityExposure::Conductive, 1,
        TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Lightning.NS_Free_Spells_Aura_Lightning"));
    const auto& Initializer = FObjectInitializer::Get();
    auto* Origin = Initializer.CreateDefaultSubobject<UAbilityTargetOriginConfig>(Graph, TEXT("RechargeOrigin"));
    auto* Restore = Initializer.CreateDefaultSubobject<UAbilityRestoreSelfNodeConfig>(Graph, TEXT("RechargeResource"));
    Restore->bMana = true;
    Restore->bOncePerCast = true;
    const FGuid RestoreId(0, 0, 0, 5);
    Graph->Nodes.Add({ResultId, TEXT("Spatial.TargetOrigin"), Origin});
    Graph->Nodes.Add({RestoreId, TEXT("Effect.RestoreSelf"), Restore});
    Graph->Edges.Add({ConsumeId, ResultId, TEXT("Consumed")});
    Graph->Edges.Add({ResultId, RestoreId, TEXT("Located")});
}
