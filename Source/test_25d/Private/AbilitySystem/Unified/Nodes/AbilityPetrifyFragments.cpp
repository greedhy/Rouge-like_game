// 调用链：旧Area宏/新Query节点 -> Gather冻结原生命 -> 独立批状态动作 -> 原AbilityStatusService。
// 只有服务器执行；组件/实体的状态复制、时钟和池生命收尾沿用原实现。
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityTargetSet.h"
#include "AbilitySystem/Unified/AbilityTargetSelectionService.h"
#include "AbilitySystem/Unified/AbilityStatusService.h"
#include "Algo/StableSort.h"
#include "Character/EnemyCharacter.h"
#include "Engine/World.h"

bool UAbilityPetrifyAreaQueryConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Radius) || Radius < 0.f)
    {
        Error = TEXT("Petrify query requires a finite nonnegative original radius.");
    }
    else if (FocusedTargetCount < 0 || FocusedTargetCount > 16)
    {
        Error = TEXT("Focused petrify accepts between zero and sixteen targets.");
    }
    return Error.IsEmpty();
}

bool UAbilityPetrifyAreaQueryConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Radius || !FMath::IsFinite(Multiplier) || Multiplier <= 0.f ||
        !FMath::IsFinite(Radius * Multiplier))
    {
        return false;
    }
    Radius *= Multiplier;
    return true;
}

bool UAbilityPetrifyAreaQueryConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                           FString& Error)
{
    if (Parameter != EAbilityConfigParameter::Count)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!FMath::IsFinite(Value) || Value < 1.f || Value > 16.f || FMath::RoundToFloat(Value) != Value)
    {
        Error = TEXT("Focused petrify requires a whole target count from one to sixteen.");
        return false;
    }
    FocusedTargetCount = static_cast<int32>(Value);
    return Validate(Error);
}

bool UAbilityPetrifyAreaQueryConfig::SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                                                FString& Error) const
{
    if (RegisteredType != TEXT("Query.PetrifyArea") || Incoming == EAbilityPortPayload::PreparedInventory ||
        !Validate(Error) || Radius > 2000.f)
    {
        Error = TEXT("Repeated petrify query requires its registered position-compatible input and radius <= 2000 cm.");
        return false;
    }
    // Origin只消费位置；已确认接触、伤害及目标集合保留的位置也可降级，不能丢弃库存归还责任。
    return Incoming == EAbilityPortPayload::Origin || Incoming == EAbilityPortPayload::LiveTarget ||
           Incoming == EAbilityPortPayload::ConfirmedImpact || Incoming == EAbilityPortPayload::DamageResult ||
           Incoming == EAbilityPortPayload::TargetSet;
}

bool UAbilityPetrifyBatchConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Duration) || Duration <= 0.f)
    {
        Error = TEXT("Petrify batch requires a finite positive duration.");
    }
    return Error.IsEmpty();
}

bool UAbilityPetrifyBatchConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Duration || !FMath::IsFinite(Multiplier) || Multiplier <= 0.f ||
        !FMath::IsFinite(Duration * Multiplier) || Duration * Multiplier <= 0.f)
    {
        return false;
    }
    Duration *= Multiplier;
    return true;
}

bool UAbilityPetrifyBatchConfig::SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                                            FString& Error) const
{
    if (RegisteredType != TEXT("Status.PetrifyBatch") || Incoming != EAbilityPortPayload::TargetSet ||
        !Validate(Error) || Duration > 60.f)
    {
        Error = TEXT("Repeated petrify batch requires the registered complete TargetSet and duration <= 60 seconds.");
        return false;
    }
    return true;
}

bool AbilityPetrifyFragments::Gather(const FAbilitySourceToken& Source, FVector Origin, float Radius,
                                     TArray<FAbilityTargetToken>& OutTargets, FString& Error)
{
    OutTargets.Reset();
    Error.Reset();
    AActor* Caster = nullptr;
    if (!FMath::IsFinite(Radius) || Radius < 0.f || Origin.ContainsNaN() ||
        !AbilityExecutionIdentity::ResolveSource(Source, Caster))
    {
        Error = TEXT("Petrify query source or geometry is invalid.");
        return false;
    }
    for (const auto& Candidate : PureMassCombat::Gather(Source.World.Get(), Caster, Origin, Radius))
    {
        if ((Candidate.IsActor() && !Cast<AEnemyCharacter>(Candidate.Actor.Get())) ||
            !PureMassCombat::Visible(Source.World.Get(), Caster, Origin, Candidate))
        {
            continue;
        }
        FAbilityTargetToken Life;
        FString CaptureError;
        if (AbilityExecutionIdentity::CaptureTarget(Source, Candidate, Life, CaptureError))
        {
            OutTargets.Add(Life);
        }
    }
    return true;
}

bool AbilityPetrifyFragments::Apply(const FAbilitySourceToken& Source, const TArray<FAbilityTargetToken>& Targets,
                                    float Duration, UMaterialInterface* Material, TFunctionRef<bool()> Continue)
{
    for (const auto& Life : Targets)
    {
        AActor* Caster = nullptr;
        if (!Continue() || !AbilityExecutionIdentity::ResolveSource(Source, Caster))
        {
            return false;
        }
        FVector Position;
        if (AbilityExecutionIdentity::ResolveTarget(Source, Life, Position))
        {
            // 状态施加可能同步取消GA或改变后续目标生命；不跨该调用缓存任何容器元素/活目标指针。
            AbilityStatusService::ApplyPetrify(Caster, Life.Target, Duration, Material);
        }
    }
    return Continue();
}

bool UAbilityPetrifyAreaQueryExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Configuration, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityPetrifyAreaQueryConfig>(&Configuration);
    auto Targets = MakeShared<FAbilityTargetSetSnapshot>();
    Targets->Source = Input.SourceToken;
    if (!AbilityPetrifyFragments::Gather(Input.SourceToken, Input.Origin, Settings->Radius, Targets->Targets, Error))
    {
        return false;
    }
    if (Targets->Targets.Num() > 65536)
    {
        Error = TEXT("Complete petrify selection exceeds the typed target-set capacity; no prefix was applied.");
        return false;
    }
    if (Settings->FocusedTargetCount > 0)
    {
        // 先从原合法目标读取一次血量再排序。并列维持原查询顺序，应用时仍复核原生命。
        struct FRankedTarget
        {
            FAbilityTargetToken Life;
            float HealthRatio;
        };
        TArray<FRankedTarget> Ranked;
        Ranked.Reserve(Targets->Targets.Num());
        for (auto& Life : Targets->Targets)
        {
            const float HealthRatio =
                AbilityTargetSelectionService::ReadHealthRatio(Life).Get(TNumericLimits<float>::Max());
            Ranked.Add({MoveTemp(Life), HealthRatio});
        }
        Algo::StableSortBy(Ranked, &FRankedTarget::HealthRatio);
        const int32 Count = FMath::Min(Settings->FocusedTargetCount, Ranked.Num());
        Targets->Targets.Reset(Count);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Targets->Targets.Add(MoveTemp(Ranked[Index].Life));
        }
    }
    auto Payload = Input;
    Payload.TargetSet = MoveTemp(Targets);
    Payload.ShapeRadius = Settings->Radius;
    Payload.LiveTargetToken.Reset();
    return Owner.Publish(Id, TEXT("Completed"), Payload);
}

bool UAbilityPetrifyBatchExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                          const UAbilityNodeConfig& Configuration, const FAbilityNodeInput& InInput,
                                          FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error) || !Input.TargetSet)
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityPetrifyBatchConfig>(&Configuration);
    const auto Targets = Input.TargetSet;
    const bool bFinishedBatch =
        AbilityPetrifyFragments::Apply(Input.SourceToken, Targets->Targets, Settings->Duration, Settings->StoneMaterial,
                                       [&]()
                                       {
                                           return !bStopped && !Owner.IsFinished();
                                       });
    return bFinishedBatch ? Owner.Publish(Id, TEXT("Completed"), Input) : true;
}

UAbilityGraphDefinition* AbilityPetrifyFragments::Build(UObject* Outer, float Radius, float Duration,
                                                        UMaterialInterface* Material)
{
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    const FGuid Query = FGuid::NewGuid();
    const FGuid Status = FGuid::NewGuid();
    auto* Selection = NewObject<UAbilityPetrifyAreaQueryConfig>(Graph);
    Selection->Radius = Radius;
    auto* Payload = NewObject<UAbilityPetrifyBatchConfig>(Graph);
    Payload->Duration = Duration;
    Payload->StoneMaterial = Material;
    Graph->EntryNodeId = Query;
    Graph->Nodes = {{Query, TEXT("Query.PetrifyArea"), Selection}, {Status, TEXT("Status.PetrifyBatch"), Payload}};
    Graph->Edges = {{Query, Status, TEXT("Completed")}};
    return Graph;
}

void AbilityPetrifyFragments::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Query;
    Query.TypeId = TEXT("Query.PetrifyArea");
    Query.ConfigClass = UAbilityPetrifyAreaQueryConfig::StaticClass();
    Query.ExecutionClass = UAbilityPetrifyAreaQueryExecution::StaticClass();
    Query.InputPayload = EAbilityPortPayload::Origin;
    Query.bSupportsActorTargets = Query.bSupportsMassTargets = Query.bSupportsSnapshotTargets = true;
    Query.bSustainedSafe = Query.bPrototypeCompatibilitySafe = true;
    Query.bRuntimeFiniteSafe = true;
    Query.bRuntimePurePayload = true;
    Query.Ports = {{TEXT("Completed"), EAbilityPortPayload::TargetSet, true}};
    FAbilityNodeDescriptor Status = Query;
    Status.TypeId = TEXT("Status.PetrifyBatch");
    Status.ConfigClass = UAbilityPetrifyBatchConfig::StaticClass();
    Status.ExecutionClass = UAbilityPetrifyBatchExecution::StaticClass();
    Status.InputPayload = EAbilityPortPayload::TargetSet;
    Status.bRuntimePurePayload = true;
    Status.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    Status.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Query, Status})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Petrify fragment registration: %s"), *Error);
        }
    }
}
