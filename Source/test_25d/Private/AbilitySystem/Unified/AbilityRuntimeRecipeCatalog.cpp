// 调用链：面板/自动化 -> 只读职业目录 -> 分类与默认图提取；本模块不执行玩法或授予技能。
#include "AbilitySystem/Unified/AbilityRuntimeRecipeCatalog.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "AbilitySystem/Unified/MageGraphAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionAbility.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "AbilitySystem/Ability/MyFireBolt.h"
#include "AbilitySystem/Ability/MyArcaneNova.h"
#include "AbilitySystem/Ability/MyArcaneShardAbility.h"
#include "AbilitySystem/Ability/MyBeamSpell.h"
#include "AbilitySystem/Ability/MyOrbitingFireBoltAbility.h"
#include "AbilitySystem/Ability/MyAttributeEventAbility.h"
#include "AbilitySystem/Ability/MyPassiveAbility.h"
#include "PureMass/PureMassAbility.h"

namespace
{
    /** 使用稳定身份标记已有占位项；不把名字所暗示的吸取/护盾效果当成已经存在。 */
    bool IsProtectedPlaceholder(FGameplayTag Tag)
    {
        const FName Name = Tag.GetTagName();
        return Name == TEXT("Abilities.Passive.HaloOfProtection") || Name == TEXT("Abilities.Passive.LifeSiphon") ||
               Name == TEXT("Abilities.Passive.ManaSiphon");
    }

    /** 分类按实际派生类先匹配覆写前端，再检查原型基类；不按资产目录猜执行语义。 */
    void ClassifyFrontend(const UGameplayAbility& Ability, FAbilityRuntimeBaseDefinition& Definition)
    {
        using EFront = EAbilityRuntimeFrontendKind;
        using ETarget = EAbilityRuntimeTargetContract;
        if (Ability.IsA<UMyAttributeEventAbility>())
        {
            Definition.FrontendKind = EFront::Infrastructure;
            Definition.TargetContract = ETarget::Self;
            Definition.bSelectable = false;
            Definition.Diagnostic = TEXT("Internal attribute/experience listener; not a combat composition fragment.");
        }
        else if (IsProtectedPlaceholder(Definition.AbilityTag))
        {
            Definition.FrontendKind = EFront::Passive;
            Definition.TargetContract = ETarget::Self;
            Definition.bSelectable = false;
            Definition.Diagnostic =
                TEXT("Existing placeholder passive: preserve activation and presentation; no inferred combat effect.");
        }
        else if (const auto* Unified = Cast<UUnifiedGraphAbility>(&Ability))
        {
            Definition.FrontendKind = EFront::UnifiedGraph;
            Definition.TargetContract = Unified->TargetMode == EUnifiedAbilityTargetMode::Self     ? ETarget::Self
                                        : Unified->TargetMode == EUnifiedAbilityTargetMode::Ground ? ETarget::Ground
                                                                                                   : ETarget::Enemy;
            if (const auto* Mage = Cast<UMageGraphAbility>(&Ability); Mage && !Mage->SpellName.IsEmpty())
            {
                Definition.DisplayName = Mage->SpellName;
            }
        }
        else if (Ability.IsA<UBatch6ChainAbility>())
        {
            Definition.FrontendKind = EFront::Batch6Chain;
            Definition.TargetContract = ETarget::Enemy;
            Definition.bSupportsActorTargets = Definition.bSupportsMassTargets = true;
        }
        else if (Ability.IsA<UBatch6PetrifyAbility>())
        {
            Definition.FrontendKind = EFront::Batch6Petrify;
            Definition.TargetContract = ETarget::Self;
            Definition.bSupportsActorTargets = Definition.bSupportsMassTargets = true;
        }
        else if (const auto* Prototype = Cast<UPrototypeAbility>(&Ability))
        {
            Definition.FrontendKind = Ability.IsA<UExtensionAbility>()  ? EFront::Extension
                                      : Ability.IsA<UPureMassAbility>() ? EFront::MixedPrototype
                                                                        : EFront::ActorPrototype;
            Definition.bRequiresMassSubsystem =
                Definition.FrontendKind == EFront::MixedPrototype || Definition.FrontendKind == EFront::Extension;
            // 具体前端已有目标通路的能力；基础配置能否运行由 bHasDefaultGraph 单独说明。
            Definition.bSupportsActorTargets = true;
            Definition.bSupportsMassTargets = Definition.bRequiresMassSubsystem;
            if (Prototype->Prototype)
            {
                const auto* Config = Prototype->Prototype.Get();
                Definition.DisplayName = Config->DisplayName.IsEmpty() ? Definition.DisplayName : Config->DisplayName;
                if (Config->bTargetSelf || Prototype->IsInputToggleAbility())
                {
                    Definition.TargetContract = ETarget::Self;
                }
                else if (const auto* Extension = Cast<UExtensionNode>(Config))
                {
                    Definition.TargetContract =
                        Extension->Behavior == EExtensionKind::Attach || Extension->Behavior == EExtensionKind::Stack ||
                                Extension->Behavior == EExtensionKind::Chain ||
                                (Extension->Behavior == EExtensionKind::Displace && Extension->bSingleTarget)
                            ? ETarget::Enemy
                            : ETarget::GroundOrEnemy;
                }
                else
                {
                    Definition.TargetContract = Config->Kind == EAbilityPrototypeKind::Beam ||
                                                        Config->Kind == EAbilityPrototypeKind::Homing ||
                                                        Config->Kind == EAbilityPrototypeKind::Chain
                                                    ? ETarget::Enemy
                                                    : ETarget::GroundOrEnemy;
                }
            }
        }
        else if (Ability.IsA<UMyFireBolt>())
        {
            Definition.FrontendKind = EFront::FireBolt;
            Definition.bSupportsActorTargets = true;
        }
        else if (Ability.IsA<UMyArcaneNova>())
        {
            Definition.FrontendKind = EFront::Nova;
            Definition.TargetContract = ETarget::Self;
            Definition.bSupportsActorTargets = true;
        }
        else if (Ability.IsA<UMyArcaneShardAbility>())
        {
            Definition.FrontendKind = EFront::Shards;
            Definition.TargetContract = ETarget::Ground;
            Definition.bSupportsActorTargets = true;
        }
        else if (Ability.IsA<UMyBeamSpell>())
        {
            Definition.FrontendKind = EFront::Beam;
            Definition.bSupportsActorTargets = true;
        }
        else if (Ability.IsA<UMyOrbitingFireBoltAbility>())
        {
            Definition.FrontendKind = EFront::Orbit;
            Definition.TargetContract = ETarget::Self;
            Definition.bSupportsActorTargets = true;
        }
        else if (Ability.IsA<UMyPassiveAbility>())
        {
            Definition.FrontendKind = EFront::Passive;
            Definition.TargetContract = ETarget::Self;
        }
    }

    /** 图中声明的能力再按明确配置收紧；不把 Registry 的可选 Mass 能力冒充当前配置已启用。 */
    void DescribeGraph(FAbilityRuntimeBaseDefinition& Definition, const FAbilityNodeRegistry& Registry)
    {
        bool bActor = true;
        bool bMass = true;
        for (const auto& Node : Definition.DefaultGraph->Nodes)
        {
            const auto* Descriptor = Registry.Find(Node.TypeId);
            Definition.Capabilities.AddUnique(Node.TypeId);
            Definition.bContainsLegacyMacro |= Node.TypeId.ToString().StartsWith(TEXT("Legacy."));
            if (!Descriptor)
            {
                bActor = bMass = false;
                continue;
            }
            bActor &= Descriptor->bSupportsActorTargets;
            bMass &= Descriptor->bSupportsMassTargets;
            if (const auto* Query = Cast<UAbilityQueryNodeConfig>(Node.Config))
            {
                bActor &= Query->Query.bIncludeActors;
                bMass &= Query->Query.bIncludeMass;
            }
            if (const auto* Nova = Cast<UAbilityNovaNodeConfig>(Node.Config))
            {
                bMass &= Nova->bIncludeMass;
            }
            if (const auto* Shards = Cast<UAbilityArcaneShardNodeConfig>(Node.Config))
            {
                bMass &= Shards->bIncludeMass;
            }
        }
        Definition.bSupportsActorTargets = bActor;
        Definition.bSupportsMassTargets = bMass;
        Definition.bRequiresMassSubsystem |= bMass;
    }

    /** 建立一行目录元数据；默认图缺口保留在行内，不通过漏行掩盖尚未完成的拆分。 */
    FAbilityRuntimeBaseDefinition DescribeAbility(UObject* Outer, const FMyAbilityInfo& Item,
                                                  const FAbilityNodeRegistry& Registry)
    {
        FAbilityRuntimeBaseDefinition Definition;
        Definition.AbilityTag = Item.AbilityTag;
        Definition.AbilityClass = Item.Ability;
        Definition.AbilityType = Item.AbilityType;
        Definition.Icon = Item.Icon;
        Definition.DisplayName = FText::FromName(Item.Ability->GetFName());
        Definition.bSelectable = true;
        const auto* Ability = Item.Ability->GetDefaultObject<UGameplayAbility>();
        ClassifyFrontend(*Ability, Definition);
        if (!Definition.bSelectable)
        {
            return Definition;
        }
        FString Error;
        auto* Graph = AbilityRuntimeRecipes::BuildDefaultGraph(Outer, Ability, Error);
        TArray<FString> Validation;
        if (!Graph || !Graph->Validate(Registry, Validation))
        {
            Definition.Diagnostic = Graph ? FString::Join(Validation, TEXT("; ")) : Error;
            return Definition;
        }
        Definition.DefaultGraph = Graph;
        Definition.bHasDefaultGraph = true;
        DescribeGraph(Definition, Registry);
        return Definition;
    }
} // namespace

bool AbilityRuntimeRecipeCatalog::CollectBaseDefinitions(UObject* Outer, const UAbilityInfo* PreferredCatalog,
                                                         TArray<FAbilityRuntimeBaseDefinition>& Out,
                                                         TArray<FString>& Diagnostics)
{
    Out.Reset();
    Diagnostics.Reset();
    TArray<UAbilityInfo*> Catalogs;
    for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                              TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"),
                              TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo")})
    {
        auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
        if (!Catalog)
        {
            Diagnostics.Add(FString::Printf(TEXT("Required mage catalog failed to load: %s"), Path));
            return false;
        }
        Catalogs.Add(Catalog);
    }
    return CollectFromCatalogs(Outer, Catalogs, PreferredCatalog, Out, Diagnostics);
}

bool AbilityRuntimeRecipeCatalog::CollectFromCatalogs(UObject* Outer, const TArray<UAbilityInfo*>& Catalogs,
                                                      const UAbilityInfo* PreferredCatalog,
                                                      TArray<FAbilityRuntimeBaseDefinition>& Out,
                                                      TArray<FString>& Diagnostics)
{
    Out.Reset();
    Diagnostics.Reset();
    if (!IsValid(Outer) || Catalogs.IsEmpty())
    {
        Diagnostics.Add(TEXT("A live definition owner and at least one catalog are required."));
        return false;
    }
    // 将当前世界目录排在第一位，仅影响首选版本，不删除其它世界的同身份实现。
    TArray<const UAbilityInfo*> Ordered;
    if (PreferredCatalog && !IsValid(PreferredCatalog))
    {
        Diagnostics.Add(TEXT("The preferred world catalog is invalid."));
        return false;
    }
    if (PreferredCatalog)
    {
        Ordered.Add(PreferredCatalog);
    }
    for (const auto* Catalog : Catalogs)
    {
        if (!IsValid(Catalog))
        {
            Diagnostics.Add(TEXT("A supplied catalog is invalid."));
            return false;
        }
        Ordered.AddUnique(Catalog);
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    TMap<FString, int32> Variants;
    TSet<FGameplayTag> PreferredTags;
    for (const auto* Catalog : Ordered)
    {
        for (const auto& Item : Catalog->AbilityInformation)
        {
            if (Item.RequiredProfession != EPlayerProfession::Mage)
            {
                continue;
            }
            if (!Item.AbilityTag.IsValid() || !Item.Ability)
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid mage entry in %s."), *Catalog->GetPathName()));
                Out.Reset();
                return false;
            }
            const FString Key = Item.AbilityTag.ToString() + TEXT("|") + Item.Ability->GetPathName();
            if (const int32* Index = Variants.Find(Key))
            {
                Out[*Index].SourceCatalogPaths.AddUnique(Catalog->GetPathName());
                continue;
            }
            FAbilityRuntimeBaseDefinition Definition = DescribeAbility(Outer, Item, Registry);
            Definition.SourceCatalogPaths.Add(Catalog->GetPathName());
            Definition.bPreferredForWorld = !PreferredTags.Contains(Item.AbilityTag);
            PreferredTags.Add(Item.AbilityTag);
            if (!Definition.Diagnostic.IsEmpty())
            {
                Diagnostics.Add(Item.AbilityTag.ToString() + TEXT(": ") + Definition.Diagnostic);
            }
            Variants.Add(Key, Out.Num());
            Out.Add(MoveTemp(Definition));
        }
    }
    Out.Sort(
        [](const FAbilityRuntimeBaseDefinition& A, const FAbilityRuntimeBaseDefinition& B)
        {
            if (A.AbilityTag != B.AbilityTag)
            {
                return A.AbilityTag.ToString() < B.AbilityTag.ToString();
            }
            return A.AbilityClass->GetPathName() < B.AbilityClass->GetPathName();
        });
    return true;
}

const FAbilityRuntimeBaseDefinition* AbilityRuntimeRecipeCatalog::FindPreferred(
    const TArray<FAbilityRuntimeBaseDefinition>& Definitions, FGameplayTag AbilityTag)
{
    return Definitions.FindByPredicate(
        [AbilityTag](const FAbilityRuntimeBaseDefinition& Definition)
        {
            return Definition.AbilityTag == AbilityTag && Definition.bPreferredForWorld;
        });
}
