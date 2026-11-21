// 测试调用链：同帧两个真实弹体按顺序接触 -> 首次实际Actor GE回调 -> 来源换Avatar/目标池复用 -> 第二接触。
// 原native即时结算作为对照；动态图必须在同一执行器接触边界排空payload，不能等整批物理接触后才改世界状态。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDistributionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "Actor/MyProjectile.h"
#include "Components/CapsuleComponent.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"
#include "test_25d/test_25d.h"

namespace AbilityNativeContactOrderingTests
{
    void Target(AEnemyCharacter* A, FVector Position)
    {
        A->Tags.AddUnique(TEXT("Enemy"));
        A->SetActorLocation(Position);
        if (!A->IsActorInitialized())
        {
            A->InitializeComponents();
            A->PostInitializeComponents();
        }
        auto* ASC = A->GetAbilitySystemComponent();
        ASC->InitAbilityActorInfo(A, A);
        if (!ASC->GetSet<UMyAttributeSet>())
        {
            ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(A));
        }
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        A->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        A->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
    }
    FDamageEffectParams Damage()
    {
        FDamageEffectParams D;
        D.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
        D.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
        D.BaseDamage = 25.f;
        return D;
    }
    FGuid Node(UAbilityGraphDefinition* G, FName Type, UAbilityNodeConfig* C)
    {
        FAbilityGraphNode N;
        N.NodeId = FGuid::NewGuid();
        N.TypeId = Type;
        N.Config = C;
        G->Nodes.Add(N);
        return N.NodeId;
    }
    AMyProjectile* Original(const FAbilitySourceToken& Source, FVector TargetPoint)
    {
        const FVector Origin(60, 0, 0);
        const FVector Direction = (TargetPoint - Origin).GetSafeNormal();
        const FTransform Transform(Direction.Rotation(), Origin);
        auto* P = AbilityTraditionalActions::CreateProjectileDeferred(Source, Damage(), AMyProjectile::StaticClass(),
                                                                      Transform);
        P->FinishSpawning(Transform);
        if (!P->IsActorInitialized())
        {
            P->InitializeComponents();
            P->PostInitializeComponents();
        }
        if (!P->HasActorBegunPlay())
        {
            P->DispatchBeginPlay();
        }
        P->Sphere->SetSphereRadius(8.f);
        P->Sphere->IgnoreActorWhenMoving(Source.Avatar.Get(), true);
        P->ProjectileMovement->InitialSpeed = P->ProjectileMovement->MaxSpeed = 300.f;
        P->ProjectileMovement->Velocity = Direction * 300.f;
        return P;
    }
    /** 原与新各跑独立世界；首GE回调是真实GAS通知，不手工调用命中或伤害。 */
    void Run(FAutomationTestBase& Test, bool bRecycle)
    {
        float OldSecondHealth = -1.f;
        for (const bool bRuntime : {false, true})
        {
            FAbilityGraphTestFixture F;
            FEditorScriptExecutionGuard ScriptGuard;
            const FVector APosition(240, -120, 0);
            const FVector BPosition(240, 120, 0);
            Target(F.Target, APosition);
            auto* B = F.World->SpawnActor<AEnemyCharacter>();
            Target(B, BPosition);
            if (bRecycle)
            {
                B->EnableDungeonPooling(0.f);
            }
            const int32 InitialLease = B->GetDungeonPoolLease();
            bool bFirstApplied = false;
            bool bWakeSucceeded = !bRecycle;
            auto* AASC = F.Target->GetAbilitySystemComponent();
            auto* BASC = B->GetAbilitySystemComponent();
            const auto Hook = AASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
                {
                    if (bFirstApplied)
                    {
                        return;
                    }
                    bFirstApplied = true;
                    if (bRecycle)
                    {
                        B->SleepForDungeonPool();
                        bWakeSucceeded = B->WakeFromDungeonPool(FTransform(BPosition), NAME_None);
                    }
                    else
                    {
                        auto* Replacement = F.World->SpawnActor<AActor>();
                        F.ASC->InitAbilityActorInfo(F.Source, Replacement);
                    }
                });
            UAbilityGraphInstance* Instance = nullptr;
            TArray<AMyProjectile*> Shots;
            if (!bRuntime)
            {
                Shots = {Original(F.SourceToken, APosition), Original(F.SourceToken, BPosition)};
            }
            else
            {
                auto* G = NewObject<UAbilityGraphDefinition>(F.World);
                G->SchemaVersion = 2;
                G->CompatibilityProfile = TEXT("UnifiedV2");
                auto* S = NewObject<UAbilityScatterNodeConfig>(G);
                S->Count = 2;
                S->Spread = FMath::RadiansToDegrees(FMath::Atan2(120.f, 180.f)) * 2.f;
                const FGuid Scatter = Node(G, TEXT("Distribution.Scatter"), S);
                auto* D = NewObject<UAbilityDeliveryNodeConfig>(G);
                D->Delivery.ProjectileClass = AMyProjectile::StaticClass();
                D->Delivery.Speed = 300.f;
                D->Delivery.CollisionRadius = 8.f;
                D->Delivery.ContactProfile = EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass;
                D->Delivery.bApplyDamage = false;
                const FGuid Delivery = Node(G, TEXT("Delivery.ProjectileAwait"), D);
                const FGuid Bridge =
                    Node(G, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(G));
                const FGuid Payload = Node(G, TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(G));
                G->EntryNodeId = Scatter;
                G->Edges = {{Scatter, Delivery, TEXT("Launch")},
                            {Delivery, Bridge, TEXT("Hit")},
                            {Bridge, Payload, TEXT("TargetFound")}};
                FAbilityNodeInput Input;
                Input.SourceToken = F.SourceToken;
                Input.Origin = FVector(60, 0, 0);
                Input.Direction = FVector::ForwardVector;
                Input.DamageParams = Damage();
                Instance = NewObject<UAbilityGraphInstance>(F.World);
                FString Error;
                Test.TestTrue(TEXT("Native contact ordering graph starts"),
                              Instance->StartWithInput(G, Input, {}, Error));
                for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
                {
                    if (!It->IsActorBeingDestroyed())
                    {
                        Shots.Add(*It);
                    }
                }
                Shots.Sort(
                    [](const AMyProjectile& A, const AMyProjectile& B)
                    {
                        return A.GetActorForwardVector().Y < B.GetActorForwardVector().Y;
                    });
            }
            if (Test.TestEqual(TEXT("Two actual physical projectiles exist before this frame"), Shots.Num(), 2))
            {
                F.World->SetBegunPlay(true);
                Shots[0]->ProjectileMovement->TickComponent(.7f, LEVELTICK_All, nullptr);
                Test.TestTrue(TEXT("First physical contact applies its GE before the next projectile tick"),
                              bFirstApplied);
                Test.TestEqual(TEXT("First physical contact changes real health in this same boundary"),
                               AASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 75.f);
                if (bRecycle)
                {
                    Test.TestTrue(TEXT("First contact completes actual pool replacement before second contact"),
                                  bWakeSucceeded && B->GetDungeonPoolLease() != InitialLease);
                }
                if (IsValid(Shots[1]) && !Shots[1]->IsActorBeingDestroyed())
                {
                    Shots[1]->ProjectileMovement->TickComponent(.7f, LEVELTICK_All, nullptr);
                }
                F.World->SetBegunPlay(false);
                if (Instance && !Instance->IsFinished())
                {
                    Instance->Advance(.01f);
                }
                const float Health = BASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
                if (!bRuntime)
                {
                    OldSecondHealth = Health;
                    Test.TestEqual(TEXT("Original immediate contract gives the expected second-contact outcome"),
                                   Health, bRecycle ? 75.f : 100.f);
                }
                else
                {
                    Test.TestEqual(
                        TEXT("Dynamic native profile preserves the actual original same-frame contact order"), Health,
                        OldSecondHealth);
                }
            }
            if (Instance)
            {
                Instance->Cancel();
            }
            AASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
        }
    }
} // namespace AbilityNativeContactOrderingTests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeContactCancelsBeforeNextTest,
                                 "test_25d.DynamicMage.NativeFireBolt.SameFrameFirstGEInvalidatesSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeContactCancelsBeforeNextTest::RunTest(const FString&)
{
    AbilityNativeContactOrderingTests::Run(*this, false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeContactRecyclesBeforeNextTest,
                                 "test_25d.DynamicMage.NativeFireBolt.SameFrameFirstGERecyclesNextTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeContactRecyclesBeforeNextTest::RunTest(const FString&)
{
    AbilityNativeContactOrderingTests::Run(*this, true);
    return true;
}
#endif
