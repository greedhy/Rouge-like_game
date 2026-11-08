/**
 * 第五批可选接入：GameMode InitGame 选择独立存档；Player PossessedBy 复用原玩家加载链并补基础监听被动；HUD Tick 在本地刷新扩展目录/技能树。UI 仅展示 ASC 已有状态，不负责权威授予。弱引用集合清除失效 Widget，运行时样式副本由 HUD 持有，不修改原 DA。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/PrototypeExtensions/ExtensionIntegration.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionAbility.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Player/MyPlayerState.h"
#include "UI/Widget/PrototypeSpellTree.h"
#include "Game/MyGameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "UObject/UObjectIterator.h"
#include "NativeGameplayTags.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

#define EXTENSION_TAGS(Name)                                                                                           \
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Ability_##Name, "Abilities.Extension." #Name);                                       \
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Cooldown_##Name, "Cooldown.Extension." #Name);
EXTENSION_TAGS(Attach)
EXTENSION_TAGS(Stack)
EXTENSION_TAGS(Displace)
EXTENSION_TAGS(StormBrand)
EXTENSION_TAGS(FrostParasite)
EXTENSION_TAGS(EmberBrand)
EXTENSION_TAGS(StormConvergence)
EXTENSION_TAGS(GravityWell)
EXTENSION_TAGS(BrambleRepulse)
#undef EXTENSION_TAGS

/** 扩展模式地图初始化：确保使用 PrototypeExtensions_ 存档前缀，再调用父类初始化。 */
void AExtensionGameMode::InitGame(const FString& MapName, const FString& Options, FString& Error)
{
    if (auto* GI = Cast<UMyGameInstance>(GetGameInstance()))
        if (!GI->LoadSlotName.StartsWith(TEXT("PrototypeExtensions_")))
        {
            GI->LoadSlotName = TEXT("PrototypeExtensions_Batch5");
            GI->LoadSlotIndex = 0;
        }
    Super::InitGame(MapName, Options, Error);
}
/** 构造时加载基础监听被动类并登记到 PersistentStartupPassives，不在此时授予 ASC。 */
AExtensionPlayerCharacter::AExtensionPlayerCharacter()
{
    static ConstructorHelpers::FClassFinder<UGameplayAbility> Listener(
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Passive_Startup/GA_ListenForEvent"));
    if (Listener.Succeeded())
        PersistentStartupPassives.Add(Listener.Class);
}
/** 服务器控制器接管：沿原 PlayerCharacter 保存/旅行加载链初始化，再只授予尚缺失的基础被动。 */
void AExtensionPlayerCharacter::PossessedBy(AController* NewController)
{
    // Retain the original save/travel implementation, with the Mass-safe ASC/HUD override.
    APlayerCharacter::PossessedBy(NewController);
    if (auto* ASC = Cast<UMyAbilitySystemComponent>(GetAbilitySystemComponent()))
    {
        // Infrastructure passives are not entries in the learnable skill catalog.
        TArray<TSubclassOf<UGameplayAbility>> Missing;
        for (const auto& Ability : PersistentStartupPassives)
            if (Ability && !ASC->FindAbilitySpecFromClass(Ability))
                Missing.Add(Ability);
        ASC->AddCharacterPassiveAbilities(Missing);
    }
}
/** 仅在扩展模式存档前缀匹配时转交原玩家保存入口，避免污染其他模式存档。 */
void AExtensionPlayerCharacter::SaveProgress_Implementation(const FName& Tag)
{
    const auto* GI = Cast<UMyGameInstance>(GetGameInstance());
    if (GI && GI->LoadSlotName.StartsWith(TEXT("PrototypeExtensions_")))
        APlayerCharacter::SaveProgress_Implementation(Tag);
}
/** 构造时启用 0.2 秒间隔 Tick，供本地技能 UI 增量刷新。 */
AExtensionHUD::AExtensionHUD()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .2f;
}
/** 仅本地控制器执行：加载目录，按 ASC/Pawn/能力哈希刷新槽位；复制富文本样式并补扩展技能树条目，弱引用去重。 */
void AExtensionHUD::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!GetOwningPlayerController() || !GetOwningPlayerController()->IsLocalController())
        return;
    if (!ExtensionCatalog)
        ExtensionCatalog =
            LoadObject<UAbilityInfo>(nullptr, TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo"));
    if (!ExtensionCatalog)
        return;
    auto* PC = GetOwningPlayerController();
    if (auto* State = PC->GetPlayerState<AMyPlayerState>();
        State && IsValid(OverlayWidget) && OverlayWidget->IsInViewport())
    {
        auto* ASC = Cast<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
        if (ASC && ASC->bStartupAbilityGiven)
        {
            uint32 Hash = 0;
            for (const auto& Spec : ASC->GetActivatableAbilities())
            {
                Hash = HashCombine(Hash, GetTypeHash(UMyAbilitySystemComponent::GetAbilityTagFromSpec(Spec)));
                Hash = HashCombine(Hash, GetTypeHash(UMyAbilitySystemComponent::GetInputTagFromSpec(Spec)));
                Hash = HashCombine(Hash, GetTypeHash(UMyAbilitySystemComponent::GetStatusFromSpec(Spec)));
                Hash = HashCombine(Hash, GetTypeHash(Spec.Level));
            }
            if (DisplayedASC != ASC || DisplayedPawn != PC->GetPawn() || DisplayedAbilityHash != Hash)
            {
                auto* Controller =
                    GetOverlayWidgetController(FWidgetControllerParams(PC, State, ASC, State->GetAttributeSet()));
                for (const auto& Spec : ASC->GetActivatableAbilities())
                {
                    auto Info =
                        ExtensionCatalog->FindAbilityInfoForTag(UMyAbilitySystemComponent::GetAbilityTagFromSpec(Spec));
                    Info.InputTag = UMyAbilitySystemComponent::GetInputTagFromSpec(Spec);
                    Info.StatusTag = UMyAbilitySystemComponent::GetStatusFromSpec(Spec);
                    Controller->AbilityInfoDelegate.Broadcast(Info);
                }
                DisplayedASC = ASC;
                DisplayedPawn = PC->GetPawn();
                DisplayedAbilityHash = Hash;
            }
        }
    }
    for (auto It = StyledDescriptions.CreateIterator(); It; ++It)
        if (!It->IsValid())
            It.RemoveCurrent();
    for (TObjectIterator<URichTextBlock> It; It; ++It)
    {
        auto* Text = *It;
        const auto* Widget = Text->GetTypedOuter<UUserWidget>();
        if (Text->GetWorld() != GetWorld() || !Widget || Widget->GetOwningPlayer() != GetOwningPlayerController() ||
            StyledDescriptions.Contains(Text) || !Text->GetName().Contains(TEXT("Description")))
            continue;
        // The original current-description widget has WrapTextAt=2, and its custom
        // font lacks CJK glyphs. Apply a private runtime style copy in this opt-in HUD.
        Text->SetWrapTextAt(0.f);
        Text->SetAutoWrapText(true);
        if (auto* Original = Text->GetTextStyleSet();
            Original && Original->GetRowStruct() &&
            Original->GetRowStruct()->IsChildOf(FRichTextStyleRow::StaticStruct()))
        {
            auto& Copy = DescriptionStyles.FindOrAdd(Original);
            if (!Copy)
            {
                Copy = DuplicateObject<UDataTable>(Original, this);
                Copy->ClearFlags(RF_Public | RF_Standalone);
                Copy->SetFlags(RF_Transient);
                for (auto& Row : Copy->GetRowMap())
                {
                    auto* Style = reinterpret_cast<FRichTextStyleRow*>(Row.Value);
                    auto Font = GetDefault<UTextBlock>()->GetFont();
                    Font.Size = Row.Key == TEXT("Title") ? 22 : 18;
                    Style->TextStyle.SetFont(Font);
                }
            }
            Text->SetTextStyleSet(Copy);
        }
        StyledDescriptions.Add(Text);
    }
    for (auto It = PopulatedTrees.CreateIterator(); It; ++It)
        if (!It->IsValid())
            It.RemoveCurrent();
    for (TObjectIterator<UPrototypeSpellTree> It; It; ++It)
    {
        auto* Tree = *It;
        if (Tree->GetWorld() != GetWorld() || Tree->GetOwningPlayer() != GetOwningPlayerController() ||
            PopulatedTrees.Contains(Tree) || !Tree->WidgetTree)
            continue;
        UUniformGridPanel* Grid = nullptr;
        Tree->WidgetTree->ForEachWidget(
            [&](UWidget* Widget)
            {
                if (auto* Candidate = Cast<UUniformGridPanel>(Widget);
                    Candidate && Candidate->GetChildrenCount() > 0 &&
                    Cast<UPrototypeSpellEntry>(Candidate->GetChildAt(0)))
                    Grid = Candidate;
            });
        if (!Grid)
            continue;
        int32 Index = Grid->GetChildrenCount();
        for (const auto& Row : ExtensionCatalog->AbilityInformation)
        {
            const auto* GA = Row.Ability ? Cast<UExtensionAbility>(Row.Ability->GetDefaultObject()) : nullptr;
            if (!GA || !GA->Prototype)
                continue;
            auto* Entry = NewObject<UPrototypeSpellEntry>(Tree);
            Entry->Tree = Tree;
            Entry->Label = GA->Prototype->DisplayName;
            Entry->AbilityTag = Row.AbilityTag;
            Entry->Initialize();
            Grid->AddChildToUniformGrid(Entry, Index / 2, Index % 2);
            ++Index;
        }
        PopulatedTrees.Add(Tree);
    }
}
