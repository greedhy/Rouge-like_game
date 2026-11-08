/**
 * 第六批 Field 参数扩展：资产继承 UAbilityPrototype，GA 继承 UPureMassAbility，执行仍由 APureMassRuntime 完成；波前判定、跟随开关、地面圆盘/雪花和减速字段由该 Runtime 消费，不创建另一套独立伤害定时器。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "PureMass/PureMassAbility.h"
#include "Batch6FieldAbility.generated.h"
class UMaterialInterface;

/** Opt-in parameters consumed by the mixed-target runtime; existing assets are unchanged. */
UCLASS(BlueprintType)
class TEST_25D_API UBatch6FieldPrototype : public UAbilityPrototype
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bFollowCasterToggle = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bWaveFrontOnly = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bUseAuthoredVFXScale = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    bool bShowAreaDisc = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    TObjectPtr<UMaterialInterface> AreaMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    bool bSnowfall = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.05", ClampMax = "1"))
    float SlowMultiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1", ClampMax = "10"))
    float SlowDuration = 1.25f;
    /** Runtime Pulse 的波前筛选：只接受当前波半径内且超过上一半径的有限非负距离。 */
    static bool IsInWaveFront(float Distance, float Previous, float Current)
    {
        return FMath::IsFinite(Distance) && Distance >= 0.f && Distance <= Current &&
               (Previous <= 0.f || Distance > Previous);
    }
};

UCLASS()
class TEST_25D_API UBatch6FieldAbility : public UPureMassAbility
{
    GENERATED_BODY()
public:
    /** UI 对跟随开关 Field 显示持续扣蓝与周期伤害，其余配置回退父类说明。 */
    virtual FString GetDescription(int32 Level) override
    {
        const auto* Config = Cast<UBatch6FieldPrototype>(Prototype);
        if (!Config || !Config->bFollowCasterToggle)
            /** UI 对跟随开关 Field 显示持续扣蓝与周期伤害，其余配置回退父类说明。 */
            return Super::GetDescription(Level);
        return FString::Printf(
            TEXT(
                "<Title>%s</>\n<Small>Lv.%d | MP %.0f | CD %.1fs</>\n<Default>Toggle: %.0f MP/s; %.0f damage/%.1fs. Radius %.0f.</>"),
            *Config->DisplayName.ToString(), Level, Config->ManaCost, Config->Cooldown, Config->ManaPerSecond,
            Config->DamageAtLevel(Level), Config->Period, Config->Radius);
    }
    /** 输入层识别跟随 Field 为开关技能；其他类型沿用父类 Orbit 等语义。 */
    virtual bool IsInputToggleAbility() const override
    {
        const auto* Config = Cast<UBatch6FieldPrototype>(Prototype);
        return (Config && Config->Kind == EAbilityPrototypeKind::Field && Config->bFollowCasterToggle) ||
               /** 输入层识别跟随 Field 为开关技能；其他类型沿用父类 Orbit 等语义。 */
               Super::IsInputToggleAbility();
    }
};
