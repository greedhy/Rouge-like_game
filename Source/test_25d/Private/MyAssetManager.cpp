/** 调用链与单例生命周期：引擎按配置创建并由GEngine->AssetManager持有；Get仅返回配置实例，不创建新对象。StartInitialLoading调用Super后注册项目原生标签。生命周期属于引擎，跨World使用，不应由游戏Actor销毁。实例本身不复制，各进程各自初始化。 */
// Fill out your copyright notice in the Description page of Project Settings.

#include "MyAssetManager.h"
#include "MyGameplayTags.h"
UMyAssetManager& UMyAssetManager::Get()
{
    // TODO: 在此处插入 return 语句

    check(GEngine);
    UMyAssetManager* MyAssetManager = Cast<UMyAssetManager>(GEngine->AssetManager);
    return *MyAssetManager;
}
void UMyAssetManager::StartInitialLoading()
{
    Super::StartInitialLoading();
    // 这里可以添加一些在游戏开始时需要加载的资源
    FMyGameplayTags::InitializeNativeGameplayTags();
}