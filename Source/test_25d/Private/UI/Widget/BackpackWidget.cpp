// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Widget/BackpackWidget.h"
#include "UI/ViewModel/MVVM_Backpack.h"
#include "MVVMSubsystem.h"
#include "View/MVVMView.h"

void UBackpackWidget::SetBackpackViewModel(UMVVM_Backpack* InBackpackViewModel)
{
    BackpackViewModel = InBackpackViewModel;
    if (UMVVMView* MVVMView = UMVVMSubsystem::GetViewFromUserWidget(this))
    {
        MVVMView->SetViewModelByClass(InBackpackViewModel);
    }
}
//OperateUI.cpp

//
//void UBackpackWidget::OnThrowButtonClick()
//{
//    //if (!IsValid(GetWorld())) return;
//    //ABackPack_CppCharacter* Player = Cast<ABackPack_CppCharacter>(GetWorld()->GetFirstPlayerController()->GetPawn());
//    //UBackPackComponent* BackPackComponent = Player->BackPackComponent;
//
//    //if (SlotIndex >= 0)  // 基本的边界检查
//    //{
//    //    BackPackComponent->ThrowItem(SlotIndex);
//    //}
//    //RemoveFromParent();
//}
//void UBackpackWidget::OnUseButtonClick()
//{
//    //if (!IsValid(GetWorld())) return;
//    //ABackPack_CppCharacter* Player = Cast<ABackPack_CppCharacter>(GetWorld()->GetFirstPlayerController()->GetPawn());
//    //UBackPackComponent* BackPackComponent = Player->BackPackComponent;
//
//    //if (SlotIndex >= 0)  // 基本的边界检查
//    //{
//    //    BackPackComponent->ThrowItem(SlotIndex);
//    //}
//    //RemoveFromParent();
//}
