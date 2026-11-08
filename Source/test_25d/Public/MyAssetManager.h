/** 调用链与单例生命周期：引擎按配置创建并由GEngine->AssetManager持有；Get仅返回配置实例，不创建新对象。StartInitialLoading调用Super后注册项目原生标签。生命周期属于引擎，跨World使用，不应由游戏Actor销毁。实例本身不复制，各进程各自初始化。 */
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "MyAssetManager.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyAssetManager : public UAssetManager
{
    GENERATED_BODY()
public:
    static UMyAssetManager& Get();

protected:
    virtual void StartInitialLoading() override;
};
