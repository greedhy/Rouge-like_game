// 主要调用链：BeginPlay -> 装备组件 RefreshWeaponVisuals -> ApplyWeaponSetVisuals；骨骼武器组件替代继承的静态剑盾外观。
#pragma once
#include "Character/SwordShieldPlayer.h"
#include "StanderPlayer.generated.h"

class UAnimInstance;
class USkeletalMesh;

UCLASS()
class TEST_25D_API AStanderPlayer : public ASwordShieldPlayer
{
    GENERATED_BODY()
public:
    // 创建武器表现组件并设置挂接、碰撞及角色默认参数。
    AStanderPlayer();
    // 首次缓存默认外观，再应用武器套装网格、插槽、变换和动画类；空套装恢复默认值。
    void ApplyWeaponSetVisuals(class UWeaponSetConfig* Set);
    // 角色开始运行后请求装备组件刷新武器外观。
    virtual void BeginPlay() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMesh> DefaultMainMesh;
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMesh> DefaultOffhandMesh;
    UPROPERTY(Transient)
    TSubclassOf<UAnimInstance> DefaultAnimClass;
    FTransform DefaultMainTransform;
    FTransform DefaultOffhandTransform;
    FName DefaultMainSocket;
    FName DefaultOffhandSocket;
    bool bWeaponVisualDefaultsCaptured = false;

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<USkeletalMeshComponent> StanderSword;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<USkeletalMeshComponent> StanderShield;
};
