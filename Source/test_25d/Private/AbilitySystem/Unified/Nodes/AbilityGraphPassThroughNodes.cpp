// 调用链：服务器 WrapBranch -> 配方编译 -> GraphInstance -> Apply 调用组收齐 -> Continue 原输入。
#include "AbilitySystem/Unified/Nodes/AbilityGraphPassThroughNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

namespace
{
    /** 显式映射是模板与注册表的共同合同，新增枚举值默认无权进入此路径。 */
    FName PassThroughType(EAbilityPortPayload Payload)
    {
        switch (Payload)
        {
        case EAbilityPortPayload::Origin:
            return TEXT("PassThrough.Origin");
        case EAbilityPortPayload::LiveTarget:
            return TEXT("PassThrough.LiveTarget");
        case EAbilityPortPayload::ConfirmedImpact:
            return TEXT("PassThrough.ConfirmedImpact");
        case EAbilityPortPayload::DamageResult:
            return TEXT("PassThrough.DamageResult");
        case EAbilityPortPayload::TargetSet:
            return TEXT("PassThrough.TargetSet");
        default:
            return NAME_None;
        }
    }
} // namespace

bool UAbilityGraphPassThroughConfig::MatchesType(FName TypeId) const
{
    return !TypeId.IsNone() && TypeId == PassThroughType(Payload);
}

bool UAbilityGraphPassThroughExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    // 基类保活参数/Context，图持有当前调用；回调弱持有策略，取消整根时不会延长其生命。
    const TWeakObjectPtr<UAbilityGraphPassThroughExecution> Weak(this);
    const auto Payload = CastChecked<UAbilityGraphPassThroughConfig>(&Config)->Payload;
    if (!Owner.PublishWithContinuation(Id, TEXT("Apply"), Input,
                                       [Weak, Payload]()
                                       {
                                           if (auto* Self = Weak.Get())
                                           {
                                               Self->Resume(Payload);
                                           }
                                       }))
    {
        Error = TEXT("Unable to schedule the typed effect subtree within the current invocation.");
        return false;
    }
    return true;
}

void UAbilityGraphPassThroughExecution::Resume(EAbilityPortPayload Payload)
{
    auto* Run = GraphOwner.Get();
    if (!Run || bStopped || Run->IsFinished())
    {
        return;
    }
    FVector Location;
    if (Payload == EAbilityPortPayload::LiveTarget &&
        (!Input.LiveTargetToken.IsSet() ||
         !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Location)))
    {
        Run->FinishInvocation(InvocationId, EAbilityGraphEndReason::TargetLost);
        return;
    }
    // Publish 再验证原源生命、载荷、事件与预算；同步终止后不再进入图。
    if (!Run->Publish(InvocationId, TEXT("Continue"), Input) && !Run->IsFinished())
    {
        Run->Cancel(EAbilityGraphEndReason::Failed);
    }
}

void AbilityGraphPassThroughNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (const auto Payload :
         {EAbilityPortPayload::Origin, EAbilityPortPayload::LiveTarget, EAbilityPortPayload::ConfirmedImpact,
          EAbilityPortPayload::DamageResult, EAbilityPortPayload::TargetSet})
    {
        FAbilityNodeDescriptor D;
        D.TypeId = PassThroughType(Payload);
        if (Registry.Find(D.TypeId))
        {
            continue;
        }
        D.ConfigClass = UAbilityGraphPassThroughConfig::StaticClass();
        D.ExecutionClass = UAbilityGraphPassThroughExecution::StaticClass();
        D.InputPayload = Payload;
        D.bSupportsActorTargets = true;
        D.bSupportsMassTargets = true;
        D.bSupportsSnapshotTargets = Payload != EAbilityPortPayload::LiveTarget;
        D.bRuntimeFiniteSafe = true;
        D.Ports = {{TEXT("Apply"), Payload, false}, {TEXT("Continue"), Payload, true}};
        FString Error;
        ensureAlwaysMsgf(Registry.Register(D, Error), TEXT("Pass-through registration: %s"), *Error);
    }
}

bool AbilityGraphPassThroughNodes::WrapBranch(UObject* Outer, const UAbilityGraphDefinition* EffectBranch,
                                              EAbilityPortPayload Payload, UAbilityGraphDefinition*& OutGraph,
                                              FGuid& OutContinuationNode, FName& OutContinuationPort, FString& Error)
{
    OutGraph = nullptr;
    OutContinuationNode.Invalidate();
    OutContinuationPort = NAME_None;
    Error.Reset();
    const FName Type = PassThroughType(Payload);
    if (Type.IsNone() || !IsValid(EffectBranch) || EffectBranch->SchemaVersion != 2 ||
        EffectBranch->BudgetPolicy != EAbilityGraphBudgetPolicy::Bounded || EffectBranch->OriginalProjection.bEnabled)
    {
        Error = TEXT("An edge fragment needs a bounded Schema2 effect subtree and a supported payload.");
        return false;
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    TArray<FString> Errors;
    if (!EffectBranch->Validate(Registry, Errors))
    {
        Error = FString::Join(Errors, TEXT("; "));
        return false;
    }
    auto* Wrapped = DuplicateObject<UAbilityGraphDefinition>(EffectBranch, Outer ? Outer : GetTransientPackage());
    const FGuid Adapter = FGuid::NewGuid();
    auto* Config = NewObject<UAbilityGraphPassThroughConfig>(Wrapped);
    Config->Payload = Payload;
    Wrapped->Nodes.Add({Adapter, Type, Config});
    Wrapped->Edges.Add({Adapter, Wrapped->EntryNodeId, TEXT("Apply")});
    Wrapped->EntryNodeId = Adapter;
    if (!Wrapped->Validate(Registry, Errors))
    {
        Error = FString::Join(Errors, TEXT("; "));
        return false;
    }
    OutGraph = Wrapped;
    OutContinuationNode = Adapter;
    OutContinuationPort = TEXT("Continue");
    return true;
}
