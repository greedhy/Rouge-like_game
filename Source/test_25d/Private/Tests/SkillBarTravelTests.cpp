/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.UI.SkillBar.RestoredAbilitiesNotifyReadyWithSlots
 * - test_25d.UI.SkillBar.ReplicationWaitsForAbilityReferencesAndRefreshesLaterUpdates
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "Game/LoadScreenSaveGame.h"
#include "Player/MyPlayerState.h"
#include "MyGameplayTags.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestoredSkillsNotifyUI, "test_25d.UI.SkillBar.RestoredAbilitiesNotifyReadyWithSlots",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.UI.SkillBar.RestoredAbilitiesNotifyReadyWithSlots 回归，失败信息由下方断言交给自动化框架收集。 */
bool FRestoredSkillsNotifyUI::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* State = World->SpawnActor<AMyPlayerState>();
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    ASC->InitAbilityActorInfo(State, State);
    auto* Save = NewObject<ULoadScreenSaveGame>();
    FSavedAbility Data;
    Data.GameplayAbility = UMyGameplayAbility::StaticClass();
    Data.AbilityLevel = 3;
    Data.AbilitySlot = FMyGameplayTags::Get().InputTag_1;
    Data.AbilityStatus = FMyGameplayTags::Get().Abilities_Status_Equipped;
    Data.AbilityType = FMyGameplayTags::Get().Abilities_Type_Offensive;
    Save->SavedAbilities.Add(Data);
    int32 Notifications = 0;
    ASC->AbilitiesGiven.AddLambda(
        [&]()
        {
            ++Notifications;
            TestTrue(TEXT("Readiness is visible before notifying HUD"), ASC->bStartupAbilityGiven);
            auto* Spec = ASC->GetSpecWithSlot(Data.AbilitySlot);
            TestNotNull(TEXT("Equipped slot is restored before notifying HUD"), Spec);
            if (Spec)
                TestEqual(TEXT("Saved rank preserved"), Spec->Level, 3);
        });
    ASC->AddCharacterAbilitiesFromSaveData(Save);
    TestTrue(TEXT("Restored abilities use the HUD readiness flag"), ASC->bStartupAbilityGiven);
    TestEqual(TEXT("HUD receives restore completion"), Notifications, 1);
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplicatedSkillsNotifyUI,
                                 "test_25d.UI.SkillBar.ReplicationWaitsForAbilityReferencesAndRefreshesLaterUpdates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.UI.SkillBar.ReplicationWaitsForAbilityReferencesAndRefreshesLaterUpdates 回归，失败信息由下方断言交给自动化框架收集。 */
bool FReplicatedSkillsNotifyUI::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* State = World->SpawnActor<AMyPlayerState>();
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    ASC->InitAbilityActorInfo(State, State);
    int32 Notifications = 0;
    ASC->AbilitiesGiven.AddLambda(
        [&]()
        {
            ++Notifications;
        });
    // Model a replicated spec whose NetGUID has not resolved to its ability CDO.
    ASC->GetActivatableAbilities().Add(FGameplayAbilitySpec());
    UFunction* OnRep = ASC->FindFunctionChecked(TEXT("OnRep_ActivateAbilities"));
    ASC->ProcessEvent(OnRep, nullptr);
    TestFalse(TEXT("Unresolved ability references are not UI-ready"), ASC->bStartupAbilityGiven);
    TestEqual(TEXT("Incomplete list is not published"), Notifications, 0);
    ASC->GetActivatableAbilities()[0].Ability = GetMutableDefault<UMyGameplayAbility>();
    ASC->ProcessEvent(OnRep, nullptr);
    TestTrue(TEXT("GAS retry makes resolved abilities ready"), ASC->bStartupAbilityGiven);
    TestEqual(TEXT("Resolved retry reaches UI"), Notifications, 1);
    ASC->GetActivatableAbilities()[0].DynamicAbilityTags.AddTag(FMyGameplayTags::Get().InputTag_1);
    ASC->ProcessEvent(OnRep, nullptr);
    TestEqual(TEXT("Later replicated slots also refresh UI"), Notifications, 2);
    World->DestroyWorld(false);
    return true;
}
#endif
