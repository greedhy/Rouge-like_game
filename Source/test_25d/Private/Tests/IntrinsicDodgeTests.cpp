// 测试链：Startup/旧档协调 -> 同一ASC Spec -> 固定按下与菜单拒绝；不修改用户存档。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Ability/PerfectDodgeAbility.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Game/LoadScreenSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Player/MyPlayerState.h"
#include "Character/PlayerCharacter.h"
#include "AbilitySystem/Defense/PerfectDodgeComponent.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIntrinsicDodgePawnAssetsTest,
                                 "test_25d.UnifiedGA.PerfectDodge.ActualPawnStartupWithoutSave",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIntrinsicDodgePawnAssetsTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    const TCHAR* PawnPaths[] = {TEXT("/Game/BluePrints/Characters/Player/BP_Player.BP_Player_C"),
                                TEXT("/Game/RoguelikeGeneration/Gameplay/BP_DungeonPlayer.BP_DungeonPlayer_C"),
                                TEXT("/Game/BluePrints/Characters/Stander/BP_StanderPlayer.BP_StanderPlayer_C")};
    UClass* DodgeClass = LoadClass<UPerfectDodgeAbility>(
        nullptr, TEXT("/Game/UnifiedAbilities/Defense/PerfectDodge/GA_PerfectDodge.GA_PerfectDodge_C"));
    if (!TestNotNull(TEXT("Authored dodge blueprint loads"), DodgeClass))
    {
        return false;
    }
    int32 PawnIndex = 0;
    for (const auto* PawnPath : PawnPaths)
    {
        UClass* PawnClass = LoadClass<APlayerCharacter>(nullptr, PawnPath);
        if (!TestNotNull(PawnPath, PawnClass))
        {
            continue;
        }
        auto* Pawn = F.World->SpawnActor<APlayerCharacter>(PawnClass, FVector(1000.f * ++PawnIndex, 0, 100),
                                                           FRotator::ZeroRotator);
        auto* State = F.World->SpawnActor<AMyPlayerState>();
        Pawn->SetPlayerState(State);
        // 真实初始化入口，不额外调用Ensure/LoadProgress，覆盖首次ASC成员为空的路径。
        Pawn->OnRep_PlayerState();
        auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
        AddInfo(FString::Printf(TEXT("Pawn %s authority=%d ASC=%s owner=%s avatar=%s expected=%s"), PawnPath,
                                Pawn->HasAuthority(), *GetNameSafe(Pawn->GetAbilitySystemComponent()),
                                *GetNameSafe(ASC->GetOwnerActor()), *GetNameSafe(ASC->GetAvatarActor()),
                                *GetPathNameSafe(DodgeClass)));
        for (const auto& Granted : ASC->GetActivatableAbilities())
        {
            AddInfo(FString::Printf(TEXT("Granted=%s"), *GetPathNameSafe(Granted.Ability)));
        }
        auto* Spec = ASC->FindAbilitySpecFromClass(DodgeClass);
        if (TestNotNull(FString::Printf(TEXT("%s grants dodge before save loading"), PawnPath), Spec))
        {
            const auto Handle = Spec->Handle;
            Pawn->OnRep_PlayerState();
            TestEqual(TEXT("Repeated actor initialization preserves handle"),
                      ASC->FindAbilitySpecFromClass(DodgeClass)->Handle, Handle);
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            ASC->AbilityInputTagPressed(FMyGameplayTags::Get().InputTag_Dodge);
            TestTrue(TEXT("Authored GA activates with actual Pawn"),
                     ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
            TestNotNull(TEXT("Shared defense component created"), Pawn->FindComponentByClass<UPerfectDodgeComponent>());
            ASC->CancelAbilityHandle(Handle);
        }
        Pawn->Destroy();
        State->Destroy();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIntrinsicDodgeTest, "test_25d.UnifiedGA.PerfectDodge.StartupSaveAndFixedInput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIntrinsicDodgeTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(F.ASC);
    const TArray<TSubclassOf<UGameplayAbility>> Defaults = {UPerfectDodgeAbility::StaticClass()};
    ASC->EnsureIntrinsicStartupAbilities(Defaults);
    auto* Spec = ASC->FindAbilitySpecFromClass(UPerfectDodgeAbility::StaticClass());
    if (!TestNotNull(TEXT("Missing old-save base ability is granted"), Spec))
    {
        return false;
    }
    const auto Handle = Spec->Handle;
    const auto Input = FMyGameplayTags::Get().InputTag_Dodge;
    TestTrue(TEXT("Fixed input bound"), Spec->DynamicAbilityTags.HasTagExact(Input));
    ASC->EnsureIntrinsicStartupAbilities(Defaults);
    TestEqual(TEXT("Repeated reconciliation keeps same spec"),
              ASC->FindAbilitySpecFromClass(UPerfectDodgeAbility::StaticClass())->Handle, Handle);
    auto* Save = NewObject<ULoadScreenSaveGame>();
    FSavedAbility Saved;
    Saved.GameplayAbility = UPerfectDodgeAbility::StaticClass();
    Saved.AbilityType = FMyGameplayTags::Get().Abilities_Type_Offensive;
    Save->SavedAbilities.Add(Saved);
    ASC->AddCharacterAbilitiesFromSaveData(Save);
    int32 Count = 0;
    for (const auto& Item : ASC->GetActivatableAbilities())
    {
        Count += Item.Ability && Item.Ability->IsA<UPerfectDodgeAbility>() ? 1 : 0;
    }
    TestEqual(TEXT("Legacy saved base entry does not duplicate"), Count, 1);
    F.Source->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    ASC->AbilityInputTagHeld(Input);
    TestFalse(TEXT("Held never starts intrinsic dodge"), ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    ASC->AbilityInputTagPressed(Input);
    TestTrue(TEXT("Pressed starts actual GA"), ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    ASC->CancelAbilityHandle(Handle);
    ASC->ServerEquipAbility(ASC->GetAbilityTagFromSpec(*ASC->FindAbilitySpecFromHandle(Handle)),
                            FMyGameplayTags::Get().InputTag_1);
    TestTrue(TEXT("Menu cannot move the base ability"),
             ASC->FindAbilitySpecFromHandle(Handle)->DynamicAbilityTags.HasTagExact(Input));
    ASC->EnsureIntrinsicStartupAbilities(Defaults);
    TestTrue(TEXT("Reconciliation retains actual cooldown"),
             ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Common.PerfectDodge"))));
    return true;
}
#endif
