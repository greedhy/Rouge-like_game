/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 生命周期链：蓝图飘字动画结束调用DestroyComponent -> 若NumberPool仍有效则Release -> 池化且世界未退出时保留组件；否则调用Super真正销毁。
 * NumberPool为弱引用，不拥有世界子系统。正常入池不等于销毁；池退出会先清回指与bNumberPooled，确保最终DestroyComponent不会被本覆盖函数拦截。组件是本地表现，不同步权威伤害。
 */
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
    virtual void DestroyComponent(bool bPromoteChildren = false) override;
    TWeakObjectPtr<UDamageNumberPool> NumberPool;
    bool bNumberPooled = false;
    bool bNumberActive = false;
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable)
    void SetDamageText(float Damage, bool bBlockHit, bool bCriticalHit);
};
