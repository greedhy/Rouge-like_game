// 调用链：环境/测试注册策略 -> 图静态校验 -> 每次调用按 ExecutionClass 实例化。
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
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
    FAbilityNodeDescriptor Delay;
    Delay.TypeId = TEXT("Control.Delay");
    Delay.ConfigClass = UAbilityDelayNodeConfig::StaticClass();
    Delay.ExecutionClass = UAbilityDelayNodeExecution::StaticClass();
    Delay.OutputPorts = {TEXT("Completed"), TEXT("Cancelled"), TEXT("Failed")};
    Delay.bSupportsActorTargets = true;
    Delay.bSupportsMassTargets = true;
    Delay.bIsAsync = true;
    Delay.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;

    FAbilityNodeDescriptor Petrify;
    Petrify.TypeId = TEXT("Status.Petrify");
    Petrify.ConfigClass = UAbilityPetrifyNodeConfig::StaticClass();
    Petrify.ExecutionClass = UAbilityPetrifyNodeExecution::StaticClass();
    Petrify.OutputPorts = {TEXT("Completed"), TEXT("Failed")};
    Petrify.bSupportsActorTargets = true;
    Petrify.bSupportsMassTargets = true;
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
