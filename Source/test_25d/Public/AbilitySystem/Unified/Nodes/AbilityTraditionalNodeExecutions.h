// 调用链：传统GA前端/统一图 -> 共享投射物或召唤动作 -> 原Actor、AI、伤害与生命计数通路。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "Components/ActorComponent.h"
#include "AbilityTraditionalNodeExecutions.generated.h"

class AMyProjectile;
class APawn;
class FAbilityNodeRegistry;

/** 原投射物配置；配置动作不会提交GA，也不会替代投射物自己的碰撞/伤害实现。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityActorProjectileSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<AMyProjectile> ProjectileClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Count = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bLimitCountToLevel = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Spread = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOverridePitch = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PitchOverride = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bConfigureHoming = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bLaunchHoming = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HomingAccelerationMin = 1600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HomingAccelerationMax = 3200.f;
    bool Validate(FString& Error) const;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityActorProjectileNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAbilityActorProjectileSettings Settings;
    /** 未设置时从输入Origin发射；设置后沿用CombatInterface的实际武器/手部插槽。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag SocketTag;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilitySummonNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<TSubclassOf<APawn>> MinionClasses;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Count = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MinDistance = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxDistance = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Spread = 90.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SpawnHeight = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Interval = .2f;
    /** 蒙太奇前端预警时已抽取的位置；空数组表示在节点启动时统一抽取一次。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FVector> PreparedLocations;
    virtual bool Validate(FString& Error) const override;
};

/** 每只召唤物保存独立来源生命；销毁后只回收原生命的计数，不借用会被AI接管的Owner。 */
UCLASS(Transient)
class TEST_25D_API UAbilitySummonedPawnLease : public UActorComponent
{
    GENERATED_BODY()
public:
    bool Initialize(const FAbilitySourceToken& InSource);

private:
    UFUNCTION()
    void OnPawnDestroyed(AActor* Pawn);
    FAbilitySourceToken Source;
    bool bCounted = false;
};

namespace AbilityTraditionalActions
{
    /** 延迟构造前验证原来源；伤害参数和生命令牌在蓝图Construction/BeginPlay前写入。 */
    TEST_25D_API AMyProjectile* CreateProjectileDeferred(const FAbilitySourceToken& Source,
                                                         const FDamageEffectParams& Damage,
                                                         TSubclassOf<AMyProjectile> Class, const FTransform& Transform);
    /** 保留原扩散顺序与每弹一次追踪随机数；返回实际完成构造的Actor。 */
    TEST_25D_API TArray<AMyProjectile*> SpawnProjectiles(const FAbilitySourceToken& Source,
                                                         const FDamageEffectParams& Damage,
                                                         const FAbilityActorProjectileSettings& Settings, float Level,
                                                         const FVector& Origin, const FVector& AimPoint,
                                                         AActor* HomingTarget = nullptr);
    /** 原地形投影与距离随机顺序；单只召唤使用正前方，非法参数失败返回空。 */
    TEST_25D_API TArray<FVector> GatherSummonLocations(const FAbilitySourceToken& Source, const FVector& Origin,
                                                       const FVector& Forward, int32 Count, float MinDistance,
                                                       float MaxDistance, float Spread);
    TEST_25D_API TSubclassOf<APawn> ChooseMinionClass(const TArray<TSubclassOf<APawn>>& Classes);
    /** 原AlwaysSpawn、无Owner/Instigator、SpawnDefaultController，再绑定按来源生命回收的计数。 */
    TEST_25D_API APawn* SpawnSummonedPawn(const FAbilitySourceToken& Source, TSubclassOf<APawn> Class,
                                          const FTransform& Transform);
} // namespace AbilityTraditionalActions

UCLASS(Transient)
class TEST_25D_API UAbilityActorProjectileNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilitySummonNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    void SpawnNext();
    UPROPERTY(Transient)
    TObjectPtr<UAbilitySummonNodeConfig> Settings;
    TArray<FVector> Locations;
    int32 NextIndex = 0;
    float Elapsed = 0.f;
};

namespace AbilityTraditionalNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
