// ASC 按下输入 -> Tag 转移 -> 已授予 Spec -> 动画完成交接；状态随 Avatar 销毁。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "StanderComboComponent.generated.h"

class UMyAbilitySystemComponent;
class UGameplayAbility;
class UStanderWeaponActionAbility;

USTRUCT(BlueprintType)
struct FStanderComboStage
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag StageTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag WeaponActionTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag RequiredTag;
    /** 兼容 DA_Stander 已有的动画和死亡冲量数组。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 DefaultStrikeIndex = 0;
};

USTRUCT(BlueprintType)
struct FStanderComboTransition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag From;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag Input;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag To;
};

/** Tag 决定下一段，GA 继续拥有费用、动画、命中和收尾。 */
UCLASS(ClassGroup = (Combat))
class TEST_25D_API UStanderComboComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UStanderComboComponent();

    bool IsComboInput(UMyAbilitySystemComponent& ASC, FGameplayTag Input) const;
    UFUNCTION(BlueprintCallable, Category = "Combat|Combo")
    bool RequestComboInput(FGameplayTag Input);
    /** 默认剑盾与固有翻滚的数据入口。无对应武器 Spec 时交给普通输入路由。 */
    bool IsWeaponActionInput(UMyAbilitySystemComponent& ASC, FGameplayTag Input) const;
    void RequestWeaponActionInput(UMyAbilitySystemComponent& ASC, FGameplayTag Input);
    bool IsWeaponHandoffPending() const
    {
        return bWeaponHandoffPending;
    }
    bool TryWeaponHandoff(UStanderWeaponActionAbility& From, FGameplayTag Input);
    FGameplayAbilitySpecHandle GetPreflightSource(FGameplayAbilitySpecHandle Target) const;
    bool CanStartWeaponStage(FGameplayAbilitySpecHandle Spec, FGameplayTag Action) const;
    int32 DefaultStrikeIndex(FGameplayAbilitySpecHandle Spec) const;
    /** 入口尚未播放时保持窗口关闭；更新时复核装备代次和当前 Avatar。 */
    bool UpdateStage(FGameplayAbilitySpecHandle Spec, bool bAfterContact, float RemainingSeconds);
    void FinishStage(FGameplayAbilitySpecHandle Spec, bool bCompleted);
    void ResetCombo();

private:
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
    FGameplayAbilitySpecHandle ResolveEntry(UMyAbilitySystemComponent& ASC) const;
    const FStanderComboStage* FindStage(FGameplayTag Stage) const;
    FGameplayTag FindNext(FGameplayTag Input) const;
    bool ValidateRules() const;
    bool IsContextCurrent() const;
    bool StartStage(FGameplayTag Stage);
    FGameplayAbilitySpecHandle ResolveWeaponStage(FGameplayTag Stage) const;
    void ContinueBuffered(uint32 ExpectedSerial);
    void RemoveReadyTags();
    void OnOtherAbilityActivated(UGameplayAbility* Ability);
    UFUNCTION()
    void OnOwnerDied(AActor* Actor);
    double Now() const;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Combo")
    FGameplayTag EntryStage;
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Combo")
    FGameplayTag EntryInput;
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Combo")
    TArray<FStanderComboStage> Stages;
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Combo")
    TArray<FStanderComboTransition> Transitions;
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Combo")
    float BufferSeconds = .15f;
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Combo")
    float ResetSeconds = .9f;

    TWeakObjectPtr<UMyAbilitySystemComponent> BoundASC;
    FDelegateHandle ActivationListener;
    FAbilitySourceToken Source;
    FGameplayAbilitySpecHandle EntrySpec;
    FGameplayAbilitySpecHandle ActiveSpec;
    FGameplayAbilitySpecHandle PreflightSource;
    FGameplayAbilitySpecHandle PreflightTarget;
    FGameplayAbilitySpecHandle PreflightStage;
    FGameplayTag CurrentStage;
    FGameplayTag BufferedInput;
    FGameplayTagContainer ReadyTags;
    uint32 WeaponGeneration = 0;
    uint32 Serial = 0;
    double ReadyUntil = 0.;
    bool bOpen = false;
    bool bWeaponHandoffPending = false;
};
