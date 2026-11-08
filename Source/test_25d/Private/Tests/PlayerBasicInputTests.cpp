/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Player.BasicInput.KeyboardOnlyAndDirectLMB
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Tests/DungeonPlayerRunTestTypes.h"
#include "Player/MyPlayerController.h"
#include "PureMass/PureMassPlayerController.h"
#include "Character/StanderPlayer.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "InputActionValue.h"
#include "MyGameplayTags.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerBasicInputTest, "test_25d.Player.BasicInput.KeyboardOnlyAndDirectLMB",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Player.BasicInput.KeyboardOnlyAndDirectLMB 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPlayerBasicInputTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    const auto& Tags = FMyGameplayTags::Get();
    for (bool Mass : {false, true})
    {
        auto* PC = Mass ? static_cast<AMyPlayerController*>(World->SpawnActor<APureMassPlayerController>())
                        : World->SpawnActor<AMyPlayerController>();
        auto* Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
        auto* State = World->SpawnActor<AMyPlayerState>();
        // SetPawn avoids loading a user's progress in this isolated input test.
        PC->SetPawn(Pawn);
        Pawn->AttachState(State);
        auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
        FGameplayAbilitySpec Spec(UDungeonPlayerRunTestTaggedAbility::StaticClass());
        Spec.GetDynamicSpecSourceTags().AddTag(Tags.InputTag_LMB);
        const auto Handle = ASC->GiveAbility(Spec);
        auto Held = [&]
        {
            if (Mass)
                CastChecked<APureMassPlayerController>(PC)->Held(Tags.InputTag_LMB);
            else
                PC->AbilityInputTagHeld(Tags.InputTag_LMB);
        };
        auto Released = [&]
        {
            if (Mass)
                CastChecked<APureMassPlayerController>(PC)->Released(Tags.InputTag_LMB);
            else
                PC->AbilityInputTagReleased(Tags.InputTag_LMB);
        };
        Held();
        TestTrue(TEXT("LMB activates with no Shift and no hovered enemy"),
                 ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
        TestTrue(TEXT("LMB does not request movement"), Pawn->GetPendingMovementInputVector().IsNearlyZero());
        ASC->AddLooseGameplayTag(Tags.Player_Block_InputReleased);
        Released();
        TestFalse(TEXT("Release reaches GAS even after input becomes blocked"),
                  ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
        ASC->CancelAbilityHandle(Handle);
        ASC->AddLooseGameplayTag(Tags.Player_Block_InputHeld);
        Held();
        TestFalse(TEXT("Blocked hold cannot activate"), ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
        ASC->RemoveLooseGameplayTag(Tags.Player_Block_InputHeld);
        PC->bBackpackUIOpen = true;
        Held();
        TestFalse(TEXT("Open backpack blocks combat"), ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
        PC->bBackpackUIOpen = false;
        if (Mass)
            CastChecked<APureMassPlayerController>(PC)->MoveMass(FInputActionValue(FVector2D(0, 1)));
        else
            PC->Move(FInputActionValue(FVector2D(0, 1)));
        TestFalse(TEXT("Keyboard still requests movement"), Pawn->GetPendingMovementInputVector().IsNearlyZero());
        ASC->ClearAbility(Handle);
        FGameplayAbilitySpec Toggle(UDungeonPlayerRunTestToggle::StaticClass());
        Toggle.GetDynamicSpecSourceTags().AddTag(Tags.InputTag_LMB);
        Toggle.GetDynamicSpecSourceTags().AddTag(Tags.Abilities_Status_Equipped);
        const auto ToggleHandle = ASC->GiveAbility(Toggle);
        if (Mass)
            CastChecked<APureMassPlayerController>(PC)->Pressed(Tags.InputTag_LMB);
        else
            PC->AbilityInputTagPressed(Tags.InputTag_LMB);
        TestTrue(TEXT("LMB press reaches toggle abilities without hovering or Shift"),
                 ASC->FindAbilitySpecFromHandle(ToggleHandle)->IsActive());
        ASC->CancelAbilityHandle(ToggleHandle);
    }
    for (const TCHAR* Path : {TEXT("/Game/BluePrints/Characters/Player/BP_Player.BP_Player_C"),
                              TEXT("/Game/BluePrints/Characters/Stander/BP_StanderPlayer.BP_StanderPlayer_C")})
    {
        auto* Class = LoadClass<APlayerCharacter>(nullptr, Path);
        auto* Player = Class ? World->SpawnActor<APlayerCharacter>(Class) : nullptr;
        TestNotNull(TEXT("Authored player loads"), Player);
        if (Player)
        {
            // This minimal world has not initialized actors for play; exercise that lifecycle explicitly.
            Player->PostInitializeComponents();
            TestTrue(TEXT("Every player has mesh-independent interaction overlaps"),
                     Player->GetCapsuleComponent()->GetGenerateOverlapEvents());
        }
    }
    World->DestroyWorld(false);
    return true;
}
#endif
