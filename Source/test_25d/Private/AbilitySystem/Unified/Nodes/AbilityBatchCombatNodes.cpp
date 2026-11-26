// 调用链：GraphInstance -> 批查询/批效果 -> 原目标证明与共享伤害服务 -> 同根后继队列。
#include "AbilitySystem/Unified/Nodes/AbilityBatchCombatNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/Unified/AbilitySelectedTargetService.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"

namespace AbilityBatchDetails
{
    /** 完成表示这个范围动作已结束，不把整批目标或某个初始目标继续当作活控制权限。 */
    FAbilityNodeInput PositionOnly(const FAbilityNodeInput& InInput)
    {
        FAbilityNodeInput Result = InInput;
        Result.LiveTargetToken.Reset();
        Result.TargetSet.Reset();
        return Result;
    }
} // namespace AbilityBatchDetails

bool UAbilityQueryBatchNodeConfig::Validate(FString& Error) const
{
    if (LifePolicy < EAbilityQueryLifePolicy::FreshQuery || LifePolicy > EAbilityQueryLifePolicy::OriginalRootLife)
    {
        Error = TEXT("Batch query requires a registered target-life policy.");
        return false;
    }
    return Query.ValidateForBatch(Error);
}

bool UAbilityDamageBatchNodeConfig::Validate(FString& Error) const
{
    if (!Super::Validate(Error))
    {
        return false;
    }
    if (Profile < EAbilityDamageActionProfile::Unified || Profile > EAbilityDamageActionProfile::LegacyMixed)
    {
        Error = TEXT("Batch damage requires a registered damage-result profile.");
        return false;
    }
    return true;
}

bool UAbilityQueryBatchNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                            const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                            FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    FAbilityQueryRequest Request;
    EAbilityQueryLifePolicy LifePolicy;
    if (Config.IsA<UAbilityPrototypeQueryNodeConfig>())
    {
        const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
        if (!Frame)
        {
            return false;
        }
        const auto& Parameters = Frame->GetParameters();
        Request.Radius = Parameters.Radius;
        Request.InnerRadius = Parameters.InnerRadius;
        Request.Range = Parameters.Range;
        Request.Width = Parameters.Width;
        Request.HalfHeight = Parameters.ShapeHalfHeight;
        Request.HalfAngleDegrees = Parameters.SpreadDegrees * .5f;
        Request.MaxTargets = 65536;
        Request.bIncludeMass = Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1");
        Request.Profile =
            Request.bIncludeMass ? EAbilityShapeQueryProfile::LegacyMixed : EAbilityShapeQueryProfile::LegacyActor;
        switch (Parameters.Kind)
        {
        case EAbilityPrototypeKind::Cone:
            Request.Geometry = EAbilityQueryGeometry::Cone;
            break;
        case EAbilityPrototypeKind::Ring:
            Request.Geometry = EAbilityQueryGeometry::Ring;
            break;
        case EAbilityPrototypeKind::Line:
            Request.Geometry = EAbilityQueryGeometry::Line;
            break;
        default:
            Request.Geometry = EAbilityQueryGeometry::Sphere;
            break;
        }
        LifePolicy = EAbilityQueryLifePolicy::OriginalRootLife;
    }
    else
    {
        const auto* Settings = Cast<UAbilityQueryBatchNodeConfig>(&Config);
        if (!Settings || !Settings->Validate(Error))
        {
            return false;
        }
        Request = Settings->Query;
        LifePolicy = Settings->LifePolicy;
    }
    Request.Origin = Input.Origin;
    Request.Direction = Input.Direction;
    auto Targets = MakeShared<FAbilityTargetSetSnapshot>();
    Targets->Source = Input.SourceToken;
    auto Resolve = [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Token, FVector& Location,
                       FString& CaptureError)
    {
        if (LifePolicy == EAbilityQueryLifePolicy::OriginalRootLife)
        {
            return Owner.ResolveOrCaptureOriginalTarget(Candidate, Token, Location, CaptureError);
        }
        return AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Candidate, Token, CaptureError) &&
               AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Token, Location);
    };
    if (!AbilityTargetQueryService::GatherBatch(Input.SourceToken, Request, Resolve, Targets->Targets, Error))
    {
        return false;
    }
    FAbilityNodeInput Payload = Input;
    Payload.LiveTargetToken.Reset();
    Payload.TargetSet = Targets;
    Payload.EventCenter = Payload.Origin;
    // 发布后仅以 const shared snapshot 传递；空集合也保留一次正常的后继调用。
    return Owner.Publish(Id, TEXT("Completed"), Payload);
}

bool UAbilityDamageBatchNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const UAbilityPrototypeParameterFrame* Frame = nullptr;
    auto Params = Input.DamageParams;
    if (Config.IsA<UAbilityPrototypeDamageNodeConfig>())
    {
        Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
        if (!Frame)
        {
            return false;
        }
        Params.BaseDamage = Frame->GetDamage();
        Params.DamageType = Frame->GetParameters().DamageType;
        if (CastChecked<UAbilityPrototypeDamageNodeConfig>(&Config)->bConsumeInputScales)
        {
            if (!FMath::IsFinite(Input.DamageScale) || Input.DamageScale < 0.f ||
                !FMath::IsFinite(Input.DebuffDamageScale) || Input.DebuffDamageScale < 0.f ||
                !FMath::IsFinite(Params.BaseDamage * Input.DamageScale) ||
                !FMath::IsFinite(Params.DebuffDamage * Input.DebuffDamageScale))
            {
                Error = TEXT("Frame payload requires finite nonnegative cumulative damage scales.");
                return false;
            }
            // 只消费该片段输入的传播倍率，不重新计算装备/等级，也不把已缩放副本传给后继。
            Params.BaseDamage *= Input.DamageScale;
            Params.DebuffDamage *= Input.DebuffDamageScale;
        }
        ActionProfile = Frame->GetProfile() == TEXT("PrototypeFine.Actor.v1")
                            ? EAbilityDamageActionProfile::LegacyActor
                            : EAbilityDamageActionProfile::LegacyMixed;
        bExtensionPayload = Frame->GetProfile() == TEXT("PrototypeFine.Extension.v1");
        if (bExtensionPayload)
        {
            // 只修改本次动作副本；后继跨族片段仍持有完整根模板，不能被该 profile 污染。
            Params.DebuffChance = 0.f;
        }
    }
    else
    {
        const auto* Settings = Cast<UAbilityDamageBatchNodeConfig>(&Config);
        if (!Settings || !Settings->Validate(Error))
        {
            return false;
        }
        Params.BaseDamage = Settings->Damage + Settings->DamagePerLevel * FMath::Clamp(Input.Level - 1.f, 0.f, 99.f);
        Params.DamageType = Settings->ResolveDamageType();
        ActionProfile = Settings->Profile;
    }
    if (!Input.TargetSet.IsValid())
    {
        Error = TEXT("Batch damage requires valid settings and a typed target-set snapshot.");
        return false;
    }
    // GraphInstance 已验证整批来源；此处逐个复核原生命，单个退休 lease 不否决其它成员。
    Targets = Input.TargetSet;
    bHasHook = Owner.HasSuccessor(Id, TEXT("DamageResolved"));
    bContactSequence = Frame && Owner.HasSuccessor(Id, TEXT("Contact"));
    if (ActionProfile != EAbilityDamageActionProfile::Unified)
    {
        // 原范围由显式查询决定；兼容动作不再让 GE 使用第二套径向衰减。
        Params.bIsRadialDamage = false;
    }
    PreparedParams = Params;
    TargetIndex = 0;
    Results.Reset();
    ContinueBatch();
    return true;
}

void UAbilityDamageBatchNodeExecution::ContinueBatch()
{
    const TStrongObjectPtr<UAbilityDamageBatchNodeExecution> SelfGuard(this);
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Targets.IsValid())
    {
        return;
    }
    while (TargetIndex < Targets->Targets.Num())
    {
        const auto Target = Targets->Targets[TargetIndex++];
        if (bStopped || Owner->IsFinished())
        {
            return;
        }
        AActor* Source = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source))
        {
            Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
            return;
        }
        FVector Location;
        if (!AbilitySelectedTargetService::ResolveSelectedTarget(*Owner, Input.SourceToken, *Targets, Target, Location))
        {
            continue;
        }
        // 固定 Config 模式只计算自身等级伤害，不消费传播的 Input DamageScale，也不改共享参数。
        FAbilityDamageResult Result;
        if (bExtensionPayload && Target.Target.IsActor() && PreparedParams.BaseDamage <= 0.f)
        {
            if (!PreparedParams.SourceAbilitySystemComponent || !PreparedParams.DamageGameplayEffectClass ||
                !UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Target.Actor.Get()))
            {
                continue;
            }
            // 原 Extension 的零伤害 Actor 接触不调用 GE，仍保留实际接触位置/OnHit事实。
            Result.ImpactLocation = Location;
        }
        else
        {
            Result = AbilityDamageService::Apply(Input.SourceToken, Target, PreparedParams, 1.f,
                                                 ActionProfile == EAbilityDamageActionProfile::LegacyActor);
        }
        if (bStopped || Owner->IsFinished())
        {
            return;
        }
        if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source))
        {
            Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
            return;
        }
        if ((!bExtensionPayload && ActionProfile == EAbilityDamageActionProfile::LegacyMixed && !Result.bAttempted) ||
            (ActionProfile == EAbilityDamageActionProfile::LegacyActor && Target.Target.IsActor() &&
             !IsValid(Target.Target.Actor.Get())))
        {
            continue;
        }
        const FAbilityBatchHitRecord Hit{Target, Result};
        if (bHasHook)
        {
            Results.Add(Hit);
        }
        if (bContactSequence)
        {
            auto Payload = HitPayload(Hit);
            Payload.Impact->Sequence = Owner->GetAcceptedEventCount() + 1;
            const TWeakObjectPtr<UAbilityDamageBatchNodeExecution> WeakThis(this);
            if (!Owner->PublishWithContinuation(InvocationId, TEXT("Contact"), Payload,
                                                [WeakThis]()
                                                {
                                                    if (auto* Execution = WeakThis.Get())
                                                    {
                                                        Execution->ContinueBatch();
                                                    }
                                                }) &&
                !Owner->IsFinished())
            {
                Owner->Cancel(EAbilityGraphEndReason::Failed);
            }
            return;
        }
    }
    FinishBatch(*Owner);
}

FAbilityNodeInput UAbilityDamageBatchNodeExecution::HitPayload(const FAbilityBatchHitRecord& Hit) const
{
    FAbilityNodeInput Payload = AbilityBatchDetails::PositionOnly(Input);
    Payload.Origin = Payload.EventCenter = Hit.Result.ImpactLocation;
    if (bExtensionPayload)
    {
        // Extension HitChild 的位置是命中点，但位移的历史作用中心属于原父片段。
        Payload.EventCenter = Input.Origin;
    }
    FAbilityImpactSnapshot Impact;
    Impact.Location = Hit.Result.ImpactLocation;
    Impact.Direction = Input.Direction;
    Impact.ConfirmedTarget = Hit.Target;
    Payload.Impact = MoveTemp(Impact);
    Payload.bHasDamageResult = true;
    Payload.bDamageAttempted = Hit.Result.bAttempted;
    Payload.bKilled = Hit.Result.bKilled;
    Payload.DamageApplied = Hit.Result.Applied;
    Payload.ActualDamage = Hit.Result.ActualDamage;
    return Payload;
}

void UAbilityDamageBatchNodeExecution::FinishBatch(UAbilityGraphInstance& Owner)
{
    // 兼容图本来就排队子调用：先完成整批根伤害，再按候选顺序预留真实 OnHit 后继。
    const auto Hits = MoveTemp(Results);
    for (const auto& Hit : Hits)
    {
        if (bStopped || Owner.IsFinished())
        {
            return;
        }
        // 预算拒绝只拒绝该子调用；已经结算的根伤害不回滚，也不伪造一次成功事件。
        Owner.RequestChild(InvocationId, TEXT("DamageResolved"), HitPayload(Hit));
        if (bStopped || Owner.IsFinished())
        {
            return;
        }
    }
    if (!bStopped && !Owner.IsFinished())
    {
        Owner.Publish(InvocationId, TEXT("Completed"), AbilityBatchDetails::PositionOnly(Input));
    }
}

void UAbilityDamageBatchNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    Targets.Reset();
    Results.Reset();
    PreparedParams = {};
}

void AbilityBatchCombatNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Query;
    Query.TypeId = TEXT("Query.ShapeBatch");
    Query.ConfigClass = UAbilityQueryBatchNodeConfig::StaticClass();
    Query.ExecutionClass = UAbilityQueryBatchNodeExecution::StaticClass();
    Query.InputPayload = EAbilityPortPayload::Origin;
    // 查询及共享批效果没有原始Actor生成；多目标输出仍受调用/事件额度约束。
    Query.bRuntimeFiniteSafe = true;
    Query.bSupportsActorTargets = Query.bSupportsMassTargets = Query.bSupportsSnapshotTargets = true;
    Query.Ports = {{TEXT("Completed"), EAbilityPortPayload::TargetSet, true}};
    FAbilityNodeDescriptor Damage = Query;
    Damage.TypeId = TEXT("Effect.DamageBatch");
    Damage.ConfigClass = UAbilityDamageBatchNodeConfig::StaticClass();
    Damage.ExecutionClass = UAbilityDamageBatchNodeExecution::StaticClass();
    Damage.InputPayload = EAbilityPortPayload::TargetSet;
    // 同步固定批动作只消费已验证的最多65536个原生命；没有 Driver、资源或嵌套执行器，可作为原型任意 payload。
    Damage.bPrototypeCompatibilitySafe = true;
    Damage.bRuntimeFiniteSafe = true;
    Damage.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                    {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Query, Damage})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Batch node registration: %s"), *Error);
        }
    }
}
