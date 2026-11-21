// 调用链：Automation → 真实职业目录 → ASC 授予/学习/装备 RPC → 技能点及状态断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "Character/StanderPlayer.h"
#include "Character/PlayerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/MyGameModeBase.h"
#include "Player/MyPlayerState.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfessionSkillTest, "test_25d.Rooms.ProfessionSkills",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 正式目录必须可学且跨职业请求不会扣点、解锁或占用技能槽。
bool FProfessionSkillTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;
    URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
    World->SetGameMode(URL);
    auto* Mode = CastChecked<AMyGameModeBase>(World->GetAuthGameMode());
    Mode->AbilityInfo = LoadObject<UAbilityInfo>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"));
    const auto& Tags = FMyGameplayTags::Get();
    const auto MeleeTag = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Art.GhostCleave"));
    const auto MageTag = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Art.ThunderPrison"));
    for (const auto Profession : {EPlayerProfession::Mage, EPlayerProfession::Stander})
    {
        auto* State = World->SpawnActor<AMyPlayerState>();
        State->Profession = Profession;
        State->SetSpellPoints(1);
        APlayerCharacter* Pawn = Profession == EPlayerProfession::Stander
                                    ? World->SpawnActor<AStanderPlayer>()
                                    : World->SpawnActor<APlayerCharacter>();
        Pawn->SetPlayerState(State);
        auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
        ASC->InitAbilityActorInfo(State, Pawn);
        const auto* Catalog = Mode->GetAbilityInfoForActor(Pawn);
        const auto AllowedTag = Profession == EPlayerProfession::Mage ? MageTag : MeleeTag;
        const auto WrongTag = Profession == EPlayerProfession::Mage ? MeleeTag : MageTag;
        TestNotNull(TEXT("Profession catalog"), Catalog);
        TestTrue(TEXT("Own skill is in the profession catalog"), Catalog && Catalog->CanLearn(AllowedTag, Pawn));
        TestFalse(TEXT("Other profession cannot learn this catalog row"), Catalog && Catalog->CanLearn(WrongTag, Pawn));
        ASC->UpdateAbilityStatuses(1);
        TestNotNull(TEXT("Level-one profession skill becomes eligible"), ASC->GetSpecFromAbilityTag(AllowedTag));
        TestNull(TEXT("Other profession skill not granted by level-up"), ASC->GetSpecFromAbilityTag(WrongTag));

        // 模拟已存在旧版跨职业 Spec，服务器仍必须拒绝学习和装备。
        const auto* Shared = Mode->AbilityInfo.Get();
        const auto WrongClass = Shared->FindAbilityInfoForTag(WrongTag).Ability;
        FGameplayAbilitySpec WrongSpec(WrongClass, 1);
        WrongSpec.DynamicAbilityTags.AddTag(Tags.Abilities_Status_Eligible);
        const auto WrongHandle = ASC->GiveAbility(WrongSpec);
        ASC->ServerSpendSpellPoint(WrongTag);
        TestEqual(TEXT("Rejected learning does not spend points"), State->GetSpellPoints(), 1);
        TestEqual(TEXT("Rejected skill remains eligible"), ASC->GetStatusFromAbilityTag(WrongTag),
                  Tags.Abilities_Status_Eligible);
        auto* Granted = ASC->FindAbilitySpecFromHandle(WrongHandle);
        Granted->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Eligible);
        Granted->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Unlocked);
        ASC->ServerEquipAbility(WrongTag, Tags.InputTag_4);
        TestFalse(TEXT("Other profession cannot occupy a slot"), ASC->GetSlotFromAbilityTag(WrongTag).IsValid());

        ASC->ServerSpendSpellPoint(AllowedTag);
        TestEqual(TEXT("Own profession skill spends exactly one point"), State->GetSpellPoints(), 0);
        TestEqual(TEXT("Own profession skill unlocks"), ASC->GetStatusFromAbilityTag(AllowedTag),
                  Tags.Abilities_Status_Unlocked);
        ASC->ServerSpendSpellPoint(AllowedTag);
        TestEqual(TEXT("Zero points cannot upgrade"), ASC->GetSpecFromAbilityTag(AllowedTag)->Level, 1);
        TestEqual(TEXT("Zero points remain zero"), State->GetSpellPoints(), 0);
        ASC->ServerEquipAbility(AllowedTag, Tags.InputTag_4);
        TestEqual(TEXT("Own profession equips its learned skill"), ASC->GetSlotFromAbilityTag(AllowedTag),
                  Tags.InputTag_4);
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
