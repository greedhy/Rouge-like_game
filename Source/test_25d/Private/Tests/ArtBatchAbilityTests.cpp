// 调用链：Automation → 正式 GA 资产授予 → 服务端激活/选点 → 图推进 → 实际生命与费用。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "AbilitySystem/Unified/MageGraphAbility.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Engine/GameInstance.h"
#include "Game/MyGameModeBase.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArtBatchAbilitiesTest, "test_25d.UnifiedGA.ArtBatch.ActorCompositions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 覆盖本批全部图的正式资产、实际 Actor 扣血、单次费用、冷却与有界结束。
bool FArtBatchAbilitiesTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    struct FCase
    {
        const TCHAR* Name;
        float Mana;
        bool bSelf;
    };
    const FCase Cases[] = {
        {TEXT("GlacialLance"), 20.f, false},
        {TEXT("InfernoVortex"), 30.f, false},
        {TEXT("ThunderPrison"), 25.f, false},
        {TEXT("GhostCleave"), 12.f, true},
        {TEXT("CrimsonEruption"), 18.f, true},
        {TEXT("GhostMoonArray"), 24.f, true},
    };
    for (const auto& Case : Cases)
    {
        const FString Path = FString::Printf(TEXT("/Game/UnifiedAbilities/ArtBatch/GA_%s.GA_%s_C"), Case.Name, Case.Name);
        UClass* Class = LoadClass<UMageGraphAbility>(nullptr, *Path);
        if (!TestNotNull(Case.Name, Class))
        {
            return false;
        }
        FAbilityGraphTestFixture Fixture;
        Fixture.World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;
        URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
        Fixture.World->SetGameMode(URL);
        auto* Mode = CastChecked<AMyGameModeBase>(Fixture.World->GetAuthGameMode());
        Mode->CharacterClassInfo = LoadObject<UCharacterClassInfo>(
            nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
        if (!TestNotNull(TEXT("Damage calculation defaults"), Mode->CharacterClassInfo.Get()))
        {
            return false;
        }
        auto* ASC = Fixture.ASC;
        auto* TargetASC = Fixture.Target->GetAbilitySystemComponent();
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
        ASC->AbilityActorInfo->PlayerController = Fixture.World->SpawnActor<AAbilityGraphRemoteTestController>();
        const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(Class, 1));
        auto* Ability = CastChecked<UMageGraphAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        FPredictionKey Key;
        Key.Current = 179;
        ASC->CallServerTryActivateAbility(Handle, false, Key);
        if (!Case.bSelf)
        {
            auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.Item = INDEX_NONE;
            Hit->HitResult.ImpactPoint = Fixture.Target->GetActorLocation();
            Hit->HitResult.HitObjectHandle = FActorInstanceHandle(Fixture.Target);
            FGameplayAbilityTargetDataHandle Data;
            Data.Add(Hit);
            Key = Ability->GetCurrentActivationInfo().GetActivationPredictionKey();
            ASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), FPredictionKey());
        }
        auto* Graphs = Fixture.World->GetSubsystem<UAbilityGraphWorldSubsystem>();
        TestEqual(TEXT("One complete mana cost"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                  1000.f - Case.Mana);
        const auto Cooldown = FGameplayTag::RequestGameplayTag(
            FName(FString::Printf(TEXT("Cooldown.Art.%s"), Case.Name)));
        TestTrue(TEXT("Cooldown starts"), ASC->HasMatchingGameplayTag(Cooldown));
        for (int32 Step = 0; Step < 300; ++Step)
        {
            Graphs->Tick(0.02f);
        }
        const float Health = TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        TestTrue(FString::Printf(TEXT("%s removes Actor health"), Case.Name), Health < 1000.f && Health > 0.f);
        TestEqual(TEXT("Finite graph releases itself"), Graphs->GetActiveExecutionCount(), 0);
        Graphs->Tick(2.f);
        TestEqual(TEXT("Finished graph cannot deal late damage"),
                  TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), Health);
        TestEqual(TEXT("No hidden per-node mana costs"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                  1000.f - Case.Mana);
    }
    return true;
}
#endif
