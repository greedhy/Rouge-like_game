// 调用链：环境/测试注册策略 -> 图静态校验 -> 每次调用按 ExecutionClass 实例化。
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityBarrier.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "AbilitySystem/Unified/WorkshopPropagationAbilities.h"
#include "AbilitySystem/Unified/WorkshopDispelAbilities.h"
#include "AbilitySystem/Unified/WorkshopEmberAbilities.h"
#include "AbilitySystem/Unified/WorkshopShatterAbilities.h"
#include "AbilitySystem/Unified/WorkshopRejuvenationAbilities.h"
#include "AbilitySystem/Unified/WorkshopSpatialStormAbilities.h"
#include "AbilitySystem/Unified/WorkshopSurfaceConversionAbilities.h"
#include "AbilitySystem/Unified/WorkshopTerrainCatalystAbilities.h"
#include "AbilitySystem/Unified/Nodes/AbilityGraphPassThroughNodes.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/Nodes/AbilityBasicNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityMeleeNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityOrbitNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityEnemyActionNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityEnemySpecialNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorldTimeNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityDefenseNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityTemporalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatchCombatNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityDistributionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaFragmentNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypePhaseNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypePresentationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityForkedPropagationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeSurfaceNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityNativeProjectileParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeTargetNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeExtensionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityOrbitFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFlightNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeDistributionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityLightningFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeEmitterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeOrbitNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeWaveNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFollowFieldNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"

TArray<FName> FAbilityNodeRegistry::GetRegisteredTypeIds() const
{
    TArray<FName> Result;
    Descriptors.GetKeys(Result);
    Result.Sort(FNameLexicalLess());
    return Result;
}

bool FAbilityNodeRegistry::Register(const FAbilityNodeDescriptor& Descriptor, FString& Error)
{
    Error.Reset();
    if (Descriptor.TypeId.IsNone() || Descriptors.Contains(Descriptor.TypeId))
    {
        Error = FString::Printf(TEXT("TypeId '%s' is empty or already registered."), *Descriptor.TypeId.ToString());
        return false;
    }
    const UClass* Config = Descriptor.ConfigClass.Get();
    const UClass* Execution = Descriptor.ExecutionClass.Get();
    if (!Config || !Config->IsChildOf(UAbilityNodeConfig::StaticClass()) || Config->HasAnyClassFlags(CLASS_Abstract))
    {
        Error = TEXT("A concrete UAbilityNodeConfig ConfigClass is required.");
        return false;
    }
    if (!Execution || !Execution->IsChildOf(UAbilityNodeExecution::StaticClass()) ||
        Execution->HasAnyClassFlags(CLASS_Abstract))
    {
        Error = TEXT("A concrete UAbilityNodeExecution ExecutionClass is required.");
        return false;
    }
    if (!Descriptor.bSupportsActorTargets && !Descriptor.bSupportsMassTargets && !Descriptor.bSupportsSnapshotTargets)
    {
        Error = TEXT("At least one target/input form must be supported.");
        return false;
    }
    FAbilityNodeDescriptor Normalized = Descriptor;
    // V1 原生调用者仍可传名称列表。注册时一次转换，之后执行只读取 Ports。
    if (!Descriptor.OutputPorts.IsEmpty())
    {
        Normalized.Ports.Reset();
        for (FName Name : Descriptor.OutputPorts)
        {
            const auto* Typed = Descriptor.Ports.FindByPredicate(
                [Name](const FAbilityPortDescriptor& Port)
                {
                    return Port.Name == Name;
                });
            Normalized.Ports.Add(Typed ? *Typed : FAbilityPortDescriptor{Name, Descriptor.InputPayload, true});
        }
    }
    if (Normalized.Ports.IsEmpty())
    {
        Error = TEXT("At least one output port must be declared.");
        return false;
    }
    TSet<FName> Seen;
    Normalized.OutputPorts.Reset();
    for (const auto& Port : Normalized.Ports)
    {
        if (Port.Name.IsNone() || Seen.Contains(Port.Name) ||
            ((Port.Name == TEXT("Failed") || Port.Name == TEXT("Cancelled") || Port.Name == TEXT("Completed")) &&
             !Port.bTerminal))
        {
            Error = TEXT("Output port is empty, duplicated, or declares a nonterminal completion.");
            return false;
        }
        Seen.Add(Port.Name);
        Normalized.OutputPorts.Add(Port.Name);
    }
    if (Normalized.bRuntimePurePayload && Normalized.bIsAsync)
    {
        Error = TEXT("A repeated pure action cannot own an asynchronous executor lifetime.");
        return false;
    }
    TSet<FName> RepeatedPorts;
    for (const FName Name : Normalized.PotentiallyUnboundedOutputPorts)
    {
        const auto* Port = Normalized.Ports.FindByPredicate(
            [Name](const FAbilityPortDescriptor& Item)
            {
                return Item.Name == Name;
            });
        if (!Port || Port->bTerminal || RepeatedPorts.Contains(Name))
        {
            Error = TEXT("Potentially unbounded ports must be distinct declared nonterminal business outputs.");
            return false;
        }
        RepeatedPorts.Add(Name);
    }
    Descriptors.Add(Descriptor.TypeId, MoveTemp(Normalized));
    return true;
}

const FAbilityNodeDescriptor* FAbilityNodeRegistry::Find(FName TypeId) const
{
    return Descriptors.Find(TypeId);
}

bool FAbilityNodeRegistry::IsLegacyProfile(FName Profile)
{
    if (Profile == TEXT("LegacyExtension.v1"))
    {
        return true;
    }
    for (const TCHAR* Backend : {TEXT("Actor"), TEXT("Mixed")})
    {
        if (Profile == FName(*FString::Printf(TEXT("Legacy%s.v1"), Backend)))
        {
            return true;
        }
        for (const TCHAR* Family : {TEXT("Shape"), TEXT("Delivery"), TEXT("Timed"), TEXT("Channel"), TEXT("Surface")})
        {
            if (Profile == FName(*FString::Printf(TEXT("Legacy%s.%s.v1"), Backend, Family)))
            {
                return true;
            }
        }
    }
    return false;
}

void FAbilityNodeRegistry::RegisterBuiltins()
{
    AbilityGraphPassThroughNodes::Register(*this);
    AbilityDeliveryNodes::Register(*this);
    AbilityPropagationNodes::Register(*this);
    AbilityBehaviorNodes::Register(*this);
    AbilityTemporalNodes::Register(*this);
    AbilityBatchCombatNodes::Register(*this);
    AbilityStateCompositionNodes::Register(*this);
    AbilityDistributionNodes::Register(*this);
    AbilityInputDamageNodes::Register(*this);
    AbilityNovaFragments::Register(*this);
    AbilityPrototypeParameterNodes::Register(*this);
    AbilityPrototypePhaseNodes::Register(*this);
    AbilityPrototypePresentationNodes::Register(*this);
    AbilityForkedPropagationNodes::Register(*this);
    AbilityGameplayRuleNodes::Register(*this);
    AbilityBarrierNodes::Register(*this);
    AbilityElementalNodes::Register(*this);
    AbilityWorkshopTerrainNodes::Register(*this);
    WorkshopPropagationAbilities::RegisterNodes(*this);
    AbilityWorkshopDispel::RegisterNodes(*this);
    WorkshopEmberAbilities::RegisterNodes(*this);
    WorkshopShatterAbilities::RegisterNodes(*this);
    WorkshopRejuvenationAbilities::RegisterNodes(*this);
    WorkshopSpatialStormAbilities::RegisterNodes(*this);
    AbilityWorkshopSurfaceConversion::RegisterNodes(*this);
    AbilityWorkshopTerrainCatalyst::RegisterNodes(*this);
    AbilityPrototypeSurfaceNodes::Register(*this);
    AbilityNativeProjectileParameters::Register(*this);
    AbilityArcaneShardFragments::Register(*this);
    AbilityPrototypeTargetNodes::Register(*this);
    AbilityPrototypeExtensionNodes::Register(*this);
    AbilityElectrocuteFragments::Register(*this);
    AbilityOrbitFragments::Register(*this);
    AbilityPrototypeFlightNodes::Register(*this);
    AbilityPetrifyFragments::Register(*this);
    AbilityPrototypeDistributionNodes::Register(*this);
    AbilityLightningFragments::Register(*this);
    AbilityPrototypeEmitterNodes::Register(*this);
    AbilityPrototypeOrbitNodes::Register(*this);
    AbilityPrototypeWaveNodes::Register(*this);
    AbilityPrototypeFollowFieldNodes::Register(*this);
    AbilityWorldTimeNodes::Register(*this);
    AbilityDefenseNodes::Register(*this);
    FAbilityNodeDescriptor Delay;
    Delay.TypeId = TEXT("Control.Delay");
    Delay.ConfigClass = UAbilityDelayNodeConfig::StaticClass();
    Delay.ExecutionClass = UAbilityDelayNodeExecution::StaticClass();
    Delay.OutputPorts = {TEXT("Completed"), TEXT("Cancelled"), TEXT("Failed")};
    Delay.bSupportsActorTargets = true;
    Delay.bSupportsMassTargets = true;
    Delay.bIsAsync = true;
    Delay.bSustainedSafe = true;
    // 原时钟只持有本次调用的剩余秒数；Stop封口，没有Actor工厂或图外定时器。
    Delay.bRuntimeFiniteSafe = true;
    Delay.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;

    // 原 Control.Delay 为 V1 活目标合同，继续要求其原生命；位置型配方使用独立端口声明复用同一时钟。
    FAbilityNodeDescriptor OriginDelay = Delay;
    OriginDelay.TypeId = TEXT("Control.DelayOrigin");
    OriginDelay.InputPayload = EAbilityPortPayload::Origin;
    OriginDelay.bRuntimeFiniteSafe = true;
    OriginDelay.OutputPorts.Reset();
    OriginDelay.bSupportsSnapshotTargets = true;
    OriginDelay.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true},
                         {TEXT("Cancelled"), EAbilityPortPayload::Origin, true},
                         {TEXT("Failed"), EAbilityPortPayload::Origin, true}};
    if (!Find(OriginDelay.TypeId))
    {
        FString Error;
        ensureMsgf(Register(OriginDelay, Error), TEXT("Invalid origin delay strategy: %s"), *Error);
    }

    FAbilityNodeDescriptor Petrify;
    Petrify.TypeId = TEXT("Status.Petrify");
    Petrify.ConfigClass = UAbilityPetrifyNodeConfig::StaticClass();
    Petrify.ExecutionClass = UAbilityPetrifyNodeExecution::StaticClass();
    Petrify.OutputPorts = {TEXT("Completed"), TEXT("Failed")};
    Petrify.bSupportsActorTargets = true;
    Petrify.bSupportsMassTargets = true;
    // 单目标有限状态委托给既有状态所有者；无生成/额外时钟，允许已准入持续宿主复用。
    Petrify.bSustainedSafe = true;
    Petrify.bPrototypeCompatibilitySafe = true;
    Petrify.bRuntimeFiniteSafe = true;
    Petrify.bRuntimePurePayload = true;
    Petrify.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    for (const FAbilityNodeDescriptor* Descriptor : {&Delay, &Petrify})
    {
        if (!Find(Descriptor->TypeId))
        {
            FString Error;
            ensureMsgf(Register(*Descriptor, Error), TEXT("Invalid builtin strategy: %s"), *Error);
        }
    }
    AbilityLegacyNodes::Register(*this);
    AbilityCombatNodes::Register(*this);
    AbilityBatch6Nodes::Register(*this);
    AbilityArcaneShardNodes::Register(*this);
    AbilityMeleeNodes::Register(*this);
    AbilityNovaNodes::Register(*this);
    AbilityTraditionalNodes::Register(*this);
    AbilityElectrocuteNodes::Register(*this);
    AbilityOrbitNodes::Register(*this);
    AbilityCharacterNodes::Register(*this);
    AbilityEnemyActionNodes::Register(*this);
    AbilityEnemySpecialNodes::Register(*this);
    AbilityMovementNodes::Register(*this);
    AbilityGameplayEffectNodes::Register(*this);
}

bool FAbilityNodeRegistry::IsPrototypeCompatibilityProfile(FName Profile)
{
    return Profile == TEXT("PrototypeFine.Actor.v1") || Profile == TEXT("PrototypeFine.Mixed.v1") ||
           Profile == TEXT("PrototypeFine.Extension.v1");
}
