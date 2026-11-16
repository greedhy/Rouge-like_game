// 自动化 -> 真实眩晕 GE/组件、Mass 快照 -> 初始同步、重叠、到期、死亡和池复用表现。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Debuff/DebuffNiagaraComponent.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "Components/CapsuleComponent.h"
#include "PureMass/PureMassCrowd.h"
#include "MyGameplayTags.h"
#include "NiagaraSystem.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    // 只启动被测组件，避免临时世界启动怪物 AI 与关卡 UI。
    UDebuffNiagaraComponent* StartStunComponent(AEnemyCharacter* Enemy)
    {
        TArray<UDebuffNiagaraComponent*> Components;
        Enemy->GetComponents(Components);
        for (auto* Component : Components)
        {
            if (Component->DebuffTag == FMyGameplayTags::Get().Debuff_Stun)
            {
                Component->SetAsset(LoadObject<UNiagaraSystem>(
                    nullptr, TEXT("/Game/BluePrints/Characters/Effects/NS_Stars_EnemyStun.NS_Stars_EnemyStun")));
                Component->RegisterAllComponentTickFunctions(true);
                static_cast<UActorComponent*>(Component)->BeginPlay();
                return Component;
            }
        }
        return nullptr;
    }

    FActiveGameplayEffectHandle StunFor(UAbilitySystemComponent* ASC, float Duration)
    {
        FGameplayEffectSpec Spec(GetDefault<UGuardParryStunEffect>(), ASC->MakeEffectContext(), 1.f);
        Spec.SetDuration(Duration, true);
        return ASC->ApplyGameplayEffectSpecToSelf(Spec);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStunStarsLifecycleTest,
                                 "test_25d.StunPresentation.InitialOverlapExpiryAndDeath",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStunStarsLifecycleTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* ASC = F.Target->GetAbilitySystemComponent();
    const auto First = StunFor(ASC, .25f);
    const auto Second = StunFor(ASC, 1.f);
    auto* Stars = StartStunComponent(F.Target);
    if (!TestNotNull(TEXT("Actual stun component"), Stars))
    {
        return false;
    }
    TestNotNull(TEXT("Actual NS_Stars loaded"), Stars->GetAsset());
    TestTrue(TEXT("Initial existing stun places stars above head"),
             Stars->GetComponentLocation().Z > F.Target->GetActorLocation().Z +
                                                  F.Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    if (FApp::CanEverRender())
    {
        TestTrue(TEXT("Initial replicated/existing stun activates stars"), Stars->IsActive());
    }
    ASC->RemoveActiveGameplayEffect(First);
    if (FApp::CanEverRender())
    {
        TestTrue(TEXT("Overlapping remaining stun keeps stars"), Stars->IsActive());
    }
    for (int32 Index = 0; Index < 24; ++Index)
    {
        ++GFrameCounter;
        F.World->Tick(LEVELTICK_All, .05f);
    }
    TestFalse(TEXT("Last GE expires"), ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun));
    TestFalse(TEXT("Expiry immediately deactivates stars"), Stars->IsActive());
    StunFor(ASC, 2.f);
    F.Target->GetOnDeathDelegate().Broadcast(F.Target);
    TestFalse(TEXT("Death immediately deactivates stars"), Stars->IsActive());
    static_cast<UActorComponent*>(Stars)->EndPlay(EEndPlayReason::Destroyed);
    ASC->RemoveActiveGameplayEffect(Second);
    ASC->RemoveActiveEffects(FGameplayEffectQuery());
    StunFor(ASC, 1.f);
    TestFalse(TEXT("EndPlay unbinds future tag changes"), Stars->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStunStarsPoolTest, "test_25d.StunPresentation.SizeAndPoolReuse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStunStarsPoolTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* ASC = F.Target->GetAbilitySystemComponent();
    auto* Stars = StartStunComponent(F.Target);
    if (!TestNotNull(TEXT("Stun component"), Stars))
    {
        return false;
    }
    F.Target->EnableDungeonPooling(0.f);
    for (int32 Life = 0; Life < 2; ++Life)
    {
        F.Target->GetCapsuleComponent()->SetCapsuleSize(Life == 0 ? 34.f : 68.f, Life == 0 ? 51.f : 130.f);
        StunFor(ASC, 5.f);
        TestTrue(TEXT("Capsule changes refit head height"),
                 Stars->GetComponentLocation().Z > F.Target->GetActorLocation().Z +
                                                      F.Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        TestTrue(TEXT("Small monster/Boss size"),
                 FMath::IsNearlyEqual(Stars->GetComponentScale().X, Life == 0 ? .15 : .3, 1.e-5));
        if (FApp::CanEverRender())
        {
            TestTrue(TEXT("Each live stun activates stars"), Stars->IsActive());
        }
        F.Target->SleepForDungeonPool();
        TestFalse(TEXT("Pool sleep leaves no stars"), Stars->IsActive());
        TestTrue(TEXT("Wake into another life"),
                 F.Target->WakeFromDungeonPool(FTransform(FVector(150, 0, 100)), NAME_None));
        TestFalse(TEXT("Wake does not replay old stun"), Stars->IsActive());
    }
    static_cast<UActorComponent*>(Stars)->EndPlay(EEndPlayReason::Destroyed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStunStarsWireTest, "test_25d.StunPresentation.MassSnapshotRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStunStarsWireTest::RunTest(const FString&)
{
    FPureMassView View;
    View.Id = 42;
    View.Health = 100.f;
    FPureMassNetEntity Wire;
    Wire.SetFromView(View);
    View.bStunned = true;
    TestTrue(TEXT("Stationary stun transition dirties snapshot"), Wire.SetFromView(View));
    TestTrue(TEXT("Client snapshot preserves stun"), Wire.ToView().bStunned);
    TestFalse(TEXT("Stun remains distinct from petrify"), Wire.ToView().bPetrified);
    TestFalse(TEXT("Unchanged snapshot stays clean"), Wire.SetFromView(View));
    View.bStunned = false;
    TestTrue(TEXT("Recovery dirties snapshot"), Wire.SetFromView(View));
    TestFalse(TEXT("Client sees recovery"), Wire.ToView().bStunned);
    return true;
}
#endif
