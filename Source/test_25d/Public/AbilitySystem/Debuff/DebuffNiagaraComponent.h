// ASC 标签变化 -> 状态特效；眩晕按敌人体型置于头顶，结束/死亡/EndPlay 清理。

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NiagaraComponent.h"
#include "DebuffNiagaraComponent.generated.h"

class UAbilitySystemComponent;

/** 复用拥有者 ASC 的状态，不为表现创建额外 GE 或独立计时器。 */
UCLASS()
class TEST_25D_API UDebuffNiagaraComponent : public UNiagaraComponent
{
    GENERATED_BODY()
public:
    UDebuffNiagaraComponent();

    UPROPERTY(VisibleAnywhere)
    FGameplayTag DebuffTag;

    /** Actor 与 Mass 共用的头顶间距和尺寸；输入为世界空间胶囊尺寸。 */
    static FTransform MakeStunTransform(float HalfHeight, float Radius);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void DebuffTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

    UFUNCTION()
    void OnOwnerDeath(AActor* DeadActor);

private:
    void BindAbilitySystem(UAbilitySystemComponent* ASC);
    void StopDebuff();
    TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
    FDelegateHandle TagChangedHandle;
    FDelegateHandle ASCRegisteredHandle;
};
