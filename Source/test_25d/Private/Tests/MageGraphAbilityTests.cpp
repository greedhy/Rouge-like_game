// 调用链：自动化授予并激活真实 GA -> 服务器目标数据 -> 图与世界 Tick -> 实际 Mass 扣血或 ASC 属性变化。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/MageGraphAbility.h"
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassSubsystem.h"
#include "UObject/Script.h"

namespace
{
    /** 加载正式技能资产，测试直接走原 ASC 授予接口。 */
    UClass* LoadMageSpell(const TCHAR* Name)
    {
        const FString Path = FString::Printf(TEXT("/Game/UnifiedAbilities/MageBatch/GA_%s.GA_%s_C"), Name, Name);
        return LoadClass<UMageGraphAbility>(nullptr, *Path);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMageGraphActives, "test_25d.UnifiedGA.MageBatch.ActiveCompositions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMageGraphActives::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    struct FCase
    {
        const TCHAR* Name;
        int32 Mana;
        float Before;
        float UntilFirstHit;
        float UntilComplete;
        float ExpectedDamage;
        bool bEnemy;
    };
    const FCase Cases[] = {
        {TEXT("MeteorCinders"), 28, .60f, .68f, 4.f, 96.f, false},
        {TEXT("RimeSigil"), 22, .90f, 1.04f, 4.3f, 72.f, false},
        {TEXT("PiercingTempest"), 20, .10f, .34f, 2.3f, 24.f, true},
        {TEXT("ArcaneSentry"), 24, .70f, .84f, 5.3f, 75.f, false},
    };
    for (const FCase& Case : Cases)
    {
        UClass* AbilityClass = LoadMageSpell(Case.Name);
        if (!TestNotNull(Case.Name, AbilityClass))
        {
            return false;
        }
        FAbilityGraphTestFixture Fixture;
        Fixture.Target->Destroy();
        Fixture.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
        Fixture.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
        Fixture.ASC->AbilityActorInfo->PlayerController =
            Fixture.World->SpawnActor<AAbilityGraphRemoteTestController>();
        auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Monster = NewObject<UPureMassMonsterType>(Fixture.World);
        Monster->Health = 1000.f;
        Monster->XPReward = 0;
        const FVector TargetPoint(300, 0, 0);
        const int32 First = Mass->SpawnMonster(Monster, TargetPoint);
        const int32 Second = Mass->SpawnMonster(Monster, FVector(470, 0, 0));
        const int32 Outside = Mass->SpawnMonster(Monster, FVector(1500, 900, 0));
        const auto Health = [Mass](int32 Id)
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : -1.f;
        };
        const auto Handle = Fixture.ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1));
        auto* Ability = Cast<UMageGraphAbility>(Fixture.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        if (!TestNotNull(TEXT("Mage ability instance"), Ability))
        {
            return false;
        }
        FPredictionKey Key;
        Key.Current = 171;
        Fixture.ASC->CallServerTryActivateAbility(Handle, false, Key);
        TestTrue(TEXT("Waits for target data"), Ability->IsActive());
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.ImpactPoint = Case.bEnemy ? TargetPoint : FVector(200, 0, 0);
        Hit->HitResult.Item = Case.bEnemy ? First : INDEX_NONE;
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        Key = Ability->GetCurrentActivationInfo().GetActivationPredictionKey();
        Fixture.ASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), FPredictionKey());
        auto* Graphs = Fixture.World->GetSubsystem<UAbilityGraphWorldSubsystem>();
        TestEqual(TEXT("Independent effect adopted"), Graphs->GetActiveExecutionCount(), 1);
        TestFalse(TEXT("Casting ability released; world graph remains"), Ability->IsActive());
        Fixture.ASC->ServerSetReplicatedTargetData(Handle, Key, Data, FGameplayTag(), FPredictionKey());
        TestEqual(TEXT("One mana cost"),
                  Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 1000.f - Case.Mana);
        const FGameplayTag Cooldown =
            FGameplayTag::RequestGameplayTag(FName(FString::Printf(TEXT("Cooldown.Mage.%s"), Case.Name)));
        TestTrue(TEXT("Configured cooldown applied"), Fixture.ASC->HasMatchingGameplayTag(Cooldown));

        // 以小步推进真实世界图，命中、后继生成和到期都由注册策略处理。
        float Elapsed = 0.f;
        const auto AdvanceTo = [&](float Limit)
        {
            while (Elapsed + .001f < Limit)
            {
                const float Step = FMath::Min(.02f, Limit - Elapsed);
                Graphs->Tick(Step);
                Elapsed += Step;
            }
        };
        AdvanceTo(Case.Before);
        TestEqual(TEXT("Initial wait or travel causes no early damage"), Health(First), 1000.f);
        AdvanceTo(Case.UntilFirstHit);
        TestTrue(TEXT("First stage dealt real damage"), Health(First) < 1000.f);
        AdvanceTo(Case.UntilComplete);
        TestTrue(FString::Printf(TEXT("%s damage budget"), Case.Name),
                 FMath::IsNearlyEqual(Health(First), 1000.f - Case.ExpectedDamage, .02f));
        TestTrue(TEXT("Second enemy also damaged"), Health(Second) < 1000.f);
        TestEqual(TEXT("Outside enemy stays untouched"), Health(Outside), 1000.f);
        TestEqual(TEXT("Finished cast is released"), Graphs->GetActiveExecutionCount(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMageGraphPassives, "test_25d.UnifiedGA.MageBatch.PassiveOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMageGraphPassives::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* Meditation = LoadMageSpell(TEXT("ArcaneMeditation"));
    auto* Ward = LoadMageSpell(TEXT("ElementalWard"));
    if (!TestNotNull(TEXT("Meditation asset"), Meditation) || !TestNotNull(TEXT("Ward asset"), Ward))
    {
        return false;
    }
    FAbilityGraphTestFixture Fixture;
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(Fixture.ASC);
    const auto Regen = UMyAttributeSet::GetManaRegenerationAttribute();
    const auto Fire = UMyAttributeSet::GetFireResistanceAttribute();
    const auto Thunder = UMyAttributeSet::GetThunderResistanceAttribute();
    ASC->SetNumericAttributeBase(Regen, 5.f);
    ASC->SetNumericAttributeBase(Fire, 3.f);
    ASC->SetNumericAttributeBase(Thunder, 4.f);
    const auto RegenSpec = ASC->GiveAbility(FGameplayAbilitySpec(Meditation, 1));
    const auto WardSpec = ASC->GiveAbility(FGameplayAbilitySpec(Ward, 1));
    TestTrue(TEXT("Meditation turns on"), ASC->TryActivateAbility(RegenSpec));
    TestTrue(TEXT("Ward turns on in separate graph"), ASC->TryActivateAbility(WardSpec));
    ASC->TickComponent(30.f, LEVELTICK_All, &ASC->PrimaryComponentTick);
    TestEqual(TEXT("Meditation remains after an ordinary lease duration"), ASC->GetNumericAttribute(Regen), 7.f);
    TestEqual(TEXT("Ward fire bonus"), ASC->GetNumericAttribute(Fire), 15.f);
    TestEqual(TEXT("Ward thunder bonus"), ASC->GetNumericAttribute(Thunder), 16.f);
    const auto RegenTag = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Mage.ArcaneMeditation"));
    ASC->DeactivatePassiveAbility.Broadcast(RegenTag);
    TestEqual(TEXT("Changing passive slot removes only meditation"), ASC->GetNumericAttribute(Regen), 5.f);
    TestEqual(TEXT("Ward remains active"), ASC->GetNumericAttribute(Fire), 15.f);
    TestTrue(TEXT("Meditation can equip again"), ASC->TryActivateAbility(RegenSpec));
    TestEqual(TEXT("Reequip applies only one modifier"), ASC->GetNumericAttribute(Regen), 7.f);
    ASC->CancelAbilityHandle(WardSpec);
    TestEqual(TEXT("Cancellation removes the owned fire GE"), ASC->GetNumericAttribute(Fire), 3.f);
    TestEqual(TEXT("Cancellation removes the owned thunder GE"), ASC->GetNumericAttribute(Thunder), 4.f);
    TestEqual(TEXT("Cancelled ward cannot remove meditation"), ASC->GetNumericAttribute(Regen), 7.f);
    ASC->ClearAbility(RegenSpec);
    ASC->ClearAbility(WardSpec);
    return true;
}
#endif
