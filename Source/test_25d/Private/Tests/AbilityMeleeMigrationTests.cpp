// 自动化链：真实 World/ASC/Mass -> 注册 Melee.Contact -> 实际生命值、空间边界、来源失效。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityMeleeNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"

namespace MeleeMigrationTests
{
    UAbilityGraphDefinition* Graph(UWorld* World, bool bLegacyExtra)
    {
        auto* Result = NewObject<UAbilityGraphDefinition>(World);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Melee.Contact");
        auto* Config = NewObject<UAbilityMeleeNodeConfig>(Result);
        Config->bIncludeMass = true;
        Config->bLegacyExtraDamageOnKnockback = bLegacyExtra;
        Node.Config = Config;
        Result->Nodes.Add(Node);
        Result->EntryNodeId = Node.NodeId;
        return Result;
    }
} // namespace MeleeMigrationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeleeMigrationDamage, "test_25d.UnifiedGA.Melee.ContactRadiusAndLegacyExtraHit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMeleeMigrationDamage::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 Near = Mass->SpawnMonster(Type, FVector(600, 0, 0));
    const int32 Far = Mass->SpawnMonster(Type, FVector(800, 0, 0));
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = FVector(600, 0, 0);
    Input.DamageParams.BaseDamage = 10.f;
    Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Physics;
    Input.DamageParams.KnockbackChance = 100.f;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Melee strategy registered"),
             Run->StartWithInput(MeleeMigrationTests::Graph(F.World, true), Input, {}, Error));
    FPureMassView View;
    Mass->Resolve(Near, View);
    TestEqual(TEXT("Compatibility preserves two damage paths on knockback branch"), View.Health, 80.f);
    Mass->Resolve(Far, View);
    TestEqual(TEXT("Outside contact range untouched"), View.Health, 100.f);
    TestTrue(TEXT("Instant contact completes"), Run->IsFinished());
    Run = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("New config may disable compatibility extra hit"),
             Run->StartWithInput(MeleeMigrationTests::Graph(F.World, false), Input, {}, Error));
    Mass->Resolve(Near, View);
    TestEqual(TEXT("Explicit single damage path"), View.Health, 70.f);
    Input.DamageParams.BaseDamage = 100.f;
    Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 KillResults = 0;
    Run->OnEvent.AddLambda(
        [&KillResults](const FAbilityGraphEvent& Event)
        {
            KillResults +=
                Event.OutputPort == TEXT("DamageResolved") && Event.Input.bDamageAttempted && Event.Input.bKilled ? 1
                                                                                                                  : 0;
        });
    TestTrue(TEXT("Lethal extra path executes"),
             Run->StartWithInput(MeleeMigrationTests::Graph(F.World, true), Input, {}, Error));
    TestFalse(TEXT("Extra path kills original entity"), Mass->Resolve(Near, View));
    TestEqual(TEXT("Lethal extra hit still emits exactly one result for impact cue"), KillResults, 1);
    F.ASC->ClearActorInfo();
    Run = NewObject<UAbilityGraphInstance>(F.World);
    TestFalse(TEXT("Old activation source rejected after rebinding"),
              Run->StartWithInput(MeleeMigrationTests::Graph(F.World, false), Input, {}, Error));
    Mass->Resolve(Far, View);
    TestEqual(TEXT("Invalid source never damages remaining entity"), View.Health, 100.f);
    return true;
}
#endif
