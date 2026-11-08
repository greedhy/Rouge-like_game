#include "UI/Widget/PrototypeSpellTree.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "Equipment/WeaponSetConfig.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "Player/StanderWeaponActionConfig.h"

void UPrototypeSpellEntry::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Button = WidgetTree->ConstructWidget<UButton>();
    auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
    Text->SetText(Label);
    FSlateFontInfo Font = Text->GetFont();
    Font.Size = 14;
    Text->SetFont(Font);
    auto* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (Icon)
    {
        auto* Image = WidgetTree->ConstructWidget<UImage>();
        Image->SetBrushFromTexture(Icon);
        auto* IconBounds = WidgetTree->ConstructWidget<USizeBox>();
        IconBounds->SetWidthOverride(32.f);
        IconBounds->SetHeightOverride(32.f);
        IconBounds->AddChild(Image);
        Content->AddChild(IconBounds);
    }
    Content->AddChild(Text);
    Button->AddChild(Content);
    Button->OnClicked.AddDynamic(this, &ThisClass::Select);
    WidgetTree->RootWidget = Button;
}
void UPrototypeSpellEntry::Select()
{
    if (Tree)
        if (auto* Controller = Cast<USpellMenuWidgetController>(Tree->WidgetController))
            Controller->SpellGlobeSelected(AbilityTag);
}
void UPrototypeSpellTree::NativeConstruct()
{
    Super::NativeConstruct();
    if (PrototypePage)
        return;
    auto* Root = Cast<USizeBox>(GetWidgetFromName(TEXT("SizeBox_Root")));
    if (!Root || !Root->GetContent())
        return;
    OriginalPage = Root->GetContent();
    OriginalPage->RemoveFromParent();
    auto* Pages = WidgetTree->ConstructWidget<UVerticalBox>();
    Root->AddChild(Pages);
    auto* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
    Pages->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0, 0, 0, 8));
    auto* OriginalButton = WidgetTree->ConstructWidget<UButton>();
    auto* PrototypeButton = WidgetTree->ConstructWidget<UButton>();
    auto* OriginalText = WidgetTree->ConstructWidget<UTextBlock>();
    auto* PrototypeText = WidgetTree->ConstructWidget<UTextBlock>();
    OriginalText->SetText(FText::FromString(TEXT("  Skills  ")));
    PrototypeText->SetText(FText::FromString(TEXT("  Prototypes  ")));
    for (auto* Text : {OriginalText, PrototypeText})
    {
        FSlateFontInfo Font = Text->GetFont();
        Font.Size = 14;
        Text->SetFont(Font);
    }
    OriginalButton->AddChild(OriginalText);
    PrototypeButton->AddChild(PrototypeText);
    OriginalButton->OnClicked.AddDynamic(this, &ThisClass::ShowOriginal);
    PrototypeButton->OnClicked.AddDynamic(this, &ThisClass::ShowPrototypes);
    Tabs->AddChild(OriginalButton);
    Tabs->AddChild(PrototypeButton);
    auto* WeaponButton = WidgetTree->ConstructWidget<UButton>();
    auto* WeaponText = WidgetTree->ConstructWidget<UTextBlock>();
    WeaponText->SetText(FText::FromString(TEXT("  Weapon Skills  ")));
    FSlateFontInfo WeaponFont = WeaponText->GetFont();
    WeaponFont.Size = 14;
    WeaponText->SetFont(WeaponFont);
    WeaponButton->AddChild(WeaponText);
    WeaponButton->OnClicked.AddDynamic(this, &ThisClass::ShowWeapons);
    Tabs->AddChild(WeaponButton);
    Pages->AddChild(OriginalPage);
    auto* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
    Grid->SetMinDesiredSlotHeight(42.f);
    Grid->SetSlotPadding(FMargin(3.f));
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    auto* Bounds = WidgetTree->ConstructWidget<USizeBox>();
    Bounds->SetHeightOverride(215.f);
    Bounds->AddChild(Scroll);
    Scroll->AddChild(Grid);
    PrototypePage = Bounds;
    Pages->AddChild(Bounds);
    const auto* Catalog =
        LoadObject<UAbilityInfo>(nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"));
    int32 Index = 0;
    if (Catalog)
        for (const auto& Row : Catalog->AbilityInformation)
        {
            const auto* GA = Row.Ability ? Cast<UPrototypeAbility>(Row.Ability->GetDefaultObject()) : nullptr;
            if (!GA || !GA->Prototype)
                continue;
            // NewObject + Initialize lets us supply the label before NativeOnInitialized builds its button.
            auto* Entry = NewObject<UPrototypeSpellEntry>(this);
            Entry->Tree = this;
            Entry->Label = GA->Prototype->DisplayName;
            Entry->AbilityTag = Row.AbilityTag;
            Entry->Icon = const_cast<UTexture2D*>(Row.Icon.Get());
            Entry->Initialize();
            Grid->AddChildToUniformGrid(Entry, Index / 2, Index % 2);
            ++Index;
        }
    WeaponGrid = WidgetTree->ConstructWidget<UUniformGridPanel>();
    WeaponGrid->SetMinDesiredSlotHeight(42.f);
    WeaponGrid->SetSlotPadding(FMargin(3.f));
    auto* WeaponScroll = WidgetTree->ConstructWidget<UScrollBox>();
    auto* WeaponBounds = WidgetTree->ConstructWidget<USizeBox>();
    WeaponBounds->SetHeightOverride(215.f);
    WeaponBounds->AddChild(WeaponScroll);
    WeaponScroll->AddChild(WeaponGrid);
    WeaponPage = WeaponBounds;
    Pages->AddChild(WeaponBounds);
    RefreshWeapons();
    if (ListedWeaponSet)
        ShowWeapons();
    else
        ShowOriginal();
}
void UPrototypeSpellTree::ShowOriginal()
{
    if (auto* Controller = Cast<USpellMenuWidgetController>(WidgetController))
        Controller->GlobeDeselect();
    if (OriginalPage)
        OriginalPage->SetVisibility(ESlateVisibility::Visible);
    if (PrototypePage)
        PrototypePage->SetVisibility(ESlateVisibility::Collapsed);
    if (WeaponPage)
        WeaponPage->SetVisibility(ESlateVisibility::Collapsed);
}
void UPrototypeSpellTree::ShowPrototypes()
{
    if (auto* Controller = Cast<USpellMenuWidgetController>(WidgetController))
        Controller->GlobeDeselect();
    if (OriginalPage)
        OriginalPage->SetVisibility(ESlateVisibility::Collapsed);
    if (PrototypePage)
        PrototypePage->SetVisibility(ESlateVisibility::Visible);
    if (WeaponPage)
        WeaponPage->SetVisibility(ESlateVisibility::Collapsed);
}

void UPrototypeSpellTree::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    WeaponRefreshTime += InDeltaTime;
    if (WeaponRefreshTime < .25f)
        return;
    WeaponRefreshTime = 0.f;
    const auto* Controller = Cast<USpellMenuWidgetController>(WidgetController);
    if (Controller && (ListedController != Controller ||
                       ListedWeaponSet != const_cast<USpellMenuWidgetController*>(Controller)->GetCurrentWeaponSet()))
        RefreshWeapons();
}

void UPrototypeSpellTree::RefreshWeapons()
{
    if (!WeaponGrid)
        return;
    auto* Controller = Cast<USpellMenuWidgetController>(WidgetController);
    ListedController = Controller;
    ListedWeaponSet = Controller ? Controller->GetCurrentWeaponSet() : nullptr;
    WeaponGrid->ClearChildren();
    if (Controller)
        Controller->GlobeDeselect();
    const auto* Catalog = Controller ? Controller->GetAbilityCatalog() : nullptr;
    int32 Index = 0;
    if (ListedWeaponSet && Catalog)
        for (const auto& Ability : ListedWeaponSet->Abilities)
        {
            const auto* GA =
                Ability.Ability ? Cast<UStanderWeaponActionAbility>(Ability.Ability->GetDefaultObject()) : nullptr;
            if (!GA || !GA->ActionConfig)
                continue;
            const auto Row = Catalog->FindAbilityInfoForTag(GA->ActionConfig->ActionTag);
            auto* Entry = NewObject<UPrototypeSpellEntry>(this);
            Entry->Tree = this;
            Entry->Label = GA->ActionConfig->DisplayName;
            Entry->AbilityTag = GA->ActionConfig->ActionTag;
            Entry->Icon = const_cast<UTexture2D*>(Row.Icon.Get());
            Entry->Initialize();
            WeaponGrid->AddChildToUniformGrid(Entry, Index / 2, Index % 2);
            ++Index;
        }
    if (Index == 0)
    {
        auto* Empty = WidgetTree->ConstructWidget<UTextBlock>();
        Empty->SetText(FText::FromString(TEXT("Equip a Stander weapon to choose its skills.")));
        Empty->SetAutoWrapText(true);
        WeaponGrid->AddChildToUniformGrid(Empty, 0, 0);
    }
}

void UPrototypeSpellTree::ShowWeapons()
{
    RefreshWeapons();
    if (OriginalPage)
        OriginalPage->SetVisibility(ESlateVisibility::Collapsed);
    if (PrototypePage)
        PrototypePage->SetVisibility(ESlateVisibility::Collapsed);
    if (WeaponPage)
        WeaponPage->SetVisibility(ESlateVisibility::Visible);
}
