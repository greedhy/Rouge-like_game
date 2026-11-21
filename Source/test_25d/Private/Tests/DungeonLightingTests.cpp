// 自动化调用链：临时世界 → 房型灯光组 → 预算、重复配置和远房停用验证。
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "Roguelike/DungeonRoomLightingRig.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLightingBudgetTest, "test_25d.DungeonLighting.RoomBudgetAndCulling",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDungeonLightingBudgetTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    auto* Rig = Fixture.World->SpawnActor<ADungeonRoomLightingRig>();
    const TArray<EDungeonRoomRole> Roles = {
        EDungeonRoomRole::Entrance, EDungeonRoomRole::Combat, EDungeonRoomRole::Elite,   EDungeonRoomRole::Boss,
        EDungeonRoomRole::Treasure, EDungeonRoomRole::Rest,   EDungeonRoomRole::Merchant};
    for (const EDungeonRoomRole Role : Roles)
    {
        Rig->RoomRole = Role;
        Rig->OnConstruction(Rig->GetActorTransform());
        Rig->OnConstruction(Rig->GetActorTransform());
        Rig->SetViewState(true, true);
        TInlineComponentArray<ULightComponent*> Lights(Rig);
        int32 Visible = 0;
        int32 Shadows = 0;
        for (const auto* Light : Lights)
        {
            Visible += Light->IsVisible() && Light->Intensity > 0.f;
            Shadows += Light->IsVisible() && Light->Intensity > 0.f && Light->CastShadows;
        }
        TestTrue(TEXT("Every role has multiple light pools within the nine-light budget"),
                 Visible >= 3 && Visible <= 9);
        TestTrue(TEXT("Current room has at most three local shadow lights"), Shadows <= 3);
        TestTrue(TEXT("Reconstruction does not duplicate components"), Lights.Num() <= 9);
        Rig->SetViewState(true, false);
        for (const auto* Light : Lights)
        {
            TestFalse(TEXT("Neighbor previews cast no shadows"), Light->CastShadows);
        }
        Rig->SetViewState(false, false);
        for (const auto* Light : Lights)
        {
            TestFalse(TEXT("Distant room light is disabled"), Light->IsVisible());
        }
        for (const auto* Flame : TInlineComponentArray<UNiagaraComponent*>(Rig))
        {
            TestFalse(TEXT("Distant room flame is inactive"), Flame->IsActive());
        }
        for (const auto* Base : TInlineComponentArray<UStaticMeshComponent*>(Rig))
        {
            TestEqual(TEXT("Decorative fixtures do not block combat or navigation"), Base->GetCollisionEnabled(),
                      ECollisionEnabled::NoCollision);
            TestFalse(TEXT("Decorative fixtures do not rebuild navigation"), Base->CanEverAffectNavigation());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLightingBoundaryTest, "test_25d.DungeonLighting.DoorThresholdAndReconfigure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDungeonLightingBoundaryTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    auto* First = Fixture.World->SpawnActor<ADungeonRoomLightingRig>();
    auto* Next = Fixture.World->SpawnActor<ADungeonRoomLightingRig>();
    Next->SetActorLocation(FVector(2400, 0, 0));
    Next->SetActorRotation(FRotator(0, 90, 0));
    TestFalse(TEXT("Threshold leaves the preceding cell"), First->ContainsPlayerLocation(FVector(1200, 0, 0)));
    TestTrue(TEXT("Threshold belongs to the next cell even for rotated templates"),
             Next->ContainsPlayerLocation(FVector(1200, 0, 0)));
    TestTrue(TEXT("Teleport selects the destination room"), Next->ContainsPlayerLocation(FVector(2400, 0, 0)));
    TestFalse(TEXT("Teleport deactivates the previous room"), First->ContainsPlayerLocation(FVector(2400, 0, 0)));
    First->SetViewState(false, false);
    First->OnConstruction(First->GetActorTransform());
    for (const auto* Light : TInlineComponentArray<ULightComponent*>(First))
    {
        TestFalse(TEXT("Game-world recipe rebuild must not activate a culled room"), Light->IsVisible());
    }
    return true;
}
#endif
