/**
 * 第五批可选接入：GameMode InitGame 选择独立存档；Player PossessedBy 复用原玩家加载链并补基础监听被动；HUD Tick 在本地刷新扩展目录/技能树。UI 仅展示 ASC 已有状态，不负责权威授予。弱引用集合清除失效 Widget，运行时样式副本由 HUD 持有，不修改原 DA。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "PureMass/PureMassPlayerCharacter.h"
#include "PureMass/PureMassGameMode.h"
#include "UI/HUD/MyHUD.h"
#include "ExtensionIntegration.generated.h"

UCLASS()
class TEST_25D_API AExtensionPlayerCharacter : public APureMassPlayerCharacter
{
    GENERATED_BODY()
public:
    /** 构造时加载基础监听被动类并登记到 PersistentStartupPassives，不在此时授予 ASC。 */
    AExtensionPlayerCharacter();
    /** 服务器控制器接管：沿原 PlayerCharacter 保存/旅行加载链初始化，再只授予尚缺失的基础被动。 */
    virtual void PossessedBy(AController* Controller) override;
    /** 仅在扩展模式存档前缀匹配时转交原玩家保存入口，避免污染其他模式存档。 */
    virtual void SaveProgress_Implementation(const FName& CheckpointTag) override;
    UPROPERTY(EditDefaultsOnly, Category = "Abilities")
    TArray<TSubclassOf<class UGameplayAbility>> PersistentStartupPassives;
};
UCLASS()
class TEST_25D_API AExtensionGameMode : public APureMassGameMode
{
    GENERATED_BODY()
public:
    /** 扩展模式地图初始化：确保使用 PrototypeExtensions_ 存档前缀，再调用父类初始化。 */
    virtual void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
};
/** Appends extension entries to constructed menu instances only in the opt-in HUD. */
UCLASS()
class TEST_25D_API AExtensionHUD : public AMyHUD
{
    GENERATED_BODY()
public:
    /** 构造时启用 0.2 秒间隔 Tick，供本地技能 UI 增量刷新。 */
    AExtensionHUD();
    /** 仅本地控制器执行：加载目录，按 ASC/Pawn/能力哈希刷新槽位；复制富文本样式并补扩展技能树条目，弱引用去重。 */
    virtual void Tick(float Delta) override;

private:
    UPROPERTY()
    TObjectPtr<class UAbilityInfo> ExtensionCatalog;
    UPROPERTY()
    TMap<TObjectPtr<class UDataTable>, TObjectPtr<class UDataTable>> DescriptionStyles;
    TSet<TWeakObjectPtr<class UPrototypeSpellTree>> PopulatedTrees;
    TSet<TWeakObjectPtr<class URichTextBlock>> StyledDescriptions;
    TWeakObjectPtr<class UMyAbilitySystemComponent> DisplayedASC;
    TWeakObjectPtr<APawn> DisplayedPawn;
    uint32 DisplayedAbilityHash = 0;
};
