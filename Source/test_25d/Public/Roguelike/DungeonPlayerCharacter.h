// 主要链路：地下城死亡改为可逆倒地；保存被动能力句柄，复制倒地状态并封锁行动，复活验证能力归属后恢复资源与被动能力。
#pragma once
#include "CoreMinimal.h"
#include "PureMass/PureMassPlayerCharacter.h"
#include "DungeonPlayerCharacter.generated.h"

/** Run-owned death lifecycle retains the same pawn, ASC and backpack. */
UCLASS()
class TEST_25D_API ADungeonPlayerCharacter : public APureMassPlayerCharacter
{
    GENERATED_BODY()
public:
    // 服务器将死亡转为倒地，缓存活动被动能力并通知局内导演。
    virtual void Die(const FVector& DeathImpulse) override;
    // 移除自身持有的倒地标签与能力封锁，再执行父类清理。
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    // 注册本类需要复制的状态，同时保留父类复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(ReplicatedUsing = OnRep_Downed, BlueprintReadOnly)
    bool bDungeonDowned = false;
    // 服务器验证复活位置、比例及能力归属，清除残留伤害并恢复资源与已装备被动。
    UFUNCTION(BlueprintCallable)
    bool ReviveForDungeon(FVector SafeLocation, float Fraction = .3f);
    // 返回地下城倒地状态。
    UFUNCTION(BlueprintPure)
    bool IsDungeonDowned() const
    {
        return bDungeonDowned;
    }

protected:
    // 完成父类绑定后补应用倒地门禁，兼容复制先于能力系统就绪。
    virtual void InitAbilityActorInfo() override;

private:
    // 幂等应用倒地碰撞、移动和可见性；倒地取消能力，起身恢复行走。
    UFUNCTION()
    void OnRep_Downed();
    // 成对迁移倒地输入标签和能力封锁，避免旧能力系统残留门禁。
    void UpdateDownedInputBlock();
    TWeakObjectPtr<UAbilitySystemComponent> DownedInputASC;
    TArray<FGameplayAbilitySpecHandle> PausedPassiveAbilities;
    bool bAppliedDowned = false;
    ECollisionEnabled::Type LivingCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
};
