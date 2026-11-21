// 调用链：原型容器 Scope -> 可组合方向分布 -> 新单弹 Scope；此处没有完整 GA/Driver。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeDistributionNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "UObject/StrongObjectPtr.h"

bool UAbilityPrototypeMultishotNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                                 FString& Error)
{
    if (Parameter != EAbilityConfigParameter::PelletTargetPolicy || !FMath::IsFinite(Value) || Value < 0.f ||
        Value > 1.f || Value != FMath::RoundToFloat(Value))
    {
        Error = TEXT("Pellet target routing accepts only None(0) or OriginalSelectedLife(1).");
        return false;
    }
    PelletTargetPolicy = static_cast<EAbilityPelletTargetPolicy>(static_cast<uint8>(Value));
    Error.Reset();
    return true;
}

bool UAbilityPrototypeMultishotNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (PelletTargetPolicy < EAbilityPelletTargetPolicy::None ||
        PelletTargetPolicy > EAbilityPelletTargetPolicy::OriginalSelectedLife)
    {
        Error = TEXT("Prototype pellet target policy is not registered.");
    }
    return Error.IsEmpty();
}

bool UAbilityPrototypeMultishotNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                    const UAbilityNodeConfig& Configuration,
                                                    const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    const auto* Settings = Cast<UAbilityPrototypeMultishotNodeConfig>(&Configuration);
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    if (!Settings || !Settings->Validate(Error) || !Frame ||
        Frame->GetParameters().Kind != EAbilityPrototypeKind::Multishot || Frame->IsSingleProjectile() ||
        (Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1") &&
         Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1")))
    {
        Error = TEXT("Multishot distribution requires this root's non-single Actor/Mixed container frame");
        return false;
    }
    const TStrongObjectPtr<UAbilityPrototypeMultishotNodeExecution> SelfGuard(this);
    const int32 Count = Frame->GetParameters().Count;
    const float Spread = Frame->GetParameters().SpreadDegrees;
    const FVector Forward = Input.Direction.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
    FAbilityNodeInput Pellet = Input;
    // 默认仍不继承原容器目标。显式策略只转交原token，没有目标也不暗中选敌；Flight随后复核原生命。
    // 每个单弹 Scope 仍建立自己的同深度参数帧，任何策略都不能继承父级命中/集合权限。
    Pellet.LiveTargetToken.Reset();
    if (Settings->PelletTargetPolicy == EAbilityPelletTargetPolicy::OriginalSelectedLife)
    {
        Pellet.LiveTargetToken = Input.LiveTargetToken;
    }
    Pellet.TargetSet.Reset();
    Pellet.Impact.Reset();
    Pellet.EventCenter = FVector::ZeroVector;
    Pellet.bConfirmedHit = Pellet.bHasDamageResult = Pellet.bDamageAttempted = Pellet.bKilled = false;
    Pellet.DamageApplied.Reset();
    Pellet.ActualDamage.Reset();
    for (int32 Index = 0; Index < Count && !bStopped && !Owner.IsFinished(); ++Index)
    {
        const float Angle = Count == 1 ? 0.f : -Spread * .5f + Spread * Index / (Count - 1);
        Pellet.Direction = Forward.RotateAngleAxis(Angle, FVector::UpVector);
        // 这里只请求图后继；真实 logical 准入在每个 single Scope，物理租约在每个实际 Flight。
        // Scope达到64后拒绝后续pellet，不把一次helper请求成功谎报成一次实际弹体生成。
        Owner.RequestChild(Id, TEXT("Pellet"), Pellet);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        return Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}

void AbilityPrototypeDistributionNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Distribution.PrototypeMultishot");
    Descriptor.ConfigClass = UAbilityPrototypeMultishotNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityPrototypeMultishotNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bPrototypeCompatibilitySafe = true;
    Descriptor.Ports = {{TEXT("Pellet"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Descriptor.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype distribution registration: %s"), *Error);
    }
}
