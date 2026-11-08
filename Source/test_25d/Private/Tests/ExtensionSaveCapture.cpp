/** 主要调用链：控制台Extension.QA.PrepareSaves建立独立存档身份 -> Extension.QA.SaveSnapshot -> 玩家SaveProgress -> 重读存档并输出JSON。 */
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/MyGameInstance.h"
#include "Game/LoadScreenSaveGame.h"
#include "Player/MyPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionIntegration.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace ExtensionSaveCapture
{
    // 将报告限定在QA输出目录，调用者名称只作为文件名使用。
    void Write(const FString& Name, const TSharedRef<FJsonObject>& Report)
    {
        const FString Folder = FPaths::ProjectSavedDir() / TEXT("GAAuthoring/Batch5");
        IFileManager::Get().MakeDirectory(*Folder, true);
        FString JSON;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&JSON));
        FFileHelper::SaveStringToFile(JSON, *(Folder / FPaths::GetCleanFilename(Name) + TEXT(".json")));
    }
} // namespace ExtensionSaveCapture
// 每个PIE世界分配唯一QA槽，避免覆盖用户当前存档。
static FAutoConsoleCommand ExtensionPrepareSaves(
    TEXT("Extension.QA.PrepareSaves"), TEXT("Prepare unique isolated PIE saves for extension travel tests."),
    FConsoleCommandDelegate::CreateLambda(
        []
        {
            if (!GEngine)
                return;
            auto Report = MakeShared<FJsonObject>();
            TArray<TSharedPtr<FJsonValue>> Names;
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                UWorld* World = Context.World();
                if (Context.WorldType != EWorldType::PIE || !World)
                    continue;
                auto* PC = World->GetFirstPlayerController();
                auto* GI = Cast<UMyGameInstance>(World->GetGameInstance());
                if (!GI || !PC || !Cast<AExtensionPlayerCharacter>(PC->GetPawn()))
                    continue;
                GI->LoadSlotName = TEXT("PrototypeExtensions_QA_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
                GI->LoadSlotIndex = 0;
                Names.Add(MakeShared<FJsonValueString>(GI->LoadSlotName));
            }
            Report->SetArrayField(TEXT("saves"), Names);
            ExtensionSaveCapture::Write(TEXT("qa-saves"), Report);
        }));
// 仅保存独立QA身份下的权威玩家，再从磁盘读回以验证真正持久化的能力记录。
static FAutoConsoleCommand ExtensionSaveSnapshot(
    TEXT("Extension.QA.SaveSnapshot"),
    TEXT(
        "SaveSnapshot <report>: save/reload each authoritative extension player's skill records in isolated QA slots."),
    FConsoleCommandWithArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args)
        {
            if (!GEngine || Args.Num() != 1)
                return;
            auto Report = MakeShared<FJsonObject>();
            TArray<TSharedPtr<FJsonValue>> Players;
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                UWorld* World = Context.World();
                if (Context.WorldType != EWorldType::PIE || !World ||
                    !Cast<AExtensionGameMode>(World->GetAuthGameMode()))
                    continue;
                auto* GI = Cast<UMyGameInstance>(World->GetGameInstance());
                if (!GI || !GI->LoadSlotName.StartsWith(TEXT("PrototypeExtensions_QA_")))
                    continue;
                for (auto It = World->GetPlayerControllerIterator(); It; ++It)
                {
                    auto* Player = Cast<AExtensionPlayerCharacter>(It->Get()->GetPawn());
                    auto* State = It->Get()->GetPlayerState<AMyPlayerState>();
                    if (!Player || !State)
                        continue;
                    Player->SaveProgress_Implementation(TEXT("Batch5QA"));
                    auto* Save = Cast<ULoadScreenSaveGame>(UGameplayStatics::LoadGameFromSlot(GI->LoadSlotName, 0));
                    if (!Save)
                        continue;
                    auto Row = MakeShared<FJsonObject>();
                    Row->SetNumberField(TEXT("slot"), State->GetTravelPlayerSlot());
                    Row->SetNumberField(TEXT("points"), Save->SpellPoints);
                    TArray<TSharedPtr<FJsonValue>> Abilities;
                    for (const auto& Ability : Save->SavedAbilities)
                    {
                        const FString Value =
                            FString::Printf(TEXT("%s|%s|%d|%s|%s"), *Ability.AbilityTag.ToString(),
                                            *GetPathNameSafe(Ability.GameplayAbility.Get()), Ability.AbilityLevel,
                                            *Ability.AbilitySlot.ToString(), *Ability.AbilityStatus.ToString());
                        Abilities.Add(MakeShared<FJsonValueString>(Value));
                    }
                    Row->SetArrayField(TEXT("abilities"), Abilities);
                    Players.Add(MakeShared<FJsonValueObject>(Row));
                }
            }
            Report->SetArrayField(TEXT("players"), Players);
            ExtensionSaveCapture::Write(Args[0], Report);
        }));
#endif
