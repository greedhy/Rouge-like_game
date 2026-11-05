#include "Misc/AutomationTest.h"
#include "Actor/EnemySlingProjectile.h"
#include "AbilitySystem/Ability/LegacyMonsterAbility.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "LegacyProjectileTestListener.h"
#include "UObject/Script.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacyMonsterAssetsTest,"test_25d.LegacyMonsters.AuthoredConfiguration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLegacyMonsterAssetsTest::RunTest(const FString&)
{
 for(const TCHAR* Name:{TEXT("Spear"),TEXT("Sling"),TEXT("GhoulL"),TEXT("GhoulR")})
 {
  auto* Config=LoadObject<UStoneGolemAttackConfig>(nullptr,*FString::Printf(TEXT("/Game/BluePrints/Characters/LegacyExpansion/DA_Attack_%s.DA_Attack_%s"),Name,Name));
  if(!TestNotNull(Name,Config)) continue;
  FString Error;TestTrue(TEXT("Valid timing and warning dependencies"),Config->Tuning.Validate(Error));
  TestTrue(TEXT("Single target attack has exclamation"),Config->Tuning.bExclamation && Config->Tuning.bSingleTarget && !Config->Tuning.bDrawFootprint);
  if(auto* Sling=Cast<USlingAttackConfig>(Config))
  {
   TestTrue(TEXT("Valid projectile tuning"),Sling->ValidateProjectile());
   if(Sling->ProjectileClass)
   {
    const auto* Shot=Sling->ProjectileClass->GetDefaultObject<AEnemySlingProjectile>();
    TestEqual(TEXT("Straight shot has no inherited gravity"),Shot->ProjectileMovement->ProjectileGravityScale,0.f);
    TestFalse(TEXT("World aim is not rotated a second time"),Shot->ProjectileMovement->bInitialVelocityInLocalSpace);
   }
  }
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacySlingLeaseTest,"test_25d.LegacyMonsters.ProjectileLease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLegacySlingLeaseTest::RunTest(const FString&)
{
 auto* W=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 auto* Source=W->SpawnActor<AEnemyCharacter>();auto* Target=W->SpawnActor<AEnemyCharacter>();
 Source->Tags={FName("Enemy")};Target->Tags={FName("Player")};
 Source->GetAbilitySystemComponent()->InitAbilityActorInfo(Source,Source);
 Source->EnableDungeonPooling(0);
 auto* Shot=W->SpawnActor<AEnemySlingProjectile>();Shot->DamageEffectParams.SourceAbilitySystemComponent=Source->GetAbilitySystemComponent();Shot->InitializeSource(Source);
 TestTrue(TEXT("Live hostile target accepted"),Shot->IsValidOverlap(Target));
 Target->Tags={FName("Enemy")};TestFalse(TEXT("Friendly target ignored"),Shot->IsValidOverlap(Target));Target->Tags={FName("Player")};
 Source->SleepForDungeonPool(false);
 TestFalse(TEXT("Dormant source cannot damage"),Shot->IsValidOverlap(Target));
 TestTrue(TEXT("Source can wake"),Source->WakeFromDungeonPool(FTransform::Identity,NAME_None));
 TestFalse(TEXT("Old projectile cannot borrow new life"),Shot->IsValidOverlap(Target));
 GEngine->DestroyWorldContext(W);W->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacySlingBeginPlayTest,"test_25d.LegacyMonsters.ProjectileBeginPlayReentrancy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLegacySlingBeginPlayTest::RunTest(const FString&)
{
 FEditorScriptExecutionGuard ScriptGuard;
 auto* W=UWorld::CreateWorld(EWorldType::Game,false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 auto* Shot=W->SpawnActor<AEnemySlingProjectile>();
 TestNotNull(TEXT("Stop callback is reflected"),Shot->FindFunction(TEXT("WorldStopped")));
 auto* Listener=NewObject<ULegacyProjectileTestListener>(W);
 // Collision changes are synchronous and may destroy the actor through gameplay
 // callbacks. Reproduce that boundary without relying on level overlap ordering.
 Shot->Sphere->OnComponentCollisionSettingsChangedEvent.AddDynamic(Listener,&ULegacyProjectileTestListener::DestroyOnCollisionChange);
 Shot->DispatchBeginPlay();
 TestTrue(TEXT("Collision setup invokes the destructive callback"),Listener->bCalled);
 TestTrue(TEXT("Projectile is destroyed during collision setup"),Shot->IsActorBeingDestroyed());
 TestFalse(TEXT("Destroyed projectile does not receive a movement prerequisite"),
  Shot->ProjectileMovement->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate(
   [Shot](const FTickPrerequisite& P){return P.PrerequisiteObject.Get()==Shot;}));
 auto* LiveShot=W->SpawnActor<AEnemySlingProjectile>();
 LiveShot->DispatchBeginPlay();
 TestTrue(TEXT("Live projectile binds its stop callback"),LiveShot->ProjectileMovement->OnProjectileStop.IsBound());
 TestTrue(TEXT("Live projectile retains movement ordering"),
  LiveShot->ProjectileMovement->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate(
   [LiveShot](const FTickPrerequisite& P){return P.PrerequisiteObject.Get()==LiveShot;}));
 LiveShot->ProjectileMovement->OnProjectileStop.Broadcast(FHitResult());
 TestTrue(TEXT("World stop still destroys the live projectile"),LiveShot->IsActorBeingDestroyed());
 GEngine->DestroyWorldContext(W);W->DestroyWorld(false);return true;
}
#endif
