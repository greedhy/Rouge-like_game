// 调用链：开发面板/逐资产验证 -> 真实 UAbilityInfo 目录并集 -> 原 GA CDO 与基础图提取 -> 玩家独立配方。
// 本目录只读资产；不授予技能、不修改 CDO，也不以文档 JSON 作为产品运行时依赖。
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AbilityRuntimeRecipeCatalog.generated.h"

class UAbilityInfo;
class UAbilityGraphDefinition;
class UGameplayAbility;
class UTexture2D;

/** 显式区分覆写前端，不能因为继承原型类便误用原型默认执行图。 */
UENUM(BlueprintType)
enum class EAbilityRuntimeFrontendKind : uint8
{
    UnifiedGraph,
    ActorPrototype,
    MixedPrototype,
    Extension,
    Batch6Chain,
    Batch6Petrify,
    FireBolt,
    Nova,
    Shards,
    Beam,
    Orbit,
    Passive,
    Infrastructure,
    Unsupported
};

/** GroundOrEnemy 保留原允许空地瞄准的前端，不强行将其改成必选活目标。 */
UENUM(BlueprintType)
enum class EAbilityRuntimeTargetContract : uint8
{
    Self,
    Ground,
    Enemy,
    GroundOrEnemy
};

/** 一个技能身份可拥有 Actor/Mixed 不同版本；每个版本独立登记并保活自己的内存默认定义。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeBaseDefinition
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag AbilityTag;
    UPROPERTY(BlueprintReadOnly)
    TSubclassOf<UGameplayAbility> AbilityClass;
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag AbilityType;
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<const UTexture2D> Icon;
    UPROPERTY(BlueprintReadOnly)
    FText DisplayName;
    UPROPERTY(BlueprintReadOnly)
    EAbilityRuntimeFrontendKind FrontendKind = EAbilityRuntimeFrontendKind::Unsupported;
    UPROPERTY(BlueprintReadOnly)
    EAbilityRuntimeTargetContract TargetContract = EAbilityRuntimeTargetContract::GroundOrEnemy;
    UPROPERTY(BlueprintReadOnly)
    bool bSupportsActorTargets = false;
    UPROPERTY(BlueprintReadOnly)
    bool bSupportsMassTargets = false;
    UPROPERTY(BlueprintReadOnly)
    bool bRequiresMassSubsystem = false;
    /** 是否属于可编辑战斗身份；基础图尚未提取时另外显示 bHasDefaultGraph/Diagnostic。 */
    UPROPERTY(BlueprintReadOnly)
    bool bSelectable = false;
    UPROPERTY(BlueprintReadOnly)
    bool bHasDefaultGraph = false;
    /** 宏图可运行不等于宏内部可自由替换，此字段防止面板/台账混淆两种证据。 */
    UPROPERTY(BlueprintReadOnly)
    bool bContainsLegacyMacro = false;
    UPROPERTY(BlueprintReadOnly)
    bool bPreferredForWorld = false;
    UPROPERTY(Transient, BlueprintReadOnly)
    TObjectPtr<UAbilityGraphDefinition> DefaultGraph;
    /** 实际默认图使用的注册 TypeId，不虚构当前未提供的内部原子接口。 */
    UPROPERTY(BlueprintReadOnly)
    TArray<FName> Capabilities;
    UPROPERTY(BlueprintReadOnly)
    FString Diagnostic;
    UPROPERTY(BlueprintReadOnly)
    TArray<FString> SourceCatalogPaths;
};

namespace AbilityRuntimeRecipeCatalog
{
    /** 显式加载三个现有法师目录；当前世界目录优先选版本，完整目录并集仍全部返回。 */
    TEST_25D_API bool CollectBaseDefinitions(UObject* Outer, const UAbilityInfo* PreferredCatalog,
                                             TArray<FAbilityRuntimeBaseDefinition>& Out, TArray<FString>& Diagnostics);
    /** 共用无全局缓存的只读合并实现，便于自动化与不同游戏目录使用。 */
    TEST_25D_API bool CollectFromCatalogs(UObject* Outer, const TArray<UAbilityInfo*>& Catalogs,
                                          const UAbilityInfo* PreferredCatalog,
                                          TArray<FAbilityRuntimeBaseDefinition>& Out, TArray<FString>& Diagnostics);
    /** 返回本次集合中该身份的首选版本；集合被更新后调用者不得保留旧指针。 */
    TEST_25D_API const FAbilityRuntimeBaseDefinition* FindPreferred(
        const TArray<FAbilityRuntimeBaseDefinition>& Definitions, FGameplayTag AbilityTag);
} // namespace AbilityRuntimeRecipeCatalog
