// 调用链：原 Prototype defaults/细节点默认图 -> 同一执行器 -> 实际 Actor GE 或 Mass -> 原 hooks。
// 单独 FrameScale 用已经验证的 Sequence，不借尚未支持的编译分支掩盖实际伤害倍率缺口。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeTargetNodes.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeFineTargetDefaultTests
{
    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        bool bActor;
        TArray<FPureMassCombatTarget> Victims;
        TArray<float> ActualGEScalars;
        TArray<TPair<TWeakObjectPtr<UAbilitySystemComponent>, FDelegateHandle>> Handles;
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Context = nullptr;

        explicit FWorld(bool bInActor) : bActor(bInActor)
        {
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
            if (bActor)
            {
                Prepare(*Target, FVector(200, 0, 0));
                Victims.Add(FPureMassCombatTarget::FromActor(Target));
                Actor(FVector(250, 0, 0));
                Actor(FVector(300, 0, 0));
            }
            else
            {
                Target->Destroy();
                Enemy(FVector(200, 0, 0));
                Enemy(FVector(250, 0, 0));
                Enemy(FVector(300, 0, 0));
            }
        }

        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
            for (const auto& Item : Handles)
            {
                if (auto* Component = Item.Key.Get())
                {
                    Component->OnGameplayEffectAppliedDelegateToSelf.Remove(Item.Value);
                }
            }
        }

        void Prepare(AEnemyCharacter& Victim, FVector Position)
        {
            Victim.SetActorLocation(Position);
            auto* TargetASC = Victim.GetAbilitySystemComponent();
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
            Victim.GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Victim.GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Victim.GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
            Victim.GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
            const auto Handle = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [this](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                {
                    if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                    {
                        ActualGEScalars.Add(
                            Spec.GetSetByCallerMagnitude(FMyGameplayTags::Get().Damage_Magic, false, -1.f));
                    }
                });
            Handles.Add({TargetASC, Handle});
        }

        void Actor(FVector Position)
        {
            auto* Victim = World->SpawnActor<AEnemyCharacter>();
            Victim->Tags = {TEXT("Enemy")};
            auto* TargetASC = Victim->GetAbilitySystemComponent();
            TargetASC->InitAbilityActorInfo(Victim, Victim);
            TargetASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Victim));
            Prepare(*Victim, Position);
            Victims.Add(FPureMassCombatTarget::FromActor(Victim));
        }

        void Enemy(FVector Position)
        {
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 1000.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            Type->XPReward = 0;
            Victims.Add(FPureMassCombatTarget(Mass->SpawnMonster(Type, Position)));
        }

        float Health(int32 Index) const
        {
            const auto& Victim = Victims[Index];
            if (Victim.IsActor())
            {
                return CastChecked<AEnemyCharacter>(Victim.Actor.Get())
                    ->GetAbilitySystemComponent()
                    ->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
            }
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Victim.EntityId, View) ? View.Health : -1.f;
        }

        UAbilityPrototype* Parameters(EAbilityPrototypeKind Kind, float Damage)
        {
            auto* Result = NewObject<UAbilityPrototype>(World);
            Result->Kind = Kind;
            Result->Cooldown = 0.f;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Damage = Damage;
            Result->DamagePerLevel = 0.f;
            Result->Radius = Kind == EAbilityPrototypeKind::Burst ? 1.f : 110.f;
            Result->Range = 1200.f;
            Result->Period = .1f;
            Result->Duration = Kind == EAbilityPrototypeKind::Burst ? .1f : 2.f;
            Result->Count = 3;
            Result->DamageFalloff = .5f;
            return Result;
        }

        UAbilityGraphDefinition* Compile(FAutomationTestBase& Test, UAbilityPrototype& Root, bool bFine)
        {
            const FName Profile = bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1");
            FString Error;
            auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(World, &Root, Profile, Error)
                                     : UAbilityLegacyGraphCompiler::Compile(World, &Root, Profile, Error);
            Test.TestNotNull(FString(bFine ? TEXT("Fine default: ") : TEXT("Original default: ")) + Error, Definition);
            if (bFine && Definition)
            {
                Test.TestFalse(TEXT("Fine default does not wrap a complete legacy runtime"),
                               Definition->Nodes.ContainsByPredicate(
                                   [](const auto& Node)
                                   {
                                       return Node.TypeId.ToString().StartsWith(TEXT("Legacy."));
                                   }));
            }
            return Definition;
        }

        bool Start(UAbilityGraphDefinition* Definition, FString& Error)
        {
            Context = NewObject<UPrototypeCastContext>(World);
            Context->Caster = Source;
            Context->Level = 1;
            Context->CastId = FGuid::NewGuid();
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            Context->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            if (!Context->CaptureSourceLife(Error))
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Context->GetSourceLife();
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            Input.Direction = FVector::ForwardVector;
            FAbilityTargetToken Token;
            if (!AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Victims[0], Token, Error))
            {
                return false;
            }
            Input.LiveTargetToken = Token;
            Run = NewObject<UAbilityGraphInstance>(World);
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            return Run->StartWithInput(Definition, Input, Options, Error);
        }

        void Advance(float Delta)
        {
            ++GFrameCounter;
            Run->Advance(Delta);
        }
    };
} // namespace AbilityPrototypeFineTargetDefaultTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineChainDefaultHooksAndFalloff,
    "test_25d.UnifiedGA.DynamicMage.FineTargetDefaults.ChainActualDamageAndIndependentHitExpiryFragments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineChainDefaultHooksAndFalloff::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineTargetDefaultTests;
    for (const bool bFine : {false, true})
    {
        for (const bool bActor : {false, true})
        {
            FWorld F(bActor);
            auto* Root = F.Parameters(EAbilityPrototypeKind::Chain, 16.f);
            Root->OnHit = F.Parameters(EAbilityPrototypeKind::Burst, 3.f);
            Root->OnExpire = F.Parameters(EAbilityPrototypeKind::Burst, 7.f);
            auto* Definition = F.Compile(*this, *Root, bFine);
            if (!Definition)
            {
                return false;
            }
            FString Error;
            if (!TestTrue(TEXT("Actual chain default starts: ") + Error, F.Start(Definition, Error)))
            {
                return false;
            }
            F.Advance(0.f);
            for (int32 Index = 0; Index < 10; ++Index)
            {
                F.Advance(.11f);
            }
            if (bActor)
            {
                for (const float Value : {16.f, 8.f, 4.f, 7.f})
                {
                    TestEqual(TEXT("Actual GE receives each root falloff/expiry scalar once"),
                              F.ActualGEScalars
                                  .FilterByPredicate(
                                      [Value](float Item)
                                      {
                                          return Item == Value;
                                      })
                                  .Num(),
                              1);
                }
                TestEqual(TEXT("OnHit uses its own fixed damage, not the parent falloff"),
                          F.ActualGEScalars
                              .FilterByPredicate(
                                  [](float Item)
                                  {
                                      return Item == 3.f;
                                  })
                              .Num(),
                          3);
                TestEqual(TEXT("Exactly seven real Actor payload applications"), F.ActualGEScalars.Num(), 7);
            }
            else
            {
                TestEqual(TEXT("First root16 plus independent child3"), F.Health(0), 981.f);
                TestEqual(TEXT("Second root8 plus independent child3"), F.Health(1), 989.f);
                TestEqual(TEXT("Last root4 plus child3 plus expiry7 at final location"), F.Health(2), 986.f);
            }
            TestEqual(TEXT("Root plus three hit fragments plus expiry preserve logical budget"), F.Context->Spawned, 5);
            TestTrue(TEXT("Actual finite default fully completes"), F.Run->IsFinished());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineBeamDefaultHooks, "test_25d.UnifiedGA.DynamicMage.FineTargetDefaults.BeamDamagePeriodAndExpiryUseOwnOrigins",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineBeamDefaultHooks::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineTargetDefaultTests;
    for (const bool bFine : {false, true})
    {
        for (const bool bActor : {false, true})
        {
            FWorld F(bActor);
            if (bActor)
            {
                F.Actor(FVector(0, 0, 30));
                // 此敌人只用于观察源位置的 Period/Expiry，不是 LOS 阻挡物。
                // Enemy 的 Widget/Mesh 等组件也可能阻挡 Visibility，不能只关闭其 Capsule。
                AActor* Sentinel = F.Victims.Last().Actor.Get();
                FHitResult Before;
                FCollisionQueryParams Sight(SCENE_QUERY_STAT(FineBeamFixturePreflight), false, F.Source);
                F.World->LineTraceSingleByChannel(Before, F.Source->GetActorLocation() + FVector(0, 0, 30),
                                                  F.Victims[0].Actor->GetActorLocation(), ECC_Visibility, Sight);
                AddInfo(FString::Printf(TEXT("Actor beam fixture preflight blocker=%s component=%s"),
                                        *GetNameSafe(Before.GetActor()), *GetNameSafe(Before.GetComponent())));
                TArray<UPrimitiveComponent*> Parts;
                Sentinel->GetComponents(Parts);
                for (auto* Part : Parts)
                {
                    Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
                }
                FHitResult After;
                const bool bBlocked =
                    F.World->LineTraceSingleByChannel(After, F.Source->GetActorLocation() + FVector(0, 0, 30),
                                                      F.Victims[0].Actor->GetActorLocation(), ECC_Visibility, Sight);
                TestTrue(TEXT("Period-position sentinel leaves the old Actor beam's actual visibility ray clear"),
                         !bBlocked || After.GetActor() == F.Victims[0].Actor.Get());
            }
            else
            {
                F.Enemy(FVector(0, 0, 30));
            }
            auto* Root = F.Parameters(EAbilityPrototypeKind::Beam, 5.f);
            Root->Duration = .35f;
            Root->OnHit = F.Parameters(EAbilityPrototypeKind::Burst, 3.f);
            Root->OnPeriod = F.Parameters(EAbilityPrototypeKind::Burst, 7.f);
            Root->OnExpire = F.Parameters(EAbilityPrototypeKind::Burst, 11.f);
            auto* Definition = F.Compile(*this, *Root, bFine);
            if (!Definition)
            {
                return false;
            }
            FString Error;
            if (!TestTrue(TEXT("Actual beam default starts: ") + Error, F.Start(Definition, Error)))
            {
                return false;
            }
            for (int32 Index = 0; Index < 6; ++Index)
            {
                F.Advance(.1f);
            }
            TestEqual(TEXT("Three actual root pulses and hit fragments settle at the target"), F.Health(0),
                      bActor ? 940.f : 976.f);
            TestEqual(TEXT("Period and expiry fragments center at actual caster+30 host origin"), F.Health(3),
                      bActor ? 960.f : 968.f);
            TestEqual(TEXT("Root + hit3 + period3 + expiry preserve actual logical spawns"), F.Context->Spawned, 8);
            TestTrue(TEXT("Finite beam and its children complete normally"), F.Run->IsFinished());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFineFrameDamageConsumesFalloff,
    "test_25d.UnifiedGA.DynamicMage.FineTargetDefaults.FrameDamageConsumesSequenceScaleExactlyOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFineFrameDamageConsumesFalloff::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineTargetDefaultTests;
    FWorld F(false);
    auto* Parameters = F.Parameters(EAbilityPrototypeKind::Chain, 16.f);
    auto* Definition = NewObject<UAbilityGraphDefinition>(F.World);
    Definition->SchemaVersion = 2;
    Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
    Definition->CompatibilityProfile = TEXT("PrototypeFine.Mixed.v1");
    auto Add = [&](FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Definition->Nodes.Add(Node);
        return Node.NodeId;
    };
    auto Edge = [&](FGuid From, FName Port, FGuid To)
    {
        FAbilityGraphEdge Link;
        Link.From = From;
        Link.OutputPort = Port;
        Link.To = To;
        Definition->Edges.Add(Link);
    };
    auto* ScopeConfig = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
    ScopeConfig->Parameters = DuplicateObject(Parameters, ScopeConfig);
    ScopeConfig->Profile = Definition->CompatibilityProfile;
    const auto Scope = Add(TEXT("Scope.Prototype"), ScopeConfig);
    const auto Sequence = Add(TEXT("Control.TargetSequence"), NewObject<UAbilityTargetSequenceNodeConfig>(Definition));
    auto* PayloadConfig = NewObject<UAbilityPrototypeDamageNodeConfig>(Definition);
    PayloadConfig->bConsumeInputScales = true;
    const auto Damage = Add(TEXT("Effect.PrototypeDamageBatch"), PayloadConfig);
    Definition->EntryNodeId = Scope;
    Edge(Scope, TEXT("Ready"), Sequence);
    Edge(Sequence, TEXT("Action"), Damage);
    FString Error;
    if (!TestTrue(TEXT("Actual registered sequence uses a real immutable frame: ") + Error, F.Start(Definition, Error)))
    {
        return false;
    }
    F.Advance(0.f);
    F.Advance(.11f);
    F.Advance(.11f);
    TestEqual(TEXT("First actual contact consumes1.0 once"), F.Health(0), 984.f);
    TestEqual(TEXT("Second actual contact consumes0.5 once"), F.Health(1), 992.f);
    TestEqual(TEXT("Third actual contact consumes0.25 once"), F.Health(2), 996.f);
    TestEqual(TEXT("Runtime falloff never mutates source frame parameters"), Parameters->Damage, 16.f);
    return true;
}
#endif
