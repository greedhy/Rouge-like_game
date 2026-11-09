// 主要调用链：GA或战斗运行时 → 组装效果参数与自定义Context → 应用伤害GE → 属性集结算；UI通过库解析PlayerState和WidgetController。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Roguelike/DungeonRunDirector.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffectTypes.h"
#include "Game/MyGameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "UI/WidgetController/MyWidgetController.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"

#include "Player/MyPlayerState.h"
#include "AbilitySystemComponent.h"
#include "MyAbilityTypes.h"
#include "Interface/CombatInterface.h"
#include "UI/HUD/MyHUD.h"
#include "Engine/OverlapResult.h"
#include "Game/LoadScreenSaveGame.h"
// 获取本地首位玩家 HUD 与 PlayerState，汇总 ASC 和属性集依赖。
bool UMyAbilitySystemLibrary::MakeWidgetControllerParams(const UObject* WorldContextObject,
                                                         FWidgetControllerParams& OutWCParams, AMyHUD*& OutMyHUD)
{

    APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0);

    if (PC)
    {
        OutMyHUD = Cast<AMyHUD>(PC->GetHUD());
        if (OutMyHUD)
        {
            AMyPlayerState* PS = PC->GetPlayerState<AMyPlayerState>();
            OutWCParams.PlayerController = PC;
            OutWCParams.PlayerState = PS;
            OutWCParams.AbilitySystemComponent = PS->GetAbilitySystemComponent();
            OutWCParams.AttributeSet = PS->GetAttributeSet();

            return true;
        }
    }
    return false;
}
UOverlayWidgetController* UMyAbilitySystemLibrary::GetOverlayWidgetController(const UObject* WorldContextObject)
{

    FWidgetControllerParams WCParams;

    AMyHUD* MyHUD = nullptr;
    bool bSuccess = MakeWidgetControllerParams(WorldContextObject, WCParams, MyHUD);

    if (bSuccess)
    {
        return MyHUD->GetOverlayWidgetController(WCParams);
    }
    return nullptr;
}

UAttributeMenuWidgetController* UMyAbilitySystemLibrary::GetAttributeMenuWidgetController(
    const UObject* WorldContextObject)
{
    FWidgetControllerParams WCParams;

    AMyHUD* MyHUD = nullptr;
    bool bSuccess = MakeWidgetControllerParams(WorldContextObject, WCParams, MyHUD);

    if (bSuccess)
    {
        return MyHUD->GetAttributeMenuWidgetController(WCParams);
    }
    return nullptr;
}
USpellMenuWidgetController* UMyAbilitySystemLibrary::GetSpellMenuWidgetController(const UObject* WorldContextObject)
{
    FWidgetControllerParams WCParams;

    AMyHUD* MyHUD = nullptr;
    bool bSuccess = MakeWidgetControllerParams(WorldContextObject, WCParams, MyHUD);

    if (bSuccess)
    {
        return MyHUD->GetSpellMenuWidgetController(WCParams);
    }
    return nullptr;
}
// 按职业配置依次施加主属性、次属性和生命法力 GE。
void UMyAbilitySystemLibrary::InitializeDefaultAttributes(const UObject* WorldContextObject,
                                                          ECharacterClass CharacterClass, float Level,
                                                          UAbilitySystemComponent* ASC)
{
    //获取到当前关卡的GameMode实例
    //从实例获取到关卡角色的配置
    UCharacterClassInfo* ClassInfo = GetCharacterClassInfo(WorldContextObject);
    //获取到默认的基础角色数据
    const FCharacterClassDefaultInfo ClassDefaultInfo = ClassInfo->GetClassDefaultInfo(CharacterClass);
    //应用基础属性
    FGameplayEffectContextHandle PrimaryContextHandle = ASC->MakeEffectContext();
    PrimaryContextHandle.AddSourceObject(WorldContextObject);
    const FGameplayEffectSpecHandle PrimarySpecHandle =
        ASC->MakeOutgoingSpec(ClassDefaultInfo.PrimaryAttributes, Level, PrimaryContextHandle);
    ASC->ApplyGameplayEffectSpecToSelf(*PrimarySpecHandle.Data.Get());

    //设置次级属性
    FGameplayEffectContextHandle SecondaryContextHandle = ASC->MakeEffectContext();
    SecondaryContextHandle.AddSourceObject(WorldContextObject);
    const FGameplayEffectSpecHandle SecondarySpecHandle =
        ASC->MakeOutgoingSpec(ClassInfo->SecondaryAttributes, Level, SecondaryContextHandle);
    ASC->ApplyGameplayEffectSpecToSelf(*SecondarySpecHandle.Data.Get());

    //填充血量和蓝量
    FGameplayEffectContextHandle VitalContextHandle = ASC->MakeEffectContext();
    VitalContextHandle.AddSourceObject(WorldContextObject);
    const FGameplayEffectSpecHandle VitalSpecHandle =
        ASC->MakeOutgoingSpec(ClassInfo->VitalAttributes, Level, VitalContextHandle);
    ASC->ApplyGameplayEffectSpecToSelf(*VitalSpecHandle.Data.Get());
}
ULootTiers* UMyAbilitySystemLibrary::GetLootTiers(const UObject* WorldContextObject)
{
    const AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(WorldContextObject));
    if (MyGameMode == nullptr)
        return nullptr;
    return MyGameMode->LootTiers;
}
// 授予通用一级技能和按角色等级配置的职业技能。
void UMyAbilitySystemLibrary::GiveStartupAbilities(const UObject* WorldContextObject, UAbilitySystemComponent* ASC,
                                                   ECharacterClass CharacterClass)
{
    UCharacterClassInfo* CharacterClassInfo = GetCharacterClassInfo(WorldContextObject);
    if (CharacterClassInfo == nullptr)
        return;

    int32 CharacterLevel = 1;
    if (ASC->GetAvatarActor()->Implements<UCombatInterface>())
    {
        CharacterLevel = ICombatInterface::Execute_GetPlayerLevel(ASC->GetAvatarActor());
    }

    for (TSubclassOf<UGameplayAbility> AbilityClass : CharacterClassInfo->CommonAbilities)
    {
        //common ability do not change with level
        FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, 1);
        ASC->GiveAbility(AbilitySpec);
    }

    const FCharacterClassDefaultInfo DefaultInfo = CharacterClassInfo->GetClassDefaultInfo(CharacterClass);

    //应用职业技能数组
    for (TSubclassOf<UGameplayAbility> AbilityClass : DefaultInfo.StartupAbilities)
    {
        FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, CharacterLevel); //创建技能实例
        ASC->GiveAbility(AbilitySpec);                                                         //只应用不激活
    }
}

UCharacterClassInfo* UMyAbilitySystemLibrary::GetCharacterClassInfo(const UObject* WorldContextObject)
{
    AMyGameModeBase* GameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(WorldContextObject));
    if (GameMode == nullptr)
        return nullptr;
    return GameMode->CharacterClassInfo;
}

UAbilityInfo* UMyAbilitySystemLibrary::GetAbilityInfo(const UObject* WorldContextObject)
{
    AMyGameModeBase* GameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(WorldContextObject));
    if (GameMode == nullptr)
        return nullptr;
    return GameMode->GetAbilityInfoForActor(Cast<AActor>(WorldContextObject));
}

bool UMyAbilitySystemLibrary::IsBlockedHit(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->IsBlockedHit();
    }
    return false;
}

bool UMyAbilitySystemLibrary::IsCriticalHit(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->IsCriticalHit();
    }
    return false;
}

// 球形检索动态对象，去重收集实现战斗接口的存活 Actor。
void UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(const UObject* WorldContextObject,
                                                         TArray<AActor*>& OutOverlappingActors,
                                                         const TArray<AActor*>& ActorsToIgnore, float Radius,
                                                         const FVector& SphereOrigin)
{
    FCollisionQueryParams SphereParams;            //创建一个碰撞查询的配置
    SphereParams.AddIgnoredActors(ActorsToIgnore); //添加忽略的Actor

    TArray<FOverlapResult> Overlaps; //创建存储检索到的与碰撞体产生碰撞的Actor
    if (UWorld* World = GEngine->GetWorldFromContextObject(
            WorldContextObject,
            EGetWorldErrorMode::LogAndReturnNull)) //获取当前所处的场景，如果获取失败，将打印并返回Null
    {
        //获取到所有与此球体碰撞的动态物体
        World->OverlapMultiByObjectType(
            Overlaps, SphereOrigin, FQuat::Identity,
            FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllDynamicObjects),
            FCollisionShape::MakeSphere(Radius), SphereParams);
        for (FOverlapResult& Overlap : Overlaps) //遍历所有获取到的动态Actor
        {
            //判断当前Actor是否包含战斗接口   Overlap.GetActor() 从碰撞检测结果中获取到碰撞的Actor
            const bool ImplementsCombatInterface = Overlap.GetActor()->Implements<UCombatInterface>();
            //判断当前Actor是否存活，如果不包含战斗接口，将不会判断存活（放置的火堆也属于动态Actor，这样保证不会报错）
            if (ImplementsCombatInterface && !ICombatInterface::Execute_IsDead(Overlap.GetActor()))
            {
                OutOverlappingActors.AddUnique(
                    Overlap.GetActor()); //将Actor添加到返回数组，AddUnique 只有在此Actor未被添加时，才可以添加到数组
            }
        }
    }
}

void UMyAbilitySystemLibrary::SetIsBlockedHit(FGameplayEffectContextHandle& EffectContextHandle, bool bInIsBlockedHit)
{
    FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get());
    MyEffectContext->SetIsBlockedHit(bInIsBlockedHit);
}

void UMyAbilitySystemLibrary::SetIsCriticalHit(FGameplayEffectContextHandle& EffectContextHandle, bool bInIsCriticalHit)
{
    FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get());
    MyEffectContext->SetIsCriticalHit(bInIsCriticalHit);
}
// 依据首个 Actor 的 Player 或 Enemy 标签判断另一方是否异阵营。
bool UMyAbilitySystemLibrary::IsNotFriend(AActor* FirstActor, AActor* SecondActor)
{
    if (FirstActor->ActorHasTag("Player"))
    {
        return !SecondActor->ActorHasTag("Player");
    }

    if (FirstActor->ActorHasTag("Enemy"))
    {
        return !SecondActor->ActorHasTag("Enemy");
    }

    return false;
}

int32 UMyAbilitySystemLibrary::GetXPRewardForClassAndLevel(const UObject* WorldContextObject,
                                                           ECharacterClass CharacterClass, int32 CharacterLevel)
{
    UCharacterClassInfo* CharacterClassInfo = GetCharacterClassInfo(WorldContextObject);
    if (CharacterClassInfo == nullptr)
        return 0;

    const FCharacterClassDefaultInfo& Info = CharacterClassInfo->GetClassDefaultInfo(CharacterClass);
    const float XPReward = Info.XPReward.GetValueAtLevel(CharacterLevel);

    return static_cast<int32>(XPReward);
}
// 校验战斗许可，填写上下文与 SetByCaller 数据后向目标施加伤害 GE。
FGameplayEffectContextHandle UMyAbilitySystemLibrary::ApplyDamageEffect(const FDamageEffectParams& DamageEffectParams)
{
    if (!IsValid(DamageEffectParams.SourceAbilitySystemComponent) ||
        !IsValid(DamageEffectParams.TargetAbilitySystemComponent) || !DamageEffectParams.DamageGameplayEffectClass)
        return FGameplayEffectContextHandle();
    const AActor* SourceAvatarActor = DamageEffectParams.SourceAbilitySystemComponent->GetAvatarActor();
    const AActor* TargetAvatarActor = DamageEffectParams.TargetAbilitySystemComponent->GetAvatarActor();
    if (!IsValid(SourceAvatarActor) || !IsValid(TargetAvatarActor) ||
        !ADungeonRunDirector::AllowsCombat(SourceAvatarActor, TargetAvatarActor))
        return FGameplayEffectContextHandle();
    const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();

    FGameplayEffectContextHandle EffectContexthandle =
        DamageEffectParams.SourceAbilitySystemComponent->MakeEffectContext();
    EffectContexthandle.AddSourceObject(SourceAvatarActor);
    SetDeathImpulse(EffectContexthandle, DamageEffectParams.DeathImpulse);
    SetKnockbackForce(EffectContexthandle, DamageEffectParams.KnockbackForce);

    SetIsRadialDamage(EffectContexthandle, DamageEffectParams.bIsRadialDamage);
    SetRadialDamageInnerRadius(EffectContexthandle, DamageEffectParams.RadialDamageInnerRadius);
    SetRadialDamageOuterRadius(EffectContexthandle, DamageEffectParams.RadialDamageOuterRadius);
    SetRadialDamageOrigin(EffectContexthandle, DamageEffectParams.RadialDamageOrigin);

    const FGameplayEffectSpecHandle SpecHandle = DamageEffectParams.SourceAbilitySystemComponent->MakeOutgoingSpec(
        DamageEffectParams.DamageGameplayEffectClass, DamageEffectParams.AbilityLevel, EffectContexthandle);
    if (!SpecHandle.IsValid())
        return FGameplayEffectContextHandle();

    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, DamageEffectParams.DamageType,
                                                                  DamageEffectParams.BaseDamage);
    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Debuff_Chance,
                                                                  DamageEffectParams.DebuffChance);
    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Debuff_Damage,
                                                                  DamageEffectParams.DebuffDamage);
    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Debuff_Duration,
                                                                  DamageEffectParams.DebuffDuration);
    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Debuff_Frequency,
                                                                  DamageEffectParams.DebuffFrequency);

    DamageEffectParams.TargetAbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
    return EffectContexthandle;
}
void UMyAbilitySystemLibrary::SetIsRadialDamageEffectParam(FDamageEffectParams& DamageEffectParams, bool bIsRadial,
                                                           float InnerRadius, float OuterRadius, FVector Origin)
{
    DamageEffectParams.bIsRadialDamage = bIsRadial;
    DamageEffectParams.RadialDamageInnerRadius = InnerRadius;
    DamageEffectParams.RadialDamageOuterRadius = OuterRadius;
    DamageEffectParams.RadialDamageOrigin = Origin;
}
void UMyAbilitySystemLibrary::SetKnockbackForce(FGameplayEffectContextHandle& EffectContextHandle,
                                                const FVector& InForce)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetKnockbackForce(InForce);
    }
}
void UMyAbilitySystemLibrary::SetRadialDamageOuterRadius(FGameplayEffectContextHandle& EffectContextHandle,
                                                         float InOuterRadius)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetRadialDamageOuterRadius(InOuterRadius);
    }
}
void UMyAbilitySystemLibrary::SetRadialDamageInnerRadius(FGameplayEffectContextHandle& EffectContextHandle,
                                                         float InInnerRadius)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetRadialDamageInnerRadius(InInnerRadius);
    }
}
void UMyAbilitySystemLibrary::SetRadialDamageOrigin(FGameplayEffectContextHandle& EffectContextHandle,
                                                    const FVector& InOrigin)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetRadialDamageOrigin(InOrigin);
    }
}
void UMyAbilitySystemLibrary::SetIsRadialDamage(FGameplayEffectContextHandle& EffectContextHandle,
                                                bool bInIsRadialDamage)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetIsRadialDamage(bInIsRadialDamage);
    }
}
void UMyAbilitySystemLibrary::SetDeathImpulseDirection(FDamageEffectParams& DamageEffectParams,
                                                       FVector ImpulseDirection, float Magnitude)
{
    ImpulseDirection.Normalize();
    if (Magnitude == 0.f)
    {
        DamageEffectParams.DeathImpulse = ImpulseDirection * DamageEffectParams.DeathImpulseMagnitude;
    }
    else
    {
        DamageEffectParams.DeathImpulse = ImpulseDirection * Magnitude;
    }
}
void UMyAbilitySystemLibrary::SetDeathImpulse(FGameplayEffectContextHandle& EffectContextHandle,
                                              const FVector& InImpulse)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetDeathImpulse(InImpulse);
    }
}
void UMyAbilitySystemLibrary::SetKnockbackDirection(FDamageEffectParams& DamageEffectParams, FVector KnockbackDirection,
                                                    float Magnitude)
{
    KnockbackDirection.Normalize();
    if (Magnitude == 0.f)
    {
        DamageEffectParams.KnockbackForce = KnockbackDirection * DamageEffectParams.KnockbackForceMagnitude;
    }
    else
    {
        DamageEffectParams.KnockbackForce = KnockbackDirection * Magnitude;
    }
}
void UMyAbilitySystemLibrary::SetTargetEffectParamsASC(FDamageEffectParams& DamageEffectParams,
                                                       UAbilitySystemComponent* InASC)
{
    DamageEffectParams.TargetAbilitySystemComponent = InASC;
}
void UMyAbilitySystemLibrary::SetIsSuccessfulDebuff(FGameplayEffectContextHandle& EffectContextHandle,
                                                    bool bInSuccessfulDebuff)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetIsSuccessfulDebuff(bInSuccessfulDebuff);
    }
}

void UMyAbilitySystemLibrary::SetDebuffDamage(FGameplayEffectContextHandle& EffectContextHandle, float InDamage)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetDebuffDamage(InDamage);
    }
}

void UMyAbilitySystemLibrary::SetDebuffDuration(FGameplayEffectContextHandle& EffectContextHandle, float InDuration)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetDebuffDuration(InDuration);
    }
}

void UMyAbilitySystemLibrary::SetDebuffFrequency(FGameplayEffectContextHandle& EffectContextHandle, float InFrequency)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        MyEffectContext->SetDebuffFrequency(InFrequency);
    }
}

void UMyAbilitySystemLibrary::SetDamageType(FGameplayEffectContextHandle& EffectContextHandle,
                                            const FGameplayTag& InDamageType)
{
    if (FMyGameplayEffectContext* MyEffectContext = static_cast<FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        const TSharedPtr<FGameplayTag> DamageType = MakeShared<FGameplayTag>(InDamageType);
        MyEffectContext->SetDamageType(DamageType);
    }
}

bool UMyAbilitySystemLibrary::IsSuccessfulDebuff(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->IsSuccessfulDebuff();
    }
    return false;
}

float UMyAbilitySystemLibrary::GetDebuffDamage(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->GetDebuffDamage();
    }
    return 0.f;
}

float UMyAbilitySystemLibrary::GetDebuffDuration(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->GetDebuffDuration();
    }
    return 0.f;
}

float UMyAbilitySystemLibrary::GetDebuffFrequency(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->GetDebuffFrequency();
    }
    return 0.f;
}

FGameplayTag UMyAbilitySystemLibrary::GetDamageType(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        if (MyEffectContext->GetDamageType().IsValid())
        {
            return *MyEffectContext->GetDamageType();
        }
    }
    return FGameplayTag();
}

FVector UMyAbilitySystemLibrary::GetDeathImpulse(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->GetDeathImpulse();
    }
    return FVector::ZeroVector;
}
FVector UMyAbilitySystemLibrary::GetKnockbackForce(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->GetKnockbackForce();
    }
    return FVector::ZeroVector;
}
bool UMyAbilitySystemLibrary::IsRadialDamage(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->IsRadialDamage();
    }
    return false;
}

float UMyAbilitySystemLibrary::GetRadialDamageInnerRadius(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->GetRadialDamageInnerRadius();
    }
    return 0.f;
}

float UMyAbilitySystemLibrary::GetRadialDamageOuterRadius(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->GetRadialDamageOuterRadius();
    }
    return 0.f;
}

FVector UMyAbilitySystemLibrary::GetRadialDamageOrigin(const FGameplayEffectContextHandle& EffectContextHandle)
{
    if (const FMyGameplayEffectContext* MyEffectContext =
            static_cast<const FMyGameplayEffectContext*>(EffectContextHandle.Get()))
    {
        return MyEffectContext->GetRadialDamageOrigin();
    }
    return FVector::ZeroVector;
}

TArray<FRotator> UMyAbilitySystemLibrary::EvenlySpacedRotators(const FVector& Forward, const FVector& Axis,
                                                               float Spread, int32 NumRotators)
{
    TArray<FRotator> Rotators;

    const FVector LeftOfSpread = Forward.RotateAngleAxis(-Spread / 2.f, Axis);
    if (NumRotators > 1)
    {
        const float DeltaSpread = Spread / (NumRotators - 1);
        for (int32 i = 0; i < NumRotators; i++)
        {
            const FVector Direction = LeftOfSpread.RotateAngleAxis(DeltaSpread * i, FVector::UpVector);
            Rotators.Add(Direction.Rotation());
        }
    }
    else
    {
        Rotators.Add(Forward.Rotation());
    }
    return Rotators;
}
TArray<FVector> UMyAbilitySystemLibrary::EvenlyRotatedVectors(const FVector& Forward, const FVector& Axis, float Spread,
                                                              int32 NumVectors)
{
    TArray<FVector> Vectors;

    const FVector LeftOfSpread = Forward.RotateAngleAxis(-Spread / 2.f, Axis);
    if (NumVectors > 1)
    {
        const float DeltaSpread = Spread / (NumVectors - 1);
        for (int32 i = 0; i < NumVectors; i++)
        {
            const FVector Direction = LeftOfSpread.RotateAngleAxis(DeltaSpread * i, FVector::UpVector);
            Vectors.Add(Direction);
        }
    }
    else
    {
        Vectors.Add(Forward);
    }
    return Vectors;
}
void UMyAbilitySystemLibrary::GetClosestTargets(int32 MaxTargets, const TArray<AActor*>& Actors,
                                                TArray<AActor*>& OutClosestTargets, const FVector& Origin)
{
    if (Actors.Num() <= MaxTargets)
    {
        OutClosestTargets = Actors;
        return;
    }

    TArray<AActor*> ActorsToCheck = Actors;
    int32 NumTargetsFound = 0;

    while (NumTargetsFound < MaxTargets)
    {
        if (ActorsToCheck.Num() == 0)
            break;
        double ClosestDistance = TNumericLimits<double>::Max();
        AActor* ClosestActor;
        for (AActor* PotentialTarget : ActorsToCheck)
        {
            const double Distance = (PotentialTarget->GetActorLocation() - Origin).Length();
            if (Distance < ClosestDistance)
            {
                ClosestDistance = Distance;
                ClosestActor = PotentialTarget;
            }
        }
        ActorsToCheck.Remove(ClosestActor);
        OutClosestTargets.AddUnique(ClosestActor);
        ++NumTargetsFound;
    }
}

void UMyAbilitySystemLibrary::InitializeDefaultAttributesFromSaveData(const UObject* WorldContextObject,
                                                                      UAbilitySystemComponent* ASC,
                                                                      ULoadScreenSaveGame* SaveGame)
{
    UCharacterClassInfo* CharacterClassInfo = GetCharacterClassInfo(WorldContextObject);
    if (CharacterClassInfo == nullptr)
        return;

    const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();

    const AActor* SourceAvatarActor = ASC->GetAvatarActor();

    FGameplayEffectContextHandle EffectContexthandle = ASC->MakeEffectContext();
    EffectContexthandle.AddSourceObject(SourceAvatarActor);

    const FGameplayEffectSpecHandle SpecHandle =
        ASC->MakeOutgoingSpec(CharacterClassInfo->PrimaryAttributes_SetByCaller, 1.f, EffectContexthandle);

    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Attributes_Primary_Strength,
                                                                  SaveGame->Strength);
    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(
        SpecHandle, GameplayTags.Attributes_Primary_Intelligence, SaveGame->Intelligence);
    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(
        SpecHandle, GameplayTags.Attributes_Primary_Resilience, SaveGame->Resilience);
    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Attributes_Primary_Vigor,
                                                                  SaveGame->Vigor);

    ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);

    FGameplayEffectContextHandle SecondaryAttributesContextHandle = ASC->MakeEffectContext();
    SecondaryAttributesContextHandle.AddSourceObject(SourceAvatarActor);
    const FGameplayEffectSpecHandle SecondaryAttributesSpecHandle =
        ASC->MakeOutgoingSpec(CharacterClassInfo->SecondaryAttributes_Infinite, 1.f, SecondaryAttributesContextHandle);
    ASC->ApplyGameplayEffectSpecToSelf(*SecondaryAttributesSpecHandle.Data.Get());

    FGameplayEffectContextHandle VitalAttributesContextHandle = ASC->MakeEffectContext();
    VitalAttributesContextHandle.AddSourceObject(SourceAvatarActor);
    const FGameplayEffectSpecHandle VitalAttributesSpecHandle =
        ASC->MakeOutgoingSpec(CharacterClassInfo->VitalAttributes, 1.f, VitalAttributesContextHandle);
    ASC->ApplyGameplayEffectSpecToSelf(*VitalAttributesSpecHandle.Data.Get());
}
