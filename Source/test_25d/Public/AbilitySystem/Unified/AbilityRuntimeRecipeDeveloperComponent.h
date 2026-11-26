// 调用链：开发面板/开发输入 -> 可信目录 ID 的服务器 RPC -> 配方事务 -> 原 ASC 临时 Spec -> 原 GA 前端。
// 组件随 PlayerState 存活；只有开发预览持有的授予/来源可被回收，不复制临时图或改变原学习/存档状态。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeCatalog.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeObservations.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeGraphView.h"
#include "GameplayAbilitySpecHandle.h"
#include "AbilityRuntimeRecipeDeveloperComponent.generated.h"

class UAbilityInfo;
class UAbilityGraphInstance;
class UMyAbilitySystemComponent;
struct FAbilityGraphEvent;
class UGameplayAbility;

/** ID 来自服务器读取的真实 Tag/Class，不允许请求端提供 UObject/Class 或任意资产路径。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeDeveloperDefinition
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FName DefinitionId;
    UPROPERTY(BlueprintReadOnly)
    FAbilityRuntimeBaseDefinition Base;
    UPROPERTY(BlueprintReadOnly)
    FString BaseHash;
};

/** 每次编辑必须说明所见版本；节点索引只相对于同一个基础摘要有效。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeDeveloperEdit
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName DefinitionId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName ModifierId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 NodeIndex = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 ExpectedRevision = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ExpectedBaseHash;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Value = 1.f;
    /** 传播模板的显式有界参数；普通数值缩放仍只使用 Value，服务器不接收任意配置对象。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Count = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Interval = .15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 350.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Falloff = 1.f;
    /** BFS传播专用整数轴；由已登记Forked策略限制，不隐藏在伤害数值或半径中。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxDepth = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 BranchesPerTarget = 2;
    /** 只解析服务器已登记片段及该节点已声明端口；不能上传图、类或任意属性名。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName FragmentId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName OutputPortId;
    /** 新画布按当前已接受图选准确节点；为空时保留旧NodeIndex相对基础图的测试协议。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGuid TargetNodeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGuid TargetEdgeTo;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ExpectedAcceptedHash;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGuid ExpectedPreviewSessionId;
    /** 只用于准确撤销本预览的来源；添加时由服务器生成，客户端不能取得任意来源所有权。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName SourceId;
};

/** 玩家页的卡片投影。安装来源和图位置由服务器解析与保存。 */
USTRUCT()
struct FAbilityRuntimeWorkshopCardView
{
    GENERATED_BODY()

    UPROPERTY()
    FName Id;
    UPROPERTY()
    FString Slot;
    UPROPERTY()
    FString Label;
    UPROPERTY()
    FString Description;
    UPROPERTY()
    bool bInstalled = false;
};

/** 只向所属玩家同步可显示的值，不把服务器瞬态图引用发给客户端。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeDeveloperState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bPreviewActive = false;
    UPROPERTY(BlueprintReadOnly)
    FName SelectedId;
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag AbilityTag;
    UPROPERTY(BlueprintReadOnly)
    int32 Revision = 0;
    UPROPERTY(BlueprintReadOnly)
    FString BaseHash;
    UPROPERTY(BlueprintReadOnly)
    FString ContentHash;
    UPROPERTY(BlueprintReadOnly)
    FString Error;
    /** 服务器显示状态的单调序号，避免可靠 RPC 回执与属性复制交错时退回旧 UI 状态。 */
    UPROPERTY(BlueprintReadOnly)
    int64 StateSequence = 0;
    UPROPERTY(BlueprintReadOnly)
    FGuid PreviewSessionId;
    /** 区分新面板自有nonce与旧工具创建的服务器预览，不能认领别的图面板会话。 */
    UPROPERTY(BlueprintReadOnly)
    bool bGraphPanelSession = false;
    UPROPERTY()
    bool bCardPanelSession = false;
    UPROPERTY()
    TArray<FAbilityRuntimeWorkshopCardView> Cards;
    UPROPERTY()
    FName DraftCard;
    UPROPERTY(BlueprintReadOnly)
    FAbilityRuntimeGraphView AcceptedGraph;
    UPROPERTY(BlueprintReadOnly)
    FAbilityRuntimeGraphView DraftGraph;
    UPROPERTY(BlueprintReadOnly)
    FGuid DraftToken;
    UPROPERTY(BlueprintReadOnly)
    FString DraftError;
};

DECLARE_MULTICAST_DELEGATE(FAbilityRuntimeDeveloperStateChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FAbilityRuntimePreviewCloseAcknowledged, FGuid, bool);

/** 开发测试入口，复用生产配方和生产 ASC；不实现另一套施法、伤害或能力存档系统。 */
UCLASS(ClassGroup = (Abilities))
class TEST_25D_API UAbilityRuntimeRecipeDeveloperComponent : public UAbilityRuntimeRecipeComponent
{
    GENERATED_BODY()
public:
    /** 无 Tick；仅拥有者回执复制。Shipping 中拒绝预览事务。 */
    UAbilityRuntimeRecipeDeveloperComponent();
    /** 本地/服务器分别只读真实 Mage 目录；Preferred 只影响同身份版本首选，不过滤完整目录。 */
    bool InitializeCatalog(const UAbilityInfo* Preferred, FString& Error);
    /** 只读行包含保护项与不支持诊断，不能把“目录中存在”误称为“可动态修改”。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    const TArray<FAbilityRuntimeDeveloperDefinition>& GetDefinitions() const;
    /** 服务器开始独占临时输入授予；旧 Spec、槽位、等级和来源均保持原值。 */
    bool BeginPreview(FName DefinitionId, FString& Error);
    /** RPC 与自动化共用同一服务器校验入口；失败不修改已接受配方或临时授予。 */
    bool ApplyEdit(const FAbilityRuntimeDeveloperEdit& Edit, FString& Error);
    /** 仅生成同租约/版本下的私有候选，配置跨GC保活；不发布新版本或执行任何游戏动作。 */
    bool PreviewGraphEdit(const FAbilityRuntimeDeveloperEdit& Edit, FString& Error);
    /** 只接受当前会话的卡片 ID，服务器用当前版本重新定位并编译候选。 */
    bool PreviewCard(FName CardId, FGuid Session, int32 ExpectedRevision, FString& Error);
    /** 第四页只列出具备首批卡片的现有技能。 */
    static FText GetCardAbilityLabel(FGameplayTag AbilityTag);
    /** 只提交准确草稿；旧令牌/版本/外部修改均拒绝，应用重用已冻结片段及节点身份。 */
    bool ApplyGraphDraft(FGuid Token, int32 ExpectedRevision, FString& Error);
    /** 准确移除自己的历史来源；依赖仍存在时编译拒绝，不悄悄丢弃其它已接受规则。 */
    bool RemoveGraphSource(FName SourceId, int32 ExpectedRevision, FString& Error);
    /** 仅丢弃同一令牌的草稿；迟到的旧UI不能清除新草稿。 */
    bool DiscardGraphDraft(FGuid Token, FString& Error);
    /** 正式Spec与临时预览共用ASC时，只读取正式基础/外部贡献，防测试编辑污染正式同标签技能。 */
    bool CreateFormalCastSnapshot(FGameplayTag AbilityTag, const UGameplayAbility* FormalAbility, UObject* Outer,
                                  FAbilityRuntimeRecipeSnapshot& Out, bool& bHasRecipe, FString& Error) const;
    /** 根据准确模板/节点与版本只撤本预览的对应来源，不影响外部修正或在途快照。 */
    bool RemoveEdit(const FAbilityRuntimeDeveloperEdit& Edit, FString& Error);
    /** 保留当前临时授予并撤本次全部编辑；不退款、不清除外部来源。 */
    bool ResetPreview(FName DefinitionId, int32 ExpectedRevision, const FString& ExpectedBaseHash, FString& Error);
    /** 只移除本预览拥有的贡献/授予；外部来源变化须合并，基础冲突须保留诊断。 */
    bool EndPreview(FString& Error);
    /** 明确放弃旧基础恢复，只撤销自己的来源与授予；用于处理外部基础变化后的退出。 */
    bool AbandonPreview(FString& Error);
    /** 玩家存档遍历以此排除开发临时 Spec，不能按类名排除其他正式授予。 */
    bool OwnsTemporaryAbility(FGameplayAbilitySpecHandle Handle) const;
    /** 控制器使用真实 ASC 输入入口操作此句柄所属的独占开发槽。 */
    FGameplayAbilitySpecHandle GetPreviewHandle() const;
    /** 只有准确临时 Spec 可取得服务器预览租约；前端在激活开始时冻结，正式同类授予返回空。 */
    FGuid GetPreviewOwnerLease(FGameplayAbilitySpecHandle Handle) const;
    /** 面板读取服务器回执；本地目录不能替代此权威状态。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    const FAbilityRuntimeDeveloperState& GetDeveloperState() const;
    /** 只读取所属PlayerState原已复制的PlayerId，用于双端匹配；不新增复制状态或创建玩家身份。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    int32 GetOwnerPlayerId() const;
    /** 只读当前世界的准确临时Spec/原冷却门；未复制授予、活动或重复槽位都不视为空闲，不替代CanActivate。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    bool IsPreviewFrontendIdle() const;
    /** 仅实际ClientClosePreviewAcknowledged回执更新；QA/面板不能把普通inactive状态当成本次关闭确认。 */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Development|Runtime Recipe")
    FGuid LastCloseAcknowledgedId;
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Development|Runtime Recipe")
    bool bLastCloseSucceeded = false;
    /** 在 StartWithInput 前订阅；仅弱观察本玩家图，不取得执行所有权，最多同时 16 个。 */
    bool ObserveExecution(UAbilityGraphInstance* Run, const FAbilitySourceToken& Source, FGameplayTag RecipeTag,
                          int32 FrozenRevision, const FString& FrozenHash, FString& Error, FGuid RootCastId = FGuid());
    /** 启动失败等无终态通知的路径显式解绑；不取消或推进图。 */
    void ForgetExecution(UAbilityGraphInstance* Run);
    /** 原费用前端提交真实判定及前后资源值；不接受客户端 RPC 写入观测。 */
    bool RecordCost(const FAbilitySourceToken& Source, const FAbilityRuntimeDeveloperCost& Cost, FString& Error);
    /** 当前所属玩家的有界值日志；返回引用只在下一次追加前保持当前数组布局。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    const TArray<FAbilityRuntimeDeveloperObservation>& GetObservations() const;
    /** 用于显示观察上限；包括已订阅但尚未 Start 的调用，不将其初始 Finished 误认为结束。 */
    int32 GetObservedExecutionCount() const;

    UFUNCTION(Server, Reliable)
    void ServerBeginPreview(FName DefinitionId);
    /** 开始请求绑定面板自有随机身份，使开始回执前的析构关闭也不会误关后来会话。 */
    UFUNCTION(Server, Reliable)
    void ServerBeginGraphPreview(FName DefinitionId, FGuid RequestedSessionId);
    UFUNCTION(Server, Reliable)
    void ServerBeginCardPreview(FName DefinitionId, FGuid RequestedSessionId);
    UFUNCTION(Server, Reliable)
    void ServerPreviewCard(FName CardId, FGuid Session, int32 ExpectedRevision);
    UFUNCTION(Server, Reliable)
    void ServerRemoveCard(FName CardId, FGuid Session, int32 ExpectedRevision);
    UFUNCTION(Server, Reliable)
    void ServerApplyEdit(const FAbilityRuntimeDeveloperEdit& Edit);
    UFUNCTION(Server, Reliable)
    void ServerPreviewGraphEdit(const FAbilityRuntimeDeveloperEdit& Edit);
    UFUNCTION(Server, Reliable)
    void ServerApplyGraphDraft(FGuid Token, int32 ExpectedRevision);
    UFUNCTION(Server, Reliable)
    void ServerRemoveGraphSource(FName SourceId, int32 ExpectedRevision);
    UFUNCTION(Server, Reliable)
    void ServerDiscardGraphDraft(FGuid Token);
    UFUNCTION(Server, Reliable)
    void ServerResetGraphPreview(FGuid ExpectedPreviewSessionId, int32 ExpectedRevision);
    UFUNCTION(Server, Reliable)
    void ServerRemoveEdit(const FAbilityRuntimeDeveloperEdit& Edit);
    UFUNCTION(Server, Reliable)
    void ServerResetPreview(FName DefinitionId, int32 ExpectedRevision, const FString& ExpectedBaseHash);
    UFUNCTION(Server, Reliable)
    void ServerEndPreview();
    UFUNCTION(Server, Reliable)
    void ServerAbandonPreview();
    /** 关闭用独立请求 ID 匹配可靠回执；不能将旧 Error/其他状态复制当作本次关闭结果。 */
    UFUNCTION(Server, Reliable)
    void ServerClosePreview(FGuid RequestId, bool bAbandon);
    /** 工坊界面关闭同时绑定租约，不能让旧面板/迟到回执关闭后来新开的预览。 */
    UFUNCTION(Server, Reliable)
    void ServerCloseGraphPreview(FGuid RequestId, FGuid ExpectedPreviewSessionId, bool bAbandon);
    UFUNCTION(Client, Reliable)
    void ClientClosePreviewAcknowledged(FGuid RequestId, bool bSucceeded,
                                        const FAbilityRuntimeDeveloperState& StateSnapshot);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    /** Owner 退出时只清理自己的临时资源，再由基类释放所有配方引用。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    FAbilityRuntimeDeveloperStateChanged OnDeveloperStateChanged;
    FAbilityRuntimePreviewCloseAcknowledged OnPreviewCloseAcknowledged;

protected:
    virtual void OnRegister() override;

private:
    /** 共享正式规则编译：过滤测试来源，拒绝正式图不存在的临时节点依赖，不发布中间图。 */
    bool CompileFormalRecipe(const FAbilityRuntimeRecipeEntry& Entry, const UGameplayAbility* FormalAbility,
                             UObject* Outer, const FAbilityRuntimeRecipeContribution* Pending,
                             UAbilityGraphDefinition*& Compiled, FString& Error) const;
    virtual bool ValidateContributionForCurrentOwner(FGameplayTag AbilityTag,
                                                     const FAbilityRuntimeRecipeContribution& Contribution,
                                                     FString& Error) const override;
    UPROPERTY(Transient)
    TArray<FAbilityRuntimeDeveloperDefinition> Definitions;
    UPROPERTY(ReplicatedUsing = OnRep_DeveloperState)
    FAbilityRuntimeDeveloperState DeveloperState;
    UPROPERTY(ReplicatedUsing = OnRep_Observations)
    TArray<FAbilityRuntimeDeveloperObservation> Observations;
    UPROPERTY(Transient)
    FAbilityRuntimeDeveloperState LastAcceptedState;
    UPROPERTY(Transient)
    FAbilityRuntimeRecipeEntry PreviousEntry;
    bool bHadPreviousEntry = false;
    FGameplayAbilitySpecHandle PreviewHandle;
    TArray<FName> OwnedSources;
    FGuid PreviewId;
    FGuid GraphPanelSessionId;
    bool bCardPanel = false;
    TMap<FName, FName> InstalledCardsBySource;
    FString PreviewBaseHash;
    UPROPERTY(Transient)
    FAbilityRuntimeRecipeContribution PendingGraphContribution;
    FGuid PendingGraphSession;
    int32 PendingGraphRevision = 0;
    FString PendingGraphAcceptedHash;
    int32 NextGraphEditSequence = 0;
    FDelegateHandle RecipeChangeHandle;
    FDelegateHandle AvatarBindingHandle;
    TWeakObjectPtr<UMyAbilitySystemComponent> BoundASC;
    TWeakObjectPtr<AActor> PreviewAvatar;
    bool bInternalEdit = false;
    bool bLifecycleCleanup = false;
    int64 NextStateSequence = 0;
    int64 ObservationSequence = 0;

    struct FObservedRun
    {
        FAbilitySourceToken Source;
        FGameplayTag AbilityTag;
        int32 Revision = 0;
        FString FrozenHash;
        FGuid RootCastId;
        FDelegateHandle EventHandle;
        FDelegateHandle FinishedHandle;
    };
    TMap<TWeakObjectPtr<UAbilityGraphInstance>, FObservedRun> ObservedRuns;

    /** 权威观察只接受属于本 PlayerState ASC 的当前来源，不能借别人的合法 Token 写日志。 */
    bool CheckObservationSource(const FAbilitySourceToken& Source, FString& Error) const;
    /** 清除已经回收的弱对象订阅；不取得效果所有权或延长其寿命。 */
    void PruneObservations();
    /** 复制下一次施法实际使用的图，并保留服务器提交顺序；刷新日志不会重新创建图节点。 */
    void RefreshGraphView();
    /** 仅从接受的权威配置投影选项，工具图不参与玩家状态复制。 */
    void RefreshCardView();
    /** 编译并冻结草稿，再共用原精确令牌提交。 */
    bool StageGraphContribution(const FAbilityRuntimeRecipeContribution& Contribution, FName CardId,
                                FString& Error);
    /** 所有修改/租约失效先清除候选，避免旧令牌在新会话中重放。 */
    void InvalidateGraphDraft();
    /** 复制已接受端口载荷及原生命标识，不重新查找目标或结算伤害。 */
    void GraphEvent(UAbilityGraphInstance* Run, const FAbilityGraphEvent& Event);
    /** 终态先解除本观察订阅，再发布纯值记录；图自身的结束与归还不属于本组件。 */
    void GraphFinished(UAbilityGraphInstance* Run, EAbilityGraphEndReason Reason);
    /** 维持 128 环限并通知所属客户端；不保留载荷中的 UObject 引用。 */
    void AppendObservation(FAbilityRuntimeDeveloperObservation Record);

    /** 外部系统可通过基类 API 修改，必须订阅实际提交事件，不能仅监听面板自身按钮。 */
    void BindRecipeChanges();
    void RecipeChanged(FGameplayTag AbilityTag, int32 Revision);
    /** ASC 属于持久 PlayerState；换 Avatar 的通知只绑定一次，不按客户端状态复制推断清理。 */
    void BindAvatarChanges();
    void AvatarBindingChanged(AActor* PreviousAvatar, AActor* NewAvatar);
    /** 仅在预览期间订阅真实战斗死亡和 Actor 销毁；结束时解除两种入口。 */
    void BindPreviewAvatar(AActor* Avatar);
    void UnbindPreviewAvatar();
    /** 解绑旧生命日志但不让观察器取得效果所有权。 */
    void ForgetAvatarObservations(TWeakObjectPtr<AActor> Avatar);
    /** 先封口并精确恢复；若外部基础已变化则放弃旧基础恢复，只撤自己的来源/授予。 */
    void CleanupPreviewLifecycle();
    UFUNCTION()
    void PreviewAvatarEnded(AActor* Avatar);

    /** 校验权威世界及开发构建；开始/编辑另外要求法师职业，退出不受职业变化阻碍。 */
    bool CheckAuthority(bool bRequireMage, FString& Error) const;
    /** 统一失败回执，保留已经接受的版本、图和授予。 */
    bool Reject(const FString& Reason, FString& Error);
    /** 从已提交的生产配方更新可复制值；不把图引用复制给客户端。 */
    void PublishState();
    /** 成功恢复之后回收本组件准确授予的句柄，不取消同类的永久能力。 */
    void ReleasePreviewGrant();

    UFUNCTION()
    void OnRep_DeveloperState();
    UFUNCTION()
    void OnRep_Observations();
};
