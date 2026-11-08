/** 主要调用链：开发控制台试炼命令 -> 隔离自动化存档身份 -> RoomSubsystem试炼入口 -> 真实生成房间。 */
// Explicit, development-only opt-in. Uses a dedicated save identity, never a selected user's save.
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "Game/MyGameInstance.h"
#include "Online/RoomSubsystem.h"
#include "Engine/World.h"
static FAutoConsoleCommandWithWorld SwordTrialProbe(
    TEXT("test25d.SwordShield.EnterTrial"), TEXT("Enter sword trial using an isolated automation save identity."),
    FConsoleCommandWithWorldDelegate::CreateLambda(
        [](UWorld* World)
        {
            if (!World || World->WorldType != EWorldType::PIE)
                return;
            auto* GI = World->GetGameInstance<UMyGameInstance>();
            if (!GI)
                return;
            GI->LoadSlotName = TEXT("SwordShield_Automation");
            GI->LoadSlotIndex = 0;
            // Match the real new-save menu, which seeds the legacy dungeon's arrival group.
            GI->PlayerStartTag = TEXT("Dungeon1PlayerStart1");
            auto* Rooms = GI->GetSubsystem<URoomSubsystem>();
            Rooms->ConfigureSelection(TEXT("SwordShieldTest"),
                                      TSoftObjectPtr<UWorld>(FSoftObjectPath(
                                          TEXT("/Game/RoguelikeGeneration/Maps/L_RoguelikeRun.L_RoguelikeRun"))));
            Rooms->StartSwordShieldTrial();
        }));
#endif
