// 主要调用链：伤害参数构建 → 自定义EffectContext携带暴击、冲量和范围信息 → GAS复制/结算读取；结构体本身不驱动Tick。

#pragma once

#include "GameplayEffectTypes.h"
#include "MyAbilityTypes.generated.h"
class UGameplayEffect;
/**
 * 
 */
USTRUCT(BlueprintType)
struct FDamageEffectParams
{
    GENERATED_BODY()

    FDamageEffectParams()
    {
    }

    UPROPERTY(BlueprintReadWrite)
    TObjectPtr<UObject> WorldContextObject = nullptr;

    UPROPERTY(BlueprintReadWrite)
    TSubclassOf<UGameplayEffect> DamageGameplayEffectClass = nullptr;

    UPROPERTY(BlueprintReadWrite)
    TObjectPtr<UAbilitySystemComponent> SourceAbilitySystemComponent;

    UPROPERTY(BlueprintReadWrite)
    TObjectPtr<UAbilitySystemComponent> TargetAbilitySystemComponent;

    UPROPERTY(BlueprintReadWrite)
    float BaseDamage = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float AbilityLevel = 1.f;

    UPROPERTY(BlueprintReadWrite)
    FGameplayTag DamageType = FGameplayTag();

    UPROPERTY(BlueprintReadWrite)
    float DebuffChance = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float DebuffDamage = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float DebuffDuration = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float DebuffFrequency = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float DeathImpulseMagnitude = 0.f;

    UPROPERTY(BlueprintReadWrite)
    FVector DeathImpulse = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite)
    float KnockbackForceMagnitude = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float KnockbackChance = 0.f;

    UPROPERTY(BlueprintReadWrite)
    FVector KnockbackForce = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite)
    bool bIsRadialDamage = false;

    UPROPERTY(BlueprintReadWrite)
    float RadialDamageInnerRadius = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float RadialDamageOuterRadius = 0.f;

    UPROPERTY(BlueprintReadWrite)
    FVector RadialDamageOrigin = FVector::ZeroVector;
};

USTRUCT(BlueprintType) //在蓝图中可作为类型使用
struct FMyGameplayEffectContext : public FGameplayEffectContext
{
    GENERATED_BODY() //宏 自动生成构造函数、析构函数、拷贝构造函数等

public:
    bool IsBlockedHit() const
    {
        return bIsBlockedHit;
    }
    bool IsCriticalHit() const
    {
        return bIsCriticalHit;
    }

    void SetIsBlockedHit(const bool bInIsBlockedHit)
    {
        bIsBlockedHit = bInIsBlockedHit;
    }
    void SetIsCriticalHit(const bool bInIsCriticalHit)
    {
        bIsCriticalHit = bInIsCriticalHit;
    }

    //
    bool IsSuccessfulDebuff() const
    {
        return bIsSuccessfulDebuff;
    }
    float GetDebuffDamage() const
    {
        return DebuffDamage;
    }
    float GetDebuffDuration() const
    {
        return DebuffDuration;
    }
    float GetDebuffFrequency() const
    {
        return DebuffFrequency;
    }
    TSharedPtr<FGameplayTag> GetDamageType() const
    {
        return DamageType;
    }
    FVector GetDeathImpulse() const
    {
        return DeathImpulse;
    }

    FVector GetKnockbackForce() const
    {
        return KnockbackForce;
    }
    bool IsRadialDamage() const
    {
        return bIsRadialDamage;
    }
    float GetRadialDamageInnerRadius() const
    {
        return RadialDamageInnerRadius;
    }
    float GetRadialDamageOuterRadius() const
    {
        return RadialDamageOuterRadius;
    }
    FVector GetRadialDamageOrigin() const
    {
        return RadialDamageOrigin;
    }
    //
    void SetIsSuccessfulDebuff(bool bInIsDebuff)
    {
        bIsSuccessfulDebuff = bInIsDebuff;
    }
    void SetDebuffDamage(float InDamage)
    {
        DebuffDamage = InDamage;
    }
    void SetDebuffDuration(float InDuration)
    {
        DebuffDuration = InDuration;
    }
    void SetDebuffFrequency(float InFrequency)
    {
        DebuffFrequency = InFrequency;
    }
    void SetDamageType(TSharedPtr<FGameplayTag> InDamageType)
    {
        DamageType = InDamageType;
    }
    void SetDeathImpulse(const FVector& InImpulse)
    {
        DeathImpulse = InImpulse;
    }

    void SetKnockbackForce(const FVector& InForce)
    {
        KnockbackForce = InForce;
    }
    void SetIsRadialDamage(bool bInIsRadialDamage)
    {
        bIsRadialDamage = bInIsRadialDamage;
    }
    void SetRadialDamageInnerRadius(float InRadialDamageInnerRadius)
    {
        RadialDamageInnerRadius = InRadialDamageInnerRadius;
    }
    void SetRadialDamageOuterRadius(float InRadialDamageOuterRadius)
    {
        RadialDamageOuterRadius = InRadialDamageOuterRadius;
    }
    void SetRadialDamageOrigin(const FVector& InRadialDamageOrigin)
    {
        RadialDamageOrigin = InRadialDamageOrigin;
    }
    /** 返回用于序列化的实际结构体 */
    virtual UScriptStruct* GetScriptStruct() const override
    {
        return FMyGameplayEffectContext::StaticStruct();
    }

    /** 用于序列化类的参数 */
    // 以二十位存在标记按需序列化上下文，加载后重建施加者 ASC 引用。
    virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;
    /** 创建一个副本，用于后续网络复制或者后续修改 */
    virtual FMyGameplayEffectContext* Duplicate() const override
    {
        FMyGameplayEffectContext* NewContext = new FMyGameplayEffectContext();
        *NewContext = *this; //WithCopy 设置为true，就可以通过赋值操作进行拷贝
        if (GetHitResult())
        {
            // 深拷贝 hit result
            NewContext->AddHitResult(*GetHitResult(), true);
        }
        return NewContext;
    }

protected:
    UPROPERTY()
    bool bIsBlockedHit = false; //格挡

    UPROPERTY()
    bool bIsCriticalHit = false; //暴击

    UPROPERTY()
    bool bIsSuccessfulDebuff = false;

    UPROPERTY()
    float DebuffDamage = 0.f;

    UPROPERTY()
    float DebuffDuration = 0.f;

    UPROPERTY()
    float DebuffFrequency = 0.f;

    TSharedPtr<FGameplayTag> DamageType;

    UPROPERTY()
    FVector DeathImpulse = FVector::ZeroVector;

    UPROPERTY()
    FVector KnockbackForce = FVector::ZeroVector;

    UPROPERTY()
    bool bIsRadialDamage = false;

    UPROPERTY()
    float RadialDamageInnerRadius = 0.f;

    UPROPERTY()
    float RadialDamageOuterRadius = 0.f;

    UPROPERTY()
    FVector RadialDamageOrigin = FVector::ZeroVector;
};
template <>
struct TStructOpsTypeTraits<FMyGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FMyGameplayEffectContext>
{
    enum
    {
        WithNetSerializer = true,
        WithCopy = true // Necessary so that TSharedPtr<FHitResult> Data is copied around
    };
};
