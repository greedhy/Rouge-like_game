// 调用链：可信 ID RPC -> BeginPreview/ApplyEdit/EndPreview -> 生产配方组件及原 ASC。
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "Player/MyPlayerState.h"
#include "Character/PlayerCharacter.h"
#include "MyGameplayTags.h"
#include "Misc/SecureHash.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Interface/CombatInterface.h"

namespace
{
    /** 直接 PIE 没有经过职业选择；只凭本玩家 ASC 的真实法师 Avatar 提供开发预览，不写职业或存档。 */
    bool HasUnselectedMageAvatar(const AMyPlayerState& State)
    {
        if (State.Profession != EPlayerProfession::Unselected)
        {
            return false;
        }
        const auto* ASC = State.GetAbilitySystemComponent();
        AActor* Avatar = ASC ? ASC->GetAvatarActor() : nullptr;
        const auto* Player = Cast<APlayerCharacter>(Avatar);
        return IsValid(Player) && !Player->IsActorBeingDestroyed() && ASC->GetOwnerActor() == &State &&
               Player->GetWorld() == State.GetWorld() && Player->GetPlayerState<AMyPlayerState>() == &State &&
               Player->GetAbilitySystemComponent() == ASC &&
               ICombatInterface::Execute_GetCharacterClass(Avatar) == ECharacterClass::Elementalist;
    }

    /** 来源身份由本次服务器租约和可信编辑键决定；不允许客户端指定别人的SourceId。 */
    FName OwnedEditSource(FGuid Preview, const FAbilityRuntimeDeveloperEdit& Edit)
    {
        FString Key = FString::Printf(TEXT("DeveloperRecipe.%s.%d.%s"), *Preview.ToString(), Edit.NodeIndex,
                                      *Edit.ModifierId.ToString());
        if (!Edit.FragmentId.IsNone() || !Edit.OutputPortId.IsNone())
        {
            Key += TEXT(".") + Edit.FragmentId.ToString() + TEXT(".") + Edit.OutputPortId.ToString();
        }
        return FName(*Key);
    }
} // namespace

UAbilityRuntimeRecipeDeveloperComponent::UAbilityRuntimeRecipeDeveloperComponent()
{
    SetIsReplicatedByDefault(true);
}

bool UAbilityRuntimeRecipeDeveloperComponent::InitializeCatalog(const UAbilityInfo* Preferred, FString& Error)
{
    Error.Reset();
    TArray<FAbilityRuntimeBaseDefinition> Collected;
    TArray<FString> Diagnostics;
    if (!Preferred && GetOwner())
    {
        Preferred = UMyAbilitySystemLibrary::GetAbilityInfo(GetOwner());
    }
    if (!AbilityRuntimeRecipeCatalog::CollectBaseDefinitions(this, Preferred, Collected, Diagnostics))
    {
        Error = FString::Join(Diagnostics, TEXT("; "));
        return false;
    }
    TArray<FAbilityRuntimeDeveloperDefinition> Candidate;
    TSet<FName> Ids;
    for (const auto& Item : Collected)
    {
        FAbilityRuntimeDeveloperDefinition Row;
        Row.Base = Item;
        const FString ClassPath = GetPathNameSafe(Item.AbilityClass.Get());
        Row.DefinitionId = FName(*(Item.AbilityTag.ToString() + TEXT("|") + FMD5::HashAnsiString(*ClassPath)));
        if (Ids.Contains(Row.DefinitionId))
        {
            Error = TEXT("Concrete mage catalog IDs collide; catalog was not replaced.");
            return false;
        }
        Ids.Add(Row.DefinitionId);
        if (Item.DefaultGraph)
        {
            Row.BaseHash = AbilityRuntimeRecipes::Fingerprint(*Item.DefaultGraph);
        }
        Candidate.Add(MoveTemp(Row));
    }
    Definitions = MoveTemp(Candidate);
    return true;
}

const TArray<FAbilityRuntimeDeveloperDefinition>& UAbilityRuntimeRecipeDeveloperComponent::GetDefinitions() const
{
    return Definitions;
}

bool UAbilityRuntimeRecipeDeveloperComponent::BeginPreview(FName Id, FString& Error)
{
    Error.Reset();
    BindRecipeChanges();
    if (!CheckAuthority(true, Error))
    {
        return Reject(Error, Error);
    }
    if (bLifecycleCleanup)
    {
        return Reject(TEXT("Previous avatar preview is still being released."), Error);
    }
    BindAvatarChanges();
    if (Definitions.IsEmpty() && !InitializeCatalog(nullptr, Error))
    {
        return Reject(Error, Error);
    }
    const auto* Definition = Definitions.FindByPredicate(
        [Id](const FAbilityRuntimeDeveloperDefinition& Value)
        {
            return Value.DefinitionId == Id;
        });
    if (!Definition || !Definition->Base.bSelectable || !Definition->Base.DefaultGraph)
    {
        return Reject(TEXT("Choose a registered combat definition with a validated runtime base graph."), Error);
    }
    if (DeveloperState.bPreviewActive)
    {
        if (DeveloperState.SelectedId == Id)
        {
            PublishState();
            return true;
        }
        return Reject(TEXT("End the current preview before selecting another concrete frontend."), Error);
    }
    auto* State = CastChecked<AMyPlayerState>(GetOwner());
    auto* ASC = Cast<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    const auto Slot = FGameplayTag::RequestGameplayTag(TEXT("InputTag.DeveloperRecipe"), false);
    if (!ASC || !Slot.IsValid())
    {
        return Reject(TEXT("Player ASC or registered developer input slot is unavailable."), Error);
    }
    for (const auto& Spec : ASC->GetActivatableAbilities())
    {
        if (Spec.DynamicAbilityTags.HasTagExact(Slot))
        {
            return Reject(TEXT("Developer input slot already belongs to another grant; no slot was overwritten."),
                          Error);
        }
    }
    const auto Tag = Definition->Base.AbilityTag;
    InvalidateGraphDraft();
    NextGraphEditSequence = 0;
    const auto* Existing = Entries.Find(Tag);
    const bool bHadEntry = Existing != nullptr;
    FAbilityRuntimeRecipeEntry Prior = Existing ? *Existing : FAbilityRuntimeRecipeEntry{};
    bool bBaseRegistered = false;
    {
        TGuardValue<bool> Guard(bInternalEdit, true);
        TGuardValue<bool> QuietBase(bSuppressRecipeNotifications, true);
        bBaseRegistered = RegisterBase(Tag, Definition->Base.DefaultGraph, Error);
    }
    if (!bBaseRegistered)
    {
        return Reject(Error, Error);
    }
    // GiveAbility可触发同步授予回调；先建立隔离视图，正式同标签激活不会读取临时基础图。
    PreviousEntry = Prior;
    bHadPreviousEntry = bHadEntry;
    PreviewBaseHash = Definition->BaseHash;
    DeveloperState.bPreviewActive = true;
    DeveloperState.SelectedId = Id;
    DeveloperState.AbilityTag = Tag;
    // GiveAbility 不激活技能；动作/目标/费用仍由该 Spec 的原前端收到真实输入后执行。
    FGameplayAbilitySpec Spec(Definition->Base.AbilityClass, 1, INDEX_NONE, this);
    Spec.DynamicAbilityTags.AddTag(Slot);
    Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
    const auto Handle = ASC->GiveAbility(Spec);
    if (!Handle.IsValid())
    {
        DeveloperState.bPreviewActive = false;
        PreviousEntry = {};
        bHadPreviousEntry = false;
        PreviewBaseHash.Reset();
        if (bHadEntry)
        {
            Entries.Add(Tag, MoveTemp(Prior));
        }
        else
        {
            Entries.Remove(Tag);
        }
        OnRecipeChanged.Broadcast(Tag, GetRevision(Tag));
        return Reject(TEXT("ASC refused temporary grant; previous recipe was restored."), Error);
    }
    PreviousEntry = MoveTemp(Prior);
    bHadPreviousEntry = bHadEntry;
    PreviewHandle = Handle;
    PreviewId = FGuid::NewGuid();
    PreviewBaseHash = Definition->BaseHash;
    DeveloperState.bPreviewActive = true;
    DeveloperState.SelectedId = Id;
    DeveloperState.AbilityTag = Tag;
    BindPreviewAvatar(ASC->GetAvatarActor());
    PublishState();
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::ApplyEdit(const FAbilityRuntimeDeveloperEdit& Edit, FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(true, Error))
    {
        return Reject(Error, Error);
    }
    const auto* Entry = Entries.Find(DeveloperState.AbilityTag);
    if (bCardPanel || !DeveloperState.bPreviewActive || !Entry || !Entry->Base)
    {
        return Reject(TEXT("Start a preview before changing its recipe."), Error);
    }
    if (Edit.DefinitionId != DeveloperState.SelectedId)
    {
        return Reject(
            TEXT("Selected definition differs from the active preview. Start that definition before editing it."),
            Error);
    }
    const FString CurrentBaseHash = AbilityRuntimeRecipes::Fingerprint(*Entry->Base);
    if (Edit.ExpectedRevision != Entry->Revision || Edit.ExpectedBaseHash != CurrentBaseHash ||
        CurrentBaseHash != PreviewBaseHash)
    {
        return Reject(TEXT("Recipe version/base changed; refresh before editing, or abandon this preview."), Error);
    }
    if (!Entry->Base->Nodes.IsValidIndex(Edit.NodeIndex) || !FMath::IsFinite(Edit.Value))
    {
        return Reject(TEXT("Node index and a finite parameter value are required."), Error);
    }
    FAbilityRuntimeRecipePatch Patch;
    if (!AbilityRuntimeModifierTemplates::BuildPatch(this, Entry->Base->Nodes[Edit.NodeIndex], Edit, Patch, Error,
                                                     Entry->Base.Get()))
    {
        return Reject(Error, Error);
    }
    FAbilityRuntimeRecipeContribution Contribution;
    Contribution.SourceId = OwnedEditSource(PreviewId, Edit);
    Contribution.Patches.Add(Patch);
    const bool bSourceAlreadyOwned = OwnedSources.Contains(Contribution.SourceId);
    const FGuid ApplyingPreview = PreviewId;
    OwnedSources.AddUnique(Contribution.SourceId);
    bool bAccepted = false;
    {
        TGuardValue<bool> Guard(bInternalEdit, true);
        bAccepted = SetContribution(DeveloperState.AbilityTag, Contribution, Error);
    }
    if (!bAccepted)
    {
        if (!bSourceAlreadyOwned && PreviewId == ApplyingPreview)
        {
            OwnedSources.Remove(Contribution.SourceId);
        }
        return Reject(Error, Error);
    }
    if (PreviewId != ApplyingPreview || !DeveloperState.bPreviewActive)
    {
        Error = TEXT("The preview closed during its edit notification; its owned resources were released.");
        return false;
    }
    OwnedSources.AddUnique(Contribution.SourceId);
    InvalidateGraphDraft();
    PublishState();
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::RemoveEdit(const FAbilityRuntimeDeveloperEdit& Edit, FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(true, Error))
    {
        return Reject(Error, Error);
    }
    const auto Tag = DeveloperState.AbilityTag;
    const auto* Current = Entries.Find(Tag);
    const FName Source = OwnedEditSource(PreviewId, Edit);
    if (!DeveloperState.bPreviewActive || !Current || !Current->Base ||
        Edit.DefinitionId != DeveloperState.SelectedId || Edit.ExpectedRevision != Current->Revision ||
        Edit.ExpectedBaseHash != PreviewBaseHash ||
        AbilityRuntimeRecipes::Fingerprint(*Current->Base) != PreviewBaseHash || !OwnedSources.Contains(Source))
    {
        return Reject(TEXT("Remove requires the current definition/base/revision and an edit owned by this preview."),
                      Error);
    }
    auto Candidate = *Current;
    Candidate.Contributions.RemoveAll(
        [Source](const FAbilityRuntimeRecipeContribution& Value)
        {
            return Value.SourceId == Source;
        });
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!AbilityRuntimeRecipes::Compile(this, Candidate.Base, Candidate.Contributions, Compiled, Error))
    {
        return Reject(Error, Error);
    }
    Candidate.Compiled = Compiled;
    ++Candidate.Revision;
    {
        TGuardValue<bool> Guard(bInternalEdit, true);
        Entries.Add(Tag, MoveTemp(Candidate));
        OwnedSources.Remove(Source);
        OnRecipeChanged.Broadcast(Tag, GetRevision(Tag));
    }
    // 只替换下一次施法的配置；不清Spec、不取消OwnerLease，也不修改已经在途的强持有快照。
    InvalidateGraphDraft();
    PublishState();
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::ResetPreview(FName DefinitionId, int32 ExpectedRevision,
                                                           const FString& ExpectedBaseHash, FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(true, Error))
    {
        return Reject(Error, Error);
    }
    const auto Tag = DeveloperState.AbilityTag;
    const auto* Current = Entries.Find(Tag);
    if (!DeveloperState.bPreviewActive || !Current || !Current->Base || DefinitionId != DeveloperState.SelectedId ||
        ExpectedRevision != Current->Revision || ExpectedBaseHash != PreviewBaseHash ||
        AbilityRuntimeRecipes::Fingerprint(*Current->Base) != PreviewBaseHash)
    {
        return Reject(TEXT("Reset requires the current preview definition/base/revision."), Error);
    }
    if (OwnedSources.IsEmpty())
    {
        InvalidateGraphDraft();
        PublishState();
        return true;
    }
    auto Candidate = *Current;
    Candidate.Contributions.RemoveAll(
        [this](const FAbilityRuntimeRecipeContribution& Value)
        {
            return OwnedSources.Contains(Value.SourceId);
        });
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!AbilityRuntimeRecipes::Compile(this, Candidate.Base, Candidate.Contributions, Compiled, Error))
    {
        return Reject(Error, Error);
    }
    Candidate.Compiled = Compiled;
    ++Candidate.Revision;
    {
        TGuardValue<bool> Guard(bInternalEdit, true);
        Entries.Add(Tag, MoveTemp(Candidate));
        OwnedSources.Reset();
        OnRecipeChanged.Broadcast(Tag, GetRevision(Tag));
    }
    // 外部来源继续保留；预览授予、费用和在途图均不归“重置编辑”回收。
    InvalidateGraphDraft();
    PublishState();
    return true;
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerRemoveEdit_Implementation(const FAbilityRuntimeDeveloperEdit& Edit)
{
    FString Error;
    if ((GraphPanelSessionId.IsValid() && Edit.ExpectedPreviewSessionId != GraphPanelSessionId) ||
        (!GraphPanelSessionId.IsValid() && Edit.ExpectedPreviewSessionId.IsValid() &&
         Edit.ExpectedPreviewSessionId != PreviewId))
    {
        Reject(TEXT("The base-index removal belongs to a different graph panel session."), Error);
        return;
    }
    RemoveEdit(Edit, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerResetPreview_Implementation(FName DefinitionId,
                                                                                int32 ExpectedRevision,
                                                                                const FString& ExpectedBaseHash)
{
    FString Error;
    if (GraphPanelSessionId.IsValid())
    {
        Reject(TEXT("Reset requires the exact graph panel session."), Error);
        return;
    }
    ResetPreview(DefinitionId, ExpectedRevision, ExpectedBaseHash, Error);
}

bool UAbilityRuntimeRecipeDeveloperComponent::EndPreview(FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(false, Error))
    {
        return Reject(Error, Error);
    }
    if (!DeveloperState.bPreviewActive)
    {
        return true;
    }
    const auto Tag = DeveloperState.AbilityTag;
    const auto* Current = Entries.Find(Tag);
    if (!Current || !Current->Base || AbilityRuntimeRecipes::Fingerprint(*Current->Base) != PreviewBaseHash)
    {
        return Reject(TEXT("Another system changed the base. Keep that change and abandon preview to exit safely."),
                      Error);
    }
    FAbilityRuntimeRecipeEntry Candidate = *Current;
    Candidate.Contributions.RemoveAll(
        [this](const FAbilityRuntimeRecipeContribution& Value)
        {
            return OwnedSources.Contains(Value.SourceId);
        });
    // 不回拷整个旧 Entry：预览期间外部来源的新增、修改与撤销都必须保留。
    if (bHadPreviousEntry)
    {
        Candidate.Base = PreviousEntry.Base;
    }
    else if (!Candidate.Contributions.IsEmpty())
    {
        // 正式授予也可能在预览打开后出现；保留外部来源时仍应按当前正式类恢复基础。
        const auto* State = Cast<AMyPlayerState>(GetOwner());
        const auto* ASC = State ? State->GetAbilitySystemComponent() : nullptr;
        const UGameplayAbility* FormalDefault = nullptr;
        if (ASC)
        {
            for (const auto& Spec : ASC->GetActivatableAbilities())
            {
                if (!Spec.Ability || Spec.SourceObject.Get() == this || !Spec.Ability->GetAssetTags().HasTagExact(Tag))
                {
                    continue;
                }
                if (FormalDefault && FormalDefault->GetClass() != Spec.Ability->GetClass())
                {
                    return Reject(TEXT("Formal class variants conflict; resolve their base before closing."), Error);
                }
                FormalDefault = Spec.Ability->GetClass()->GetDefaultObject<UGameplayAbility>();
            }
        }
        if (FormalDefault)
        {
            Candidate.Base = AbilityRuntimeRecipes::BuildDefaultGraph(this, FormalDefault, Error);
            if (!Candidate.Base)
            {
                return Reject(Error, Error);
            }
        }
    }
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!AbilityRuntimeRecipes::Compile(this, Candidate.Base, Candidate.Contributions, Compiled, Error))
    {
        return Reject(Error, Error);
    }
    Candidate.Compiled = Compiled;
    ++Candidate.Revision;
    if (!bHadPreviousEntry && Candidate.Contributions.IsEmpty())
    {
        Entries.Remove(Tag);
    }
    else
    {
        Entries.Add(Tag, MoveTemp(Candidate));
    }
    ReleasePreviewGrant();
    OnRecipeChanged.Broadcast(Tag, GetRevision(Tag));
    PublishState();
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::AbandonPreview(FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(false, Error))
    {
        return Reject(Error, Error);
    }
    if (!DeveloperState.bPreviewActive)
    {
        return true;
    }
    const auto Tag = DeveloperState.AbilityTag;
    if (const auto* Current = Entries.Find(Tag);
        Current && Current->Base && AbilityRuntimeRecipes::Fingerprint(*Current->Base) == PreviewBaseHash)
    {
        // 没有外部基础冲突时“放弃”也必须恢复正式类，不能把临时目录版本留给正式施法。
        return EndPreview(Error);
    }
    if (const auto* Current = Entries.Find(Tag))
    {
        FAbilityRuntimeRecipeEntry Candidate = *Current;
        const int32 Removed = Candidate.Contributions.RemoveAll(
            [this](const FAbilityRuntimeRecipeContribution& Value)
            {
                return OwnedSources.Contains(Value.SourceId);
            });
        if (Removed > 0)
        {
            UAbilityGraphDefinition* Compiled = nullptr;
            if (!AbilityRuntimeRecipes::Compile(this, Candidate.Base, Candidate.Contributions, Compiled, Error))
            {
                return Reject(Error, Error);
            }
            // 明确保留当前外部基础，不借放弃操作恢复 Preview 前保存的旧图。
            Candidate.Compiled = Compiled;
            ++Candidate.Revision;
            Entries.Add(Tag, MoveTemp(Candidate));
        }
    }
    ReleasePreviewGrant();
    OnRecipeChanged.Broadcast(Tag, GetRevision(Tag));
    PublishState();
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::OwnsTemporaryAbility(FGameplayAbilitySpecHandle Handle) const
{
    return Handle.IsValid() && Handle == PreviewHandle;
}

FGameplayAbilitySpecHandle UAbilityRuntimeRecipeDeveloperComponent::GetPreviewHandle() const
{
    return PreviewHandle;
}

FGuid UAbilityRuntimeRecipeDeveloperComponent::GetPreviewOwnerLease(FGameplayAbilitySpecHandle Handle) const
{
    return DeveloperState.bPreviewActive && OwnsTemporaryAbility(Handle) ? PreviewId : FGuid();
}

const FAbilityRuntimeDeveloperState& UAbilityRuntimeRecipeDeveloperComponent::GetDeveloperState() const
{
    return DeveloperState;
}

int32 UAbilityRuntimeRecipeDeveloperComponent::GetOwnerPlayerId() const
{
    const auto* State = Cast<APlayerState>(GetOwner());
    return State ? State->GetPlayerId() : INDEX_NONE;
}

bool UAbilityRuntimeRecipeDeveloperComponent::IsPreviewFrontendIdle() const
{
    const auto* State = Cast<AMyPlayerState>(GetOwner());
    const auto* ASC = State ? State->GetAbilitySystemComponent() : nullptr;
    const FGameplayTag Slot = FGameplayTag::RequestGameplayTag(TEXT("InputTag.DeveloperRecipe"), false);
    if (!DeveloperState.bPreviewActive || !ASC || !Slot.IsValid())
    {
        return false;
    }
    const FGameplayAbilitySpec* OwnedSpec = nullptr;
    for (const auto& Spec : ASC->GetActivatableAbilities())
    {
        if (!Spec.DynamicAbilityTags.HasTagExact(Slot))
        {
            continue;
        }
        // 客户端PreviewHandle不复制；等待准确SourceObject/slot的Spec实际到达，不能从空句柄推断空闲。
        // 同槽出现外部或重复授予时也拒绝；本查询不修复、不移除任何Spec。
        if (OwnedSpec || Spec.SourceObject.Get() != this || !Spec.Ability || Spec.PendingRemove)
        {
            return false;
        }
        OwnedSpec = &Spec;
    }
    if (!OwnedSpec || OwnedSpec->IsActive())
    {
        return false;
    }
    const auto* Ability = OwnedSpec->Ability.Get();
    if (const auto* Tags = Ability->GetCooldownTags(); Tags && ASC->HasAnyMatchingGameplayTags(*Tags))
    {
        return false;
    }
    // 原型前端自定义CheckCooldown读取根配置Tag，并不保证原生GetCooldownTags返回它。
    if (const auto* Prototype = Cast<UPrototypeAbility>(Ability);
        Prototype && Prototype->Prototype && Prototype->Prototype->CooldownTag.IsValid() &&
        ASC->HasMatchingGameplayTag(Prototype->Prototype->CooldownTag))
    {
        return false;
    }
    // ActiveCount不是复制字段；这只是当前世界的只读门，远端QA同时轮询权威端，实际输入仍需CanActivate。
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::ObserveExecution(UAbilityGraphInstance* Run,
                                                               const FAbilitySourceToken& Source,
                                                               FGameplayTag RecipeTag, int32 FrozenRevision,
                                                               const FString& FrozenHash, FString& Error,
                                                               FGuid RootCastId)
{
    Error.Reset();
    if (!CheckObservationSource(Source, Error))
    {
        return false;
    }
    PruneObservations();
    if (!IsValid(Run) || !RecipeTag.IsValid() || FrozenRevision < 0 || FrozenHash.IsEmpty() || FrozenHash.Len() > 128 ||
        ObservedRuns.Contains(Run) || ObservedRuns.Num() >= 16 ||
        (Run->GetExecutionId().IsValid() && Run->IsFinished()))
    {
        Error = TEXT(
            "Observation needs an unobserved owned run, valid frozen metadata, and an available slot (maximum 16).");
        return false;
    }
    FObservedRun Observation;
    Observation.Source = Source;
    Observation.AbilityTag = RecipeTag;
    Observation.Revision = FrozenRevision;
    Observation.FrozenHash = FrozenHash;
    Observation.RootCastId = RootCastId;
    const TWeakObjectPtr<UAbilityGraphInstance> WeakRun(Run);
    Observation.EventHandle = Run->OnEvent.AddWeakLambda(this,
                                                         [this, WeakRun](const FAbilityGraphEvent& Event)
                                                         {
                                                             if (auto* Current = WeakRun.Get())
                                                             {
                                                                 GraphEvent(Current, Event);
                                                             }
                                                         });
    Observation.FinishedHandle = Run->OnFinished.AddWeakLambda(this,
                                                               [this, WeakRun](EAbilityGraphEndReason Reason)
                                                               {
                                                                   if (auto* Current = WeakRun.Get())
                                                                   {
                                                                       GraphFinished(Current, Reason);
                                                                   }
                                                               });
    ObservedRuns.Add(WeakRun, MoveTemp(Observation));
    return true;
}

void UAbilityRuntimeRecipeDeveloperComponent::ForgetExecution(UAbilityGraphInstance* Run)
{
    if (auto* Observation = ObservedRuns.Find(Run))
    {
        if (IsValid(Run))
        {
            Run->OnEvent.Remove(Observation->EventHandle);
            Run->OnFinished.Remove(Observation->FinishedHandle);
        }
        ObservedRuns.Remove(Run);
    }
}

bool UAbilityRuntimeRecipeDeveloperComponent::RecordCost(const FAbilitySourceToken& Source,
                                                         const FAbilityRuntimeDeveloperCost& Cost, FString& Error)
{
    Error.Reset();
    if (!CheckObservationSource(Source, Error))
    {
        return false;
    }
    if (!Cost.RootCastId.IsValid() || !Cost.AbilityTag.IsValid() || Cost.Revision < 0 || Cost.FrozenHash.IsEmpty() ||
        Cost.FrozenHash.Len() > 128 || Cost.Stage.IsNone() || !FMath::IsFinite(Cost.ResourceBefore) ||
        !FMath::IsFinite(Cost.ResourceAfter))
    {
        Error =
            TEXT("Cost observation requires a cast identity, frozen recipe, stage and finite actual resource values.");
        return false;
    }
    FAbilityRuntimeDeveloperObservation Record;
    Record.Kind = EAbilityRuntimeObservationKind::Cost;
    Record.RootCastId = Cost.RootCastId;
    Record.AbilityTag = Cost.AbilityTag;
    Record.Revision = Cost.Revision;
    Record.FrozenHash = Cost.FrozenHash;
    Record.Stage = Cost.Stage;
    Record.bCostSucceeded = Cost.bSuccess;
    Record.ResourceBefore = Cost.ResourceBefore;
    Record.ResourceAfter = Cost.ResourceAfter;
    AppendObservation(MoveTemp(Record));
    return true;
}

const TArray<FAbilityRuntimeDeveloperObservation>& UAbilityRuntimeRecipeDeveloperComponent::GetObservations() const
{
    return Observations;
}

int32 UAbilityRuntimeRecipeDeveloperComponent::GetObservedExecutionCount() const
{
    int32 Count = 0;
    for (const auto& Item : ObservedRuns)
    {
        const auto* Run = Item.Key.Get();
        if (Run && (!Run->GetExecutionId().IsValid() || !Run->IsFinished()))
        {
            ++Count;
        }
    }
    return Count;
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerBeginPreview_Implementation(FName Id)
{
    FString Error;
    BeginPreview(Id, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerApplyEdit_Implementation(const FAbilityRuntimeDeveloperEdit& Edit)
{
    FString Error;
    if ((GraphPanelSessionId.IsValid() && Edit.ExpectedPreviewSessionId != GraphPanelSessionId) ||
        (!GraphPanelSessionId.IsValid() && Edit.ExpectedPreviewSessionId.IsValid() &&
         Edit.ExpectedPreviewSessionId != PreviewId))
    {
        Reject(TEXT("The base-index edit belongs to a different graph panel session."), Error);
        return;
    }
    ApplyEdit(Edit, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerEndPreview_Implementation()
{
    FString Error;
    if (GraphPanelSessionId.IsValid())
    {
        Reject(TEXT("Graph previews require their exact session when closing."), Error);
        return;
    }
    EndPreview(Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerAbandonPreview_Implementation()
{
    FString Error;
    if (GraphPanelSessionId.IsValid())
    {
        Reject(TEXT("Graph previews require their exact session when abandoning."), Error);
        return;
    }
    AbandonPreview(Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerClosePreview_Implementation(FGuid RequestId, bool bAbandon)
{
    FString Error;
    bool bSucceeded = false;
    if (DeveloperState.bPreviewActive && GraphPanelSessionId.IsValid())
    {
        Reject(TEXT("This graph preview requires its exact panel session when closing."), Error);
    }
    else if (!RequestId.IsValid())
    {
        Reject(TEXT("Close request needs a nonempty request identity."), Error);
    }
    else
    {
        bSucceeded = bAbandon ? AbandonPreview(Error) : EndPreview(Error);
        if (bSucceeded)
        {
            // 幂等关闭也发新回执，不能因状态没有变化而令远端界面永久等待。
            PublishState();
        }
    }
    ClientClosePreviewAcknowledged(RequestId, bSucceeded, DeveloperState);
}

void UAbilityRuntimeRecipeDeveloperComponent::ClientClosePreviewAcknowledged_Implementation(
    FGuid RequestId, bool bSucceeded, const FAbilityRuntimeDeveloperState& Snapshot)
{
    // 只读回执在真正所属客户端收到RPC时更新；状态复制或普通EndPreview不得伪造这个RequestId。
    LastCloseAcknowledgedId = RequestId;
    bLastCloseSucceeded = bSucceeded;
    if (Snapshot.StateSequence >= DeveloperState.StateSequence &&
        Snapshot.StateSequence >= LastAcceptedState.StateSequence)
    {
        DeveloperState = Snapshot;
        LastAcceptedState = Snapshot;
        OnDeveloperStateChanged.Broadcast();
    }
    OnPreviewCloseAcknowledged.Broadcast(RequestId, bSucceeded);
}

void UAbilityRuntimeRecipeDeveloperComponent::OnRep_Observations()
{
    OnDeveloperStateChanged.Broadcast();
}

void UAbilityRuntimeRecipeDeveloperComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UAbilityRuntimeRecipeDeveloperComponent, DeveloperState, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UAbilityRuntimeRecipeDeveloperComponent, Observations, COND_OwnerOnly);
}

void UAbilityRuntimeRecipeDeveloperComponent::OnRep_DeveloperState()
{
    if (DeveloperState.StateSequence < LastAcceptedState.StateSequence)
    {
        DeveloperState = LastAcceptedState;
    }
    else
    {
        LastAcceptedState = DeveloperState;
    }
    OnDeveloperStateChanged.Broadcast();
}

void UAbilityRuntimeRecipeDeveloperComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* ASC = BoundASC.Get())
    {
        ASC->OnAvatarBindingChanged.Remove(AvatarBindingHandle);
    }
    AvatarBindingHandle.Reset();
    BoundASC.Reset();
    UnbindPreviewAvatar();
    TArray<TWeakObjectPtr<UAbilityGraphInstance>> Runs;
    ObservedRuns.GetKeys(Runs);
    for (const auto& Run : Runs)
    {
        ForgetExecution(Run.Get());
    }
    ObservedRuns.Reset();
    OnRecipeChanged.Remove(RecipeChangeHandle);
    RecipeChangeHandle.Reset();
    if (GetOwner() && GetOwner()->HasAuthority() && DeveloperState.bPreviewActive)
    {
        FString Error;
        if (!EndPreview(Error))
        {
            // 世界退出不再恢复旧基础，但仍必须回收本组件授予的准确句柄。
            ReleasePreviewGrant();
        }
    }
    Definitions.Reset();
    PreviousEntry = {};
    OnDeveloperStateChanged.Clear();
    OnPreviewCloseAcknowledged.Clear();
    Observations.Reset();
    Super::EndPlay(Reason);
}

void UAbilityRuntimeRecipeDeveloperComponent::OnRegister()
{
    Super::OnRegister();
    BindRecipeChanges();
    BindAvatarChanges();
}

void UAbilityRuntimeRecipeDeveloperComponent::BindRecipeChanges()
{
    if (!RecipeChangeHandle.IsValid())
    {
        RecipeChangeHandle = OnRecipeChanged.AddUObject(this, &ThisClass::RecipeChanged);
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::RecipeChanged(FGameplayTag Tag, int32)
{
    if (bInternalEdit || !DeveloperState.bPreviewActive || DeveloperState.AbilityTag != Tag)
    {
        return;
    }
    InvalidateGraphDraft();
    PublishState();
    if (DeveloperState.BaseHash != PreviewBaseHash)
    {
        FString Error;
        Reject(
            TEXT(
                "Another system changed the base. Abandon preview to keep its change and release temporary resources."),
            Error);
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::BindAvatarChanges()
{
    const auto* State = Cast<AMyPlayerState>(GetOwner());
    auto* ASC = State ? Cast<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent()) : nullptr;
    if (ASC == BoundASC.Get() && AvatarBindingHandle.IsValid())
    {
        return;
    }
    if (auto* PreviousASC = BoundASC.Get())
    {
        PreviousASC->OnAvatarBindingChanged.Remove(AvatarBindingHandle);
    }
    AvatarBindingHandle.Reset();
    BoundASC = ASC;
    if (ASC)
    {
        AvatarBindingHandle = ASC->OnAvatarBindingChanged.AddUObject(this, &ThisClass::AvatarBindingChanged);
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::AvatarBindingChanged(AActor* Previous, AActor* Current)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Previous == Current)
    {
        return;
    }
    ForgetAvatarObservations(Previous);
    if (DeveloperState.bPreviewActive && PreviewAvatar != TWeakObjectPtr<AActor>(Current))
    {
        CleanupPreviewLifecycle();
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::BindPreviewAvatar(AActor* Avatar)
{
    UnbindPreviewAvatar();
    if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Avatar))
    {
        return;
    }
    PreviewAvatar = Avatar;
    Avatar->OnDestroyed.AddUniqueDynamic(this, &ThisClass::PreviewAvatarEnded);
    if (auto* Combat = Cast<ICombatInterface>(Avatar))
    {
        Combat->GetOnDeathDelegate().AddUniqueDynamic(this, &ThisClass::PreviewAvatarEnded);
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::UnbindPreviewAvatar()
{
    if (auto* Avatar = PreviewAvatar.GetEvenIfUnreachable())
    {
        Avatar->OnDestroyed.RemoveDynamic(this, &ThisClass::PreviewAvatarEnded);
        if (auto* Combat = Cast<ICombatInterface>(Avatar))
        {
            Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::PreviewAvatarEnded);
        }
    }
    PreviewAvatar.Reset();
}

void UAbilityRuntimeRecipeDeveloperComponent::ForgetAvatarObservations(TWeakObjectPtr<AActor> Avatar)
{
    TArray<TWeakObjectPtr<UAbilityGraphInstance>> Runs;
    for (const auto& Item : ObservedRuns)
    {
        if (Item.Value.Source.Avatar == Avatar)
        {
            Runs.Add(Item.Key);
        }
    }
    for (const auto& Run : Runs)
    {
        if (auto* Current = Run.Get())
        {
            ForgetExecution(Current);
        }
        else
        {
            ObservedRuns.Remove(Run);
        }
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::PreviewAvatarEnded(AActor* Avatar)
{
    // 使用弱身份比较；Destroyed 回调时 UObject 已可能进入无效状态，Get() 不一定还能返回指针。
    if (PreviewAvatar == TWeakObjectPtr<AActor>(Avatar))
    {
        CleanupPreviewLifecycle();
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::CleanupPreviewLifecycle()
{
    if (bLifecycleCleanup || !GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }
    TGuardValue<bool> Guard(bLifecycleCleanup, true);
    const auto OldAvatar = PreviewAvatar;
    UnbindPreviewAvatar();
    ForgetAvatarObservations(OldAvatar);
    if (!DeveloperState.bPreviewActive)
    {
        return;
    }
    FString Error;
    if (!EndPreview(Error) && !AbandonPreview(Error))
    {
        // 出错也先回收准确授予，保留来源清单和诊断以便重试，不能覆盖外部有效配置。
        const auto Handle = PreviewHandle;
        PreviewHandle = {};
        if (auto* ASC = BoundASC.Get(); ASC && Handle.IsValid())
        {
            ASC->CancelAbilityHandle(Handle);
            ASC->ClearAbility(Handle);
        }
        Reject(TEXT("Avatar changed; temporary grant released but recipe cleanup needs retry: ") + Error, Error);
    }
}

bool UAbilityRuntimeRecipeDeveloperComponent::CheckAuthority(bool bRequireMage, FString& Error) const
{
#if UE_BUILD_SHIPPING
    Error = TEXT("Runtime recipe preview is available only in development builds.");
    return false;
#else
    const auto* State = Cast<AMyPlayerState>(GetOwner());
    const UWorld* World = GetWorld();
    if (!IsValid(State) || !State->HasAuthority() || !World || World->bIsTearingDown ||
        World->GetNetMode() == NM_Client)
    {
        Error = TEXT("Developer recipe changes require this player's live authoritative PlayerState.");
        return false;
    }
    if (bRequireMage && State->Profession != EPlayerProfession::Mage && !HasUnselectedMageAvatar(*State))
    {
        Error = TEXT("当前角色不符合条件：技能组装测试仅支持法师角色，或直接 PIE 出生的未选职业默认法师。"
                     "下拉选择法师 GA 不会改变角色职业；请使用法师角色后重试。");
        return false;
    }
    return true;
#endif
}

bool UAbilityRuntimeRecipeDeveloperComponent::Reject(const FString& Reason, FString& Error)
{
    const FString Message = Reason;
    Error = Message;
    DeveloperState.Error = Message;
    DeveloperState.StateSequence = ++NextStateSequence;
    LastAcceptedState = DeveloperState;
    OnDeveloperStateChanged.Broadcast();
    if (GetOwner())
    {
        GetOwner()->ForceNetUpdate();
    }
    return false;
}

void UAbilityRuntimeRecipeDeveloperComponent::PublishState()
{
    const auto* Entry = Entries.Find(DeveloperState.AbilityTag);
    DeveloperState.Revision = Entry ? Entry->Revision : 0;
    DeveloperState.BaseHash = Entry && Entry->Base ? AbilityRuntimeRecipes::Fingerprint(*Entry->Base) : FString();
    DeveloperState.ContentHash =
        Entry && Entry->Compiled ? AbilityRuntimeRecipes::Fingerprint(*Entry->Compiled) : FString();
    RefreshGraphView();
    DeveloperState.Error.Reset();
    DeveloperState.StateSequence = ++NextStateSequence;
    LastAcceptedState = DeveloperState;
    OnDeveloperStateChanged.Broadcast();
    if (GetOwner())
    {
        GetOwner()->ForceNetUpdate();
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::ReleasePreviewGrant()
{
    // 先封口组件租约，再执行可能重入的取消回调。独立飞行图已正常结束原 GA，
    // 不能依赖 ClearAbility 或最多 16 个弱观察订阅回收其资源。
    TGuardValue<bool> Guard(bLifecycleCleanup, true);
    InvalidateGraphDraft();
    bCardPanel = false;
    InstalledCardsBySource.Reset();
    GraphPanelSessionId.Invalidate();
    UnbindPreviewAvatar();
    const auto Handle = PreviewHandle;
    const FGuid OwnerLease = PreviewId;
    PreviewHandle = {};
    PreviewId.Invalidate();
    DeveloperState.bPreviewActive = false;
    if (OwnerLease.IsValid())
    {
        if (UWorld* World = GetWorld())
        {
            if (auto* Host = World->GetSubsystem<UAbilityGraphWorldSubsystem>())
            {
                // 世界宿主按本次预览的精确租约取消，正式技能和其他玩家不受影响。
                Host->CancelOwnerLease(OwnerLease);
            }
        }
    }
    if (const auto* State = Cast<AMyPlayerState>(GetOwner()))
    {
        if (auto* ASC = State->GetAbilitySystemComponent(); ASC && Handle.IsValid())
        {
            ASC->CancelAbilityHandle(Handle);
            ASC->ClearAbility(Handle);
        }
    }
    PreviousEntry = {};
    bHadPreviousEntry = false;
    PreviewBaseHash.Reset();
    OwnedSources.Reset();
}

bool UAbilityRuntimeRecipeDeveloperComponent::CheckObservationSource(const FAbilitySourceToken& Source,
                                                                     FString& Error) const
{
    if (!CheckAuthority(true, Error))
    {
        return false;
    }
    AActor* Avatar = nullptr;
    if (!Source.ASC.IsValid() || Source.ASC->GetOwnerActor() != GetOwner() || Source.World.Get() != GetWorld() ||
        !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        Error = TEXT("Observation source must be this PlayerState's current authoritative ASC/Avatar life.");
        return false;
    }
    return true;
}

void UAbilityRuntimeRecipeDeveloperComponent::PruneObservations()
{
    TArray<TWeakObjectPtr<UAbilityGraphInstance>> Stale;
    for (const auto& Item : ObservedRuns)
    {
        const auto* Run = Item.Key.Get();
        if (!Run || (Run->GetExecutionId().IsValid() && Run->IsFinished()))
        {
            Stale.Add(Item.Key);
        }
    }
    for (const auto& Key : Stale)
    {
        if (auto* Run = Key.Get())
        {
            ForgetExecution(Run);
        }
        else
        {
            ObservedRuns.Remove(Key);
        }
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::GraphEvent(UAbilityGraphInstance* Run, const FAbilityGraphEvent& Event)
{
    const auto* Observation = ObservedRuns.Find(Run);
    if (!Observation || Event.ExecutionId != Run->GetExecutionId())
    {
        return;
    }
    const auto& Source = Observation->Source;
    // 记录属于原来源生命。图传入另一来源的事件不能借观察注册写入本玩家记录。
    if (Event.SourceToken.ASC != Source.ASC || Event.SourceToken.Avatar != Source.Avatar ||
        Event.SourceToken.World != Source.World || Event.SourceToken.EnemyLease != Source.EnemyLease ||
        Event.SourceToken.DungeonRunId != Source.DungeonRunId)
    {
        ForgetExecution(Run);
        return;
    }
    FAbilityRuntimeDeveloperObservation Record;
    Record.Kind = EAbilityRuntimeObservationKind::GraphEvent;
    Record.RootCastId = Observation->RootCastId.IsValid() ? Observation->RootCastId : Event.ExecutionId;
    Record.ExecutionId = Event.ExecutionId;
    Record.InvocationId = Event.NodeInvocationId;
    Record.EventSequence = static_cast<int64>(Event.EventSequence);
    Record.AbilityTag = Observation->AbilityTag;
    Record.Revision = Observation->Revision;
    Record.FrozenHash = Observation->FrozenHash;
    Record.NodeType = Event.NodeType;
    Record.Stage = Event.OutputPort;
    Record.Location = Event.Input.Impact.IsSet() ? Event.Input.Impact->Location : Event.Location;
    Record.Direction = Event.Input.Impact.IsSet() ? Event.Input.Impact->Direction : Event.Input.Direction;
    Record.bConfirmedHit = Event.Input.bConfirmedHit;
    Record.bDamageAttempted = Event.Input.bDamageAttempted;
    Record.bActualDamageKnown = Event.Input.ActualDamage.IsSet();
    Record.ActualDamage = Event.Input.ActualDamage.Get(0.f);
    Record.bAppliedKnown = Event.Input.DamageApplied.IsSet();
    Record.bApplied = Event.Input.DamageApplied.Get(false);
    Record.bKilled = Event.Input.bKilled;
    Record.EndReason = Event.EndReason;
    const FAbilityTargetToken* Target = Event.Input.Impact.IsSet()            ? &Event.Input.Impact->ConfirmedTarget
                                        : Event.Input.LiveTargetToken.IsSet() ? &Event.Input.LiveTargetToken.GetValue()
                                        : Event.TargetToken.Target.IsSet()    ? &Event.TargetToken
                                                                              : nullptr;
    if (Target)
    {
        if (Target->Target.IsActor())
        {
            Record.TargetKind = TEXT("Actor");
            Record.TargetLease = Target->ActorLease.Get(INDEX_NONE);
            Record.TargetLabel = FString::Printf(TEXT("%s#%u"), *GetNameSafe(Target->Target.Actor.Get()),
                                                 GetTypeHash(Target->Target.Actor));
        }
        else
        {
            Record.TargetKind = TEXT("Mass");
            Record.MassEntityId = Target->Target.EntityId;
            Record.TargetLabel = FString::Printf(TEXT("Mass:%d:%d:%d"), Record.MassEntityId, Target->MassEntity.Index,
                                                 Target->MassEntity.SerialNumber);
        }
    }
    AppendObservation(MoveTemp(Record));
}

void UAbilityRuntimeRecipeDeveloperComponent::GraphFinished(UAbilityGraphInstance* Run, EAbilityGraphEndReason Reason)
{
    const auto* Observation = ObservedRuns.Find(Run);
    if (!Observation)
    {
        return;
    }
    FAbilityRuntimeDeveloperObservation Record;
    Record.Kind = EAbilityRuntimeObservationKind::GraphFinished;
    Record.RootCastId = Observation->RootCastId.IsValid() ? Observation->RootCastId : Run->GetExecutionId();
    Record.ExecutionId = Run->GetExecutionId();
    Record.AbilityTag = Observation->AbilityTag;
    Record.Revision = Observation->Revision;
    Record.FrozenHash = Observation->FrozenHash;
    Record.Stage = TEXT("Graph.Finished");
    Record.EndReason = Reason;
    // 先解绑再通知面板，观察者的回调不能令本终态产生重复订阅或改变图寿命。
    ForgetExecution(Run);
    AppendObservation(MoveTemp(Record));
}

void UAbilityRuntimeRecipeDeveloperComponent::AppendObservation(FAbilityRuntimeDeveloperObservation Record)
{
    if (bCardPanel)
    {
        return;
    }
    Record.Sequence = ++ObservationSequence;
    Record.ServerSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    if (Observations.Num() >= 128)
    {
        Observations.RemoveAt(0, Observations.Num() - 127, EAllowShrinking::No);
    }
    Observations.Add(MoveTemp(Record));
    OnDeveloperStateChanged.Broadcast();
    if (GetOwner())
    {
        GetOwner()->ForceNetUpdate();
    }
}
