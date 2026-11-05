#include "Misc/AutomationTest.h"
#include "Character/PlayerCharacter.h"
#include "LifecyclePlayerProbe.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/Script.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLifecycleDeathTimerTest,"test_25d.Lifecycle.DeathTimerEarlyDestroy",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLifecycleDeathTimerTest::RunTest(const FString&)
{
 FEditorScriptExecutionGuard ScriptGuard;
 UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 APlayerCharacter* Player=World->SpawnActor<ALifecyclePlayerProbe>();
 if (!TestNotNull(TEXT("Player fixture"),Player))
 { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return false; }
 // A manually created world has not run InitializeActorsForPlay. RouteEndPlay
 // requires component initialization as well as BeginPlay.
 Player->PreInitializeComponents();
 Player->InitializeComponents();
 Player->PostInitializeComponents();
 TestTrue(TEXT("Player fully initialized for routed EndPlay"),Player->IsActorInitialized());
 Player->DispatchBeginPlay();
 Player->DeathTime=30.f;
 Player->Die(FVector::ZeroVector);
 FTimerHandle Handle=Player->DeathTimer;
 TestTrue(TEXT("Actual Die schedules delayed death"),World->GetTimerManager().TimerExists(Handle));
 TestTrue(TEXT("Actor began play before premature destruction"),Player->HasActorBegunPlay());
 Player->Destroy();
 TestFalse(TEXT("Destroy routed EndPlay"),Player->HasActorBegunPlay());
 // Do not invoke the known dangling callback on the old implementation.
 // Its survival after EndPlay is sufficient runtime evidence of the bug.
 const bool Leaked=World->GetTimerManager().TimerExists(Handle);
 TestFalse(TEXT("Death timer removed when player ends before timeout"),Leaked);
 if (Leaked)
 { FTimerHandle CleanupHandle=Handle; World->GetTimerManager().ClearTimer(CleanupHandle); }
 // Advance only once the unsafe old timer is removed, even on a failed run.
 World->Tick(LEVELTICK_All,.1f);
 World->Tick(LEVELTICK_All,31.f);
 TestFalse(TEXT("No delayed death timer survives past its original deadline"),World->GetTimerManager().TimerExists(Handle));
 GEngine->DestroyWorldContext(World);
 World->DestroyWorld(false);
 return true;
}
#endif

