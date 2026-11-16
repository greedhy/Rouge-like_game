// 回归：左键输入 -> 真实剑盾 Spec 和蒙太奇 -> 1、2、3 -> 中断清理和重新起手。
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/AbilityTimelineTestAbility.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "AbilitySystem/Ability/StanderAbility.h"
#include "AbilitySystem/Combo/StanderComboComponent.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Timeline/AbilityTask_RunTimeline.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/WeaponSetConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "MyGameplayTags.h"
#include "Player/MyPlayerState.h"
#include "TimerManager.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanderComboInputTest, "test_25d.StanderCombo.AuthoredWeaponInput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStanderComboInputTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* Set = LoadObject<UWeaponSetConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_WeaponSet.DA_WeaponSet"));
    if (!TestNotNull(TEXT("Authored sword and shield"), Set))
    {
        return false;
    }
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Pawn = World->SpawnActor<AAbilityTimelineTestStander>();
    auto* State = World->SpawnActor<AMyPlayerState>();
    Pawn->AttachState(State);
    Pawn->DefaultWeaponSet = Set;
    Pawn->GetMesh()->SetSkeletalMeshAsset(
        LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander")));
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    TestTrue(TEXT("Default weapon grants real attack specs"), State->GetEquipmentComponent()->ReconcileEffects());
    Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    const FGameplayTag LMB = FMyGameplayTags::Get().InputTag_LMB;
    auto CurrentStage = [&]()
    {
        const auto* Ability = Cast<UStanderWeaponActionAbility>(ASC->GetAnimatingAbility());
        return Ability ? Ability->ActionConfig->ActionTag.ToString() : FString();
    };
    auto AdvanceFrame = [&]()
    {
        ++GFrameCounter;
        Pawn->GetMesh()->TickAnimation(.01f, false);
        Pawn->GetMesh()->GetAnimInstance()->DispatchQueuedAnimEvents();
        UAbilityTask_RunTimeline* Task = nullptr;
        if (const auto* Ability = Cast<UStanderWeaponActionAbility>(ASC->GetAnimatingAbility()))
        {
            Task = Ability->GetActiveTimelineTask();
        }
        else if (const auto* Fallback = Cast<UStanderAbility>(ASC->GetAnimatingAbility()))
        {
            Task = Fallback->GetComboTimelineTask();
        }
        if (Task)
        {
            Task->TickTask(.01f);
        }
        World->GetTimerManager().Tick(.01f);
    };
    auto Advance = [&]()
    {
        for (int32 Frame = 0; Frame < 170 && ASC->GetAnimatingAbility(); ++Frame)
        {
            AdvanceFrame();
        }
    };
    ASC->AbilityInputTagHeld(LMB);
    TestTrue(TEXT("Hold cannot start the press-only combo"), CurrentStage().IsEmpty());
    for (int32 Stage : {1, 2, 3, 1})
    {
        ASC->AbilityInputTagPressed(LMB);
        TestTrue(*FString::Printf(TEXT("LMB starts Attack%d"), Stage),
                 CurrentStage().EndsWith(FString::Printf(TEXT(".Attack%d"), Stage)));
        Advance();
    }
    ASC->AbilityInputTagPressed(LMB);
    TestTrue(TEXT("Second step is active before explicit cancel"), CurrentStage().EndsWith(TEXT(".Attack2")));
    ASC->CancelAllAbilities();
    ASC->AbilityInputTagPressed(LMB);
    TestTrue(TEXT("Cancelled chain restarts with first attack"), CurrentStage().EndsWith(TEXT(".Attack1")));
    TestFalse(TEXT("Windup cannot queue an early input"), Pawn->ComboComponent->RequestComboInput(LMB));
    bool bBuffered = false;
    for (int32 Frame = 0; Frame < 150 && !CurrentStage().EndsWith(TEXT(".Attack2")); ++Frame)
    {
        if (!bBuffered && CurrentStage().EndsWith(TEXT(".Attack1")))
        {
            bBuffered = Pawn->ComboComponent->RequestComboInput(LMB);
            if (bBuffered)
            {
                TestFalse(TEXT("Only one pending input per completed stage"),
                          Pawn->ComboComponent->RequestComboInput(LMB));
            }
        }
        AdvanceFrame();
    }
    TestTrue(TEXT("Release/recovery admitted a buffer"), bBuffered);
    TestTrue(TEXT("Real completion consumes buffer and starts the next GA"), CurrentStage().EndsWith(TEXT(".Attack2")));
    ASC->CancelAllAbilities();
    auto* Clock = World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Clock->SampleClockForTesting(100., false);
    ASC->AbilityInputTagPressed(LMB);
    Advance();
    const auto Ready2 = FGameplayTag::RequestGameplayTag(TEXT("State.Combo.Ready.Light2"));
    TestTrue(TEXT("Only successful first completion grants its specific successor"), ASC->HasMatchingGameplayTag(Ready2));
    Clock->SampleClockForTesting(101., false);
    ASC->AbilityInputTagPressed(LMB);
    TestTrue(TEXT("Expired right to continue starts from first"), CurrentStage().EndsWith(TEXT(".Attack1")));
    TestFalse(TEXT("Previous ready tag cannot authorize stages after expiry"), ASC->HasMatchingGameplayTag(Ready2));
    ASC->CancelAllAbilities();

    // 在隔离夹具模拟无默认武器组的旧角色。实际旧 GA 仍通过原启动授予和自己的变体时间轴。
    auto* Fallback = LoadClass<UStanderComboAbility>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/GA_Stander_Combo.GA_Stander_Combo_C"));
    Pawn->SwordConfig = LoadObject<UStanderCombatConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/DA_Stander.DA_Stander"));
    Pawn->DefaultWeaponSet = nullptr;
    const bool bFallbackReady = TestTrue(TEXT("Restore fallback weapon group"), State->GetEquipmentComponent()->ReconcileEffects()) &&
                                TestNotNull(TEXT("Authored fallback ability"), Fallback) &&
                                TestNotNull(TEXT("Authored fallback config"), Pawn->SwordConfig.Get());
    if (bFallbackReady)
    {
        Pawn->GetMesh()->SetAnimInstanceClass(Set->AnimClass);
        ASC->AddCharacterAbilities({Fallback});
        for (int32 Stage : {1, 2, 3})
        {
            ASC->AbilityInputTagPressed(LMB);
            TestEqual(*FString::Printf(TEXT("Fallback plays its authored stage %d"), Stage),
                      ASC->GetCurrentMontage(), Pawn->SwordConfig->Combo[Stage - 1].Montage.Get());
            Advance();
        }
    }
    ASC->CancelAllAbilities();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
