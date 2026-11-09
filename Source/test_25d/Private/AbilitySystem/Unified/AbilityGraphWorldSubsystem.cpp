// 生命周期链：权威 GA 移交 -> 世界持有/推进 -> 正常完成移除；世界退出先取消后释放。
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Engine/World.h"

bool UAbilityGraphWorldSubsystem::Adopt(UAbilityGraphInstance* Instance, FString& Error)
{
    Error.Reset();
    UWorld* World = GetWorld();
    if (bClosing || !IsValid(Instance) || !World || World->bIsTearingDown || World->GetNetMode() == NM_Client ||
        Instance->GetTypedOuter<UWorld>() != World)
    {
        Error = TEXT("Only a live graph created in this authoritative world may be adopted.");
        return false;
    }
    if (!Instance->IsFinished())
    {
        Executions.AddUnique(Instance);
    }
    return true;
}

void UAbilityGraphWorldSubsystem::CancelExecution(FGuid ExecutionId, EAbilityGraphEndReason Reason)
{
    const auto Snapshot = Executions;
    for (UAbilityGraphInstance* Instance : Snapshot)
    {
        if (Instance && Instance->GetExecutionId() == ExecutionId)
        {
            Instance->Cancel(Reason);
        }
    }
}

void UAbilityGraphWorldSubsystem::Tick(float DeltaSeconds)
{
    if (bClosing || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
    {
        return;
    }
    const auto Snapshot = Executions;
    for (UAbilityGraphInstance* Instance : Snapshot)
    {
        if (!bClosing && IsValid(Instance) && !Instance->IsFinished())
        {
            Instance->Advance(DeltaSeconds);
        }
    }
    Executions.RemoveAll(
        [](const UAbilityGraphInstance* Instance)
        {
            return !IsValid(Instance) || Instance->IsFinished();
        });
}

void UAbilityGraphWorldSubsystem::Deinitialize()
{
    bClosing = true;
    const auto Snapshot = Executions;
    for (UAbilityGraphInstance* Instance : Snapshot)
    {
        if (IsValid(Instance))
        {
            Instance->Cancel(EAbilityGraphEndReason::SourceInvalid);
        }
    }
    Executions.Reset();
    Super::Deinitialize();
}

bool UAbilityGraphWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

TStatId UAbilityGraphWorldSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UAbilityGraphWorldSubsystem, STATGROUP_Tickables);
}

int32 UAbilityGraphWorldSubsystem::GetActiveExecutionCount() const
{
    return Executions.Num();
}
