/** 主要调用链：UI生命周期测试 -> 绑定Health委托 -> 累计Calls -> 检查重复绑定和解绑。 */
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LifecycleUIProbe.generated.h"
UCLASS()
class ULifecycleUIProbe : public UObject
{
    GENERATED_BODY()
public:
    int32 Calls = 0;
    UFUNCTION()
    void Health(float Value)
    {
        ++Calls;
    }
};
