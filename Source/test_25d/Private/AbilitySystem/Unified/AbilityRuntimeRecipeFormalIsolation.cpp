// 调用链：正式 Spec 的 PreActivate / 外部来源事务 -> 按租约筛选正式来源 -> 私有编译供施法或验证。
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Player/MyPlayerState.h"
#include "UObject/StrongObjectPtr.h"

bool UAbilityRuntimeRecipeDeveloperComponent::CreateFormalCastSnapshot(FGameplayTag AbilityTag,
                                                                       const UGameplayAbility* FormalAbility,
                                                                       UObject* Outer,
                                                                       FAbilityRuntimeRecipeSnapshot& Out,
                                                                       bool& bHasRecipe, FString& Error) const
{
    Out = {};
    Error.Reset();
    bHasRecipe = false;
    if (!DeveloperState.bPreviewActive || DeveloperState.AbilityTag != AbilityTag)
    {
        bHasRecipe = HasContributions(AbilityTag);
        return !bHasRecipe || CreateCastSnapshot(AbilityTag, Outer, Out, Error);
    }

    if (!CheckAuthority(false, Error))
    {
        return false;
    }
    const auto* Entry = Entries.Find(AbilityTag);
    if (!Entry || !IsValid(Entry->Base.Get()) || !IsValid(Outer) || !IsValid(FormalAbility) ||
        !FormalAbility->GetAssetTags().HasTagExact(AbilityTag))
    {
        Error = TEXT("An authoritative formal activation requires its ability identity and a live recipe source.");
        return false;
    }

    const int32 Revision = Entry->Revision;
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!CompileFormalRecipe(*Entry, FormalAbility, Outer, nullptr, Compiled, Error))
    {
        return false;
    }
    if (Compiled)
    {
        Out.AbilityTag = AbilityTag;
        Out.Revision = Revision;
        Out.Graph = Compiled;
        Out.ContentHash = AbilityRuntimeRecipes::Fingerprint(*Compiled);
        bHasRecipe = true;
    }
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::ValidateContributionForCurrentOwner(
    FGameplayTag AbilityTag, const FAbilityRuntimeRecipeContribution& Contribution, FString& Error) const
{
    if (!DeveloperState.bPreviewActive || DeveloperState.AbilityTag != AbilityTag)
    {
        return true;
    }
    // 只豁免明确归属本次预览的内部写入。组件广播重入的其他来源仍须走正式校验。
    if (OwnedSources.Contains(Contribution.SourceId))
    {
        if (bInternalEdit)
        {
            return true;
        }
        Error = TEXT("An external source cannot replace a workshop-owned contribution.");
        return false;
    }
    const auto* Entry = Entries.Find(AbilityTag);
    if (!Entry || !IsValid(Entry->Base.Get()))
    {
        Error = TEXT("The current recipe has no base for validating its formal sources.");
        return false;
    }

    const UGameplayAbility* FormalAbility = nullptr;
    const bool bNeedsFormalClass =
        !bHadPreviousEntry && AbilityRuntimeRecipes::Fingerprint(*Entry->Base) == PreviewBaseHash;
    if (bNeedsFormalClass)
    {
        // 关闭时只能为外部来源恢复一个基础。若需推导它，必须匹配正式 Spec 的唯一实际类。
        const auto* State = Cast<AMyPlayerState>(GetOwner());
        const auto* ASC = State ? State->GetAbilitySystemComponent() : nullptr;
        if (ASC)
        {
            for (const auto& Spec : ASC->GetActivatableAbilities())
            {
                if (!Spec.Ability || Spec.SourceObject.Get() == this ||
                    !Spec.Ability->GetAssetTags().HasTagExact(AbilityTag))
                {
                    continue;
                }
                if (FormalAbility && FormalAbility->GetClass() != Spec.Ability->GetClass())
                {
                    Error = TEXT("Close the workshop or register a formal base before adding a source to multiple "
                                 "formal class variants.");
                    return false;
                }
                FormalAbility = Spec.Ability.Get();
            }
        }
        // 若随后才授予不同正式类，用临时基础接受的来源可能无法在关闭时恢复。
        if (GraphPanelSessionId.IsValid() && !FormalAbility)
        {
            Error = TEXT("Close the graph workshop before adding this source without a formal ability or base.");
            return false;
        }
    }

    UAbilityGraphDefinition* Compiled = nullptr;
    if (!CompileFormalRecipe(*Entry, FormalAbility, GetTransientPackage(), &Contribution, Compiled, Error))
    {
        Error =
            TEXT("External contribution is incompatible with the formal recipe while the workshop is open. ") + Error;
        return false;
    }
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::CompileFormalRecipe(const FAbilityRuntimeRecipeEntry& Entry,
                                                                  const UGameplayAbility* FormalAbility, UObject* Outer,
                                                                  const FAbilityRuntimeRecipeContribution* Pending,
                                                                  UAbilityGraphDefinition*& Compiled,
                                                                  FString& Error) const
{
    Compiled = nullptr;
    // 从当前版本读取外部来源，保留预览打开之后新增、覆盖或撤销的正式修改。
    TArray<FAbilityRuntimeRecipeContribution> Contributions = Entry.Contributions;
    Contributions.RemoveAll(
        [this, Pending](const FAbilityRuntimeRecipeContribution& Contribution)
        {
            return OwnedSources.Contains(Contribution.SourceId) ||
                   (Pending && Contribution.SourceId == Pending->SourceId);
        });
    if (Pending)
    {
        Contributions.Add(*Pending);
    }
    if (Contributions.IsEmpty())
    {
        return true;
    }

    const bool bPreviewOwnsBase = AbilityRuntimeRecipes::Fingerprint(*Entry.Base) == PreviewBaseHash;
    const UAbilityGraphDefinition* Base = Entry.Base.Get();
    TStrongObjectPtr<UAbilityGraphDefinition> FormalDefault;
    if (bPreviewOwnsBase)
    {
        if (bHadPreviousEntry)
        {
            Base = PreviousEntry.Base.Get();
        }
        else if (FormalAbility)
        {
            // 预览为此 Tag 首次注册了基础图时，按正式 Spec 的实际类导出；同 Tag 的目录类可以不同。
            const auto* AbilityDefault = FormalAbility->GetClass()->GetDefaultObject<UGameplayAbility>();
            FormalDefault.Reset(AbilityRuntimeRecipes::BuildDefaultGraph(Outer, AbilityDefault, Error));
            if (!FormalDefault.IsValid())
            {
                return false;
            }
            Base = FormalDefault.Get();
        }
    }

    // 只生成私有候选。本次施法持有通过的快照，来源事务通过后仍由公共组件编译可见预览。
    return AbilityRuntimeRecipes::Compile(Outer, Base, Contributions, Compiled, Error);
}
