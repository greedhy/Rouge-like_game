// 调用链：图注册表 -> Scatter方向分布 -> Launch载荷；本文件不生成Actor或调用伤害。
#include "AbilitySystem/Unified/Nodes/AbilityDistributionNodes.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Unified/Nodes/AbilityNativeProjectileParameterNodes.h"

namespace
{
    /** 准备帧与显式Scatter共用相同角序；已编码的Notify俯仰作为方向原样保留。 */
    TArray<FVector> ScatterDirections(FVector Direction, int32 Count, float Spread)
    {
        TArray<FVector> Result;
        if (Count < 1 || Count > 64 || !FMath::IsFinite(Spread) || Spread < 0.f || Spread > 360.f)
        {
            return Result;
        }
        const auto Rotations =
            UMyAbilitySystemLibrary::EvenlySpacedRotators(Direction, FVector::UpVector, Spread, Count);
        for (const FRotator& Rotation : Rotations)
        {
            Result.Add(Rotation.Vector());
        }
        return Result;
    }
} // namespace

bool UAbilityScatterNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (bUseNativeLaunchParameters)
    {
        if ((bOverridePreparedCount && (Count < 1 || Count > 64)) ||
            (bOverridePreparedSpread && (!FMath::IsFinite(Spread) || Spread < 0.f || Spread > 360.f)))
        {
            Error = TEXT("Selected prepared launch overrides require bounded final count/spread.");
        }
        return Error.IsEmpty(); // 未选中的显式值不覆盖原生准备帧。
    }
    if (Count < 1 || Count > 64 || !FMath::IsFinite(Spread) || Spread < 0.f || Spread > 360.f ||
        !FMath::IsFinite(PitchOverride) || PitchOverride < -90.f || PitchOverride > 90.f)
    {
        Error = TEXT("Scatter requires 1-64 directions, spread in [0,360] and pitch in [-90,90].");
    }
    return Error.IsEmpty();
}
bool UAbilityScatterNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Scatter configuration needs a finite typed value.");
        return false;
    }
    if (Parameter == EAbilityConfigParameter::Count && Value >= 1.f && Value <= 64.f &&
        Value == FMath::RoundToFloat(Value))
    {
        Count = static_cast<int32>(Value);
        bOverridePreparedCount = bUseNativeLaunchParameters;
        return Validate(Error);
    }
    if (Parameter == EAbilityConfigParameter::Spread && Value >= 0.f && Value <= 360.f)
    {
        Spread = Value;
        bOverridePreparedSpread = bUseNativeLaunchParameters;
        return Validate(Error);
    }
    Error = TEXT("Scatter only consumes bounded integer Count and Spread.");
    return false;
}
bool UAbilityScatterNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                         const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                         FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityScatterNodeConfig>(&Config);
    if (!Settings->Validate(Error) || Input.Direction.ContainsNaN() || Input.Direction.IsNearlyZero() ||
        !FMath::IsFinite(Input.Level))
    {
        Error = TEXT("Scatter requires validated settings and a finite nonzero input direction/level.");
        return false;
    }
    TArray<FVector> Directions;
    if (Settings->bUseNativeLaunchParameters)
    {
        const auto* Frame = Cast<UAbilityNativeProjectileParameterFrame>(Input.ParameterFrame.Get());
        if (!Frame || !Frame->BelongsTo(Owner.GetExecutionId(), Input.SourceToken))
        {
            Error = TEXT("Native scatter requires its original root's prepared launch frame.");
            return false;
        }
        // min(level)与装备Extra已经准备一次；不能再次截断装备追加的弹数或覆写Notify实际俯仰。
        Directions =
            ScatterDirections(Input.Direction, Settings->bOverridePreparedCount ? Settings->Count : Frame->GetCount(),
                              Settings->bOverridePreparedSpread ? Settings->Spread : Frame->GetSpread());
    }
    else
    {
        Directions = AbilityDistributionNodes::BuildDirections(Input.Direction, Input.Level, *Settings);
    }
    if (Settings->bUseNativeLaunchParameters && Directions.IsEmpty())
    {
        Error = TEXT("Scatter prepared no bounded launch directions.");
        return false;
    }
    for (const FVector& Direction : Directions)
    {
        if (bStopped || Owner.IsFinished())
        {
            return true;
        }
        auto Payload = Input;
        Payload.Direction = Direction;
        // 图事件预算在Publish预留后继；实际物理生成额度仍由Delivery逐次预留。
        if (!Owner.Publish(Id, TEXT("Launch"), Payload))
        {
            Error = TEXT("Root graph rejected a scatter launch.");
            return false;
        }
    }
    if (!bStopped && !Owner.IsFinished())
    {
        return Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}
TArray<FVector> AbilityDistributionNodes::BuildDirections(FVector Direction, float Level,
                                                          const UAbilityScatterNodeConfig& Config)
{
    FString Error;
    TArray<FVector> Result;
    if (!Config.Validate(Error) || Direction.ContainsNaN() || Direction.IsNearlyZero() || !FMath::IsFinite(Level))
    {
        return Result;
    }
    const int32 Count = Config.bLimitCountToLevel
                            ? FMath::Min(Config.Count, FMath::FloorToInt(FMath::Clamp(Level, 0.f, 64.f)))
                            : Config.Count;
    if (Count <= 0)
    {
        return Result;
    }
    FRotator Rotation = Direction.Rotation();
    if (Config.bOverridePitch)
    {
        Rotation.Pitch = Config.PitchOverride;
    }
    // 与原FireBolt共用同一角序，保留从负半角到正半角以及单弹朝前的合同。
    return ScatterDirections(Rotation.Vector(), Count, Config.Spread);
}
void AbilityDistributionNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Distribution.Scatter")))
    {
        return;
    }
    FAbilityNodeDescriptor D;
    D.TypeId = TEXT("Distribution.Scatter");
    D.ConfigClass = UAbilityScatterNodeConfig::StaticClass();
    D.ExecutionClass = UAbilityScatterNodeExecution::StaticClass();
    D.InputPayload = EAbilityPortPayload::Origin;
    D.bSupportsActorTargets = D.bSupportsMassTargets = D.bSupportsSnapshotTargets = true;
    D.Ports = {{TEXT("Launch"), EAbilityPortPayload::Origin, false},
               {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(D, Error), TEXT("Scatter registration: %s"), *Error);
}
