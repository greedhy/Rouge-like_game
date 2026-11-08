// 主要调用链：剑盾技能激活读取 Validate -> ResolveComboIndex 选择连段 -> 命中时 FSwordStrike::Contains 判断扇形。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SwordShieldConfig.generated.h"
class UAnimMontage;
class UNiagaraSystem;

USTRUCT(BlueprintType)
struct FSwordStrike
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UAnimMontage> Montage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Damage = 25;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Radius = 160;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Angle = 100;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Contact = .22f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float PlayRate = 1;
    // 检查配置引用、数值范围与时间约束是否可供技能使用。
    bool Validate() const;
    // 按高度差、水平半径和扇形角判断目标是否位于攻击范围。
    bool Contains(FVector Origin, FVector Forward, FVector Target) const;
};
UCLASS(BlueprintType)
class TEST_25D_API USwordShieldConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FSwordStrike> Combo;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FSwordStrike Spin;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UAnimMontage> GuardMontage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float ComboReset = .9f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float GuardDuration = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float GuardCooldown = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float SpinCooldown = 5;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float SpinMana = 15;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UNiagaraSystem> HitEffect;
    // 检查配置引用、数值范围与时间约束是否可供技能使用。
    bool Validate() const;
    // 在有效连击间隔内保留下一段索引，否则从首段重新开始。
    static int32 ResolveComboIndex(int32 Next, double LastEnd, double Now, float Reset);
};
