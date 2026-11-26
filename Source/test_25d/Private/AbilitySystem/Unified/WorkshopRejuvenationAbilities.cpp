// 调用链：工作台 T 键 -> UUnifiedGraphAbility -> GAS Commit -> GraphInstance -> 属性恢复、状态净化和现有护盾。
// 恢复额度在本次图内计数，属性经 ASC 同步。护盾由当前 Avatar 持有及复制，到期或死亡时清理。
#include "AbilitySystem/Unified/WorkshopRejuvenationAbilities.h"

#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Unified/AbilityBarrier.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystemComponent.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorkshopTidalMend, "Abilities.Mage.TidalMend");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorkshopTidalMendCooldown, "Cooldown.Mage.TidalMend");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorkshopFrostAegis, "Abilities.Mage.FrostAegis");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorkshopFrostAegisCooldown, "Cooldown.Mage.FrostAegis");

namespace
{
    constexpr float MaxRestoreAmount = 20.f;
    constexpr uint8 FirstExposure = static_cast<uint8>(EAbilityExposure::Burning);
    constexpr uint8 LastExposure = static_cast<uint8>(EAbilityExposure::Frozen);

    /** 与工作台 ResourceAmount 参数轴一致的单节点资源额度。 */
    bool IsValidRestoreAmount(float Amount)
    {
        return FMath::IsFinite(Amount) && Amount >= 1.f && Amount <= MaxRestoreAmount;
    }
} // namespace

bool UAbilityRestoreSelfNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!IsValidRestoreAmount(Amount))
    {
        Error = TEXT("Self restoration requires a finite resource amount between 1 and 20.");
    }
    return Error.IsEmpty();
}

bool UAbilityRestoreSelfNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                          FString& Error)
{
    if (Parameter != EAbilityConfigParameter::ResourceAmount)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!IsValidRestoreAmount(Value))
    {
        Error = TEXT("Self restoration requires a finite resource amount between 1 and 20.");
        return false;
    }
    Amount = Value;
    return true;
}

bool UAbilityRestoreSelfNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityRestoreSelfNodeConfig>(&Config);
    // 同一图中，重复经过同一个节点定义只消费一次准入额度。此节点无需附带伤害结果。
    if ((!Settings.bOncePerCast || Owner.AdmitDamageTrigger(Id, 1, 1, 0.f)) &&
        AbilityGameplayRuleNodes::RestoreResource(Input.SourceToken, Settings.bMana, Settings.Amount) && !bStopped &&
        !Owner.IsFinished())
    {
        Owner.Publish(Id, TEXT("Restored"), Input);
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

bool UAbilityClearSelfExposureNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    const auto Index = static_cast<uint8>(Expected);
    if (Index < FirstExposure || Index > LastExposure)
    {
        Error = TEXT("Self cleansing requires a known, nonempty elemental exposure.");
    }
    return Error.IsEmpty();
}

bool UAbilityClearSelfExposureNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                                FString& Error)
{
    if (Parameter != EAbilityConfigParameter::ElementalStatus)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!FMath::IsFinite(Value) || Value < FirstExposure || Value > LastExposure || Value != FMath::RoundToFloat(Value))
    {
        Error = TEXT("Self cleansing requires a known, nonempty elemental exposure.");
        return false;
    }
    Expected = static_cast<EAbilityExposure>(static_cast<uint8>(Value));
    return true;
}

bool UAbilityClearSelfExposureNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                   const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                   FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Error = TEXT("Self cleansing requires the caster's current life.");
        return false;
    }
    // ResolveSource 验证了服务器、Avatar 生命及其 ASC 的身份。状态宿主仍由原属性集管理。
    auto* Attributes = const_cast<UMyAttributeSet*>(Input.SourceToken.ASC->GetSet<UMyAttributeSet>());
    const auto& Settings = *CastChecked<UAbilityClearSelfExposureNodeConfig>(&Config);
    if (Attributes && Attributes->ConsumeElementalExposure(Settings.Expected) && !bStopped && !Owner.IsFinished())
    {
        Owner.Publish(Id, TEXT("Cleared"), Input);
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

UWorkshopTidalMend::UWorkshopTidalMend()
{
    SetAssetTags(FGameplayTagContainer(TAG_WorkshopTidalMend));
    CooldownTags.AddTag(TAG_WorkshopTidalMendCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Self;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ManaCost = 12.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "TidalMend", "潮息复苏");
    SpellSummary = NSLOCTEXT("SkillCards", "TidalMendSummary", "立即并每隔 0.6 秒恢复 10 点生命，共 3 次。");

    const auto& Initializer = FObjectInitializer::Get();
    auto* Repeat = Initializer.CreateDefaultSubobject<UAbilityRepeatNodeConfig>(Graph, TEXT("MendRepeat"));
    Repeat->Period = .6f;
    Repeat->Duration = 2.f;
    Repeat->bEmitImmediately = true;
    auto* Restore = Initializer.CreateDefaultSubobject<UAbilityRestoreSelfNodeConfig>(Graph, TEXT("MendRestore"));
    Restore->Amount = 10.f;
    auto* Anchor = Initializer.CreateDefaultSubobject<UAbilityAnchorNodeConfig>(Graph, TEXT("MendVisualAnchor"));
    Anchor->Anchor = EAbilitySpatialAnchor::Source;
    auto* Visual = Initializer.CreateDefaultSubobject<UAbilityElementPulseVisualConfig>(Graph, TEXT("MendVisual"));
    Visual->FallbackRadius = 150.f;
    Visual->System = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Healing.NS_Free_Spells_Buff_Healing")));

    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    const FGuid RestoreId(0, 0, 0, 2);
    const FGuid AnchorId(0, 0, 0, 3);
    const FGuid VisualId(0, 0, 0, 4);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("Control.Repeat"), Repeat},
                    {RestoreId, TEXT("Effect.RestoreSelf"), Restore},
                    {AnchorId, TEXT("Spatial.Anchor"), Anchor},
                    {VisualId, TEXT("Presentation.ElementPulse"), Visual}};
    Graph->Edges = {{Graph->EntryNodeId, RestoreId, TEXT("Pulse")},
                    {RestoreId, AnchorId, TEXT("Restored")},
                    {AnchorId, VisualId, TEXT("Completed")}};
}

UWorkshopFrostAegis::UWorkshopFrostAegis()
{
    SetAssetTags(FGameplayTagContainer(TAG_WorkshopFrostAegis));
    CooldownTags.AddTag(TAG_WorkshopFrostAegisCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Self;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ManaCost = 16.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "FrostAegis", "霜甲净化");
    SpellSummary = NSLOCTEXT("SkillCards", "FrostAegisSummary", "清除自身的冻僵，并获得 30 点护盾，持续 4 秒。");

    const auto& Initializer = FObjectInitializer::Get();
    auto* Cleanse =
        Initializer.CreateDefaultSubobject<UAbilityClearSelfExposureNodeConfig>(Graph, TEXT("AegisCleanse"));
    auto* Barrier = Initializer.CreateDefaultSubobject<UAbilityBarrierNodeConfig>(Graph, TEXT("AegisBarrier"));
    Barrier->Capacity = 30.f;
    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    const FGuid BarrierId(0, 0, 0, 2);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("Status.ClearSelfExposure"), Cleanse},
                    {BarrierId, TEXT("Status.Barrier"), Barrier}};
    Graph->Edges = {{Graph->EntryNodeId, BarrierId, TEXT("Completed")}};
}

void WorkshopRejuvenationAbilities::RegisterNodes(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Restore;
    Restore.TypeId = TEXT("Effect.RestoreSelf");
    Restore.ConfigClass = UAbilityRestoreSelfNodeConfig::StaticClass();
    Restore.ExecutionClass = UAbilityRestoreSelfNodeExecution::StaticClass();
    Restore.InputPayload = EAbilityPortPayload::Origin;
    Restore.bSupportsActorTargets = Restore.bSupportsMassTargets = Restore.bSupportsSnapshotTargets = true;
    Restore.bRuntimeFiniteSafe = true;
    Restore.Ports = {{TEXT("Restored"), EAbilityPortPayload::Origin, false},
                     {TEXT("Completed"), EAbilityPortPayload::Origin, true}};

    FAbilityNodeDescriptor Cleanse = Restore;
    Cleanse.TypeId = TEXT("Status.ClearSelfExposure");
    Cleanse.ConfigClass = UAbilityClearSelfExposureNodeConfig::StaticClass();
    Cleanse.ExecutionClass = UAbilityClearSelfExposureNodeExecution::StaticClass();
    Cleanse.Ports = {{TEXT("Cleared"), EAbilityPortPayload::Origin, false},
                     {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Restore, Cleanse})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Rejuvenation node registration: %s"), *Error);
        }
    }
}
