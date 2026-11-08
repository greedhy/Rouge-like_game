/** 主要调用链：死亡生命周期测试 -> 构造无模型Player夹具 -> 继承生产Die/EndPlay -> 检查计时器与销毁清理。 */
#pragma once
#include "CoreMinimal.h"
#include "Character/PlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "LifecyclePlayerProbe.generated.h"
// Meshless fixture exercises the inherited production Die/EndPlay without querying an absent socket.
UCLASS()
class ALifecyclePlayerProbe : public APlayerCharacter
{
    GENERATED_BODY()
public:
    ALifecyclePlayerProbe()
    {
        GetWeapon_Implementation()->SetupAttachment(GetMesh());
    }
};
