// 调用链：GA/图 -> 原 Nova 球形查询 -> 冻结目标生命 -> 共享 GE/Mass 伤害 -> 脚底 Cue。
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Interface/CombatInterface.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Equipment/EquipmentAbilityModifiers.h"
#include "MyGameplayTags.h"

bool UAbilityNovaNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Radius) || Radius <= 0.f)
    {
        Error = TEXT("Nova radius must be finite and positive.");
        return false;
    }
    if (EquipmentAbilityTag.IsValid() && EquipmentAbilityTag != FMyGameplayTags::Get().Abilities_Arcane_ArcaneNova)
    {
        Error = TEXT("Nova equipment rules require the exact Nova identity.");
        return false;
    }
    return true;
}

void AbilityNovaNodes::GatherActorTargets(AActor* Source, const FVector& Center, float Radius, TArray<AActor*>& Out)
{
    Out.Reset();
    if (!IsValid(Source) || !FMath::IsFinite(Radius) || Radius <= 0.f || Center.ContainsNaN())
    {
        return;
    }
    TArray<AActor*> Candidates;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Source, Candidates, {Source}, Radius, Center);
    for (auto* Target : Candidates)
    {
        if (IsValid(Target) && Target != Source && Target->Implements<UCombatInterface>() &&
            !ICombatInterface::Execute_IsDead(Target) && UMyAbilitySystemLibrary::IsNotFriend(Source, Target) &&
            FVector::DistSquared(Center, Target->GetActorLocation()) <= FMath::Square(Radius) &&
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
        {
            Out.AddUnique(Target);
        }
    }
}

bool UAbilityNovaNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                      const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityNovaNodeConfig>(&Config);
    FAbilityNodeInput Prepared;
    if (!AbilityNovaNodes::PrepareTargets(Owner, Settings, Input, Prepared, Error))
    {
        return false;
    }
    if (!bStopped && !Owner.IsFinished())
    {
        AbilityNovaNodes::ApplyPreparedDamage(Owner, Id, Prepared, &Input);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        AbilityNovaNodes::PlayPreparedCue(Prepared, Settings.CueTag);
        if (!bStopped && !Owner.IsFinished())
        {
            // 兼容整体适配器继续使用原输入完成合同；细节点版本另传冻结的空间参数。
            Owner.Publish(Id, TEXT("Completed"), Input);
        }
    }
    return true;
}

void AbilityNovaNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Node;
    Node.TypeId = TEXT("Shape.ArcaneNova");
    Node.ConfigClass = UAbilityNovaNodeConfig::StaticClass();
    Node.ExecutionClass = UAbilityNovaNodeExecution::StaticClass();
    Node.InputPayload = EAbilityPortPayload::Origin;
    Node.bSupportsActorTargets = Node.bSupportsMassTargets = Node.bSupportsSnapshotTargets = true;
    Node.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Node.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Node, Error), TEXT("Nova registration: %s"), *Error);
    }
}
