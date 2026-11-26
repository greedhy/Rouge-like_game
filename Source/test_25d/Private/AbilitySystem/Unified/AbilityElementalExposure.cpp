// 调用链：Actor/Mass 承伤或图节点 -> Evaluate 共享反应或精确消耗 -> 各目标生命提交并管理到期。
#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "MyGameplayTags.h"
#include "NiagaraSystem.h"
#include "PureMass/PureMassSubsystem.h"

EAbilityExposure FAbilityElementalExposure::Get(double Now) const
{
    return Now < ExpiresAt ? Status : EAbilityExposure::None;
}

UNiagaraSystem* AbilityElements::GetFrostVisual()
{
    // 冰霜地表 DA 同时持有此资源的硬引用，承伤与专用服务器均不加载表现。
    return LoadObject<UNiagaraSystem>(
        nullptr, TEXT("/Game/Art/TerrainSurfaces/Effects/NS_Frost_Status.NS_Frost_Status"));
}

EAbilityElement AbilityElements::FromDamageTag(FGameplayTag Tag)
{
    if (Tag.IsValid())
    {
        for (const auto Element : {EAbilityElement::Fire, EAbilityElement::Water, EAbilityElement::Thunder,
                                   EAbilityElement::Arcane, EAbilityElement::Ice})
        {
            if (Tag == ToDamageTag(Element))
            {
                return Element;
            }
        }
    }
    return EAbilityElement::Original;
}

FGameplayTag AbilityElements::ToDamageTag(EAbilityElement Element)
{
    const auto& Tags = FMyGameplayTags::Get();
    switch (Element)
    {
    case EAbilityElement::Fire:
        return Tags.Damage_Fire;
    case EAbilityElement::Water:
        return Tags.Damage_Water;
    case EAbilityElement::Thunder:
        return Tags.Damage_Thunder;
    case EAbilityElement::Arcane:
        return Tags.Damage_Magic;
    case EAbilityElement::Ice:
        return Tags.Damage_Ice;
    default:
        return {};
    }
}

FAbilityElementalReaction AbilityElements::Evaluate(EAbilityExposure Current, EAbilityElement Incoming)
{
    using E = EAbilityElement;
    using S = EAbilityExposure;
    if (Current == S::Frozen)
    {
        return {S::Frozen, 1.f, 0.f, true};
    }
    static constexpr struct
    {
        S Current;
        E Incoming;
        FAbilityElementalReaction Reaction;
    } Rules[] = {
        {S::Burning, E::Water, {S::Steam, 1.f, 2.f}},         {S::Wet, E::Fire, {S::Steam, 1.f, 2.f}},
        {S::Wet, E::Thunder, {S::Conductive, 1.25f, 2.f}},    {S::Steam, E::Thunder, {S::Conductive, 1.25f, 2.f}},
        {S::Shocked, E::Water, {S::Conductive, 1.25f, 2.f}},  {S::Conductive, E::Thunder, {S::Shocked, 1.25f, 4.f}},
        {S::ArcaneMark, E::Fire, {S::Burning, 1.2f, 4.f}},    {S::ArcaneMark, E::Water, {S::Wet, 1.2f, 4.f}},
        {S::ArcaneMark, E::Thunder, {S::Shocked, 1.2f, 4.f}}, {S::Burning, E::Arcane, {S::ArcaneMark, 1.2f, 4.f}},
        {S::Chilled, E::Ice, {S::Frozen, 1.f, FreezeSeconds}},
        {S::Wet, E::Ice, {S::Frozen, 1.f, FreezeSeconds}},
        {S::Chilled, E::Water, {S::Frozen, 1.f, FreezeSeconds}}};
    for (const auto& Rule : Rules)
    {
        if (Current == Rule.Current && Incoming == Rule.Incoming)
        {
            return Rule.Reaction;
        }
    }
    switch (Incoming)
    {
    case E::Fire:
        return {S::Burning, 1.f, 4.f};
    case E::Water:
        return {S::Wet, 1.f, 4.f};
    case E::Thunder:
        return {S::Shocked, 1.f, 4.f};
    case E::Arcane:
        return {S::ArcaneMark, 1.f, 4.f};
    case E::Ice:
        return {S::Chilled, 1.f, ChillSeconds};
    default:
        return {};
    }
}

EAbilityExposure AbilityElements::Read(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target)
{
    FVector Position;
    if (!AbilityExecutionIdentity::ResolveTarget(Source, Target, Position))
    {
        return EAbilityExposure::None;
    }
    if (Target.Target.IsActor())
    {
        const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Target.Actor.Get());
        const auto* Attributes = ASC ? ASC->GetSet<UMyAttributeSet>() : nullptr;
        return Attributes ? Attributes->GetElementalExposure() : EAbilityExposure::None;
    }
    const auto* Mass = Source.World->GetSubsystem<UPureMassSubsystem>();
    return Mass ? Mass->GetElementalExposure(Target.Target.EntityId) : EAbilityExposure::None;
}

bool AbilityElements::CanApply(EAbilityElement Element, float DurationScale)
{
    return Element >= EAbilityElement::Fire && Element <= EAbilityElement::Ice &&
           FMath::IsFinite(DurationScale) && DurationScale >= .5f && DurationScale <= 2.f;
}

bool AbilityElements::Apply(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                            EAbilityElement Element, float DurationScale)
{
    AActor* Avatar = nullptr;
    FVector Location;
    if (!CanApply(Element, DurationScale) || !AbilityExecutionIdentity::ResolveSource(Source, Avatar) ||
        !AbilityExecutionIdentity::ResolveTarget(Source, Target, Location))
    {
        return false;
    }
    if (Target.Target.IsActor())
    {
        const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Target.Actor.Get());
        // GAS 的读取接口返回 const，属性子对象在已验证的权威 ASC 上是可写对象。
        auto* Attributes = ASC && ASC->GetAvatarActor() == Target.Target.Actor.Get()
                               ? const_cast<UMyAttributeSet*>(ASC->GetSet<UMyAttributeSet>())
                               : nullptr;
        return Attributes && Attributes->ApplyElementalExposure(Element, DurationScale);
    }
    auto* Mass = Source.World->GetSubsystem<UPureMassSubsystem>();
    return Mass && Mass->ApplyElementalExposure(Target.Target.EntityId, Avatar, Element, DurationScale);
}

bool AbilityElements::Consume(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                              EAbilityExposure Expected)
{
    AActor* Avatar = nullptr;
    FVector Location;
    if (Expected == EAbilityExposure::None || !AbilityExecutionIdentity::ResolveSource(Source, Avatar) ||
        !AbilityExecutionIdentity::ResolveTarget(Source, Target, Location))
    {
        return false;
    }
    if (Target.Target.IsActor())
    {
        const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Target.Actor.Get());
        // 只写当前 Avatar 上的权威属性集，防止已解绑的 ASC 操作新角色。
        auto* Attributes = ASC && ASC->GetAvatarActor() == Target.Target.Actor.Get()
                               ? const_cast<UMyAttributeSet*>(ASC->GetSet<UMyAttributeSet>())
                               : nullptr;
        return Attributes && Attributes->ConsumeElementalExposure(Expected);
    }
    auto* Mass = Source.World->GetSubsystem<UPureMassSubsystem>();
    return Mass && Mass->ConsumeElementalExposure(Target.Target.EntityId, Target.MassEntity, Avatar, Expected);
}
