/** 主要调用链：旧投射物重入测试 -> 碰撞改变委托 -> DestroyOnCollisionChange -> 销毁宿主 -> 验证回调后有效性保护。 */
#pragma once
#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "LegacyProjectileTestListener.generated.h"

UCLASS()
class ULegacyProjectileTestListener : public UObject
{
    GENERATED_BODY()
public:
    bool bCalled = false;
    UFUNCTION()
    void DestroyOnCollisionChange(UPrimitiveComponent* Component)
    {
        bCalled = true;
        Component->GetOwner()->Destroy();
    }
};
