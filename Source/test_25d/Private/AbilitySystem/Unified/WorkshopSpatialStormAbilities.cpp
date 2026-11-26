// 调用链：统一服务器施法 -> Query.SelectTargets/Control.StaggeredPoints -> Query.Shape -> 共享伤害服务。
// 图实例关闭后停止发射与引信。位置参数不持有目标生命，查询完成后 ASC 仅同步范围表现。
#include "AbilitySystem/Unified/WorkshopSpatialStormAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/WorkshopEmberAbilities.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ThunderMarkBurst, "Abilities.Mage.ThunderMarkBurst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ThunderMarkBurstCooldown, "Cooldown.Mage.ThunderMarkBurst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostShower, "Abilities.Mage.FrostShower");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostShowerCooldown, "Cooldown.Mage.FrostShower");

namespace
{
    constexpr int32 MaxPoints = 8;
    constexpr float MinRadius = .01f;
    constexpr float MaxRadius = 1000.f;
    constexpr float MinPeriod = .1f;
    constexpr float MaxPeriod = 2.f;
    constexpr double PointAngleDegrees = 137.507764;

    /** 用同一范围判断配置校验和运行时配方参数，拒绝 NaN 与无穷。 */
    bool IsFiniteInRange(float Value, float Min, float Max)
    {
        return FMath::IsFinite(Value) && Value >= Min && Value <= Max;
    }
} // namespace

bool UAbilityStaggeredPointsNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (Count < 1 || Count > MaxPoints || !IsFiniteInRange(Radius, MinRadius, MaxRadius) ||
        !IsFiniteInRange(Period, MinPeriod, MaxPeriod))
    {
        Error = TEXT("Staggered points need 1 to 8 points, 0.01 to 1000 cm radius, and a 0.1 to 2 second period.");
    }
    return Error.IsEmpty();
}

bool UAbilityStaggeredPointsNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Radius)
    {
        return Super::TryScaleRecipeParameter(Parameter, Multiplier);
    }
    FString Error;
    return TrySetRecipeParameter(EAbilityConfigParameter::Radius, Radius * Multiplier, Error);
}

bool UAbilityStaggeredPointsNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                              FString& Error)
{
    Error.Reset();
    switch (Parameter)
    {
    case EAbilityConfigParameter::Count:
        if (IsFiniteInRange(Value, 1.f, static_cast<float>(MaxPoints)) && Value == FMath::TruncToFloat(Value))
        {
            Count = static_cast<int32>(Value);
            return true;
        }
        Error = TEXT("Staggered point count must be an integer from 1 to 8.");
        return false;
    case EAbilityConfigParameter::Radius:
        if (IsFiniteInRange(Value, MinRadius, MaxRadius))
        {
            Radius = Value;
            return true;
        }
        Error = TEXT("Staggered point radius must be from 0.01 to 1000 cm.");
        return false;
    case EAbilityConfigParameter::Period:
        if (IsFiniteInRange(Value, MinPeriod, MaxPeriod))
        {
            Period = Value;
            return true;
        }
        Error = TEXT("Staggered point period must be from 0.1 to 2 seconds.");
        return false;
    default:
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
}

bool UAbilityStaggeredPointsExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Config.Validate(Error))
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityStaggeredPointsNodeConfig>(&Config);
    Count = Settings->Count;
    Radius = Settings->Radius;
    Period = Settings->Period;
    return EmitPoint(Owner);
}

void UAbilityStaggeredPointsExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return;
    }
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f)
    {
        Owner->Cancel(EAbilityGraphEndReason::Failed);
        return;
    }

    // 对齐执行开始后的绝对时间，帧长变化不会挪动之后的发射时刻。点数上限同时限制单帧补发。
    Elapsed += DeltaSeconds;
    while (Emitted < Count && Elapsed + KINDA_SMALL_NUMBER >= static_cast<double>(Emitted) * Period)
    {
        if (!EmitPoint(*Owner))
        {
            return;
        }
    }
}

bool UAbilityStaggeredPointsExecution::EmitPoint(UAbilityGraphInstance& Owner)
{
    // 每次从原始位置计算，不继承上一点的偏移或命中。角度固定，外围点按半径均方逐步展开。
    const int32 Index = Emitted++;
    FAbilityNodeInput Point = Input;
    if (Index > 0)
    {
        const double Distance = Radius * FMath::Sqrt(static_cast<double>(Index) / (Count - 1));
        Point.Origin +=
            FVector::ForwardVector.RotateAngleAxis((Index - 1) * PointAngleDegrees, FVector::UpVector) * Distance;
    }
    Point.EventCenter = Point.Origin;
    Point.LiveTargetToken.Reset();
    Point.Impact.Reset();
    Point.ShapeRadius.Reset();
    if (!Owner.Publish(InvocationId, TEXT("Point"), Point))
    {
        return false;
    }
    if (!bStopped && !Owner.IsFinished() && Emitted == Count)
    {
        return Owner.Publish(InvocationId, TEXT("Completed"), Input);
    }
    return !bStopped && !Owner.IsFinished();
}

UWorkshopThunderMarkBurst::UWorkshopThunderMarkBurst()
{
    SetAssetTags(FGameplayTagContainer(TAG_ThunderMarkBurst));
    CooldownTags.AddTag(TAG_ThunderMarkBurstCooldown);
    ManaCost = 21.f;
    Cooldown = 8.f;
    SpellName = NSLOCTEXT("SkillCards", "ThunderMarkBurst", "雷印迟爆");
    SpellSummary =
        NSLOCTEXT("SkillCards", "ThunderMarkBurstSummary", "锁定附近最多两名带电敌人的位置，延迟后爆发雷击。");
    ConfigureArea(200.f, 24.f, EAbilityElement::Thunder,
                  TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Lightning.NS_Free_Spells_Buff_Lightning"));
    const FGuid BurstId = Graph->EntryNodeId;
    CastChecked<UAbilityQueryNodeConfig>(Graph->Nodes[0].Config)->Query.MaxTargets = 5;

    const auto& Initializer = FObjectInitializer::Get();
    auto* Candidates = Initializer.CreateDefaultSubobject<UAbilitySelectTargetsConfig>(Graph, TEXT("ThunderCarriers"));
    Candidates->Radius = 450.f;
    Candidates->MaxTargets = 2;
    Candidates->TargetSelection = EAbilityTargetSelectionPolicy::Nearest;
    Candidates->TargetFilter = 3 + static_cast<uint8>(EAbilityExposure::Shocked);
    auto* Anchor = Initializer.CreateDefaultSubobject<UAbilityTargetOriginConfig>(Graph, TEXT("ThunderLocation"));
    auto* Fuse = Initializer.CreateDefaultSubobject<UAbilityDelayNodeConfig>(Graph, TEXT("ThunderFuse"));
    Fuse->DelaySeconds = .45f;

    // 先把原生命解析为位置再等待。期间移动或死亡不会改变后续查询的中心。
    const FGuid CandidatesId(0, 0, 0, 4);
    const FGuid AnchorId(0, 0, 0, 5);
    const FGuid FuseId(0, 0, 0, 6);
    Graph->EntryNodeId = CandidatesId;
    Graph->Nodes.Append({{CandidatesId, TEXT("Query.SelectTargets"), Candidates},
                         {AnchorId, TEXT("Spatial.TargetOrigin"), Anchor},
                         {FuseId, TEXT("Control.DelayOrigin"), Fuse}});
    Graph->Edges.Append({{CandidatesId, AnchorId, TEXT("TargetFound")},
                         {AnchorId, FuseId, TEXT("Located")},
                         {FuseId, BurstId, TEXT("Completed")}});
}

UWorkshopFrostShower::UWorkshopFrostShower()
{
    SetAssetTags(FGameplayTagContainer(TAG_FrostShower));
    CooldownTags.AddTag(TAG_FrostShowerCooldown);
    ManaCost = 22.f;
    Cooldown = 8.f;
    SpellName = NSLOCTEXT("SkillCards", "FrostShower", "霜晶阵雨");
    SpellSummary = NSLOCTEXT("SkillCards", "FrostShowerSummary", "在落点附近依次释放数枚霜晶，各自造成小范围冰伤害。");
    ConfigureArea(140.f, 17.f, EAbilityElement::Ice,
                  TEXT("/Game/Art/CompositionEffects/NS_Frost_Impact.NS_Frost_Impact"));
    CastChecked<UAbilityQueryNodeConfig>(Graph->Nodes[0].Config)->Query.MaxTargets = 4;

    // 控制节点独立产生位置，覆盖范围、冰伤害和网络表现分别交给下面的真实节点。
    auto* Points = FObjectInitializer::Get().CreateDefaultSubobject<UAbilityStaggeredPointsNodeConfig>(
        Graph, TEXT("FrostPointSequence"));
    PrependTiming(TEXT("Control.StaggeredPoints"), Points, TEXT("Point"));
}

void WorkshopSpatialStormAbilities::RegisterNodes(FAbilityNodeRegistry& Registry)
{
    const FName Type(TEXT("Control.StaggeredPoints"));
    if (Registry.Find(Type))
    {
        return;
    }
    FAbilityNodeDescriptor Node;
    Node.TypeId = Type;
    Node.ConfigClass = UAbilityStaggeredPointsNodeConfig::StaticClass();
    Node.ExecutionClass = UAbilityStaggeredPointsExecution::StaticClass();
    Node.InputPayload = EAbilityPortPayload::Origin;
    Node.bIsAsync = true;
    Node.bRuntimeFiniteSafe = true;
    Node.bSupportsActorTargets = Node.bSupportsMassTargets = Node.bSupportsSnapshotTargets = true;
    Node.Ports = {{TEXT("Point"), EAbilityPortPayload::Origin, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Node, Error), TEXT("Staggered points registration: %s"), *Error);
}
