// 调用链：显式开发探针命令 -> 下一次原生CoreTicker -> 本地PlayerState开发组件的真实可靠RPC。
// 桥接面板已有的Begin/Edit/Close及原T键输入，不授予额外权限、不注入TargetData或伤害；避开Python编辑器调用保护对RPC的本地执行改写。
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Player/MyPlayerController.h"
#include "Player/MyPlayerState.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputCoreTypes.h"
#include "UI/Widget/AbilityRuntimeRecipePanel.h"
#include "UI/Widget/AbilityRuntimeGraphCanvas.h"
#include "UI/Widget/CharacterMenuWidget.h"
#include "UI/HUD/MyHUD.h"
#include "Widgets/Input/SButton.h"
#include "Input/Events.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "UObject/UObjectIterator.h"

namespace AbilityRuntimeRecipeNetworkProbe
{
    /** 只查本地控制器实际持有的在屏面板；不寻找另一玩家或创建测试专用Widget替身。 */
    UAbilityRuntimeRecipePanel* FindPanel(APlayerController& Controller)
    {
        if (const auto* HUD = Controller.GetHUD<AMyHUD>())
        {
            const auto* Menu = HUD->GetCharacterMenu();
            auto* Embedded = Menu ? Menu->GetRuntimeRecipePanel() : nullptr;
            if (Menu && Menu->IsInViewport() && Embedded && !Embedded->IsSessionClosed())
            {
                return Embedded;
            }
        }
        for (TObjectIterator<UAbilityRuntimeRecipePanel> It; It; ++It)
        {
            if (IsValid(*It) && It->GetWorld() == Controller.GetWorld() && It->GetOwningPlayer() == &Controller &&
                It->IsInViewport())
            {
                return *It;
            }
        }
        return nullptr;
    }

    /** 按语义 Tag 寻找真实 Slate 页签/关闭按钮；SimulateClick 执行生产委托，不直接切页。 */
    TSharedPtr<SButton> FindMenuButton(const TSharedRef<SWidget>& Widget, FName Tag)
    {
        if (Widget->GetTag() == Tag && Widget->GetType() == TEXT("SButton"))
        {
            return StaticCastSharedRef<SButton>(Widget);
        }
        FChildren* Children = Widget->GetChildren();
        for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
        {
            if (auto Button = FindMenuButton(Children->GetChildAt(Index), Tag))
            {
                return Button;
            }
        }
        return nullptr;
    }

    /** 有界UMG探针只选真实选项/数值控件并广播原按钮委托，绝不直接调用Developer编辑或注入施法。 */
    bool DispatchPanelAction(AMyPlayerController& Controller, UAbilityRuntimeRecipeDeveloperComponent& Developer,
                             const TArray<FString>& Arguments)
    {
        const FString& Action = Arguments[0];
        if (Action.StartsWith(TEXT("UMG.Character.")))
        {
            auto* HUD = Controller.GetHUD<AMyHUD>();
            if (!HUD)
            {
                return false;
            }
            if (Action == TEXT("UMG.Character.Attributes") && Arguments.Num() == 1)
            {
                auto* Entry =
                    HUD->OverlayWidget
                        ? Cast<UUserWidget>(HUD->OverlayWidget->GetWidgetFromName(TEXT("AttributeMenuButton")))
                        : nullptr;
                auto* Button = Entry ? Cast<UButton>(Entry->GetWidgetFromName(TEXT("Button"))) : nullptr;
                if (!Button || !Button->GetIsEnabled() || !Button->IsVisible())
                {
                    return false;
                }
                Button->OnClicked.Broadcast();
                return HUD->GetCharacterMenu() && HUD->GetCharacterMenu()->IsVisible();
            }
            auto* Menu = HUD->GetCharacterMenu();
            if (!Menu || !Menu->IsInViewport() || !Menu->IsVisible())
            {
                return false;
            }
            if (Action == TEXT("UMG.Character.Remove") && Arguments.Num() == 1)
            {
                // 与实际 HUD 拆卸一样在原生 Tick 移除宿主；避开 Python 编辑器 RPC 调用空间改写。
                Menu->RemoveFromParent();
                return !Menu->IsInViewport();
            }
            FName Tag;
            if (Action == TEXT("UMG.Character.Close") && Arguments.Num() == 1)
            {
                Tag = TEXT("CharacterMenuClose");
            }
            else if (Action == TEXT("UMG.Character.Page") && Arguments.Num() == 2)
            {
                int32 Page = INDEX_NONE;
                if (!LexTryParseString(Page, *Arguments[1]) || Page < 0 || Page > 3)
                {
                    return false;
                }
                Tag = FName(*FString::Printf(TEXT("CharacterMenuPage%d"), Page));
            }
            auto Button = FindMenuButton(Menu->TakeWidget(), Tag);
            if (Tag.IsNone() || !Button || !Button->IsEnabled())
            {
                return false;
            }
            Button->SimulateClick();
            return true;
        }
        auto* Panel = FindPanel(Controller);
        if (Action == TEXT("UMG.Open") && Arguments.Num() == 1)
        {
            const auto* HUD = Controller.GetHUD<AMyHUD>();
            const auto* Menu = HUD ? HUD->GetCharacterMenu() : nullptr;
            if (!Panel || !Panel->IsVisible() || (Menu && (!Menu->IsVisible() || Menu->GetActivePage() != 3)))
            {
                Controller.RuntimeRecipe();
            }
            return FindPanel(Controller) != nullptr;
        }
        const auto* HUD = Controller.GetHUD<AMyHUD>();
        const auto* Menu = HUD ? HUD->GetCharacterMenu() : nullptr;
        if (!Panel || !Panel->IsVisible() || (Menu && (!Menu->IsVisible() || Menu->GetActivePage() != 3)))
        {
            return false;
        }
        if (Action == TEXT("UMG.GraphNode") || Action == TEXT("UMG.GraphPort") || Action == TEXT("UMG.GraphEdge"))
        {
            auto* Canvas = Cast<UAbilityRuntimeGraphCanvas>(Panel->GetWidgetFromName(TEXT("RecipeGraphCanvas")));
            FGuid From;
            FGuid To;
            const bool bNode = Action == TEXT("UMG.GraphNode") && Arguments.Num() == 2;
            const bool bPort = Action == TEXT("UMG.GraphPort") && Arguments.Num() == 3;
            const bool bEdge = Action == TEXT("UMG.GraphEdge") && Arguments.Num() == 4;
            if (!Canvas || !Canvas->IsVisible() || (!bNode && !bPort && !bEdge) || !FGuid::Parse(Arguments[1], From) ||
                (bEdge && !FGuid::Parse(Arguments[3], To)))
            {
                return false;
            }
            FVector2D Position;
            const bool bLocated = bNode ? Canvas->GetCanvasNodePosition(From, Position)
                                  : bPort
                                      ? Canvas->GetCanvasPortPosition(From, FName(*Arguments[2]), Position)
                                      : Canvas->GetCanvasEdgePosition(From, FName(*Arguments[2]), To, .86f, Position);
            if (!bLocated)
            {
                return false;
            }
            // 在下一次原生Tick向真正Slate画布送指针事件，经过与鼠标相同的几何命中和选择委托。
            // 不直接调用面板选择回调、服务器编辑或任何游戏效果。
            const auto Geometry = Canvas->GetCachedGeometry();
            const FVector2D Absolute = Geometry.LocalToAbsolute(Position);
            if (!Geometry.IsUnderLocation(Absolute))
            {
                // 不可把画布外的逻辑点直接派发成“真实点击”；测试应先适应窗口或平移到可见区域。
                return false;
            }
            const FPointerEvent Event(0, Absolute, Absolute, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton,
                                      0.f, FModifierKeysState());
            Canvas->SetUserFocus(&Controller);
            return Canvas->TakeWidget()->OnMouseButtonDown(Geometry, Event).IsEventHandled();
        }
        if (Action == TEXT("UMG.Select") && Arguments.Num() == 3)
        {
            const FString& Control = Arguments[1];
            if (Control != TEXT("RecipeDefinition") && Control != TEXT("RecipeNode") &&
                Control != TEXT("RecipeModifier") && Control != TEXT("RecipeFragment") &&
                Control != TEXT("RecipePort") && Control != TEXT("RecipeGraphSource"))
            {
                return false;
            }
            auto* Selector = Cast<UComboBoxString>(Panel->GetWidgetFromName(FName(*Control)));
            if (!Selector || !Selector->GetIsEnabled() || !Selector->IsVisible())
            {
                return false;
            }
            int32 Index = INDEX_NONE;
            if (Control == TEXT("RecipeDefinition"))
            {
                Index = Developer.GetDefinitions().IndexOfByPredicate(
                    [&](const FAbilityRuntimeDeveloperDefinition& Row)
                    {
                        return Row.Base.bSelectable && Row.DefinitionId == FName(*Arguments[2]);
                    });
            }
            else if (Control == TEXT("RecipeNode"))
            {
                if (!LexTryParseString(Index, *Arguments[2]))
                {
                    return false;
                }
            }
            else
            {
                Index = Selector->FindOptionIndex(Arguments[2]);
            }
            if (Index < 0 || Index >= Selector->GetOptionCount())
            {
                return false;
            }
            Selector->SetUserFocus(&Controller);
            Selector->SetSelectedIndex(Index);
            return Selector->GetSelectedIndex() == Index;
        }
        if (Action == TEXT("UMG.Value") && Arguments.Num() == 3)
        {
            const FString& Control = Arguments[1];
            if (Control != TEXT("RecipeValue") && Control != TEXT("RecipeRadius") &&
                Control != TEXT("RecipeInterval") && Control != TEXT("RecipeMaxDepth") &&
                Control != TEXT("RecipeBranches") && Control != TEXT("RecipeCount") && Control != TEXT("RecipeFalloff"))
            {
                return false;
            }
            auto* Spin = Cast<USpinBox>(Panel->GetWidgetFromName(FName(*Control)));
            float Value;
            if (!Spin || !Spin->GetIsEnabled() || !Spin->IsVisible() || !LexTryParseString(Value, *Arguments[2]) ||
                !FMath::IsFinite(Value) || Value < Spin->GetMinValue() || Value > Spin->GetMaxValue())
            {
                return false;
            }
            // SSpinBox的焦点进入会把绑定值装入独立EditableText编辑缓冲；编辑中SetValue不会改该缓冲。
            // 先提交此前控件的旧编辑，再设新数值，最后进入新编辑，否则下一次按钮失焦会把旧文本写回1。
            // 只操作真实控件/焦点，不直接调用Developer事务或伪造OnValueCommitted。
            auto* Neutral = Panel->GetWidgetFromName(TEXT("RecipeGraphAccepted"));
            if (!Neutral || !Neutral->IsVisible() || !Neutral->GetIsEnabled())
            {
                Neutral = Panel->GetWidgetFromName(TEXT("RecipeApply"));
            }
            if (Neutral)
            {
                Neutral->SetUserFocus(&Controller);
            }
            const float Before = Spin->GetValue();
            Spin->SetValue(Value);
            const float AfterSet = Spin->GetValue();
            Spin->SetUserFocus(&Controller);
            const float AfterFocus = Spin->GetValue();
            UE_LOG(LogTemp, Display,
                   TEXT("[RuntimeRecipeProbe] UMG numeric %s requested=%g before=%g afterSet=%g afterFocus=%g"),
                   *Control, Value, Before, AfterSet, AfterFocus);
            return FMath::IsNearlyEqual(AfterFocus, Value);
        }
        if (Action == TEXT("UMG.Click") && Arguments.Num() == 2)
        {
            const FString& Control = Arguments[1];
            if (Control != TEXT("RecipeBegin") && Control != TEXT("RecipeApply") && Control != TEXT("RecipeRemove") &&
                Control != TEXT("RecipeHide") && Control != TEXT("RecipeClose") &&
                Control != TEXT("RecipePreviewEdit") && Control != TEXT("RecipeApplyDraft") &&
                Control != TEXT("RecipeDiscardDraft") && Control != TEXT("RecipeUndoLast") &&
                Control != TEXT("RecipeRemoveSource") && Control != TEXT("RecipeReset") &&
                Control != TEXT("RecipeGraphFit") && Control != TEXT("RecipeGraphAccepted") &&
                Control != TEXT("RecipeGraphDraft") && Control != TEXT("RecipeBaseMode"))
            {
                return false;
            }
            auto* Button = Cast<UButton>(Panel->GetWidgetFromName(FName(*Control)));
            if (!Button || !Button->GetIsEnabled() || !Button->IsVisible())
            {
                return false;
            }
            // 与真实UMG点击相同委托；Hide/Close仍经原控制器的菜单注销及焦点恢复路径。
            Button->SetUserFocus(&Controller);
            for (const FName SpinName :
                 {FName(TEXT("RecipeValue")), FName(TEXT("RecipeRadius")), FName(TEXT("RecipeInterval"))})
            {
                if (const auto* Spin = Cast<USpinBox>(Panel->GetWidgetFromName(SpinName)))
                {
                    UE_LOG(LogTemp, Display, TEXT("[RuntimeRecipeProbe] UMG afterClickFocus %s %s=%g"), *Control,
                           *SpinName.ToString(), Spin->GetValue());
                }
            }
            Button->OnClicked.Broadcast();
            return true;
        }
        return false;
    }
    /** 仅临时空QA地图的真实法师Pawn可在Unselected/Mage之间往返；不加载/写入存档，不修改生产预览资格校验。 */
    bool SetFixtureProfession(UWorld& World, const TArray<FString>& Arguments)
    {
        if (Arguments.Num() != 3 || World.GetNetMode() == NM_Client ||
            !World.GetOutermost()->GetName().EndsWith(TEXT("L_Batch6Test")))
        {
            return false;
        }
        auto* Pawn = FindObject<APawn>(nullptr, *Arguments[1]);
        auto* State = Pawn ? Pawn->GetPlayerState<AMyPlayerState>() : nullptr;
        auto* MageClass = LoadClass<APawn>(nullptr, TEXT("/Game/BluePrints/Characters/Player/BP_Player.BP_Player_C"));
        if (!Pawn || Pawn->GetWorld() != &World || !Pawn->HasAuthority() || !Pawn->IsPlayerControlled() || !State ||
            State->GetWorld() != &World || !State->HasAuthority() || Pawn->GetController()->PlayerState != State ||
            !MageClass || !Pawn->IsA(MageClass) ||
            (State->Profession != EPlayerProfession::Unselected && State->Profession != EPlayerProfession::Mage))
        {
            return false;
        }
        const auto* Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
        if (!Developer || Developer->GetDeveloperState().bPreviewActive)
        {
            return false;
        }
        EPlayerProfession Requested;
        if (Arguments[2].Equals(TEXT("Mage"), ESearchCase::IgnoreCase))
        {
            Requested = EPlayerProfession::Mage;
        }
        else if (Arguments[2].Equals(TEXT("Unselected"), ESearchCase::IgnoreCase))
        {
            Requested = EPlayerProfession::Unselected;
        }
        else
        {
            return false;
        }
        State->Profession = Requested;
        State->ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("[RuntimeRecipeProbe] Fixture profession %s=%s"), *Pawn->GetPathName(),
               *Arguments[2]);
        return true;
    }

    /** 空QA地图的显式权威夹具设置，走ASC base-value通路以保留聚合器和复制；不是客户端RPC。 */
    bool SetFixtureBase(UWorld& World, const TArray<FString>& Arguments)
    {
        if (Arguments.Num() != 4 || World.GetNetMode() == NM_Client ||
            !World.GetOutermost()->GetName().EndsWith(TEXT("L_Batch6Test")))
        {
            return false;
        }
        auto* Actor = FindObject<AActor>(nullptr, *Arguments[1]);
        auto* Pawn = Cast<APawn>(Actor);
        auto* ASC = Actor ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor) : nullptr;
        float Value = 0.f;
        if (!Actor || Actor->GetWorld() != &World || !Actor->HasAuthority() || !ASC ||
            (!Actor->ActorHasTag(TEXT("DynamicMageRealCastingQA")) && !(Pawn && Pawn->IsPlayerControlled())) ||
            !LexTryParseString(Value, *Arguments[3]) || !FMath::IsFinite(Value) || FMath::Abs(Value) > 1000000.f)
        {
            return false;
        }
        const FString Name = Arguments[2].Replace(TEXT("_"), TEXT("")).ToLower();
        FGameplayAttribute Attribute;
        if (Name == TEXT("mana"))
        {
            Attribute = UMyAttributeSet::GetManaAttribute();
        }
        else if (Name == TEXT("maxmana"))
        {
            Attribute = UMyAttributeSet::GetMaxManaAttribute();
        }
        else if (Name == TEXT("manaregeneration"))
        {
            Attribute = UMyAttributeSet::GetManaRegenerationAttribute();
        }
        else if (Name == TEXT("health"))
        {
            Attribute = UMyAttributeSet::GetHealthAttribute();
        }
        else if (Name == TEXT("maxhealth"))
        {
            Attribute = UMyAttributeSet::GetMaxHealthAttribute();
        }
        else if (Name == TEXT("blockchance"))
        {
            Attribute = UMyAttributeSet::GetBlockChanceAttribute();
        }
        else if (Name == TEXT("armor"))
        {
            Attribute = UMyAttributeSet::GetArmorAttribute();
        }
        else
        {
            return false;
        }
        ASC->SetNumericAttributeBase(Attribute, Value);
        UE_LOG(LogTemp, Display, TEXT("[RuntimeRecipeProbe] Fixture base %s %s=%g"), *Actor->GetPathName(),
               *Arguments[2], ASC->GetNumericAttributeBase(Attribute));
        return true;
    }

    /** 只允许来自指定世界的本地控制器；联机客户端必须沿组件RPC，不能寻找服务器副本代替。 */
    void Schedule(const TArray<FString>& Arguments, UWorld* World)
    {
        APlayerController* Controller = nullptr;
        if (World)
        {
            for (auto It = World->GetPlayerControllerIterator(); It; ++It)
            {
                if (It->IsValid() && It->Get()->IsLocalController())
                {
                    Controller = It->Get();
                    break;
                }
            }
        }
        if (!Controller || !Controller->IsLocalController() || Arguments.IsEmpty() || Arguments.Num() > 10 ||
            (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
        {
            UE_LOG(LogTemp, Warning,
                   TEXT("[RuntimeRecipeProbe] Requires an explicit local game controller and action."));
            return;
        }
        for (const FString& Argument : Arguments)
        {
            if (Argument.Len() > 256)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Argument exceeds the developer transport bound."));
                return;
            }
        }
        const TWeakObjectPtr<APlayerController> WeakController = Controller;
        const TWeakObjectPtr<UWorld> WeakWorld = World;
        // 一次性委托只保存弱身份和有界文本；世界/控制器结束后不续订、不延长PlayerState或编辑预览寿命。
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [WeakController, WeakWorld, Arguments](float)
            {
                auto* LocalController = WeakController.Get();
                auto* LocalWorld = WeakWorld.Get();
                if (!LocalController || !LocalWorld || LocalController->GetWorld() != LocalWorld ||
                    !LocalController->IsLocalController() || !LocalController->PlayerState)
                {
                    return false;
                }
                if (Arguments[0].Equals(TEXT("SetBase"), ESearchCase::IgnoreCase))
                {
                    if (!SetFixtureBase(*LocalWorld, Arguments))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Rejected fixture base request."));
                    }
                    return false;
                }
                if (Arguments[0].Equals(TEXT("Profession"), ESearchCase::IgnoreCase))
                {
                    if (!SetFixtureProfession(*LocalWorld, Arguments))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Rejected QA profession request."));
                    }
                    return false;
                }
                auto* Developer =
                    LocalController->PlayerState->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
                FString Error;
                if (!Developer || !Developer->InitializeCatalog(nullptr, Error))
                {
                    UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Local catalog unavailable: %s"), *Error);
                    return false;
                }
                const FString& Action = Arguments[0];
                if (Action.StartsWith(TEXT("UMG.")))
                {
                    auto* Player = Cast<AMyPlayerController>(LocalController);
                    if (!Player || !DispatchPanelAction(*Player, *Developer, Arguments))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Rejected bounded real UMG action: %s"),
                               *Action);
                        return false;
                    }
                }
                else if (Action.Equals(TEXT("Begin"), ESearchCase::IgnoreCase) && Arguments.Num() == 2)
                {
                    const auto* Definition = Developer->GetDefinitions().FindByPredicate(
                        [&Arguments](const FAbilityRuntimeDeveloperDefinition& Item)
                        {
                            return Item.Base.bSelectable && Item.Base.bPreferredForWorld &&
                                   Item.Base.AbilityTag.ToString() == Arguments[1];
                        });
                    if (Definition)
                    {
                        Developer->ServerBeginPreview(Definition->DefinitionId);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Selectable preferred identity not found."));
                    }
                }
                else if (Action.Equals(TEXT("BeginDefinition"), ESearchCase::IgnoreCase) && Arguments.Num() == 2)
                {
                    const auto* Definition = Developer->GetDefinitions().FindByPredicate(
                        [&Arguments](const FAbilityRuntimeDeveloperDefinition& Item)
                        {
                            return Item.Base.bSelectable && Item.DefinitionId == FName(*Arguments[1]);
                        });
                    if (Definition)
                    {
                        Developer->ServerBeginPreview(Definition->DefinitionId);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Exact trusted definition ID not found."));
                    }
                }
                else if (Action.Equals(TEXT("Edit"), ESearchCase::IgnoreCase) &&
                         (Arguments.Num() == 4 || Arguments.Num() == 8 || Arguments.Num() == 10))
                {
                    const auto& State = Developer->GetDeveloperState();
                    FAbilityRuntimeDeveloperEdit Edit;
                    Edit.DefinitionId = State.SelectedId;
                    Edit.ExpectedBaseHash = State.BaseHash;
                    Edit.ExpectedRevision = State.Revision;
                    Edit.ModifierId = FName(*Arguments[1]);
                    if (!LexTryParseString(Edit.NodeIndex, *Arguments[2]) ||
                        !LexTryParseString(Edit.Value, *Arguments[3]) ||
                        (Arguments.Num() >= 8 && (!LexTryParseString(Edit.Count, *Arguments[4]) ||
                                                  !LexTryParseString(Edit.Interval, *Arguments[5]) ||
                                                  !LexTryParseString(Edit.Radius, *Arguments[6]) ||
                                                  !LexTryParseString(Edit.Falloff, *Arguments[7]))))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Invalid typed numeric arguments."));
                        return false;
                    }
                    if (Arguments.Num() == 10 && (!LexTryParseString(Edit.MaxDepth, *Arguments[8]) ||
                                                  !LexTryParseString(Edit.BranchesPerTarget, *Arguments[9])))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Invalid explicit BFS integer budget."));
                        return false;
                    }
                    Developer->ServerApplyEdit(Edit);
                }
                else if (Action.Equals(TEXT("Close"), ESearchCase::IgnoreCase) &&
                         (Arguments.Num() == 1 || Arguments.Num() == 2))
                {
                    FGuid Request = FGuid::NewGuid();
                    if (Arguments.Num() == 2 && (!FGuid::Parse(Arguments[1], Request) || !Request.IsValid()))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[RuntimeRecipeProbe] Close requires a nonzero valid GUID."));
                        return false;
                    }
                    Developer->ServerClosePreview(Request, false);
                }
                else if (Action.Equals(TEXT("Reset"), ESearchCase::IgnoreCase) && Arguments.Num() == 1)
                {
                    const auto& State = Developer->GetDeveloperState();
                    Developer->ServerResetPreview(State.SelectedId, State.Revision, State.BaseHash);
                }
                else if (Action.Equals(TEXT("Remove"), ESearchCase::IgnoreCase) &&
                         (Arguments.Num() == 3 || Arguments.Num() == 5))
                {
                    const auto& State = Developer->GetDeveloperState();
                    FAbilityRuntimeDeveloperEdit Edit;
                    Edit.DefinitionId = State.SelectedId;
                    Edit.ExpectedRevision = State.Revision;
                    Edit.ExpectedBaseHash = State.BaseHash;
                    Edit.ModifierId = FName(*Arguments[1]);
                    if (!LexTryParseString(Edit.NodeIndex, *Arguments[2]))
                    {
                        return false;
                    }
                    if (Arguments.Num() == 5)
                    {
                        Edit.FragmentId = FName(*Arguments[3]);
                        Edit.OutputPortId = FName(*Arguments[4]);
                    }
                    Developer->ServerRemoveEdit(Edit);
                }
                else if (Action.Equals(TEXT("Append"), ESearchCase::IgnoreCase) && Arguments.Num() == 6)
                {
                    const auto& State = Developer->GetDeveloperState();
                    FAbilityRuntimeDeveloperEdit Edit;
                    Edit.DefinitionId = State.SelectedId;
                    Edit.ExpectedRevision = State.Revision;
                    Edit.ExpectedBaseHash = State.BaseHash;
                    Edit.ModifierId = TEXT("Append.Registered");
                    Edit.FragmentId = FName(*Arguments[1]);
                    Edit.OutputPortId = FName(*Arguments[3]);
                    if (!LexTryParseString(Edit.NodeIndex, *Arguments[2]) ||
                        !LexTryParseString(Edit.Value, *Arguments[4]) || !LexTryParseString(Edit.Radius, *Arguments[5]))
                    {
                        return false;
                    }
                    // 工具仍只传可信ID和有限数值；实际片段/端口由服务器模板验证，未登记项明确拒绝。
                    Developer->ServerApplyEdit(Edit);
                }
                else if ((Action.Equals(TEXT("Press"), ESearchCase::IgnoreCase) ||
                          Action.Equals(TEXT("Release"), ESearchCase::IgnoreCase) ||
                          Action.Equals(TEXT("PressPrimary"), ESearchCase::IgnoreCase) ||
                          Action.Equals(TEXT("ReleasePrimary"), ESearchCase::IgnoreCase)) &&
                         Arguments.Num() == 1)
                {
                    if (auto* Player = Cast<AMyPlayerController>(LocalController))
                    {
                        const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
                        const bool bPrimary = Action.EndsWith(TEXT("Primary"), ESearchCase::IgnoreCase);
                        const EInputEvent Event =
                            Action.StartsWith(TEXT("Press"), ESearchCase::IgnoreCase) ? IE_Pressed : IE_Released;
                        // 库存联动仍依赖真实LMB GA的原输入/目标/Notify/Commit，绝不直接广播进攻信号。
                        const FKey Key = bPrimary ? EKeys::LeftMouseButton : EKeys::T;
                        Player->InputKey(FInputKeyEventArgs(nullptr, Device, Key, Event, FPlatformTime::Cycles64()));
                    }
                }
                else
                {
                    UE_LOG(
                        LogTemp, Warning,
                        TEXT(
                            "[RuntimeRecipeProbe] Usage: Begin <identity> | Edit <modifier> <node> <value> [count interval radius falloff] | Append <fragment> <node> <port> <duration> <radius> | Remove <modifier> <node> [fragment port] | Reset | Press | Release | Close [guid]"));
                    return false;
                }
                UE_LOG(LogTemp, Display,
                       TEXT("[RuntimeRecipeProbe] Dispatched %s world=%s netMode=%d role=%d local=%d"), *Action,
                       *LocalWorld->GetPathName(), static_cast<int32>(LocalWorld->GetNetMode()),
                       static_cast<int32>(LocalController->GetLocalRole()), LocalController->IsLocalController());
                return false;
            }));
    }

    FAutoConsoleCommandWithWorldAndArgs Command(
        TEXT("RuntimeRecipe.Probe"),
        TEXT(
            "Development-only next-native-tick panel RPC/input: Begin <identity>, Edit <modifier> <node> <value> [count interval radius falloff], Press, Release, Close. Never bypasses original casting or damages directly."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Schedule));
} // namespace AbilityRuntimeRecipeNetworkProbe
#endif
