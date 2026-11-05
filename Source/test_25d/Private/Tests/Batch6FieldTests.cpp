#include "Misc/AutomationTest.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6WaveFrontTest, "test_25d.Batch6.WaveFront", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBatch6WaveFrontTest::RunTest(const FString&)
{
    TestTrue(TEXT("Initial origin included"), UBatch6FieldPrototype::IsInWaveFront(0,0,100));
    TestTrue(TEXT("New outer boundary included"), UBatch6FieldPrototype::IsInWaveFront(200,100,200));
    TestFalse(TEXT("Previous boundary cannot hit again"), UBatch6FieldPrototype::IsInWaveFront(100,100,200));
    TestFalse(TEXT("Entering swept interior does not hit"), UBatch6FieldPrototype::IsInWaveFront(50,100,200));
    TestFalse(TEXT("Ahead of front excluded"), UBatch6FieldPrototype::IsInWaveFront(201,100,200));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace Batch6FieldTests
{
struct FFixture
{
 UWorld* World;
 AEnemyCharacter* Caster;
 UPureMassSubsystem* Mass;
 UPureMassMonsterType* Type;
 FEditorScriptExecutionGuard Guard;
 FFixture()
 {
  World=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("Batch6Field")));
  GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
  Mass=World->GetSubsystem<UPureMassSubsystem>(); Mass->Enable();
  Type=NewObject<UPureMassMonsterType>(World); Type->Health=1000; Type->Radius=20; Type->HalfHeight=40; Type->Speed=0; Type->XPReward=0;
  Caster=World->SpawnActor<AEnemyCharacter>(); Caster->Tags.Add(TEXT("Player"));
  auto* ASC=Caster->GetAbilitySystemComponent(); ASC->InitAbilityActorInfo(Caster,Caster);
  ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Caster));
  ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(),100);
 }
 ~FFixture() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
 float Health(int32 Id) { FPureMassView V; return Mass->Resolve(Id,V)?V.Health:0; }
 UBatch6FieldPrototype* Config(EAbilityPrototypeKind Kind)
 {
  auto* P=NewObject<UBatch6FieldPrototype>(World); P->Kind=Kind; P->DamageType=FMyGameplayTags::Get().Damage_Magic;
  P->Damage=25; P->DamagePerLevel=0; P->Period=.25f; P->Duration=1; P->Radius=200; P->Speed=200; P->ManaPerSecond=0;
  P->Cooldown=0; // Runtime-only fixture: no GA commit/cooldown tag is involved.
  return P;
 }
 APureMassRuntime* Start(UBatch6FieldPrototype* P)
 {
  auto* C=NewObject<UPrototypeCastContext>(Caster); C->Caster=Caster; C->CastId=FGuid::NewGuid(); C->Level=1;
  auto* R=APureMassRuntime::Spawn(C,P,FVector::ZeroVector,FVector::ForwardVector);
  if(R && !R->HasActorBegunPlay()) R->DispatchBeginPlay(); return R;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6FieldRuntimeTest,"test_25d.Batch6.Field.RuntimeLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBatch6FieldRuntimeTest::RunTest(const FString&)
{
 using namespace Batch6FieldTests;
 FFixture F;
 const int32 Id=F.Mass->SpawnMonster(F.Type,FVector(100,0,0));
 auto* P=F.Config(EAbilityPrototypeKind::Field);
 auto* R=F.Start(P); if(!TestNotNull(TEXT("Real field spawned"),R)) return false;
 R->Tick(.75f); TestEqual(TEXT("Three scheduled pulses before final boundary"),F.Health(Id),925.f);
 R->Tick(.5f); TestEqual(TEXT("Overshoot clamps and preserves final pulse"),F.Health(Id),900.f);
 TestTrue(TEXT("Finite field finishes"),R->IsFinished());
 R->Tick(.5f); TestEqual(TEXT("Finished field never damages again"),F.Health(Id),900.f);
 auto* Toggle=F.Config(EAbilityPrototypeKind::Field); Toggle->bFollowCasterToggle=true;
 auto* Aura=F.Start(Toggle); if(!TestNotNull(TEXT("Follow field spawned"),Aura)) return false;
 F.Caster->SetActorLocation(FVector(500,100,0)); Aura->Tick(1.25f);
 TestFalse(TEXT("Toggle survives configured finite duration"),Aura->IsFinished());
 TestTrue(TEXT("Authority runtime follows caster"),Aura->GetActorLocation().Equals(F.Caster->GetActorLocation()));
 Aura->CancelCast(); TestTrue(TEXT("Explicit cancel ends aura"),Aura->IsFinished());
 TestTrue(TEXT("Cancel invalidates shared context"),Aura->Context->bCancelled);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6WaveRuntimeTest,"test_25d.Batch6.Wave.RuntimeSingleHit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBatch6WaveRuntimeTest::RunTest(const FString&)
{
 using namespace Batch6FieldTests;
 FFixture F; const int32 Inner=F.Mass->SpawnMonster(F.Type,FVector(50,0,0));
 const int32 Edge=F.Mass->SpawnMonster(F.Type,FVector(200,0,0));
 auto* P=F.Config(EAbilityPrototypeKind::Wave); P->bWaveFrontOnly=true;
 auto* R=F.Start(P); if(!TestNotNull(TEXT("Real wave spawned"),R)) return false;
 R->Tick(.5f); TestEqual(TEXT("Wave damages inner target on crossing"),F.Health(Inner),975.f);
 TestEqual(TEXT("Wave cannot damage target ahead of front"),F.Health(Edge),1000.f);
 R->Tick(.75f); TestEqual(TEXT("Inner target is not hit twice"),F.Health(Inner),975.f);
 TestEqual(TEXT("Final boundary reaches edge target once"),F.Health(Edge),975.f);
 TestTrue(TEXT("Wave terminates at its end"),R->IsFinished()); return true;
}
#endif
