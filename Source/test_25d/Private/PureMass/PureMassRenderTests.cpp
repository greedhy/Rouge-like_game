#include "Misc/AutomationTest.h"
#include "PureMass/PureMassCrowd.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassNetworkQuantizationTest,
                                 "test_25d.PureMass.Network.QuantizationSuppressesSubWireMotion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPureMassNetworkQuantizationTest::RunTest(const FString&)
{
    FPureMassView View;
    View.Id = 123;
    View.Location = FVector(10.01, -20.01, 30.01);
    View.Velocity = FVector(40.01, -50.01, 0.01);
    View.Health = 75.f;
    View.MaxHealth = 100.f;
    FPureMassNetEntity Item;
    TestTrue(TEXT("A new entity produces a network change"), Item.SetFromView(View));
    TestFalse(TEXT("Repeated server snapshots remain clean"), Item.SetFromView(View));

    View.Location += FVector(0.01, -0.01, 0.01);
    View.Velocity += FVector(0.01, -0.01, 0.01);
    TestFalse(TEXT("Motion smaller than the wire precision must not dirty the fast array"), Item.SetFromView(View));
    TestTrue(TEXT("Negative and positive positions use the same 0.1 cm grid"),
             FVector(Item.Location).Equals(FVector(10.0, -20.0, 30.0), 1.e-6));
    TestTrue(TEXT("Velocity is quantized independently"),
             FVector(Item.Velocity).Equals(FVector(40.0, -50.0, 0.0), 1.e-6));

    View.Location.X = 10.08;
    TestTrue(TEXT("Crossing a position quantization boundary sends a change"), Item.SetFromView(View));
    TestFalse(TEXT("The new quantized position is stable"), Item.SetFromView(View));
    View.Velocity.Y = -50.08;
    TestTrue(TEXT("Crossing a velocity quantization boundary sends a change"), Item.SetFromView(View));
    TestFalse(TEXT("The new quantized velocity is stable"), Item.SetFromView(View));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassNetworkCombatStateTest,
                                 "test_25d.PureMass.Network.CombatChangesAndClientView",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPureMassNetworkCombatStateTest::RunTest(const FString&)
{
    FPureMassView View;
    View.Id = 456;
    View.Location = FVector(-100.12, 200.23, 60.34);
    View.Velocity = FVector(3.45, -4.56, 0.0);
    View.Health = 85.f;
    View.MaxHealth = 120.f;
    View.Type = NewObject<UPureMassMonsterType>();
    FPureMassNetEntity Item;
    Item.SetFromView(View);

    View.Health = 84.999f;
    TestTrue(TEXT("Even small authoritative damage replicates while stationary"), Item.SetFromView(View));
    TestFalse(TEXT("Unchanged health does not repeat a delta"), Item.SetFromView(View));
    View.MaxHealth = 140.f;
    TestTrue(TEXT("Maximum-health changes replicate independently"), Item.SetFromView(View));
    View.State = 2;
    TestTrue(TEXT("An attack without movement produces a delta"), Item.SetFromView(View));
    View.Type = NewObject<UPureMassMonsterType>();
    TestTrue(TEXT("Changing monster type is visible to the client"), Item.SetFromView(View));

    const FPureMassView ClientView = Item.ToView();
    TestEqual(TEXT("Stable entity ID survives the client projection"), ClientView.Id, View.Id);
    TestTrue(TEXT("Client queries receive the quantized network location"),
             ClientView.Location.Equals(FVector(Item.Location), 1.e-6));
    TestTrue(TEXT("Client queries receive network velocity"),
             ClientView.Velocity.Equals(FVector(Item.Velocity), 1.e-6));
    TestEqual(TEXT("Client health is exactly the last server value"), ClientView.Health, View.Health);
    TestEqual(TEXT("Client maximum health is exactly the last server value"), ClientView.MaxHealth, View.MaxHealth);
    TestEqual(TEXT("Client attack state is preserved"), ClientView.State, uint8(2));
    TestTrue(TEXT("The rendering configuration reference is preserved"), ClientView.Type == View.Type);

    View.Health = 0.f;
    View.State = 3;
    TestTrue(TEXT("Death changes state even when the entity has stopped"), Item.SetFromView(View));
    TestEqual(TEXT("The value conversion never restores health on the client"), Item.ToView().Health, 0.f);
    TestEqual(TEXT("The value conversion preserves death"), Item.ToView().State, uint8(3));
    View.Id = 457;
    TestTrue(TEXT("A new stable ID cannot alias an old network item"), Item.SetFromView(View));
    TestFalse(TEXT("An unchanged replacement remains clean"), Item.SetFromView(View));
    return true;
}

#endif
