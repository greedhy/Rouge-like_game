// 调用链：Query.Shape.TargetFound → Condition.ElementalStatus → Passed/Rejected → 共享伤害及各自命中表现。
// 服务器使用查询时捕获的目标生命，配置中的两个伤害节点各自被配方编译并冻结。
#include "AbilitySystem/Unified/WorkshopTidalPhasesAbility.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/WorkshopPropagationAbilities.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TidalPhases, "Abilities.Mage.TidalPhases");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TidalPhasesCooldown, "Cooldown.Mage.TidalPhases");

UWorkshopTidalPhases::UWorkshopTidalPhases()
{
    SetAssetTags(FGameplayTagContainer(TAG_TidalPhases));
    CooldownTags.AddTag(TAG_TidalPhasesCooldown);
    ManaCost = 19.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "TidalPhases", "潮痕两相");
    SpellSummary = NSLOCTEXT("SkillCards", "TidalPhasesSummary",
                             "逐个判断落点 3 米内的敌人。潮湿目标受 24 点基础雷伤，其余受 18 点基础火伤。");

    ConfigureArea(300.f, 24.f, EAbilityElement::Thunder,
                  TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Air.NS_Free_Spells_Buff_Air"));
    const FGuid QueryId = Graph->EntryNodeId;
    const FGuid MatchDamageId(0, 0, 0, 2);
    const FGuid GateId(0, 0, 0, 4);
    const FGuid OtherDamageId(0, 0, 0, 5);
    const FGuid HitVisualId(0, 0, 0, 6);
    const auto& Initializer = FObjectInitializer::Get();
    auto* Gate = Initializer.CreateDefaultSubobject<UAbilityCombatRuleConfig>(Graph, TEXT("PhaseStateGate"));
    Gate->Rule = EAbilityCombatRule::ElementalStatus;
    Gate->Value = static_cast<float>(EAbilityExposure::Wet);
    auto* OtherDamage = Initializer.CreateDefaultSubobject<UAbilityDamageNodeConfig>(Graph, TEXT("OtherPhaseDamage"));
    OtherDamage->Damage = 18.f;
    OtherDamage->DamagePerLevel = 0.f;
    OtherDamage->ElementOverride = EAbilityElement::Fire;
    auto* HitVisual = Initializer.CreateDefaultSubobject<UWorkshopElementImpactConfig>(Graph, TEXT("PhaseImpact"));
    // 伤害元素可以分别替换，使用中性的命中表现，实际元素仍由目标状态体现。
    HitVisual->System = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Assets/Effects/Combat/NS_CombatImpact.NS_CombatImpact")));

    // 同一个 TargetFound 只经过一次条件判断，保留独立的查询完成端表现。
    Graph->Edges.RemoveAll(
        [QueryId, MatchDamageId](const FAbilityGraphEdge& Edge)
        {
            return Edge.From == QueryId && Edge.To == MatchDamageId && Edge.OutputPort == TEXT("TargetFound");
        });
    Graph->Nodes.Append({{GateId, TEXT("Condition.ElementalStatus"), Gate},
                         {OtherDamageId, TEXT("Effect.Damage"), OtherDamage},
                         {HitVisualId, TEXT("Presentation.ElementImpact"), HitVisual}});
    Graph->Edges.Append({{QueryId, GateId, TEXT("TargetFound")},
                         {GateId, MatchDamageId, TEXT("Passed")},
                         {GateId, OtherDamageId, TEXT("Rejected")},
                         {MatchDamageId, HitVisualId, TEXT("DamageResolved")},
                         {OtherDamageId, HitVisualId, TEXT("DamageResolved")}});
}
