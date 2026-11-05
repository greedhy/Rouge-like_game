// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Widget/DamageTextComponent.h"
#include "UI/Widget/DamageNumberPool.h"
#include "Engine/World.h"

void UDamageTextComponent::DestroyComponent(bool bPromoteChildren)
{
 if(NumberPool.IsValid())
 {
  NumberPool->Release(this);
  if(bNumberPooled && GetWorld() && !GetWorld()->bIsTearingDown) return;
 }
 Super::DestroyComponent(bPromoteChildren);
}

