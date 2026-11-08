/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Terrain.GroundedFootprintAndTeams
 * - test_25d.Terrain.PeriodicDamageMergesTilesAndExpires
 * - test_25d.Terrain.SlowRootAndStunComposition
 * - test_25d.Terrain.DeathDestroyAndStrongestSource
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Terrain/TerrainSurface.h"
#include "Terrain/TerrainStatusComponent.h"
#include "Terrain/TerrainStatusEffects.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Character/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace TerrainTests
{
    struct FWorld
    {
        UWorld* World;
        AEnemyCharacter* Character;
        UAbilitySystemComponent* ASC;
        FEditorScriptExecutionGuard Guard;
        FWorld()
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TerrainTest")));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Character = World->SpawnActor<AEnemyCharacter>();
            Character->SetActorLocation(FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
            Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Character->Tags.Add(TEXT("Enemy"));
            ASC = Character->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(Character, Character);
            ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Character));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000);
        }
        ~FWorld()
        {
            Character->TerrainStatus->ClearTerrainEffects();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        ATerrainSurface* Surface(ETerrainSurfaceType Type)
        {
            auto* Surface = World->SpawnActor<ATerrainSurface>();
            Surface->Config = NewObject<UTerrainSurfaceConfig>(Surface);
            Surface->Config->SurfaceType = Type;
            Surface->Config->DamagePerSecond = Type == ETerrainSurfaceType::Lava ? 8.f : 4.f;
            Surface->Config->ExitDuration = Type == ETerrainSurfaceType::Frost ? 0.f : 2.f;
            return Surface;
        }
        void Advance(float Seconds, TArray<ATerrainSurface*> Surfaces = {})
        {
            for (float T = 0; T < Seconds - KINDA_SMALL_NUMBER; T += .05f)
            {
                for (auto* Surface : Surfaces)
                    Character->TerrainStatus->RefreshContact(Surface);
                ++GFrameCounter;
                World->GetTimerManager().Tick(.05f);
                Character->TerrainStatus->TickComponent(.05f, LEVELTICK_All, nullptr);
            }
        }
    };
} // namespace TerrainTests
using namespace TerrainTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainContactTest, "test_25d.Terrain.GroundedFootprintAndTeams",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Terrain.GroundedFootprintAndTeams 回归，失败信息由下方断言交给自动化框架收集。 */
bool FTerrainContactTest::RunTest(const FString&)
{
    FWorld F;
    auto* S = F.Surface(ETerrainSurfaceType::Lava);
    TestTrue(TEXT("Grounded creature on surface"), S->CanAffectCharacter(F.Character));
    S->bAffectEnemies = false;
    TestFalse(TEXT("Enemy filter"), S->CanAffectCharacter(F.Character));
    S->bAffectEnemies = true;
    F.Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    TestFalse(TEXT("Jumping avoids fresh contact"), S->CanAffectCharacter(F.Character));
    F.Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    F.Character->AddActorWorldOffset(FVector(201, 0, 0));
    TestFalse(TEXT("Capsule overlap beyond feet does not count"), S->CanAffectCharacter(F.Character));
    F.Character->AddActorWorldOffset(FVector(-201, 0, 100));
    TestFalse(TEXT("Different floor height excluded"), S->CanAffectCharacter(F.Character));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainDamageTest, "test_25d.Terrain.PeriodicDamageMergesTilesAndExpires",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Terrain.PeriodicDamageMergesTilesAndExpires 回归，失败信息由下方断言交给自动化框架收集。 */
bool FTerrainDamageTest::RunTest(const FString&)
{
    FWorld F;
    auto* A = F.Surface(ETerrainSurfaceType::Lava);
    auto* B = F.Surface(ETerrainSurfaceType::Lava);
    F.Advance(2.2f, {A, B});
    TestEqual(TEXT("Two tiles inflict two ticks, not four; contact refresh does not postpone ticks"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 984.f);
    TestTrue(TEXT("Burning tag is a real GE granted tag"), F.ASC->HasMatchingGameplayTag(TerrainTags::Burning));
    auto* P = F.Surface(ETerrainSurfaceType::Toxic);
    F.Advance(1.1f, {A, B, P});
    TestEqual(TEXT("Poison and fire coexist"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
              972.f);
    F.Advance(2.5f);
    TestFalse(TEXT("Burn expires after leaving"), F.ASC->HasMatchingGameplayTag(TerrainTags::Burning));
    TestFalse(TEXT("Poison expires after leaving"), F.ASC->HasMatchingGameplayTag(TerrainTags::Poison));
    const float Health = F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    F.Advance(2);
    TestEqual(TEXT("Expired DOT has no residual timer damage"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), Health);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainMovementTest, "test_25d.Terrain.SlowRootAndStunComposition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Terrain.SlowRootAndStunComposition 回归，失败信息由下方断言交给自动化框架收集。 */
bool FTerrainMovementTest::RunTest(const FString&)
{
    FWorld F;
    auto* Ice = F.Surface(ETerrainSurfaceType::Frost);
    auto* Vines = F.Surface(ETerrainSurfaceType::Vines);
    F.Advance(.1f, {Ice});
    TestEqual(TEXT("40 percent slow"), F.Character->GetCharacterMovement()->MaxWalkSpeed, 150.f);
    F.Character->StunTagChanged(FMyGameplayTags::Get().Debuff_Stun, 1);
    TestEqual(TEXT("Stun wins"), F.Character->GetCharacterMovement()->MaxWalkSpeed, 0.f);
    F.Character->StunTagChanged(FMyGameplayTags::Get().Debuff_Stun, 0);
    TestEqual(TEXT("Ending stun preserves slow"), F.Character->GetCharacterMovement()->MaxWalkSpeed, 150.f);
    F.Character->TerrainStatus->RefreshContact(Vines);
    TestEqual(TEXT("Entangled stops movement"), F.Character->GetCharacterMovement()->MaxWalkSpeed, 0.f);
    TestFalse(TEXT("Entangle permits attacks and skills"),
              F.ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_InputPressed));
    F.Advance(2.2f, {Ice, Vines});
    TestFalse(TEXT("Root releases even while standing on vines"),
              F.Character->TerrainStatus->IsTerrainStatusActive(ETerrainSurfaceType::Vines));
    TestEqual(TEXT("Root release preserves ice"), F.Character->GetCharacterMovement()->MaxWalkSpeed, 150.f);
    F.Advance(3.1f, {Ice, Vines});
    TestTrue(TEXT("Vines can entangle again after recovery"), F.ASC->HasMatchingGameplayTag(TerrainTags::Entangled));
    F.Character->TerrainStatus->ClearTerrainEffects();
    TestEqual(TEXT("Clear restores original speed"), F.Character->GetCharacterMovement()->MaxWalkSpeed, 250.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainCleanupTest, "test_25d.Terrain.DeathDestroyAndStrongestSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Terrain.DeathDestroyAndStrongestSource 回归，失败信息由下方断言交给自动化框架收集。 */
bool FTerrainCleanupTest::RunTest(const FString&)
{
    FWorld F;
    auto* A = F.Surface(ETerrainSurfaceType::Lava);
    auto* B = F.Surface(ETerrainSurfaceType::Lava);
    B->Config->DamagePerSecond = 12;
    F.Advance(1.1f, {A, B});
    TestEqual(TEXT("Highest same-kind damage only"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
              988.f);
    A->Destroy();
    B->Destroy();
    F.Advance(.2f);
    TestFalse(TEXT("Removed surface cannot leave an infinite GE"), F.ASC->HasMatchingGameplayTag(TerrainTags::Burning));
    auto* P = F.Surface(ETerrainSurfaceType::Toxic);
    F.Advance(.1f, {P});
    FindFProperty<FBoolProperty>(ABaseCharacter::StaticClass(), TEXT("bDead"))
        ->SetPropertyValue_InContainer(F.Character, true);
    F.Advance(.2f, {P});
    TestFalse(TEXT("Dead creatures lose terrain effects"), F.ASC->HasMatchingGameplayTag(TerrainTags::Poison));
    TestEqual(TEXT("Death clears all terrain visuals"), F.Character->TerrainStatus->GetTerrainMovementMultiplier(),
              1.f);
    return true;
}
#endif
