// 主要调用链：构造角色剑盾组件 -> 剑盾技能命中调用 ShowSwordImpact -> 多播实现生成 Niagara 特效。
#pragma once
#include "CoreMinimal.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "SwordShieldPlayer.generated.h"
class UStaticMeshComponent;
class USwordShieldConfig;
class UNiagaraSystem;
UCLASS()
class TEST_25D_API ASwordShieldPlayer : public ADungeonPlayerCharacter
{
    GENERATED_BODY()
public:
    // 创建武器表现组件并设置挂接、碰撞及角色默认参数。
    ASwordShieldPlayer();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TObjectPtr<USwordShieldConfig> SwordConfig;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UStaticMeshComponent> SwordVisual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UStaticMeshComponent> ShieldVisual;
    // 多播命中特效，在指定世界位置播放剑盾打击表现。
    UFUNCTION(NetMulticast, Unreliable)
    void ShowSwordImpact(UNiagaraSystem* Effect, FVector Location);
};
