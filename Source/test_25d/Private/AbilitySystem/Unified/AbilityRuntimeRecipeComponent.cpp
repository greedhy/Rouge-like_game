// 调用链：PlayerState/开发工具 -> 来源事务 -> 编译 -> 每次激活快照；EndPlay 归还配置引用。
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
    /** 事务只在来源所有者的权威世界执行；客户端不能以复制配置代替服务器批准。 */
    bool CanEdit(const UAbilityRuntimeRecipeComponent& Component, FString& Error)
    {
        const AActor* Owner = Component.GetOwner();
        const UWorld* World = Component.GetWorld();
        if (!IsValid(Owner) || !Owner->HasAuthority() || !World || World->bIsTearingDown ||
            World->GetNetMode() == NM_Client)
        {
            Error = TEXT("Runtime recipe changes require a live authoritative owner and world.");
            return false;
        }
        return true;
    }

    /** 接受来源时复制可变配置；之后移除其他来源重新编译不能读到调用者改写的新参数。 */
    FAbilityRuntimeRecipeContribution OwnContribution(UObject* Outer, const FAbilityRuntimeRecipeContribution& Input)
    {
        FAbilityRuntimeRecipeContribution Copy = Input;
        for (auto& Patch : Copy.Patches)
        {
            if (Patch.ReplacementConfig)
            {
                Patch.ReplacementConfig = DuplicateObject<UAbilityNodeConfig>(Patch.ReplacementConfig, Outer);
            }
            if (Patch.BranchGraph)
            {
                Patch.BranchGraph = DuplicateObject<UAbilityGraphDefinition>(Patch.BranchGraph, Outer);
            }
        }
        return Copy;
    }
} // namespace

UAbilityRuntimeRecipeComponent::UAbilityRuntimeRecipeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UAbilityRuntimeRecipeComponent::ValidateContributionForCurrentOwner(FGameplayTag,
                                                                         const FAbilityRuntimeRecipeContribution&,
                                                                         FString&) const
{
    return true;
}

bool UAbilityRuntimeRecipeComponent::RegisterBase(FGameplayTag AbilityTag, const UAbilityGraphDefinition* Base,
                                                  FString& Error)
{
    Error.Reset();
    if (!CanEdit(*this, Error) || !AbilityTag.IsValid() || !IsValid(Base))
    {
        if (Error.IsEmpty())
        {
            Error = TEXT("A registered ability tag and base definition are required.");
        }
        LastError = Error;
        return false;
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    TArray<FString> Validation;
    if (!Base->Validate(Registry, Validation))
    {
        Error = FString::Join(Validation, TEXT("; "));
        LastError = Error;
        return false;
    }
    const auto* Existing = Entries.Find(AbilityTag);
    if (Existing && Existing->Base &&
        AbilityRuntimeRecipes::Fingerprint(*Existing->Base) == AbilityRuntimeRecipes::Fingerprint(*Base))
    {
        LastError.Reset();
        return true;
    }
    FAbilityRuntimeRecipeEntry Candidate;
    if (Existing)
    {
        Candidate = *Existing;
    }
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!AbilityRuntimeRecipes::Compile(this, Base, Candidate.Contributions, Compiled, Error))
    {
        LastError = Error;
        return false;
    }
    Candidate.Base = DuplicateObject<UAbilityGraphDefinition>(Base, this);
    Candidate.Compiled = Compiled;
    ++Candidate.Revision;
    Entries.Add(AbilityTag, MoveTemp(Candidate));
    LastError.Reset();
    if (!bSuppressRecipeNotifications)
    {
        OnRecipeChanged.Broadcast(AbilityTag, GetRevision(AbilityTag));
    }
    return true;
}

bool UAbilityRuntimeRecipeComponent::SetContribution(FGameplayTag AbilityTag,
                                                     const FAbilityRuntimeRecipeContribution& Contribution,
                                                     FString& Error)
{
    Error.Reset();
    if (!CanEdit(*this, Error))
    {
        LastError = Error;
        return false;
    }
    const auto* Existing = Entries.Find(AbilityTag);
    if (!Existing || Contribution.SourceId.IsNone())
    {
        Error = TEXT("A registered base and nonempty contribution SourceId are required.");
        LastError = Error;
        return false;
    }
    if (!ValidateContributionForCurrentOwner(AbilityTag, Contribution, Error))
    {
        LastError = Error;
        return false;
    }
    FAbilityRuntimeRecipeEntry Candidate = *Existing;
    Candidate.Contributions.RemoveAll(
        [&Contribution](const FAbilityRuntimeRecipeContribution& Value)
        {
            return Value.SourceId == Contribution.SourceId;
        });
    Candidate.Contributions.Add(OwnContribution(this, Contribution));
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!AbilityRuntimeRecipes::Compile(this, Candidate.Base, Candidate.Contributions, Compiled, Error))
    {
        LastError = Error;
        return false;
    }
    // 只有新定义整体合法才替换当前版本，失败时 Entries 与已经发出的快照均保持原状。
    Candidate.Compiled = Compiled;
    ++Candidate.Revision;
    Entries.Add(AbilityTag, MoveTemp(Candidate));
    LastError.Reset();
    OnRecipeChanged.Broadcast(AbilityTag, GetRevision(AbilityTag));
    return true;
}

bool UAbilityRuntimeRecipeComponent::RemoveContribution(FGameplayTag AbilityTag, FName SourceId, FString& Error)
{
    Error.Reset();
    if (!CanEdit(*this, Error))
    {
        LastError = Error;
        return false;
    }
    const auto* Existing = Entries.Find(AbilityTag);
    if (!Existing || SourceId.IsNone())
    {
        Error = TEXT("A registered base and nonempty SourceId are required for removal.");
        LastError = Error;
        return false;
    }
    FAbilityRuntimeRecipeEntry Candidate = *Existing;
    const int32 Removed = Candidate.Contributions.RemoveAll(
        [SourceId](const FAbilityRuntimeRecipeContribution& Value)
        {
            return Value.SourceId == SourceId;
        });
    if (Removed == 0)
    {
        LastError.Reset();
        return true;
    }
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!AbilityRuntimeRecipes::Compile(this, Candidate.Base, Candidate.Contributions, Compiled, Error))
    {
        LastError = Error;
        return false;
    }
    Candidate.Compiled = Compiled;
    ++Candidate.Revision;
    Entries.Add(AbilityTag, MoveTemp(Candidate));
    LastError.Reset();
    OnRecipeChanged.Broadcast(AbilityTag, GetRevision(AbilityTag));
    return true;
}

bool UAbilityRuntimeRecipeComponent::CreateCastSnapshot(FGameplayTag AbilityTag, UObject* Outer,
                                                        FAbilityRuntimeRecipeSnapshot& Out, FString& Error) const
{
    Out = {};
    Error.Reset();
    const auto* Entry = Entries.Find(AbilityTag);
    if (!Entry || !Entry->Compiled || !IsValid(Outer))
    {
        Error = TEXT("An accepted recipe and live cast snapshot owner are required.");
        return false;
    }
    Out.AbilityTag = AbilityTag;
    Out.Revision = Entry->Revision;
    Out.Graph = DuplicateObject<UAbilityGraphDefinition>(Entry->Compiled, Outer);
    Out.Graph->SetFlags(RF_Transient);
    Out.ContentHash = AbilityRuntimeRecipes::Fingerprint(*Out.Graph);
    return true;
}

bool UAbilityRuntimeRecipeComponent::HasContributions(FGameplayTag AbilityTag) const
{
    const auto* Entry = Entries.Find(AbilityTag);
    return Entry && !Entry->Contributions.IsEmpty();
}

int32 UAbilityRuntimeRecipeComponent::GetRevision(FGameplayTag AbilityTag) const
{
    const auto* Entry = Entries.Find(AbilityTag);
    return Entry ? Entry->Revision : 0;
}

FString UAbilityRuntimeRecipeComponent::GetLastError() const
{
    return LastError;
}

void UAbilityRuntimeRecipeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Entries.Reset();
    LastError.Reset();
    OnRecipeChanged.Clear();
    Super::EndPlay(EndPlayReason);
}
