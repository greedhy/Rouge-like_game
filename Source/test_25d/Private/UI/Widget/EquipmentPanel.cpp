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
    if (Equipment)
    {
        Equipment->OnEquipmentChanged.RemoveDynamic(this, &ThisClass::RefreshEquipment);
        Equipment->OnRequestResult.RemoveDynamic(this, &ThisClass::OnResult);
    }
}
void UEquipmentPanel::Bind()
{
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
    IconBrushes.Reset();
}
void UEquipmentPanel::OnResult(bool bSucceeded)
{
    Status =
        FText::FromString(bSucceeded ? TEXT("装备操作成功") : TEXT("操作失败：请检查背包空间、角色状态或重新选择物品"));
    RefreshEquipment();
}
TSharedRef<SWidget> UEquipmentPanel::RebuildWidget()
{
    BackgroundTexture = LoadObject<UTexture2D>(
        nullptr, TEXT("/Game/Assets/UI/Borders/BorderLarge_BG/T_Border_Large_BG_png.T_Border_Large_BG_png"));
    BackgroundBrush.SetResourceObject(BackgroundTexture);
    BackgroundBrush.DrawAs = ESlateBrushDrawType::Box;
    BackgroundBrush.Margin = FMargin(0.12f);
    auto Result = SNew(SBox).WidthOverride(350).HeightOverride(
        520)[SNew(SBorder)
                 .BorderImage(&BackgroundBrush)
                 .BorderBackgroundColor(FLinearColor(0.10f, 0.08f, 0.055f, 1.f))
                 .Padding(18)[SNew(SVerticalBox) +
                              SVerticalBox::Slot().AutoHeight().Padding(
                                  0, 0, 0, 12)[SNew(STextBlock)
                                                   .Text(FText::FromString(TEXT("装备")))
                                                   .Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))] +
                              SVerticalBox::Slot().AutoHeight().Padding(
                                  0, 0, 0,
                                  12)[SNew(STextBlock)
                                          .AutoWrapText(true)
                                          .Text_Lambda(
                                              [this]
                                              {
                                                  return Backpack && Backpack->HasSelectedItem()
                                                             ? FText::Format(FText::FromString(TEXT("背包选中：{0}")),
                                                                             Backpack->GetSelectedItemName())
                                                             : FText::FromString(
                                                                   TEXT("先在背包选择装备，再点击对应槽位的装备按钮"));
                                              })] +
                              SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SNew(SBox).MaxDesiredHeight(
                                  120)[SNew(SScrollBox) +
                                       SScrollBox::Slot()[SNew(STextBlock)
                                                              .AutoWrapText(true)
                                                              .Text_Lambda(
                                                                  [this]
                                                                  {
                                                                      return Backpack
                                                                                 ? Backpack
                                                                                       ->GetSelectedItemDescription()
                                                                                 : FText::GetEmpty();
                                                                  })]]] +
                              SVerticalBox::Slot().FillHeight(
                                  1)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Rows, SVerticalBox)]] +
                              SVerticalBox::Slot().AutoHeight().Padding(
                                  0, 10, 0, 4)[SNew(STextBlock)
                                                   .Text(FText::FromString(TEXT("元素槽")))
                                                   .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))] +
                              SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(
                                  155)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(ElementRows, SVerticalBox)]]] +
                              SVerticalBox::Slot().AutoHeight().Padding(
                                  0, 12, 0, 0)[SNew(STextBlock)
                                                   .AutoWrapText(true)
                                                   .Text_Lambda(
                                                       [this]
                                                       {
                                                           return Equipment && Equipment->bNeedsReconcile
                                                                      ? FText::FromString(
                                                                            TEXT("装备效果待恢复，请重试操作"))
                                                                      : Status;
                                                       })]]];
    RefreshEquipment();
    return Result;
}
void UEquipmentPanel::RefreshEquipment()
{
    if (!Rows || !ElementRows)
        return;
    Rows->ClearChildren();
    ElementRows->ClearChildren();
    IconBrushes.Reset();
    IconTextures.Reset();
    if (!Equipment)
        return;
    TArray<FGameplayTag> Ordered;
    for (const auto& SlotInfo : EquipmentSlots::All())
        if (Equipment->AvailableSlots.HasTagExact(SlotInfo.Tag))
            Ordered.Add(SlotInfo.Tag);
    for (const auto& Tag : Equipment->AvailableSlots)
        Ordered.AddUnique(Tag);
    for (const FGameplayTag EquipmentSlotTag : Ordered)
    {
        const FEquipmentSlot* Record = Equipment->Slots.FindByPredicate(
            [EquipmentSlotTag](const FEquipmentSlot& Entry)
            {
                return Entry.SlotTag == EquipmentSlotTag;
            });
        const FBackpackItemDefEntry* Def =
            Record && Model ? Model->FindItemDefinition(Record->ItemDefinitionId) : nullptr;
        TSharedPtr<FSlateBrush> Icon = MakeShared<FSlateBrush>();
        if (Def && Def->Icon)
        {
            IconTextures.Add(Def->Icon);
            Icon->SetResourceObject(Def->Icon);
            Icon->ImageSize = FVector2D(40);
        }
        else
            Icon->DrawAs = ESlateBrushDrawType::NoDrawType;
        IconBrushes.Add(Icon);
        const auto* SlotDefinition = EquipmentSlots::All().FindByPredicate(
            [EquipmentSlotTag](const auto& SlotInfo)
            {
                return SlotInfo.Tag == EquipmentSlotTag;
            });
        const FString Label = SlotDefinition ? SlotDefinition->Label : EquipmentSlotTag.ToString();
        const auto TargetRows = SlotDefinition && SlotDefinition->bElement ? ElementRows : Rows;
        const FText BaseName = Def ? Def->DisplayName : FText::FromString(Record ? TEXT("未知装备") : TEXT("空槽"));
        const FText ItemName = Record ? FEquipmentGenerator::MakeDisplayName(BaseName, Record->RollData) : BaseName;
        const FText Description = Record ? FEquipmentGenerator::MakeDescription(Record->RollData) : FText::GetEmpty();
        TargetRows->AddSlot().AutoHeight().Padding(
            0,
            6)[SNew(SVerticalBox) +
               SVerticalBox::Slot().AutoHeight()
                   [SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FLinearColor(0.85f, 0.7f, 0.4f))] +
               SVerticalBox::Slot().AutoHeight().Padding(
                   0,
                   4)[SNew(SHorizontalBox) +
                      SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(40).HeightOverride(
                          40)[SNew(SImage).Image(Icon.Get())]] +
                      SHorizontalBox::Slot()
                          .FillWidth(1)
                          .VAlign(VAlign_Center)
                          .Padding(8, 0)[SNew(STextBlock).AutoWrapText(true).Text(ItemName).ToolTipText(Description)]] +
               SVerticalBox::Slot().AutoHeight().Padding(
                   0, 2, 0, 4)[SNew(STextBlock)
                                   .AutoWrapText(true)
                                   .Text(Description)
                                   .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))] +
               SVerticalBox::Slot()
                   .AutoHeight()[SNew(SHorizontalBox) +
                                 SHorizontalBox::Slot().FillWidth(1).Padding(
                                     0, 0, 4,
                                     0)[SNew(SButton)
                                            .Text(FText::FromString(Record ? TEXT("替换") : TEXT("装备")))
                                            .IsEnabled_Lambda(
                                                [this, EquipmentSlotTag]
                                                {
                                                    return Backpack &&
                                                           CanEquipToSlot(Model, Backpack->GetCurrentSelectedIndex(),
                                                                          EquipmentSlotTag);
                                                })
                                            .OnClicked_Lambda(
                                                [this, EquipmentSlotTag]
                                                {
                                                    if (Equipment && Backpack)
                                                        Equipment->RequestEquip(Backpack->GetCurrentSelectedIndex(),
                                                                                EquipmentSlotTag);
                                                    return FReply::Handled();
                                                })] +
                                 SHorizontalBox::Slot().FillWidth(1)[SNew(SButton)
                                                                         .Text(FText::FromString(TEXT("卸下")))
                                                                         .IsEnabled(Record != nullptr)
                                                                         .OnClicked_Lambda(
                                                                             [this, EquipmentSlotTag]
                                                                             {
                                                                                 if (Equipment)
                                                                                     Equipment->RequestUnequip(
                                                                                         EquipmentSlotTag);
                                                                                 return FReply::Handled();
                                                                             })]]];
    }
}
