// 调用链：Ground 目标方向 -> Source 位置锚 -> Query.Shape -> 元素条件 -> 定向推动。
// 表现使用查询完成口的落点及尺度，仅服务端选择目标并申请位移租约。
#include "AbilitySystem/Unified/WorkshopGaleAbilities.h"

#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GaleSweep, "Abilities.Mage.GaleSweep");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GaleSweepCooldown, "Cooldown.Mage.GaleSweep");

UWorkshopGaleSweep::UWorkshopGaleSweep()
{
    SetAssetTags(FGameplayTagContainer(TAG_GaleSweep));
    CooldownTags.AddTag(TAG_GaleSweepCooldown);
    ManaCost = 15.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "GaleSweep", "风压散流");
    SpellSummary = NSLOCTEXT("SkillCards", "GaleSweepSummary", "将前方扇区内潮湿的敌人沿施法方向推开。");

    const auto& Initializer = FObjectInitializer::Get();
    auto* Anchor = Initializer.CreateDefaultSubobject<UAbilityAnchorNodeConfig>(Graph, TEXT("GaleSourceAnchor"));
    Anchor->Anchor = EAbilitySpatialAnchor::Source;
    auto* Query = Initializer.CreateDefaultSubobject<UAbilityQueryNodeConfig>(Graph, TEXT("GaleCone"));
    Query->Query.Geometry = EAbilityQueryGeometry::Cone;
    Query->Query.Radius = 550.f;
    Query->Query.Range = 550.f;
    Query->Query.Width = 220.f;
    Query->Query.HalfHeight = 160.f;
    Query->Query.HalfAngleDegrees = 25.f;
    Query->Query.MaxTargets = 8;
    auto* Gate = Initializer.CreateDefaultSubobject<UAbilityCombatRuleConfig>(Graph, TEXT("GaleExposure"));
    Gate->Rule = EAbilityCombatRule::ElementalStatus;
    Gate->Value = static_cast<float>(EAbilityExposure::Wet);

    auto* Movement = Initializer.CreateDefaultSubobject<UAbilityTargetDisplaceNodeConfig>(Graph, TEXT("GaleMovement"));
    Movement->Settings = Initializer.CreateDefaultSubobject<UExtensionNode>(Movement, TEXT("GalePush"));
    Movement->Settings->Behavior = EExtensionKind::Displace;
    Movement->Settings->bSingleTarget = true;
    Movement->Settings->bPull = false;
    Movement->Settings->MoveDistance = 210.f;
    Movement->Settings->Duration = .24f;
    Movement->bUseCastDirection = true;
    Movement->bContinueOnRejected = true;

    auto* Visual = Initializer.CreateDefaultSubobject<UAbilityElementPulseVisualConfig>(Graph, TEXT("GaleVisual"));
    Visual->System = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Shockwave.NS_Free_Spells_Shockwave")));
    Visual->FallbackRadius = 180.f;
    Visual->ForwardOffset = 145.f;

    const FGuid AnchorId(0, 0, 0, 1);
    const FGuid QueryId(0, 0, 0, 2);
    const FGuid GateId(0, 0, 0, 3);
    const FGuid MoveId(0, 0, 0, 4);
    const FGuid VisualId(0, 0, 0, 5);
    Graph->EntryNodeId = AnchorId;
    Graph->Nodes = {{AnchorId, TEXT("Spatial.Anchor"), Anchor},
                    {QueryId, TEXT("Query.Shape"), Query},
                    {GateId, TEXT("Condition.ElementalStatus"), Gate},
                    {MoveId, TEXT("Movement.TargetDisplace"), Movement},
                    {VisualId, TEXT("Presentation.ElementPulse"), Visual}};
    Graph->Edges = {{AnchorId, QueryId, TEXT("Completed")},
                    {QueryId, GateId, TEXT("TargetFound")},
                    {GateId, MoveId, TEXT("Passed")},
                    {QueryId, VisualId, TEXT("Completed")}};
}
