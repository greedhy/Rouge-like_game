// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "DamageTextComponent.generated.h"
class UDamageNumberPool;

/**
 * 
 */
UCLASS()
class TEST_25D_API UDamageTextComponent : public UWidgetComponent
{
	GENERATED_BODY()
public:
	virtual void DestroyComponent(bool bPromoteChildren=false) override;
	TWeakObjectPtr<UDamageNumberPool> NumberPool;
	bool bNumberPooled=false;
	bool bNumberActive=false;
	UFUNCTION(BlueprintImplementableEvent,BlueprintCallable)
	void SetDamageText(float Damage, bool bBlockHit, bool bCriticalHit);

};
