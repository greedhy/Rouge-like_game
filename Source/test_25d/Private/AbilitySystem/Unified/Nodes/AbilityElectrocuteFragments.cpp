// 调用链：原雷链Notify -> 共用目标/表现/费用宿主 -> 独立Period/Final批效果 -> continuation -> 原冷却/结束。
// 只读阶段帧与同根弱资源索引相互验证；真实宿主拥有Timer，阶段结束先解绑索引，取消不执行最终伤害。
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteFragments.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "UObject/StrongObjectPtr.h"

bool UAbilityElectrocutePayloadConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DamageMultiplier) || DamageMultiplier <= 0.f)
    {
        Error = TEXT("Native Electrocute payload multiplier must be finite and positive.");
        return false;
    }
    return true;
}

bool UAbilityElectrocutePayloadConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (Parameter != EAbilityConfigScalar::Damage || !FMath::IsFinite(Multiplier) || Multiplier <= 0.f ||
        !FMath::IsFinite(DamageMultiplier * Multiplier) || DamageMultiplier * Multiplier <= 0.f)
    {
        return false;
    }
    DamageMultiplier *= Multiplier;
    return true;
}

namespace
{
    const FName HostSlot(TEXT("Electrocute.NativeHost"));

    UAbilityElectrocuteHostExecution* ResolveHost(UAbilityGraphInstance& Owner, const FAbilityNodeInput& Input,
                                                  bool bFinal, FString& Error)
    {
        const auto* Frame = Cast<UAbilityElectrocutePhaseFrame>(Input.ParameterFrame.Get());
        if (!Frame || !Frame->BelongsTo(Owner.GetExecutionId(), Input.SourceToken) || Frame->IsFinal() != bFinal)
        {
            Error = TEXT("Native Electrocute payload requires its original root's matching phase frame.");
            return nullptr;
        }
        auto* Host = Cast<UAbilityElectrocuteHostExecution>(Owner.FindRuntimeResource(*Frame, HostSlot));
        if (!Host)
        {
            Error = TEXT("Native Electrocute phase has no current owning host.");
        }
        return Host;
    }
} // namespace

UAbilityElectrocutePhaseFrame* UAbilityElectrocutePhaseFrame::Create(UObject* Outer, FGuid Root,
                                                                     const FAbilitySourceToken& Source,
                                                                     const FDamageEffectParams& InDamage,
                                                                     const TArray<FAbilityTargetToken>& InTargets,
                                                                     uint64 InSequence, bool bInFinal)
{
    auto* Frame = NewObject<UAbilityElectrocutePhaseFrame>(Outer);
    if (!Frame->BindExecution(Root, Source))
    {
        return nullptr;
    }
    Frame->Damage = InDamage;
    Frame->Targets = InTargets;
    Frame->Sequence = InSequence;
    Frame->bFinal = bInFinal;
    return Frame;
}

const FDamageEffectParams& UAbilityElectrocutePhaseFrame::GetDamage() const
{
    return Damage;
}

const TArray<FAbilityTargetToken>& UAbilityElectrocutePhaseFrame::GetTargets() const
{
    return Targets;
}

uint64 UAbilityElectrocutePhaseFrame::GetSequence() const
{
    return Sequence;
}

bool UAbilityElectrocutePhaseFrame::IsFinal() const
{
    return bFinal;
}

bool UAbilityElectrocuteHostExecution::IsPeriodPending() const
{
    return bPeriodPayloadPending;
}

void UAbilityElectrocuteHostExecution::BeginPeriodEffects()
{
    bPeriodPayloadPending = true;
    if (!PublishPhase(false))
    {
        if (auto* Owner = GraphOwner.Get())
        {
            Owner->Cancel(EAbilityGraphEndReason::Failed);
        }
    }
}

void UAbilityElectrocuteHostExecution::EmitFinalDamage(const FAbilityTargetToken& Life, FVector)
{
    // 共用ReleaseTarget已先解绑表现，并在任何Final GE之前验证该原生命。
    FinalTargets.Add(Life);
}

void UAbilityElectrocuteHostExecution::FinishCompletion()
{
    if (bStopped || !GraphOwner.IsValid())
    {
        return;
    }
    if (!PublishPhase(true))
    {
        if (auto* Owner = GraphOwner.Get())
        {
            Owner->Cancel(EAbilityGraphEndReason::Failed);
        }
    }
}

bool UAbilityElectrocuteHostExecution::PublishPhase(bool bFinal)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return false;
    }
    TArray<FAbilityTargetToken> Lives;
    if (bFinal)
    {
        Lives = FinalTargets;
    }
    else
    {
        // 目标生命与引导收尾属于宿主，不能依赖可被替换的伤害sink才执行原生命检查。
        // 共用旧walk：失去主目标会转入原Final/结束；正常空地仍保留旧Avatar分支。
        WalkPeriodTargets(
            [&Lives](AActor*, const FAbilityTargetToken& Life, FVector)
            {
                Lives.Add(Life);
            });
        if (bStopped || bCompleting || Owner->IsFinished())
        {
            return true; // walk可能已经同步排入Final，不能再发布失效的周期载荷。
        }
    }
    auto* Frame = UAbilityElectrocutePhaseFrame::Create(this, Owner->GetExecutionId(), Input.SourceToken,
                                                        ResolvedDamage, Lives, PeriodSequence, bFinal);
    if (!Frame || !Owner->RegisterRuntimeResource(*Frame, HostSlot, this))
    {
        return false;
    }
    (bFinal ? FinalFrame : PeriodFrame) = Frame;
    auto Snapshot = MakeShared<FAbilityTargetSetSnapshot>();
    Snapshot->Source = Input.SourceToken;
    Snapshot->Targets = Lives;
    auto Payload = Input;
    Payload.LiveTargetToken.Reset();
    Payload.TargetSet = Snapshot;
    Payload.ParameterFrame = Frame;
    Payload.DamageParams = ResolvedDamage; // Spell装备规则仅在原宿主Start准备一次。
    const TWeakObjectPtr<UAbilityElectrocuteHostExecution> Weak(this);
    if (!Owner->PublishWithContinuation(InvocationId, bFinal ? TEXT("Final") : TEXT("Period"), Payload,
                                        [Weak, bFinal]()
                                        {
                                            if (auto* Host = Weak.Get())
                                            {
                                                Host->PhaseCompleted(bFinal);
                                            }
                                        }))
    {
        ReleasePhaseResource(Frame);
        return false;
    }
    // 物理/Timer边界维持原即时行为；在GE内重入只请求同一drainer继续，不递归执行Final或cooldown。
    Owner->FlushPublishedEvents(true);
    return true;
}

void UAbilityElectrocuteHostExecution::PhaseCompleted(bool bFinal)
{
    if (bFinal)
    {
        ReleasePhaseResource(FinalFrame);
        FinalFrame = nullptr;
        if (!bStopped && GraphOwner.IsValid())
        {
            Super::FinishCompletion(); // 全部实际Final完成之后，原费用前端才提交一次cooldown。
        }
    }
    else
    {
        ReleasePhaseResource(PeriodFrame);
        PeriodFrame = nullptr;
        bPeriodPayloadPending = false;
        EndPeriodEffects();
    }
}

void UAbilityElectrocuteHostExecution::ReleasePhaseResource(UAbilityElectrocutePhaseFrame* Frame)
{
    if (auto* Owner = GraphOwner.Get(); Owner && Frame)
    {
        Owner->UnregisterRuntimeResource(*Frame, HostSlot, this);
    }
}

void UAbilityElectrocuteHostExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (bStopped)
    {
        return;
    }
    ReleasePhaseResource(PeriodFrame);
    ReleasePhaseResource(FinalFrame);
    PeriodFrame = nullptr;
    FinalFrame = nullptr;
    bPeriodPayloadPending = false;
    Super::Stop(Reason);
}

void UAbilityElectrocuteHostExecution::ExecutePeriodPayload(const UAbilityElectrocutePhaseFrame& Frame, float Level,
                                                            float DamageScale)
{
    // 同步GE可取消根并触发GC；当前批栈退出前仍保活真正的宿主，不依赖弱资源索引保活。
    const TStrongObjectPtr<UAbilityElectrocuteHostExecution> SelfGuard(this);
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !bPeriodPayloadPending || PeriodFrame.Get() != &Frame || !Owner)
    {
        return;
    }
    auto Params = Frame.GetDamage();
    Params.BaseDamage *= DamageScale; // 唯一实际owner消费累计主伤倍率；周期仍不注入Final的DOT/force。
    // 单个批策略复用原遍历，不按65个目标创建65次调用；同步主目标死亡后共用循环立即停止。
    WalkPeriodTargets(
        [&](AActor* Actor, const FAbilityTargetToken& Life, FVector Location)
        {
            if (!Frame.GetTargets().ContainsByPredicate(
                    [&](const FAbilityTargetToken& Captured)
                    {
                        return Captured.World == Life.World && Captured.Target == Life.Target &&
                               Captured.ActorLease == Life.ActorLease && Captured.DungeonRunId == Life.DungeonRunId;
                    }))
            {
                return;
            }
            const auto Result = AbilityElectrocuteNodes::ApplyPeriod(Input.SourceToken, Life, Location, Params, Level,
                                                                     Actor == Input.SourceToken.Avatar.Get());
            if (bStopped || bCompleting || !Result.bAttempted ||
                !Owner->HasSuccessor(InvocationId, TEXT("DamageResolved")))
            {
                return;
            }
            auto Payload = Input;
            Payload.Origin = Location;
            Payload.LiveTargetToken.Reset();
            Payload.Impact = FAbilityImpactSnapshot{Location, Input.Direction, Life, Frame.GetSequence()};
            Payload.bHasDamageResult = true;
            Payload.bDamageAttempted = true;
            Payload.bKilled = Result.bKilled;
            // 从宿主发布，独立OnHit后继不属于Period/Final continuation，不会拖延原冷却时点。
            Owner->Publish(InvocationId, TEXT("DamageResolved"), Payload);
        });
}

void UAbilityElectrocuteHostExecution::ExecuteFinalPayload(const UAbilityElectrocutePhaseFrame& Frame,
                                                           float DamageScale, float DebuffDamageScale)
{
    const TStrongObjectPtr<UAbilityElectrocuteHostExecution> SelfGuard(this);
    if (bStopped || FinalFrame.Get() != &Frame)
    {
        return;
    }
    auto Params = Frame.GetDamage();
    Params.BaseDamage *= DamageScale;
    Params.DebuffDamage *= DebuffDamageScale; // 与主伤独立；原帧/后继的原参数从不写回。
    for (const auto& Life : Frame.GetTargets())
    {
        if (bStopped || !GraphOwner.IsValid())
        {
            return;
        }
        AbilityElectrocuteNodes::ApplyFinal(Input.SourceToken, Life, Params);
    }
}

bool UAbilityElectrocutePeriodExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                               FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    auto* Host = ResolveHost(Owner, Input, false, Error);
    if (!Host)
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityElectrocutePayloadConfig>(&Config);
    const auto* Frame = CastChecked<UAbilityElectrocutePhaseFrame>(Input.ParameterFrame.Get());
    const float Scale = Input.DamageScale * Settings->DamageMultiplier;
    if (!Settings->Validate(Error) || !FMath::IsFinite(Scale) ||
        !FMath::IsFinite(Frame->GetDamage().BaseDamage * Scale))
    {
        Error = TEXT("Native Electrocute period cumulative damage multiplier is invalid.");
        return false;
    }
    Host->ExecutePeriodPayload(*Frame, Input.Level, Scale);
    return !bStopped && !Owner.IsFinished() ? Owner.Publish(Id, TEXT("Completed"), Input) : true;
}

bool UAbilityElectrocuteFinalExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    auto* Host = ResolveHost(Owner, Input, true, Error);
    if (!Host)
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityElectrocutePayloadConfig>(&Config);
    const auto* Frame = CastChecked<UAbilityElectrocutePhaseFrame>(Input.ParameterFrame.Get());
    const float Scale = Input.DamageScale * Settings->DamageMultiplier;
    if (!Settings->Validate(Error) || !FMath::IsFinite(Scale) ||
        !FMath::IsFinite(Frame->GetDamage().BaseDamage * Scale) ||
        !FMath::IsFinite(Frame->GetDamage().DebuffDamage * Input.DebuffDamageScale))
    {
        Error = TEXT("Native Electrocute final cumulative damage multipliers are invalid.");
        return false;
    }
    Host->ExecuteFinalPayload(*Frame, Scale, Input.DebuffDamageScale);
    return !bStopped && !Owner.IsFinished() ? Owner.Publish(Id, TEXT("Completed"), Input) : true;
}

UAbilityGraphDefinition* AbilityElectrocuteFragments::Build(UObject* Outer,
                                                            const UAbilityElectrocuteNodeConfig& Settings)
{
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::RegisteredSustained;
    auto Add = [&](FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        return Node.NodeId;
    };
    const auto Host =
        Add(TEXT("Channel.ElectrocuteHost"), DuplicateObject<UAbilityElectrocuteNodeConfig>(&Settings, Graph));
    const auto Period = Add(TEXT("Effect.ElectrocutePeriod"), NewObject<UAbilityElectrocutePayloadConfig>(Graph));
    const auto Final = Add(TEXT("Effect.ElectrocuteFinal"), NewObject<UAbilityElectrocutePayloadConfig>(Graph));
    Graph->EntryNodeId = Host;
    Graph->Edges = {{Host, Period, TEXT("Period")}, {Host, Final, TEXT("Final")}};
    return Graph;
}

void AbilityElectrocuteFragments::Register(FAbilityNodeRegistry& Registry)
{
    auto Add = [&](FName Type, UClass* Config, UClass* Execution, EAbilityPortPayload Input,
                   TArray<FAbilityPortDescriptor> Ports, bool bHost)
    {
        if (Registry.Find(Type))
        {
            return;
        }
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = Type;
        Descriptor.ConfigClass = Config;
        Descriptor.ExecutionClass = Execution;
        Descriptor.InputPayload = Input;
        Descriptor.Ports = MoveTemp(Ports);
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bIsAsync = Descriptor.bSustainedRoot = bHost;
        Descriptor.bSustainedSafe = true;
        if (bHost)
        {
            Descriptor.PotentiallyUnboundedOutputPorts = {TEXT("Period"), TEXT("DamageResolved"),
                                                          TEXT("PeriodElapsed")};
        }
        Descriptor.bRuntimePayloadReplacementSlot = Type == TEXT("Effect.ElectrocutePeriod");
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Native Electrocute registration: %s"), *Error);
    };
    Add(TEXT("Channel.ElectrocuteHost"), UAbilityElectrocuteNodeConfig::StaticClass(),
        UAbilityElectrocuteHostExecution::StaticClass(), EAbilityPortPayload::Origin,
        {{TEXT("Period"), EAbilityPortPayload::TargetSet, false},
         {TEXT("Final"), EAbilityPortPayload::TargetSet, false},
         {TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
         {TEXT("PeriodElapsed"), EAbilityPortPayload::Origin, false},
         {TEXT("Completed"), EAbilityPortPayload::Origin, true}},
        true);
    Add(TEXT("Effect.ElectrocutePeriod"), UAbilityElectrocutePayloadConfig::StaticClass(),
        UAbilityElectrocutePeriodExecution::StaticClass(), EAbilityPortPayload::TargetSet,
        {{TEXT("Completed"), EAbilityPortPayload::TargetSet, true}}, false);
    Add(TEXT("Effect.ElectrocuteFinal"), UAbilityElectrocutePayloadConfig::StaticClass(),
        UAbilityElectrocuteFinalExecution::StaticClass(), EAbilityPortPayload::TargetSet,
        {{TEXT("Completed"), EAbilityPortPayload::TargetSet, true}}, false);
}
