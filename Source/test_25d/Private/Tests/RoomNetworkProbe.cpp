/** 主要调用链：RoomSubsystem开发探针调用 -> 读取显式RoomProbe启动参数 -> 消费递增编号的命令JSON -> UI委托/旅行 -> 输出联机状态。 */
// Explicit opt-in development-only harness for testing actual UI delegates in separate LAN processes.
#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RoomSubsystem.h"
#include "Online/RoomLobby.h"
#include "UI/Widget/RoomMenuWidget.h"
#include "UI/HUD/LoadScreenHUD.h"
#include "UI/ViewModel/MVVM_LoadScreen.h"
#include "UI/ViewModel/MVVM_LoadSlot.h"
#include "Game/MyGameModeBase.h"
#include "Game/LoadScreenSaveGame.h"
#include "Game/MyGameInstance.h"
#include "Player/MyPlayerState.h"
#include "Character/StanderPlayer.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"

// 仅在显式启用的开发联机进程中消费命令，并把真实UI与房间状态写回报告。
void TickRoomNetworkProbe(URoomSubsystem* Rooms)
{
    static FString Role, Run, Folder, LastCommandError, TestSlot;
    static bool bParsed = false, bEnabled = false;
    static int32 LastId = -1;
    static TSet<FString> SeenStanderMontages;
    // 启动参数只解析一次，限制报告目录标识，默认不启用探针。
    if (!bParsed)
    {
        bParsed = true;
        FParse::Value(FCommandLine::Get(), TEXT("RoomProbe="), Role);
        FParse::Value(FCommandLine::Get(), TEXT("RoomProbeRun="), Run);
        auto Safe = [](const FString& S)
        {
            if (S.IsEmpty() || S.Len() > 64)
                return false;
            for (TCHAR C : S)
                if (!FChar::IsAlnum(C) && C != '_')
                    return false;
            return true;
        };
        bEnabled = Safe(Role) && Safe(Run) && !IsRunningCommandlet();
        Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Rooms/Network"), Run);
        if (bEnabled)
        {
            IFileManager::Get().MakeDirectory(*Folder, true);
            // The room status poll runs every 250 ms; fast melee montages can finish in that interval.
            const TWeakObjectPtr<URoomSubsystem> WeakRooms(Rooms);
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                [WeakRooms](float)
                {
                    if (!WeakRooms.IsValid())
                    {
                        return false;
                    }
                    if (auto* World = WeakRooms->GetWorld(); World && World->HasBegunPlay())
                    {
                        for (TActorIterator<AStanderPlayer> It(World); It; ++It)
                        {
                            const auto* Anim = It->GetMesh()->GetAnimInstance();
                            const auto* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
                            if (Montage)
                            {
                                const FString View = It->IsLocallyControlled() ? TEXT("owner:") : TEXT("remote:");
                                SeenStanderMontages.Add(View + Montage->GetName());
                            }
                        }
                    }
                    return true;
                }));
        }
    }
    if (!bEnabled || !Rooms->GetWorld() || !Rooms->GetWorld()->HasBegunPlay())
        return;
    UWorld* World = Rooms->GetWorld();
    auto* PC = Rooms->GetGameInstance()->GetFirstLocalPlayerController();
    if (!PC)
        return;
    auto FindWidget = [World]()
    {
        for (TObjectIterator<URoomMenuWidget> It; It; ++It)
            if (It->GetWorld() == World && It->IsInViewport() && It->IsVisible())
                return *It;
        return (URoomMenuWidget*)nullptr;
    };
    auto* Widget = FindWidget();
    FString Input;
    TSharedPtr<FJsonObject> Command;
    if (FFileHelper::LoadFileToString(Input, *FPaths::Combine(Folder, Role + TEXT("-command.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Input), Command) && Command.IsValid())
    {
        int32 Id = Command->GetIntegerField(TEXT("id"));
        // 仅处理新编号，防止每帧重复执行旅行或按钮事件。
        if (Id > LastId)
        {
            LastId = Id;
            LastCommandError.Reset();
            const FString Op = Command->GetStringField(TEXT("op"));
            FString Value;
            Command->TryGetStringField(TEXT("value"), Value);
            if (Op == TEXT("select"))
            {
                auto* HUD = Cast<ALoadScreenHUD>(PC->GetHUD());
                auto* GM = World->GetAuthGameMode<AMyGameModeBase>();
                if (HUD && GM && HUD->LoadScreenViewModel)
                {
                    auto* Slot = HUD->LoadScreenViewModel->GetLoadSlotViewModelByIndex(0);
                    TestSlot = TEXT("RoomQA_") + Run + TEXT("_") + Role;
                    Slot->SetLoadSlotName(TestSlot);
                    Slot->SetPlayerName(Value);
                    Slot->SlotStatus = Taken;
                    Slot->SetMapName(GM->DefaultMapName);
                    Slot->MapAssetName = GM->DefaultMap.ToSoftObjectPath().GetAssetName();
                    Slot->PlayerStartTag = GM->DefaultPlayerStartTag;
                    FString Profession;
                    Command->TryGetStringField(TEXT("field"), Profession);
                    const bool bStander = Profession == TEXT("Stander");
                    GM->SaveSlotData(Slot, 0, bStander ? EPlayerProfession::Stander : EPlayerProfession::Mage);
                    HUD->LoadScreenViewModel->SelectSlotButtonPressed(0);
                    HUD->LoadScreenViewModel->PlayButtonPressed();
                }
                else
                    LastCommandError = TEXT("Load screen not ready");
            }
            else if (Op == TEXT("click"))
            {
                auto* Button = Widget ? Cast<UButton>(Widget->GetWidgetFromName(FName(*Value))) : nullptr;
                if (Button && Button->GetIsEnabled())
                    Button->OnClicked.Broadcast();
                else
                    LastCommandError = TEXT("Button missing or disabled: ") + Value;
            }
            else if (Op == TEXT("input"))
            {
                const FString Field = Command->GetStringField(TEXT("field"));
                auto* Text = Widget ? Cast<UEditableTextBox>(Widget->GetWidgetFromName(FName(*Field))) : nullptr;
                if (Text)
                    Text->SetText(FText::FromString(Value));
                else
                    LastCommandError = TEXT("Input missing");
            }
            else if (Op == TEXT("ability_input"))
            {
                auto* Pawn = Cast<AStanderPlayer>(PC->GetPawn());
                auto* ASC = Pawn ? Cast<UMyAbilitySystemComponent>(Pawn->GetAbilitySystemComponent()) : nullptr;
                const auto InputTag = FGameplayTag::RequestGameplayTag(FName(*Value), false);
                if (ASC && InputTag.IsValid())
                {
                    ASC->AbilityInputTagHeld(InputTag);
                }
                else
                {
                    LastCommandError = TEXT("Standard ASC or input tag missing");
                }
            }
            else if (Op == TEXT("screenshot"))
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(Folder, Role + TEXT("-ui.png")), true, false);
            else if (Op == TEXT("direct_join"))
                PC->ClientTravel(Value, TRAVEL_Absolute);
            else if (Op == TEXT("quit"))
            {
                if (!TestSlot.IsEmpty())
                    UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
                FPlatformMisc::RequestExit(false);
            }
        }
    }
    // 操作可能切换界面；重新查找后再采集状态。
    Widget = FindWidget();
    TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
    State->SetStringField(TEXT("role"), Role);
    State->SetStringField(TEXT("run"), Run);
    State->SetNumberField(TEXT("last_id"), LastId);
    State->SetStringField(TEXT("command_error"), LastCommandError);
    State->SetStringField(TEXT("world"), World->GetMapName());
    State->SetNumberField(TEXT("net_mode"), World->GetNetMode());
    State->SetStringField(TEXT("flow"), StaticEnum<ERoomFlowState>()->GetNameStringByValue((int64)Rooms->State));
    State->SetStringField(TEXT("error"), Rooms->LastError.ToString());
    State->SetBoolField(TEXT("busy"), Rooms->IsBusy());
    State->SetBoolField(TEXT("widget"), Widget != nullptr);
    State->SetBoolField(TEXT("has_pawn"), PC->GetPawn() != nullptr);
    if (const auto* PS = PC->GetPlayerState<AMyPlayerState>())
        State->SetStringField(TEXT("backpack_identity"), PS->GetBackpackPersistencePlayerId());
    State->SetNumberField(
        TEXT("page"),
        Widget ? CastChecked<UWidgetSwitcher>(Widget->GetWidgetFromName(TEXT("RoomPages")))->GetActiveWidgetIndex()
               : -1);
    State->SetStringField(TEXT("password"), TEXT(""));
    TArray<TSharedPtr<FJsonValue>> Players;
    if (auto* GS = World->GetGameState<ARoomLobbyGameState>())
    {
        State->SetStringField(TEXT("password"), GS->RoomPassword);
        State->SetBoolField(TEXT("is_owner"), GS->RoomOwner && GS->RoomOwner == PC->PlayerState);
        for (const auto& P : GS->PlayerArray)
            if (P)
                Players.Add(MakeShared<FJsonValueString>(P->GetPlayerName()));
    }
    else if (auto* GameState = World->GetGameState())
        for (const auto& P : GameState->PlayerArray)
            if (P)
                Players.Add(MakeShared<FJsonValueString>(P->GetPlayerName()));
    State->SetArrayField(TEXT("players"), Players);
    State->SetBoolField(TEXT("local_stander"), Cast<AStanderPlayer>(PC->GetPawn()) != nullptr);
    TArray<TSharedPtr<FJsonValue>> Montages;
    for (const FString& Montage : SeenStanderMontages)
    {
        Montages.Add(MakeShared<FJsonValueString>(Montage));
    }
    State->SetArrayField(TEXT("stander_montages"), Montages);
    FString Output;
    FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Output));
    FFileHelper::SaveStringToFile(Output, *FPaths::Combine(Folder, Role + TEXT("-state.json")),
                                  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
#endif
