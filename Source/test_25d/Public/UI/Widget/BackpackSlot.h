// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Blueprint/UserWidget.h"

#include "BackpackSlot.generated.h"

/**
 * TileView 条目基类。具体条目的 MVVM Source 由 UE 的 ListView MVVM
 * Extension 在条目生成时自动注入。
 */
UCLASS()
class TEST_25D_API UBackpackSlot : public UUserWidget, public IUserObjectListEntry
{
    GENERATED_BODY()

protected:
    virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
                                      UDragDropOperation*& Operation) override;
    virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
                                   UDragDropOperation* InOperation) override;
    virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
    virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
                              UDragDropOperation* InOperation) override;
    virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<class UMVVM_InventorySlot> SlotViewModel;
};
