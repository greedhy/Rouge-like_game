// 调用链：真实画布选择 -> 所属服务器草稿RPC -> 当前快照/可信模板 -> 原配方编译 -> 精确令牌提交。
// 免费测试不接背包或存档；候选片段由组件UPROPERTY持有，所有执行仍由原GA前端和GraphInstance负责。
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"

void UAbilityRuntimeRecipeDeveloperComponent::RefreshGraphView()
{
    DeveloperState.PreviewSessionId = GraphPanelSessionId.IsValid() ? GraphPanelSessionId : PreviewId;
    DeveloperState.bGraphPanelSession = GraphPanelSessionId.IsValid();
    DeveloperState.AcceptedGraph = {};
    if (!DeveloperState.bPreviewActive)
    {
        return;
    }
    FAbilityRuntimeRecipeSnapshot Snapshot;
    FString Error;
    if (!CreateCastSnapshot(DeveloperState.AbilityTag, this, Snapshot, Error) ||
        !AbilityRuntimeGraphViews::Build(Snapshot, DeveloperState.AcceptedGraph, Error))
    {
        DeveloperState.AcceptedGraph.Error = Error;
        return;
    }
    DeveloperState.AcceptedGraph.AppliedSources = OwnedSources;
}

void UAbilityRuntimeRecipeDeveloperComponent::InvalidateGraphDraft()
{
    PendingGraphContribution = {};
    PendingGraphSession.Invalidate();
    PendingGraphRevision = 0;
    PendingGraphAcceptedHash.Reset();
    DeveloperState.DraftGraph = {};
    DeveloperState.DraftToken.Invalidate();
    DeveloperState.DraftError.Reset();
}

bool UAbilityRuntimeRecipeDeveloperComponent::PreviewGraphEdit(const FAbilityRuntimeDeveloperEdit& Edit, FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(true, Error))
    {
        return Reject(Error, Error);
    }
    const auto* Entry = Entries.Find(DeveloperState.AbilityTag);
    const FGuid Session = GraphPanelSessionId.IsValid() ? GraphPanelSessionId : PreviewId;
    if (!DeveloperState.bPreviewActive || !Entry || !Entry->Base || !Entry->Compiled ||
        Edit.DefinitionId != DeveloperState.SelectedId || Edit.ExpectedPreviewSessionId != Session ||
        !Edit.TargetNodeId.IsValid() || !Edit.SourceId.IsNone())
    {
        return Reject(TEXT("Select an accepted node in this exact workshop session; add sources are server-owned."),
                      Error);
    }
    const FString AcceptedHash = AbilityRuntimeRecipes::Fingerprint(*Entry->Compiled);
    if (Edit.ExpectedRevision != Entry->Revision || Edit.ExpectedAcceptedHash != AcceptedHash ||
        Edit.ExpectedBaseHash != PreviewBaseHash || AbilityRuntimeRecipes::Fingerprint(*Entry->Base) != PreviewBaseHash)
    {
        // 过期请求不能清掉后来创建的合法草稿。
        return Reject(TEXT("The accepted graph changed. Refresh its session, revision and hash before previewing."),
                      Error);
    }
    const auto* Node = Entry->Compiled->Nodes.FindByPredicate(
        [&Edit](const FAbilityGraphNode& Item)
        {
            return Item.NodeId == Edit.TargetNodeId;
        });
    if (!Node || !FMath::IsFinite(Edit.Value))
    {
        return Reject(TEXT("The selected node no longer exists, or its requested value is invalid."), Error);
    }
    if (Node->TypeId.ToString().StartsWith(TEXT("PassThrough.")) && Edit.OutputPortId != TEXT("Continue"))
    {
        return Reject(TEXT("The insertion adapter's internal Apply contract is read-only; edit its effect node."),
                      Error);
    }
    FAbilityRuntimeRecipePatch Patch;
    if (!AbilityRuntimeModifierTemplates::BuildPatch(this, *Node, Edit, Patch, Error, Entry->Compiled.Get()))
    {
        DeveloperState.DraftError = Error;
        return Reject(Error, Error);
    }
    int32 Priority = 0;
    for (const auto& Contribution : Entry->Contributions)
    {
        if (Contribution.Priority == MAX_int32)
        {
            return Reject(TEXT("Edit ordering has no available priority; reset or remove conflicting rules."), Error);
        }
        Priority = FMath::Max(Priority, Contribution.Priority + 1);
    }
    FAbilityRuntimeRecipeContribution Contribution;
    const FGuid Token = FGuid::NewGuid();
    Contribution.SourceId = FName(*FString::Printf(TEXT("Workshop.%s.%d.%s"), *PreviewId.ToString(),
                                                   NextGraphEditSequence + 1, *Token.ToString()));
    Contribution.Priority = Priority;
    Contribution.bOrderedWorkshopEdit = true;
    Contribution.Patches.Add(Patch);
    auto Contributions = Entry->Contributions;
    Contributions.Add(Contribution);
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!AbilityRuntimeRecipes::Compile(this, Entry->Base, Contributions, Compiled, Error))
    {
        DeveloperState.DraftError = Error;
        return Reject(Error, Error);
    }
    FAbilityRuntimeRecipeSnapshot Snapshot;
    Snapshot.AbilityTag = DeveloperState.AbilityTag;
    Snapshot.Revision = Entry->Revision + 1;
    Snapshot.Graph = Compiled;
    Snapshot.ContentHash = AbilityRuntimeRecipes::Fingerprint(*Compiled);
    FAbilityRuntimeGraphView View;
    if (!AbilityRuntimeGraphViews::Build(Snapshot, View, Error))
    {
        return Reject(Error, Error);
    }
    // 只替换已验证的新候选；片段实例及GUID随后应用时不再从模板重建。
    InvalidateGraphDraft();
    PendingGraphContribution = MoveTemp(Contribution);
    PendingGraphSession = Session;
    PendingGraphRevision = Entry->Revision;
    PendingGraphAcceptedHash = AcceptedHash;
    DeveloperState.DraftToken = Token;
    DeveloperState.DraftGraph = MoveTemp(View);
    DeveloperState.DraftGraph.AppliedSources = OwnedSources;
    DeveloperState.DraftGraph.AppliedSources.Add(PendingGraphContribution.SourceId);
    ++NextGraphEditSequence;
    PublishState();
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::ApplyGraphDraft(FGuid Token, int32 ExpectedRevision, FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(true, Error))
    {
        return Reject(Error, Error);
    }
    const auto* Entry = Entries.Find(DeveloperState.AbilityTag);
    const FGuid Session = GraphPanelSessionId.IsValid() ? GraphPanelSessionId : PreviewId;
    if (!DeveloperState.bPreviewActive || !Token.IsValid() || Token != DeveloperState.DraftToken ||
        PendingGraphSession != Session || !Entry || !Entry->Base || !Entry->Compiled ||
        ExpectedRevision != Entry->Revision || ExpectedRevision != PendingGraphRevision ||
        AbilityRuntimeRecipes::Fingerprint(*Entry->Compiled) != PendingGraphAcceptedHash ||
        AbilityRuntimeRecipes::Fingerprint(*Entry->Base) != PreviewBaseHash)
    {
        return Reject(TEXT("This draft is stale or belongs to another workshop session. Preview again."), Error);
    }
    const FName Source = PendingGraphContribution.SourceId;
    const FGuid ApplyingPreview = PreviewId;
    // 发布回调可同步激活正式技能或关闭预览；先记录所有权，正式隔离与清理都能识别此来源。
    OwnedSources.Add(Source);
    bool bAccepted = false;
    {
        TGuardValue<bool> Guard(bInternalEdit, true);
        bAccepted = SetContribution(DeveloperState.AbilityTag, PendingGraphContribution, Error);
    }
    if (!bAccepted)
    {
        if (PreviewId == ApplyingPreview)
        {
            OwnedSources.Remove(Source);
        }
        return Reject(Error, Error);
    }
    if (PreviewId != ApplyingPreview || !DeveloperState.bPreviewActive)
    {
        Error = TEXT("The preview closed while applying; its owned contribution was released.");
        return false;
    }
    InvalidateGraphDraft();
    PublishState();
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::RemoveGraphSource(FName Source, int32 ExpectedRevision, FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(true, Error))
    {
        return Reject(Error, Error);
    }
    const auto* Entry = Entries.Find(DeveloperState.AbilityTag);
    if (!DeveloperState.bPreviewActive || !Entry || !Entry->Base || Entry->Revision != ExpectedRevision ||
        AbilityRuntimeRecipes::Fingerprint(*Entry->Base) != PreviewBaseHash || !OwnedSources.Contains(Source))
    {
        return Reject(TEXT("Undo requires an exact owned edit and the current accepted revision."), Error);
    }
    bool bRemoved = false;
    const FGuid RemovingPreview = PreviewId;
    {
        TGuardValue<bool> Guard(bInternalEdit, true);
        bRemoved = RemoveContribution(DeveloperState.AbilityTag, Source, Error);
    }
    if (!bRemoved)
    {
        return Reject(TEXT("A later edit may depend on this module. Undo the later edit first, or reset all. ") + Error,
                      Error);
    }
    if (PreviewId != RemovingPreview || !DeveloperState.bPreviewActive)
    {
        Error = TEXT("The preview closed while undoing; its resources were released.");
        return false;
    }
    OwnedSources.Remove(Source);
    InvalidateGraphDraft();
    PublishState();
    return true;
}

bool UAbilityRuntimeRecipeDeveloperComponent::DiscardGraphDraft(FGuid Token, FString& Error)
{
    Error.Reset();
    if (!CheckAuthority(true, Error))
    {
        return Reject(Error, Error);
    }
    if (!Token.IsValid() || Token != DeveloperState.DraftToken)
    {
        return Reject(TEXT("Only the currently displayed draft token can be discarded."), Error);
    }
    InvalidateGraphDraft();
    PublishState();
    return true;
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerBeginGraphPreview_Implementation(FName Id, FGuid RequestedSession)
{
    FString Error;
    if (!RequestedSession.IsValid())
    {
        Reject(TEXT("A graph panel opening request requires a nonempty session identity."), Error);
        return;
    }
    if (DeveloperState.bPreviewActive)
    {
        if ((GraphPanelSessionId == RequestedSession ||
             (!GraphPanelSessionId.IsValid() && PreviewId == RequestedSession)) &&
            DeveloperState.SelectedId == Id)
        {
            PublishState();
            return;
        }
        Reject(TEXT("Another preview is active. Close its exact session before opening a new one."), Error);
        return;
    }
    GraphPanelSessionId = RequestedSession;
    if (!BeginPreview(Id, Error))
    {
        GraphPanelSessionId.Invalidate();
    }
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerPreviewGraphEdit_Implementation(
    const FAbilityRuntimeDeveloperEdit& Edit)
{
    FString Error;
    PreviewGraphEdit(Edit, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerApplyGraphDraft_Implementation(FGuid Token, int32 Revision)
{
    FString Error;
    ApplyGraphDraft(Token, Revision, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerRemoveGraphSource_Implementation(FName Source, int32 Revision)
{
    FString Error;
    RemoveGraphSource(Source, Revision, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerDiscardGraphDraft_Implementation(FGuid Token)
{
    FString Error;
    DiscardGraphDraft(Token, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerResetGraphPreview_Implementation(FGuid Session, int32 Revision)
{
    FString Error;
    if (Session != DeveloperState.PreviewSessionId || !DeveloperState.bPreviewActive)
    {
        Reject(TEXT("Reset belongs to a previous graph panel session."), Error);
        return;
    }
    ResetPreview(DeveloperState.SelectedId, Revision, DeveloperState.BaseHash, Error);
}

void UAbilityRuntimeRecipeDeveloperComponent::ServerCloseGraphPreview_Implementation(FGuid Request, FGuid Session,
                                                                                     bool bAbandon)
{
    FString Error;
    bool bSucceeded = false;
    if (!Request.IsValid() || !Session.IsValid())
    {
        Reject(TEXT("Close requires the graph panel's exact request and session identities."), Error);
    }
    else if (!DeveloperState.bPreviewActive)
    {
        bSucceeded = true;
    }
    else if (Session != (GraphPanelSessionId.IsValid() ? GraphPanelSessionId : PreviewId))
    {
        Reject(TEXT("This close belongs to an old graph panel; the active preview was left unchanged."), Error);
    }
    else
    {
        bSucceeded = bAbandon ? AbandonPreview(Error) : EndPreview(Error);
    }
    if (bSucceeded)
    {
        PublishState();
    }
    ClientClosePreviewAcknowledged(Request, bSucceeded, DeveloperState);
}
