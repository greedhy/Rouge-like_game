// 调用链：输入绑定查询 InputTag → 遍历配置项 → 返回对应 InputAction；缺失时按调用方选项记录日志。
// Fill out your copyright notice in the Description page of Project Settings.

#include "Input/MyInputConfig.h"

// 查找 Tag 精确匹配且资源非空的输入动作，未匹配返回空指针。
const UInputAction* UMyInputConfig::FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound) const
{
    for (const FMyInputAction& Action : AbilityInputActions)
    {
        if (Action.InputAction && Action.InputTag == InputTag)
        {
            return Action.InputAction;
        }
    }
    if (bLogNotFound)
    {
        UE_LOG(LogTemp, Error, TEXT("Input action not found for tag: [%s] on InputConfig [%s]"), *InputTag.ToString(),
               *GetNameSafe(this));
    }
    return nullptr;
}
