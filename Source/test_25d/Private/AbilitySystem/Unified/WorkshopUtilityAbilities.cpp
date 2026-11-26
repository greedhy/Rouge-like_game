// 调用链：真实试装输入 -> UUnifiedGraphAbility::ExecutePreparedInput -> CheckCost/Commit ->
// 统一图运行任务 -> 护盾或目标状态服务。默认图由类拥有，局内修改沿用独立编译快照。
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"

#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Unified/AbilityBarrier.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystemComponent.h"
#include "Materials/MaterialInterface.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorkshopArcaneBarrier, "Abilities.Mage.ArcaneBarrier");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorkshopArcaneBarrierCooldown, "Cooldown.Mage.ArcaneBarrier");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorkshopStasisField, "Abilities.Mage.StasisField");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WorkshopStasisFieldCooldown, "Cooldown.Mage.StasisField");

UWorkshopUtilityAbility::UWorkshopUtilityAbility()
{
    Graph = CreateDefaultSubobject<UAbilityGraphDefinition>(TEXT("TrialGraph"));
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
}

FString UWorkshopUtilityAbility::GetDescription(int32 Level)
{
    return FText::Format(NSLOCTEXT("SkillCards", "UtilityDescription", "{0}\n{1}\n法力 {2} · 冷却 {3} 秒"), SpellName,
                         SpellSummary, FText::AsNumber(ManaCost), FText::AsNumber(Cooldown))
        .ToString();
}

const FGameplayTagContainer* UWorkshopUtilityAbility::GetCooldownTags() const
{
    return &CooldownTags;
}

bool UWorkshopUtilityAbility::CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                        FGameplayTagContainer* Relevant) const
{
    const auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
    const auto Mana = UMyAttributeSet::GetManaAttribute();
    return ASC && ASC->HasAttributeSetForAttribute(Mana) && ASC->GetNumericAttribute(Mana) >= ManaCost;
}

void UWorkshopUtilityAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                        FGameplayAbilityActivationInfo Activation) const
{
    if (Info && Info->IsNetAuthority())
    {
        APrototypeRuntime::SpendMana(Info->AvatarActor.Get(), ManaCost);
    }
}

void UWorkshopUtilityAbility::ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                            FGameplayAbilityActivationInfo Activation) const
{
    if (!Info || !Info->IsNetAuthority())
    {
        return;
    }
    auto* ASC = Info->AbilitySystemComponent.Get();
    if (!ASC)
    {
        return;
    }
    auto Spec =
        ASC->MakeOutgoingSpec(UPrototypeCooldownEffect::StaticClass(), GetAbilityLevel(), ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetDuration(Cooldown, true);
        Spec.Data->DynamicGrantedTags.AppendTags(CooldownTags);
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}

UWorkshopArcaneBarrier::UWorkshopArcaneBarrier()
{
    SetAssetTags(FGameplayTagContainer(TAG_WorkshopArcaneBarrier));
    CooldownTags.AddTag(TAG_WorkshopArcaneBarrierCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Self;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ManaCost = 18.f;
    Cooldown = 8.f;
    SpellName = NSLOCTEXT("SkillCards", "ArcaneBarrier", "奥术护盾");
    SpellSummary = NSLOCTEXT("SkillCards", "ArcaneBarrierSummary", "获得 40 点护盾，持续 4 秒。");

    // 图的私有配置必须以图为 Outer，确保预览/应用克隆时不会反写默认值。
    auto* Barrier = FObjectInitializer::Get().CreateDefaultSubobject<UAbilityBarrierNodeConfig>(Graph, TEXT("Barrier"));
    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("Status.Barrier"), Barrier}};
}

UWorkshopStasisField::UWorkshopStasisField()
{
    SetAssetTags(FGameplayTagContainer(TAG_WorkshopStasisField));
    CooldownTags.AddTag(TAG_WorkshopStasisFieldCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 24.f;
    Cooldown = 9.f;
    SpellName = NSLOCTEXT("SkillCards", "StasisField", "凝滞力场");
    SpellSummary = NSLOCTEXT("SkillCards", "StasisFieldSummary", "石化落点 2.8 米内最多 12 个敌人，持续 1.5 秒。");

    const auto& Initializer = FObjectInitializer::Get();
    auto* Query = Initializer.CreateDefaultSubobject<UAbilityQueryNodeConfig>(Graph, TEXT("AreaQuery"));
    Query->Query.Radius = 280.f;
    Query->Query.MaxTargets = 12;
    auto* Petrify = Initializer.CreateDefaultSubobject<UAbilityPetrifyNodeConfig>(Graph, TEXT("Petrify"));
    Petrify->DurationSeconds = 1.5f;
    Petrify->SoftStoneMaterial = TSoftObjectPtr<UMaterialInterface>(
        FSoftObjectPath(TEXT("/Game/CompositionBatch6/M_PetrifiedStone.M_PetrifiedStone")));
    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    const FGuid PetrifyId(0, 0, 0, 2);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("Query.Shape"), Query}, {PetrifyId, TEXT("Status.Petrify"), Petrify}};
    Graph->Edges = {{Graph->EntryNodeId, PetrifyId, TEXT("TargetFound")}};
}
