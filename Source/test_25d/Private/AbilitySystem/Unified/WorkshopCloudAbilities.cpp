// 调用链：原生默认图 → World.ConvertSurface / World.DispelSurface → 原临时地形租约与云体积。
#include "AbilitySystem/Unified/WorkshopCloudAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/WorkshopDispelAbilities.h"
#include "AbilitySystem/Unified/WorkshopSurfaceConversionAbilities.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MistLift, "Abilities.Mage.MistLift");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MistLiftCooldown, "Cooldown.Mage.MistLift");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MistCondense, "Abilities.Mage.MistCondense");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MistCondenseCooldown, "Cooldown.Mage.MistCondense");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StormMist, "Abilities.Mage.StormMist");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StormMistCooldown, "Cooldown.Mage.StormMist");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MistDispel, "Abilities.Mage.MistDispel");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MistDispelCooldown, "Cooldown.Mage.MistDispel");

namespace
{
    /** 只配置地块来源和产物，转化事务继续由旧节点持有。 */
    void ConfigureWorkshopCloudConversion(UAbilityGraphDefinition& Graph, ETerrainSurfaceType Input,
                                          ETerrainSurfaceType Result)
    {
        auto* Conversion = FObjectInitializer::Get().CreateDefaultSubobject<UAbilityConvertSurfaceNodeConfig>(
            &Graph, TEXT("CloudConversion"));
        Conversion->bAllowCloud = true;
        Conversion->InputKind = Input;
        Conversion->ResultKind = Result;
        Graph.EntryNodeId = FGuid(0, 0, 0, 1);
        Graph.Nodes = {{Graph.EntryNodeId, TEXT("World.ConvertSurface"), Conversion}};
    }
} // namespace

UWorkshopMistLift::UWorkshopMistLift()
{
    SetAssetTags(FGameplayTagContainer(TAG_MistLift));
    CooldownTags.AddTag(TAG_MistLiftCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 14.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "MistLift", "升潮化雾");
    SpellSummary = NSLOCTEXT("SkillCards", "MistLiftSummary", "将附近自有潮地升成雾云，接触雾体的敌人受到水状态。");
    ConfigureWorkshopCloudConversion(*Graph, ETerrainSurfaceType::Water, ETerrainSurfaceType::MistCloud);
}

UWorkshopMistCondense::UWorkshopMistCondense()
{
    SetAssetTags(FGameplayTagContainer(TAG_MistCondense));
    CooldownTags.AddTag(TAG_MistCondenseCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 11.f;
    Cooldown = 5.f;
    SpellName = NSLOCTEXT("SkillCards", "MistCondense", "雾凝清池");
    SpellSummary = NSLOCTEXT("SkillCards", "MistCondenseSummary", "把附近自有雾云凝成潮地，保留原范围和剩余寿命。");
    ConfigureWorkshopCloudConversion(*Graph, ETerrainSurfaceType::MistCloud, ETerrainSurfaceType::Water);
}

UWorkshopStormMist::UWorkshopStormMist()
{
    SetAssetTags(FGameplayTagContainer(TAG_StormMist));
    CooldownTags.AddTag(TAG_StormMistCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 16.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "StormMist", "雷雾环生");
    SpellSummary = NSLOCTEXT("SkillCards", "StormMistSummary", "把附近自有雾云通电，对接触雾体的敌人施加雷状态。");
    ConfigureWorkshopCloudConversion(*Graph, ETerrainSurfaceType::MistCloud, ETerrainSurfaceType::ConductiveCloud);
}

UWorkshopMistDispel::UWorkshopMistDispel()
{
    SetAssetTags(FGameplayTagContainer(TAG_MistDispel));
    CooldownTags.AddTag(TAG_MistDispelCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 10.f;
    Cooldown = 5.f;
    SpellName = NSLOCTEXT("SkillCards", "MistDispel", "旋风驱雾");
    SpellSummary = NSLOCTEXT("SkillCards", "MistDispelSummary", "清除附近最近的至多两块自有临时云，保留同处的地表。");

    const auto& Initializer = FObjectInitializer::Get();
    auto* Dispel = Initializer.CreateDefaultSubobject<UAbilityDispelSurfaceNodeConfig>(Graph, TEXT("DispelCloud"));
    Dispel->bCloudLayer = true;
    Dispel->Kind = ETerrainSurfaceType::MistCloud;
    auto* Visual = Initializer.CreateDefaultSubobject<UAbilityElementPulseVisualConfig>(Graph, TEXT("CloudDispelVFX"));
    Visual->System = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Air.NS_Free_Spells_Buff_Air")));

    const FGuid VisualId(0, 0, 0, 2);
    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("World.DispelSurface"), Dispel},
                    {VisualId, TEXT("Presentation.ElementPulse"), Visual}};
    Graph->Edges = {{Graph->EntryNodeId, VisualId, TEXT("Removed")}};
}
