// 测试链：真实敌人预警宿主/地面/ASC -> 独立图节点 -> 孢子脉冲、冲锋实际扫掠、护甲GE及生命清理。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Unified/Nodes/AbilityEnemySpecialNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Character/GruxEnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    struct FEnemySpecialFixture
    {
        FEditorScriptExecutionGuard Guard;
        UWorld* World = nullptr;
        AGruxEnemyCharacter* Source = nullptr;
        AEnemyCharacter* Target = nullptr;
        UAbilityGraphDefinition* Graph = nullptr;
        UAbilityEnemySpecialNodeConfig* Config = nullptr;
        UAbilityGraphInstance* Run = nullptr;
        FAbilityNodeInput Input;
        int32 Effects = 0;
        FDelegateHandle DamageHandle;

        explicit FEnemySpecialFixture(EAbilityEnemySpecialKind Kind)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            auto* Floor = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Floor);
            Floor->AddInstanceComponent(Box);
            Floor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(2000, 2000, 20));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Floor->SetActorLocation(FVector(0, 0, -20));
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Source = World->SpawnActor<AGruxEnemyCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
            Target = World->SpawnActor<AEnemyCharacter>(FVector(150, 0, 100), FRotator::ZeroRotator, Spawn);
            Source->Tags = {TEXT("Player")};
            Target->Tags = {TEXT("Enemy")};
            for (AEnemyCharacter* Actor : {static_cast<AEnemyCharacter*>(Source), Target})
            {
                auto* ASC = Actor->GetAbilitySystemComponent();
                ASC->InitAbilityActorInfo(Actor, Actor);
                ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
                Actor->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Actor->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
                Actor->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
                Actor->SetActorLocation(FVector(Actor == Source ? 0 : 150, 0,
                                                Actor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
                Actor->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            }
            Source->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetArmorAttribute(), 10.f);
            FString Error;
            AbilityExecutionIdentity::CaptureSource(Source, Input.SourceToken, Error);
            Input.Origin = FVector::ZeroVector;
            Input.DamageParams.SourceAbilitySystemComponent = Source->GetAbilitySystemComponent();
            Input.DamageParams.DamageGameplayEffectClass = UGameplayEffect::StaticClass();
            Input.DamageParams.BaseDamage = 13.f;
            Input.DamageParams.DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Physics"));
            Graph = NewObject<UAbilityGraphDefinition>(World);
            Graph->SchemaVersion = 2;
            Graph->CompatibilityProfile = TEXT("UnifiedV2");
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = Kind == EAbilityEnemySpecialKind::Charge  ? TEXT("Move.GruxCharge")
                          : Kind == EAbilityEnemySpecialKind::Spore ? TEXT("Field.ShroomSpore")
                                                                    : TEXT("Status.GruxRoarArmor");
            Config = NewObject<UAbilityEnemySpecialNodeConfig>(Graph);
            Config->Kind = Kind;
            Config->WarningSeconds = .1f;
            Config->Duration = Kind == EAbilityEnemySpecialKind::Spore ? 3.f : 1.f;
            Config->Period = .25f;
            Config->Shape.Shape =
                Kind == EAbilityEnemySpecialKind::Charge ? EStoneGolemShape::Rectangle : EStoneGolemShape::Circle;
            Config->Shape.Length = 300.f;
            Config->Shape.Width = 160.f;
            Config->Shape.Radius = 300.f;
            Node.Config = Config;
            Graph->Nodes.Add(Node);
            Graph->EntryNodeId = Node.NodeId;
            Run = NewObject<UAbilityGraphInstance>(World);
            DamageHandle = Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [this](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
                {
                    ++Effects;
                });
        }
        ~FEnemySpecialFixture()
        {
            Run->Cancel();
            Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(DamageHandle);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        bool Start(FString& Error)
        {
            return Run->StartWithInput(Graph, Input, {}, Error);
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySpecialSpore, "test_25d.UnifiedGA.EnemySpecial.SporeWarningAndNoCatchup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 独立节点使用原预警宿主；首脉冲在预警后，卡顿不补发，终点不伤害。 */
bool FEnemySpecialSpore::RunTest(const FString&)
{
    FEnemySpecialFixture F(EAbilityEnemySpecialKind::Spore);
    FString Error;
    TestTrue(*FString::Printf(TEXT("Spore graph starts: %s"), *Error), F.Start(Error));
    TestTrue(TEXT("Existing attack presentation owns warning"), F.Source->IsAttacking());
    TestEqual(TEXT("No damage before warning"), F.Effects, 0);
    F.Run->Advance(.05f);
    TestEqual(TEXT("Warning delay is respected"), F.Effects, 0);
    F.Run->Advance(.05f);
    TestEqual(TEXT("First pulse at field activation"), F.Effects, 1);
    F.Run->Advance(2.4f);
    TestEqual(TEXT("Hitch consumes only one slot"), F.Effects, 2);
    F.Run->Advance(0.f);
    TestEqual(TEXT("Same instant cannot repeat pulse"), F.Effects, 2);
    F.Run->Advance(.1f);
    TestEqual(TEXT("Future cadence resumes"), F.Effects, 3);
    F.Run->Advance(.6f);
    TestEqual(TEXT("Expired field never adds final pulse"), F.Effects, 3);
    TestTrue(TEXT("Field completes"), F.Run->IsFinished());
    TestFalse(TEXT("Owned footprint is released"), F.Source->IsAttacking());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySpecialCharge, "test_25d.UnifiedGA.EnemySpecial.ChargeSweptHitIsDeduplicated",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 命中依据每次实际移动线段；同一目标不能被后续冲锋帧重复结算。 */
bool FEnemySpecialCharge::RunTest(const FString&)
{
    FEnemySpecialFixture F(EAbilityEnemySpecialKind::Charge);
    FString Error;
    TestTrue(TEXT("Charge starts with safe planned corridor"), F.Start(Error));
    F.Run->Advance(.1f);
    F.Run->Advance(.6f);
    TestTrue(TEXT("Source actually swept forward"), F.Source->GetActorLocation().X > 160.f);
    TestEqual(TEXT("Passed target receives one real GE"), F.Effects, 1);
    F.Run->Advance(.3f);
    F.Run->Advance(.11f);
    TestEqual(TEXT("Later segments do not re-hit target"), F.Effects, 1);
    TestTrue(TEXT("Charge completes"), F.Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySpecialRoar, "test_25d.UnifiedGA.EnemySpecial.RoarReusesOwnedArmorEffect",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 预警后经原Grux服务施加真实护甲，正常图完成保留持续GE，池回收仍按原所有权清理。 */
bool FEnemySpecialRoar::RunTest(const FString&)
{
    FEnemySpecialFixture F(EAbilityEnemySpecialKind::Roar);
    FString Error;
    TestTrue(TEXT("Roar starts"), F.Start(Error));
    TestFalse(TEXT("Armor waits for warning"), F.Source->HasRoarArmor());
    F.Run->Advance(.1f);
    TestTrue(TEXT("Original owned armor GE is active"), F.Source->HasRoarArmor());
    TestEqual(TEXT("Armor increased once"),
              F.Source->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), 35.f);
    TestFalse(TEXT("Roar impact remains owned during original recovery tail"), F.Run->IsFinished());
    TestTrue(TEXT("Presentation is not cleared in the impact frame"), F.Source->IsAttacking());
    F.Run->Advance(F.Config->Shape.RecoveryEnd - F.Config->Shape.HitTime() + .01f);
    TestTrue(TEXT("Roar action completes without stripping duration GE"), F.Run->IsFinished());
    TestFalse(TEXT("Recovery releases its presentation"), F.Source->IsAttacking());
    F.Source->EnableDungeonPooling();
    F.Source->SleepForDungeonPool(false);
    TestFalse(TEXT("Pool lifecycle clears persistent armor"), F.Source->HasRoarArmor());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySpecialSourceLife,
                                 "test_25d.UnifiedGA.EnemySpecial.SourceReuseCannotContinueCharge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 旧图的异步推进不能移动新来源生命。 */
bool FEnemySpecialSourceLife::RunTest(const FString&)
{
    FEnemySpecialFixture F(EAbilityEnemySpecialKind::Charge);
    FString Error;
    TestTrue(TEXT("Charge starts"), F.Start(Error));
    F.Run->Advance(.1f);
    const FVector Before = F.Source->GetActorLocation();
    F.Source->EnableDungeonPooling();
    F.Run->Advance(.6f);
    TestTrue(TEXT("New source life is not moved"), Before.Equals(F.Source->GetActorLocation()));
    TestEqual(TEXT("New source life deals no old charge damage"), F.Effects, 0);
    TestTrue(TEXT("Source invalidation is explicit"), F.Run->GetEndReason() == EAbilityGraphEndReason::SourceInvalid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySpecialStunCleanup, "test_25d.UnifiedGA.EnemySpecial.StunReleasesOwnedMovement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 眩晕先撤销预警序号；旧节点仍须归还移动占用，解除眩晕后不能永久留在MOVE_None。 */
bool FEnemySpecialStunCleanup::RunTest(const FString&)
{
    FEnemySpecialFixture F(EAbilityEnemySpecialKind::Spore);
    auto* Movement = F.Source->GetCharacterMovement();
    Movement->SetMovementMode(MOVE_Walking);
    Movement->bUseControllerDesiredRotation = true;
    FString Error;
    TestTrue(TEXT("Field starts and owns movement"), F.Start(Error));
    TestEqual(TEXT("Warning temporarily disables movement"), Movement->MovementMode.GetValue(), MOVE_None);
    F.Source->StunTagChanged(FGameplayTag::RequestGameplayTag(TEXT("Debuff.Stun")), 1);
    TestFalse(TEXT("Stun invalidates the original presentation serial"), F.Source->IsAttacking());
    F.Run->Advance(0.f);
    TestTrue(TEXT("Interrupted graph is finished"), F.Run->IsFinished());
    TestEqual(TEXT("Owned movement mode is restored despite lost presentation serial"),
              Movement->MovementMode.GetValue(), MOVE_Walking);
    TestTrue(TEXT("Owned rotation preference is restored"), Movement->bUseControllerDesiredRotation);
    TestEqual(TEXT("Stun still keeps effective speed at zero"), Movement->MaxWalkSpeed, 0.f);
    F.Source->StunTagChanged(FGameplayTag::RequestGameplayTag(TEXT("Debuff.Stun")), 0);
    TestTrue(TEXT("Removing stun restores usable movement"), Movement->MaxWalkSpeed > 0.f);
    TestEqual(TEXT("Interrupted warning never damages"), F.Effects, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySpecialWarningSurface,
                                 "test_25d.UnifiedGA.EnemySpecial.StandaloneSurfaceMatchesWarning",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 传入别处的预计算三角形不能让红色范围内目标漏伤；节点以当前预警形状重新生成地面。 */
bool FEnemySpecialWarningSurface::RunTest(const FString&)
{
    FEnemySpecialFixture F(EAbilityEnemySpecialKind::Spore);
    F.Config->LockedSurface = {FVector(1000, 1000, 0), FVector(1100, 1000, 0), FVector(1000, 1100, 0)};
    F.Config->LockedTriangles = {0, 1, 2};
    FString Error;
    TestTrue(TEXT("Standalone field accepts configuration with an old surface snapshot"), F.Start(Error));
    TestTrue(TEXT("Original warning is active at this invocation's origin"), F.Source->IsAttacking());
    F.Run->Advance(.1f);
    TestEqual(TEXT("Target inside the rendered circle is hit despite unrelated supplied triangles"), F.Effects, 1);
    TestTrue(TEXT("Caller configuration remains unchanged"), F.Config->LockedSurface[0].Equals(FVector(1000, 1000, 0)));
    return true;
}
#endif
