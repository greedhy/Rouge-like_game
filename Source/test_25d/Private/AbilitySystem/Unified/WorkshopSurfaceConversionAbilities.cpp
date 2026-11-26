// 调用链：原生默认图/配方快照 -> ConvertSurface 节点 -> 服务器地表租约 -> 原 Actor/Mass 接触和复制。
#include "AbilitySystem/Unified/WorkshopSurfaceConversionAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_LavaFreeze, "Abilities.Mage.LavaFreeze");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_LavaFreezeCooldown, "Cooldown.Mage.LavaFreeze");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostCorrode, "Abilities.Mage.FrostCorrode");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostCorrodeCooldown, "Cooldown.Mage.FrostCorrode");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WaterFreeze, "Abilities.Mage.WaterFreeze");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WaterFreezeCooldown, "Cooldown.Mage.WaterFreeze");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WaterElectrify, "Abilities.Mage.WaterElectrify");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WaterElectrifyCooldown, "Cooldown.Mage.WaterElectrify");

namespace
{
    /** 两个法术共用执行策略。原生默认图的差异只在类型默认值。 */
    void ConfigureConversionRecipe(UAbilityGraphDefinition* Graph, ETerrainSurfaceType InputKind,
                                   ETerrainSurfaceType ResultKind)
    {
        auto* Conversion = FObjectInitializer::Get().CreateDefaultSubobject<UAbilityConvertSurfaceNodeConfig>(
            Graph, TEXT("SurfaceConversion"));
        Conversion->InputKind = InputKind;
        Conversion->ResultKind = ResultKind;
        Graph->EntryNodeId = FGuid(0, 0, 0, 1);
        Graph->Nodes = {{Graph->EntryNodeId, TEXT("World.ConvertSurface"), Conversion}};
    }
} // namespace

bool UAbilityConvertSurfaceNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    const auto MaxKind = bAllowCloud ? ETerrainSurfaceType::ConductiveCloud : ETerrainSurfaceType::Conductive;
    if (!AbilityWorkshopTerrainNodes::IsValidSearchRadius(Radius) || InputKind > MaxKind || ResultKind > MaxKind ||
        InputKind == ResultKind)
    {
        Error = TEXT("Surface conversion requires a radius of 25–1200 cm and two different terrain kinds.");
    }
    return Error.IsEmpty();
}

bool UAbilityConvertSurfaceNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                             FString& Error)
{
    if (Parameter == EAbilityConfigParameter::Radius && AbilityWorkshopTerrainNodes::IsValidSearchRadius(Value))
    {
        Radius = Value;
        return true;
    }
    const auto MaxKind = bAllowCloud ? ETerrainSurfaceType::ConductiveCloud : ETerrainSurfaceType::Conductive;
    if ((Parameter == EAbilityConfigParameter::TerrainInputKind ||
         Parameter == EAbilityConfigParameter::TerrainSurfaceKind) &&
        FMath::IsFinite(Value) && Value >= 0.f && Value <= static_cast<float>(MaxKind) &&
        Value == FMath::RoundToFloat(Value))
    {
        auto& Kind = Parameter == EAbilityConfigParameter::TerrainInputKind ? InputKind : ResultKind;
        Kind = static_cast<ETerrainSurfaceType>(static_cast<uint8>(Value));
        return true;
    }
    Error = TEXT("Surface conversion only accepts a bounded radius or a supported terrain kind.");
    return false;
}

bool UAbilityConvertSurfaceNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    FString Error;
    return Parameter == EAbilityConfigScalar::Radius &&
           TrySetRecipeParameter(EAbilityConfigParameter::Radius, Radius * Multiplier, Error);
}

bool UAbilityConvertSurfaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        Error = TEXT("The terrain conversion source is no longer current.");
        return false;
    }

    const auto* Settings = CastChecked<UAbilityConvertSurfaceNodeConfig>(&Config);
    const auto Position = AbilityWorkshopTerrainNodes::ConvertSurface(Input.SourceToken, Input.Origin, Settings->Radius,
                                                                      Settings->InputKind, Settings->ResultKind);
    if (Position.IsSet() && !bStopped && !Owner.IsFinished())
    {
        auto Payload = Input;
        Payload.Origin = Position.GetValue();
        Payload.EventCenter = Payload.Origin;
        Owner.Publish(Id, TEXT("Transformed"), Payload);
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

UWorkshopLavaFreeze::UWorkshopLavaFreeze()
{
    SetAssetTags(FGameplayTagContainer(TAG_LavaFreeze));
    CooldownTags.AddTag(TAG_LavaFreezeCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 14.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "LavaFreeze", "寒封熔地");
    SpellSummary = NSLOCTEXT("SkillCards", "LavaFreezeSummary",
                             "将落点 3.6 米内最近的己方火地表转换为霜地，继承原范围与剩余寿命。");
    ConfigureConversionRecipe(Graph.Get(), ETerrainSurfaceType::Lava, ETerrainSurfaceType::Frost);
}

UWorkshopFrostCorrode::UWorkshopFrostCorrode()
{
    SetAssetTags(FGameplayTagContainer(TAG_FrostCorrode));
    CooldownTags.AddTag(TAG_FrostCorrodeCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 14.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "FrostCorrode", "腐化霜地");
    SpellSummary = NSLOCTEXT("SkillCards", "FrostCorrodeSummary",
                             "将落点 3.6 米内最近的己方霜地转换为毒床，继承原范围与剩余寿命。");
    ConfigureConversionRecipe(Graph.Get(), ETerrainSurfaceType::Frost, ETerrainSurfaceType::Toxic);
}

UWorkshopWaterFreeze::UWorkshopWaterFreeze()
{
    SetAssetTags(FGameplayTagContainer(TAG_WaterFreeze));
    CooldownTags.AddTag(TAG_WaterFreezeCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 12.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "WaterFreeze", "凝潮冻土");
    SpellSummary = NSLOCTEXT("SkillCards", "WaterFreezeSummary",
                             "将落点 3.6 米内最近的自有潮地转换为霜地，保留原尺寸与剩余寿命。");
    ConfigureConversionRecipe(Graph.Get(), ETerrainSurfaceType::Water, ETerrainSurfaceType::Frost);
}

UWorkshopWaterElectrify::UWorkshopWaterElectrify()
{
    SetAssetTags(FGameplayTagContainer(TAG_WaterElectrify));
    CooldownTags.AddTag(TAG_WaterElectrifyCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 13.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "WaterElectrify", "雷沼通电");
    SpellSummary = NSLOCTEXT("SkillCards", "WaterElectrifySummary",
                             "将落点 3.6 米内最近的自有潮地转换为通电地块，对接触的敌人施加雷状态。");
    ConfigureConversionRecipe(Graph.Get(), ETerrainSurfaceType::Water, ETerrainSurfaceType::Conductive);
}

void AbilityWorkshopSurfaceConversion::RegisterNodes(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Node;
    Node.TypeId = TEXT("World.ConvertSurface");
    Node.ConfigClass = UAbilityConvertSurfaceNodeConfig::StaticClass();
    Node.ExecutionClass = UAbilityConvertSurfaceNodeExecution::StaticClass();
    Node.InputPayload = EAbilityPortPayload::Origin;
    Node.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    Node.bSupportsActorTargets = Node.bSupportsMassTargets = Node.bSupportsSnapshotTargets = true;
    Node.bRuntimeFiniteSafe = true;
    Node.Ports = {{TEXT("Transformed"), EAbilityPortPayload::Origin, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Node.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Node, Error), TEXT("Terrain conversion node registration: %s"), *Error);
    }
}
