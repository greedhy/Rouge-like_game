#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassCombatTarget.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6ChainBounds,"test_25d.Batch6.Chain.Bounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBatch6ChainBounds::RunTest(const FString&)
{
    TestTrue(TEXT("Initial child within budget"),FBatch6ChainBudget::Allows(1,1,0,12,3,2));
    TestFalse(TEXT("Global budget shared by siblings"),FBatch6ChainBudget::Allows(12,1,0,12,3,2));
    TestFalse(TEXT("Depth stops exponential growth"),FBatch6ChainBudget::Allows(1,4,0,12,3,2));
    TestFalse(TEXT("Per parent branch bound"),FBatch6ChainBudget::Allows(1,1,2,12,3,2));
    TestFalse(TEXT("Hard cap even for invalid large authoring"),FBatch6ChainBudget::Allows(128,1,0,100000,100000,100000));
    TestFalse(TEXT("Depth zero supports root only"),FBatch6ChainBudget::Allows(1,1,0,12,0,2));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6ChainWorld,"test_25d.Batch6.Chain.WorldObstructionAndPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBatch6ChainWorld::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    AActor* Caster=World->SpawnActor<AActor>();
    TestTrue(TEXT("Unobstructed chain geometry"),PureMassCombat::GroundVisible(World,Caster,FVector::ZeroVector,FVector(500,0,0)));
    AActor* Wall=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(30,200,200));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent(); Wall->SetActorLocation(FVector(250,0,0));
    TestFalse(TEXT("World wall blocks actual trace"),PureMassCombat::GroundVisible(World,Caster,FVector::ZeroVector,FVector(500,0,0)));
    auto* Visual=World->SpawnActor<ABatch6ChainPresentation>();
    FBatch6ChainSegment Segment; Segment.Start=FVector::ZeroVector; Segment.End=FVector(500,0,0);
    Visual->Segments.Add(Segment); Visual->Present(); Visual->Present();
    TArray<UStaticMeshComponent*> Meshes; Visual->GetComponents(Meshes);
    TestEqual(TEXT("Repeated replication callback owns one fallback per segment"),Meshes.Num(),1);
    if(Meshes.Num()) TestEqual(TEXT("Fallback never blocks later casts"),Meshes[0]->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
