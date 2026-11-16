// 调用链：装备和背包通知 → 更新槽位及对比 → 装备组件提交权威换装请求。
#include "UI/Widget/EquipmentPanel.h"
#include "Equipment/EquipmentSlots.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/InventoryModel.h"
#include "Backpack/ItemDefinition.h"
#include "UI/ViewModel/MVVM_Backpack.h"
#include "Engine/Texture2D.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

bool UEquipmentPanel::CanEquipToSlot(const UInventoryModel* InModel, int32 Index, FGameplayTag EquipmentSlotTag)
{
    if (!InModel || !InModel->GetSlots().IsValidIndex(Index))
        return false;
    const FInventorySlot& Item = InModel->GetSlots()[Index];
    const FBackpackItemDefEntry* Def = InModel->FindItemDefinition(Item.ItemDefinitionId);
    return !Item.IsEmpty() && Def && Def->bEquippable && Def->AllowedEquipmentSlots.HasTagExact(EquipmentSlotTag);
}
void UEquipmentPanel::Unbind()
{
    if (Backpack)
    {
        Backpack->OnSelectionChanged.Remove(SelectionHandle);
        SelectionHandle.Reset();
    }
    if (Equipment)
    {
        Equipment->OnEquipmentChanged.RemoveDynamic(this, &ThisClass::RefreshEquipment);
        Equipment->OnRequestResult.RemoveDynamic(this, &ThisClass::OnResult);
    }
}
void UEquipmentPanel::Bind()
{
    if (Backpack && !SelectionHandle.IsValid())
    {
        SelectionHandle = Backpack->OnSelectionChanged.AddUObject(this, &ThisClass::RefreshComparison);
    }
    if (Equipment)
    {
        Equipment->OnEquipmentChanged.AddUniqueDynamic(this, &ThisClass::RefreshEquipment);
        Equipment->OnRequestResult.AddUniqueDynamic(this, &ThisClass::OnResult);
    }
}
void UEquipmentPanel::InitializeEquipment(UEquipmentComponent* InEquipment, UMVVM_Backpack* InBackpack,
                                          UInventoryModel* InModel)
{
    Unbind();
    Equipment = InEquipment;
    Backpack = InBackpack;
    Model = InModel;
    Bind();
    RefreshEquipment();
}
void UEquipmentPanel::NativeConstruct()
{
    Super::NativeConstruct();
    Bind();
    RefreshEquipment();
}
void UEquipmentPanel::NativeDestruct()
{
    Unbind();
    Super::NativeDestruct();
}
void UEquipmentPanel::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Rows.Reset();
    ElementRows.Reset();
    Comparison.Reset();
    IconBrushes.Reset();
}
void UEquipmentPanel::OnResult(bool bSucceeded)
{
    Status =
        FText::FromString(bSucceeded ? TEXT("装备操作成功") : TEXT("操作失败：请检查背包空间、角色状态或重新选择物品"));
    RefreshEquipment();
}
// 使用项目原有边框资源，左侧列装备槽，右侧展示选中物品的替换对比。
TSharedRef<SWidget> UEquipmentPanel::RebuildWidget()
{
    BackgroundTexture = LoadObject<UTexture2D>(
        nullptr, TEXT("/Game/Assets/UI/Borders/BorderLarge_BG/T_Border_Large_BG_png.T_Border_Large_BG_png"));
    BackgroundBrush.SetResourceObject(BackgroundTexture);
    BackgroundBrush.DrawAs = ESlateBrushDrawType::Box;
    BackgroundBrush.Margin = FMargin(0.12f);

    const FLinearColor Gold(0.86f, 0.71f, 0.42f);
    auto Result = SNew(SBox).WidthOverride(760).HeightOverride(590)
        [SNew(SBorder)
            .BorderImage(&BackgroundBrush)
            .BorderBackgroundColor(FLinearColor(0.11f, 0.09f, 0.075f))
            .Padding(14)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 12, 0)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                            [SNew(STextBlock).Text(FText::FromString(TEXT("当前装备"))).ColorAndOpacity(Gold)
                                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))]
                        + SVerticalBox::Slot().FillHeight(1.f)
                            [SNew(SScrollBox)
                                + SScrollBox::Slot()[SAssignNew(Rows, SVerticalBox)]
                                + SScrollBox::Slot()[SAssignNew(ElementRows, SVerticalBox)]]]
                + SHorizontalBox::Slot().FillWidth(1.05f)
                    [SNew(SBorder).BorderBackgroundColor(FLinearColor(0.15f, 0.13f, 0.10f)).Padding(12)
                        [SNew(SVerticalBox)
                            + SVerticalBox::Slot().FillHeight(1.f)
                                [SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Comparison, SVerticalBox)]]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
                                [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                                {
                                    return Equipment && Equipment->bNeedsReconcile
                                               ? FText::FromString(TEXT("装备效果待恢复，请重试操作"))
                                               : Status;
                                })]]]]];
    RefreshEquipment();
    return Result;
}

// 从复制快照读取指定槽位，空槽仍由 AvailableSlots 生成。
const FEquipmentSlot* UEquipmentPanel::FindEquipped(FGameplayTag SlotTag) const
{
    return Equipment ? Equipment->Slots.FindByPredicate([SlotTag](const FEquipmentSlot& Entry)
    {
        return Entry.SlotTag == SlotTag;
    }) : nullptr;
}

// 装备快照更新后重建槽位图标并同步右侧对比。
void UEquipmentPanel::RefreshEquipment()
{
    if (!Rows || !ElementRows)
    {
        return;
    }
    Rows->ClearChildren();
    ElementRows->ClearChildren();
    IconBrushes.Reset();
    IconTextures.Reset();
    if (!Equipment)
    {
        RefreshComparison();
        return;
    }

    TArray<FGameplayTag> Ordered;
    for (const FEquipmentSlotDefinition& SlotTag : EquipmentSlots::All())
    {
        if (Equipment->AvailableSlots.HasTagExact(SlotTag.Tag))
        {
            Ordered.Add(SlotTag.Tag);
        }
    }
    for (const FGameplayTag& SlotTag : Equipment->AvailableSlots)
    {
        Ordered.AddUnique(SlotTag);
    }

    for (const FGameplayTag& SlotTag : Ordered)
    {
        const FEquipmentSlot* Record = FindEquipped(SlotTag);
        const FBackpackItemDefEntry* Definition = Record && Model ? Model->FindItemDefinition(Record->ItemDefinitionId) : nullptr;
        const auto* SlotDefinition = EquipmentSlots::All().FindByPredicate([SlotTag](const FEquipmentSlotDefinition& Entry)
        {
            return Entry.Tag == SlotTag;
        });
        const FText Label = FText::FromString(SlotDefinition ? FString(SlotDefinition->Label) : SlotTag.ToString());
        const FText Name = Record ? FEquipmentGenerator::MakeDisplayName(
            Definition ? Definition->DisplayName : FText::FromName(Record->ItemDefinitionId.PrimaryAssetName),
            Record->RollData) : FText::FromString(TEXT("空槽"));
        const FText Effect = Record ? FEquipmentGenerator::MakeDescription(Definition, Record->RollData) : FText::GetEmpty();
        const FText Tip = Effect.IsEmpty() ? Name : FText::Format(FText::FromString(TEXT("{0}\n{1}")), Name, Effect);
        const TSharedPtr<FSlateBrush> Icon = MakeShared<FSlateBrush>();
        if (Definition && Definition->Icon)
        {
            IconTextures.Add(Definition->Icon);
            Icon->SetResourceObject(Definition->Icon);
            Icon->ImageSize = FVector2D(40);
        }
        else
        {
            Icon->DrawAs = ESlateBrushDrawType::NoDrawType;
        }
        IconBrushes.Add(Icon);

        const auto& Target = SlotDefinition && SlotDefinition->bElement ? ElementRows : Rows;
        Target->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
            [SNew(SBorder).BorderBackgroundColor(FLinearColor(0.18f, 0.15f, 0.11f)).Padding(4)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SButton).IsFocusable(false).ContentPadding(2).ToolTipText(Tip)
                            .OnClicked_Lambda([this, SlotTag]
                            {
                                ComparisonSlot = SlotTag;
                                RefreshComparison();
                                return FReply::Handled();
                            })
                            [SNew(SBox).WidthOverride(40).HeightOverride(40)[SNew(SImage).Image(Icon.Get())]]]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0)
                        [SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                                [SNew(STextBlock).Text(Label).ColorAndOpacity(FLinearColor(0.86f, 0.71f, 0.42f))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))]
                            + SVerticalBox::Slot().AutoHeight()
                                [SNew(STextBlock).Text(Name).AutoWrapText(true)
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))]]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(SButton).IsFocusable(false).IsEnabled(Record != nullptr)
                            .Text(FText::FromString(TEXT("卸下"))).OnClicked_Lambda([this, SlotTag]
                            {
                                if (Equipment)
                                {
                                    Equipment->RequestUnequip(SlotTag);
                                }
                                return FReply::Handled();
                            })]]];
    }
    RefreshComparison();
}

// 兼容槽位变化时，优先显示一个已有装备的槽位。用户可以指定同类装备的另一栏。
void UEquipmentPanel::RefreshComparison()
{
    if (!Comparison)
    {
        return;
    }
    Comparison->ClearChildren();
    const int32 Index = Backpack ? Backpack->GetCurrentSelectedIndex() : INDEX_NONE;
    TArray<FGameplayTag> Compatible;
    if (Equipment && Model && Model->GetSlots().IsValidIndex(Index))
    {
        for (const FGameplayTag& SlotTag : Equipment->AvailableSlots)
        {
            if (CanEquipToSlot(Model, Index, SlotTag))
            {
                Compatible.Add(SlotTag);
            }
        }
    }
    if (Compatible.IsEmpty())
    {
        ComparisonSlot = FGameplayTag();
        Comparison->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true)
            .Text(FText::FromString(TEXT("在背包选择装备以查看效果")))];
        return;
    }
    if (!Compatible.Contains(ComparisonSlot))
    {
        const FGameplayTag* Occupied = Compatible.FindByPredicate([this](FGameplayTag SlotTag)
        {
            return FindEquipped(SlotTag) != nullptr;
        });
        ComparisonSlot = Occupied ? *Occupied : Compatible[0];
    }
    auto Slots = SNew(SHorizontalBox);
    for (const FGameplayTag& SlotTag : Compatible)
    {
        const auto* Definition = EquipmentSlots::All().FindByPredicate([SlotTag](const FEquipmentSlotDefinition& Entry)
        {
            return Entry.Tag == SlotTag;
        });
        Slots->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
            [SNew(SButton).IsFocusable(false).IsEnabled(SlotTag != ComparisonSlot)
                .Text(FText::FromString(Definition ? FString(Definition->Label) : SlotTag.ToString()))
                .OnClicked_Lambda([this, SlotTag]
                {
                    ComparisonSlot = SlotTag;
                    RefreshComparison();
                    return FReply::Handled();
                })];
    }
    Comparison->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Slots];
    const auto AddItem = [this](const FText& Heading, const FText& Name, const FText& Effect, const FLinearColor& Color)
    {
        Comparison->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
            [SNew(SBorder).BorderBackgroundColor(FLinearColor(0.20f, 0.17f, 0.13f)).Padding(9)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                        [SNew(STextBlock).Text(Heading).ColorAndOpacity(Color)
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
                        [SNew(STextBlock).Text(Name).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))]
                    + SVerticalBox::Slot().AutoHeight()
                        [SNew(STextBlock).Text(Effect).AutoWrapText(true)
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))]]];
    };
    const FEquipmentSlot* Existing = FindEquipped(ComparisonSlot);
    if (Existing)
    {
        const FBackpackItemDefEntry* Definition = Model->FindItemDefinition(Existing->ItemDefinitionId);
        AddItem(FText::FromString(TEXT("已有装备")),
                FEquipmentGenerator::MakeDisplayName(
                    Definition ? Definition->DisplayName : FText::FromName(Existing->ItemDefinitionId.PrimaryAssetName),
                    Existing->RollData),
                FEquipmentGenerator::MakeDescription(Definition, Existing->RollData),
                FLinearColor(0.86f, 0.71f, 0.42f));
    }
    AddItem(FText::FromString(TEXT("新装备")), Backpack->GetSelectedItemName(), Backpack->GetSelectedItemDescription(),
            FLinearColor(0.65f, 0.82f, 0.55f));
    Comparison->AddSlot().AutoHeight()
        [SNew(SButton).IsFocusable(false).Text(FText::FromString(Existing ? TEXT("替换装备") : TEXT("装备")))
            .OnClicked_Lambda([this]
            {
                if (Equipment && Backpack && CanEquipToSlot(Model, Backpack->GetCurrentSelectedIndex(), ComparisonSlot))
                {
                    Equipment->RequestEquip(Backpack->GetCurrentSelectedIndex(), ComparisonSlot);
                }
                return FReply::Handled();
            })];
}
