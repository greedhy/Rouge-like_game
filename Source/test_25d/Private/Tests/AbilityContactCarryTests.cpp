// 测试调用链：真实弹体接触 -> 原Actor GE/组合InputDamage GE -> 实际Context冲量；区别初始preset和上次临时随机结果。
// 两接触测试经生产图查询/投递而非手工调用命中；编辑器临时世界仅用ScriptGuard开放原UFUNCTION分发。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "Actor/MyProjectile.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"
#include "test_25d/test_25d.h"

namespace AbilityContactCarryTests
{
    /** 每次测试显式选择第一roll成功/失败，不依赖某个平台rand实现的固定seed值。 */
    int32 Seed(bool bSuccess)
    {
        for (int32 S = 1; S < 10000; ++S)
        {
            FMath::RandInit(S);
            if ((FMath::RandRange(0, 100) < 50.f) == bSuccess)
            {
                return S;
            }
        }
        return 0;
    }
    FDamageEffectParams Params(UAbilitySystemComponent* Source, FVector Preset)
    {
        FDamageEffectParams P;
        P.SourceAbilitySystemComponent = Source;
        P.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
        P.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
        P.BaseDamage = 25.f;
        P.KnockbackChance = 50.f;
        P.KnockbackForceMagnitude = 500.f;
        P.KnockbackForce = Preset;
        P.DeathImpulseMagnitude = 900.f;
        return P;
    }
    void Health(AEnemyCharacter* Target)
    {
        auto* ASC = Target->GetAbilitySystemComponent();
        ASC->InitAbilityActorInfo(Target, Target);
        if (!ASC->GetSet<UMyAttributeSet>())
        {
            ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Target));
        }
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    }
    FGuid Add(UAbilityGraphDefinition* G, FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode N;
        N.NodeId = FGuid::NewGuid();
        N.TypeId = Type;
        N.Config = Config;
        G->Nodes.Add(N);
        return N.NodeId;
    }
    UAbilityDeliveryNodeConfig* Delivery(UAbilityGraphDefinition* G)
    {
        auto* C = NewObject<UAbilityDeliveryNodeConfig>(G);
        C->Delivery.ProjectileClass = AMyProjectile::StaticClass();
        C->Delivery.Speed = 300.f;
        C->Delivery.CollisionRadius = 8.f;
        C->Delivery.MaxDistance = 1000.f;
        C->Delivery.bApplyDamage = false;
        return C;
    }
} // namespace AbilityContactCarryTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOriginalProjectileForcePresetTest,
                                 "test_25d.DynamicMage.ContactForces.OriginalPresetControl",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOriginalProjectileForcePresetTest::RunTest(const FString&)
{
    for (const FVector Preset : {FVector::ZeroVector, FVector(17, 0, 0)})
    {
        FAbilityGraphTestFixture F;
        FEditorScriptExecutionGuard ScriptGuard;
        F.Target->SetActorLocation(FVector(200, 0, 0));
        if (!F.Target->IsActorInitialized())
        {
            F.Target->InitializeComponents();
            F.Target->PostInitializeComponents();
        }
        AbilityContactCarryTests::Health(F.Target);
        F.Target->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        F.Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
        FVector Actual = FVector(999, 999, 999);
        int32 Applications = 0;
        auto* ASC = F.Target->GetAbilitySystemComponent();
        const auto Applied = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                ++Applications;
                Actual = UMyAbilitySystemLibrary::GetKnockbackForce(Spec.GetContext());
            });
        const FTransform Transform(FRotator::ZeroRotator, FVector(60, 0, 0));
        auto* Shot = AbilityTraditionalActions::CreateProjectileDeferred(
            F.SourceToken, AbilityContactCarryTests::Params(F.ASC, Preset), AMyProjectile::StaticClass(), Transform);
        Shot->FinishSpawning(Transform);
        if (!Shot->IsActorInitialized())
        {
            Shot->InitializeComponents();
            Shot->PostInitializeComponents();
        }
        if (!Shot->HasActorBegunPlay())
        {
            Shot->DispatchBeginPlay();
        }
        Shot->Sphere->SetSphereRadius(8.f);
        Shot->Sphere->IgnoreActorWhenMoving(F.Source, true);
        Shot->ProjectileMovement->InitialSpeed = Shot->ProjectileMovement->MaxSpeed = 300.f;
        Shot->ProjectileMovement->Velocity = FVector(300, 0, 0);
        FMath::RandInit(AbilityContactCarryTests::Seed(false));
        F.World->SetBegunPlay(true);
        Shot->ProjectileMovement->TickComponent(.6f, LEVELTICK_All, nullptr);
        F.World->SetBegunPlay(false);
        TestEqual(TEXT("Original native projectile applies one real Actor GE"), Applications, 1);
        TestTrue(TEXT("Original failed knockback roll retains only the initial preset"), Actual.Equals(Preset));
        TestEqual(TEXT("Original native control removes actual health once"),
                  ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 75.f);
        ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Applied);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContactForceDoesNotBecomeInputTest,
                                 "test_25d.DynamicMage.ContactForces.TwoPhysicalContactsKeepOriginalParams",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContactForceDoesNotBecomeInputTest::RunTest(const FString&)
{
    for (const FVector Preset : {FVector::ZeroVector, FVector(17, 0, 0)})
    {
        FAbilityGraphTestFixture F;
        FEditorScriptExecutionGuard ScriptGuard;
        F.Target->SetActorLocation(FVector(200, 0, 0));
        auto* B = F.World->SpawnActor<AEnemyCharacter>();
        B->Tags.AddUnique(TEXT("Enemy"));
        B->SetActorLocation(FVector(400, 0, 0));
        AbilityContactCarryTests::Health(F.Target);
        AbilityContactCarryTests::Health(B);
        FVector FirstForce = FVector::ZeroVector;
        FVector SecondForce = FVector(999, 999, 999);
        int32 FirstApplied = 0;
        int32 SecondApplied = 0;
        auto* AASC = F.Target->GetAbilitySystemComponent();
        auto* BASC = B->GetAbilitySystemComponent();
        const auto AHandle = AASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                ++FirstApplied;
                FirstForce = UMyAbilitySystemLibrary::GetKnockbackForce(Spec.GetContext());
            });
        const auto BHandle = BASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                ++SecondApplied;
                SecondForce = UMyAbilitySystemLibrary::GetKnockbackForce(Spec.GetContext());
            });
        auto* G = NewObject<UAbilityGraphDefinition>(F.World);
        G->SchemaVersion = 2;
        G->CompatibilityProfile = TEXT("UnifiedV2");
        using namespace AbilityContactCarryTests;
        const FGuid First = Add(G, TEXT("Delivery.ProjectileAwait"), Delivery(G));
        const FGuid ABridge = Add(G, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(G));
        const FGuid DamageA = Add(G, TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(G));
        auto* Anchor = NewObject<UAbilityAnchorNodeConfig>(G);
        Anchor->Anchor = EAbilitySpatialAnchor::ConfirmedImpact;
        Anchor->Offset = FVector(100, 0, 0);
        const FGuid StartB = Add(G, TEXT("Spatial.Anchor"), Anchor);
        auto* Nearest = NewObject<UAbilityNearestNodeConfig>(G);
        Nearest->Radius = 500.f;
        Nearest->bIncludeMass = false;
        Nearest->bExcludeInputTarget = true;
        const FGuid SelectB = Add(G, TEXT("Query.Nearest"), Nearest);
        const FGuid Second = Add(G, TEXT("Delivery.ProjectileAwait"), Delivery(G));
        const FGuid BBridge = Add(G, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(G));
        const FGuid DamageB = Add(G, TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(G));
        G->EntryNodeId = First;
        G->Edges = {{First, ABridge, TEXT("Hit")},
                    {ABridge, DamageA, TEXT("TargetFound")},
                    {DamageA, StartB, TEXT("DamageResolved")},
                    {StartB, SelectB, TEXT("Completed")},
                    {SelectB, Second, TEXT("TargetFound")},
                    {Second, BBridge, TEXT("Hit")},
                    {BBridge, DamageB, TEXT("TargetFound")}};
        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        Input.Origin = FVector(60, 0, 0);
        Input.Direction = FVector::ForwardVector;
        Input.DamageParams = Params(F.ASC, Preset);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 DamageEvents = 0;
        const int32 FailSeed = Seed(false);
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& E)
            {
                if (E.NodeType == TEXT("Effect.InputDamage") && E.OutputPort == TEXT("DamageResolved"))
                {
                    if (++DamageEvents == 1)
                    {
                        FMath::RandInit(FailSeed);
                    }
                }
            });
        FString Error;
        if (!TestTrue(TEXT("Two-contact composition starts"), Run->StartWithInput(G, Input, {}, Error)))
        {
            AddInfo(Error);
            AASC->OnGameplayEffectAppliedDelegateToSelf.Remove(AHandle);
            BASC->OnGameplayEffectAppliedDelegateToSelf.Remove(BHandle);
            return false;
        }
        FMath::RandInit(Seed(true));
        for (int32 Frame = 0; Frame < 300 && !Run->IsFinished(); ++Frame)
        {
            TArray<TWeakObjectPtr<AMyProjectile>> Shots;
            for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
            {
                Shots.Add(*It);
            }
            for (auto Shot : Shots)
            {
                if (Shot.IsValid() && !Shot->IsActorBeingDestroyed())
                {
                    Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
                }
            }
            Run->Advance(.01f);
        }
        TestEqual(TEXT("First physical contact applies exactly one actual GE"), FirstApplied, 1);
        TestEqual(TEXT("Second physical contact applies exactly one actual GE"), SecondApplied, 1);
        TestFalse(TEXT("The first successful roll produces a temporary force"), FirstForce.Equals(Preset));
        TestTrue(TEXT("The second failed roll uses original preset rather than first contact's temporary force"),
                 SecondForce.Equals(Preset));
        TestEqual(TEXT("First target loses health once"),
                  AASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 75.f);
        TestEqual(TEXT("Second target loses health once"),
                  BASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 75.f);
        Run->Cancel();
        AASC->OnGameplayEffectAppliedDelegateToSelf.Remove(AHandle);
        BASC->OnGameplayEffectAppliedDelegateToSelf.Remove(BHandle);
    }
    return true;
}
#endif
