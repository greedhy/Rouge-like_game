// 测试链：真实目录导出Scope -> 实际BeginPreview/ApplyEdit事务 -> 接受快照 -> 同一生产执行器/实际GE与寿命。
// 拒绝不存在的语义轴，不把Radius暗换为CollisionRadius/Range，也不让父容器倍率隐式污染子Scope。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace AbilityPrototypeScopeScalarTests
{
    struct FUnsupported
    {
        const TCHAR* Name;
        const TCHAR* ClassPath;
        const TCHAR* Modifier;
    };

    const FUnsupported Unsupported[] = {
        {TEXT("EmitterDamage"),
         TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Emitter.GA_Prototype_Emitter_C"),
         TEXT("Scale.Damage")},
        {TEXT("DelayDamage"),
         TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Delay.GA_Prototype_Delay_C"),
         TEXT("Scale.Damage")},
        {TEXT("SurfaceDamage"),
         TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Surface.GA_Prototype_Surface_C"),
         TEXT("Scale.Damage")},
        {TEXT("MultishotContainerDamage"),
         TEXT(
             "/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Multishot.GA_Prototype_Multishot_C"),
         TEXT("Scale.Damage")},
        {TEXT("MultishotContainerRadius"),
         TEXT(
             "/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Multishot.GA_Prototype_Multishot_C"),
         TEXT("Scale.Radius")},
        {TEXT("MultishotContainerDuration"),
         TEXT(
             "/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Multishot.GA_Prototype_Multishot_C"),
         TEXT("Scale.Duration")},
        {TEXT("ProjectileRadius"),
         TEXT(
             "/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Projectile.GA_Prototype_Projectile_C"),
         TEXT("Scale.Radius")},
        {TEXT("BeamRadius"),
         TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Beam.GA_Prototype_Beam_C"),
         TEXT("Scale.Radius")},
        {TEXT("OrbitRadius"),
         TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Orbit.GA_Prototype_Orbit_C"),
         TEXT("Scale.Radius")},
        {TEXT("LineRadius"),
         TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Line.GA_Prototype_Line_C"),
         TEXT("Scale.Radius")},
        {TEXT("ActorRootOrbitDuration"),
         TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Orbit.GA_Prototype_Orbit_C"),
         TEXT("Scale.Duration")},
        {TEXT("MixedRootOrbitDuration"), TEXT("/Game/PureMass/Abilities/GA_Prototype_Orbit.GA_Prototype_Orbit_C"),
         TEXT("Scale.Duration")},
        {TEXT("MixedFollowRootDuration"), TEXT("/Game/CompositionBatch6/GA_StarFlameHalo.GA_StarFlameHalo_C"),
         TEXT("Scale.Duration")},
        {TEXT("DelayDuration"),
         TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Delay.GA_Prototype_Delay_C"),
         TEXT("Scale.Duration")},
    };

    /** 请求使用实际服务器目录摘要与版本，只指定已登记模板，不能注入任意配置对象。 */
    FAbilityRuntimeDeveloperEdit Edit(FAbilityActualMageFrontendFixture& F,
                                      const FAbilityRuntimeDeveloperDefinition& Row, int32 Node, FName Modifier,
                                      float Value)
    {
        FAbilityRuntimeDeveloperEdit Result;
        Result.DefinitionId = Row.DefinitionId;
        Result.NodeIndex = Node;
        Result.ModifierId = Modifier;
        Result.Value = Value;
        Result.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
        Result.ExpectedBaseHash = Row.BaseHash;
        return Result;
    }

    /** 根与single pellet采用不同Scope；不得因TypeId相同就把容器参数当成所有子片段参数。 */
    int32 ScopeIndex(const UAbilityGraphDefinition& Graph, bool bPellet = false)
    {
        return Graph.Nodes.IndexOfByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Node.Config);
                return Scope && (bPellet ? Scope->bSingleProjectile : Node.NodeId == Graph.EntryNodeId);
            });
    }

    /** 独立动作测试明确使用实际接受快照；它不代替GA前端输入专项测试。 */
    UAbilityGraphInstance* StartActualSnapshot(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& F,
                                               const FAbilityRuntimeDeveloperDefinition& Row,
                                               UAbilityGraphDefinition& Definition, bool bFlight)
    {
        const auto* Ability = Row.Base.AbilityClass->GetDefaultObject<UPrototypeAbility>();
        auto* Context = NewObject<UPrototypeCastContext>(F.World);
        Context->Caster = F.Pawn;
        Context->Level = 1;
        Context->CastId = FGuid::NewGuid();
        Context->DamageParams.SourceAbilitySystemComponent = F.PlayerASC;
        Context->DamageParams.WorldContextObject = F.Pawn;
        const auto* Field = FindFProperty<FClassProperty>(Ability->GetClass(), TEXT("DamageEffectClass"));
        Context->DamageParams.DamageGameplayEffectClass =
            Field ? Cast<UClass>(Field->GetObjectPropertyValue_InContainer(Ability)) : nullptr;
        FString Error;
        if (!Test.TestNotNull(TEXT("Actual original damage GE is retained"),
                              Context->DamageParams.DamageGameplayEffectClass.Get()) ||
            !Test.TestTrue(TEXT("Original actual PlayerState/Avatar life is captured"),
                           Context->CaptureSourceLife(Error)))
        {
            return nullptr;
        }
        FAbilityNodeInput Input;
        Input.Context = Context;
        Input.SourceToken = Context->GetSourceLife();
        Input.DamageParams = Context->DamageParams;
        Input.Direction = FVector::ForwardVector;
        Input.Origin = F.Pawn->GetActorLocation() + (bFlight ? FVector(60, 0, 30) : FVector::ZeroVector);
        if (bFlight)
        {
            Input.Direction = (F.Target->GetActorLocation() - Input.Origin).GetSafeNormal();
        }
        Input.EventCenter = Input.Origin;
        FAbilityTargetToken Target;
        if (!AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, FPureMassCombatTarget::FromActor(F.Target),
                                                     Target, Error))
        {
            Test.AddError(Error);
            return nullptr;
        }
        Input.LiveTargetToken = Target;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Definition.CompatibilityProfile;
        Options.BudgetPolicy = Definition.BudgetPolicy;
        Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
        if (!Test.TestTrue(TEXT("Accepted actual snapshot runs in production builtins: ") + Error,
                           Run->StartWithInput(&Definition, Input, Options, Error)))
        {
            return nullptr;
        }
        return Run;
    }
} // namespace AbilityPrototypeScopeScalarTests

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FPrototypeScopeUnsupportedAxes,
                                  "test_25d.DynamicMage.ScopeScalars.UnsupportedAxisTransaction",
                                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FPrototypeScopeUnsupportedAxes::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const auto& Case : AbilityPrototypeScopeScalarTests::Unsupported)
    {
        Names.Add(Case.Name);
        Commands.Add(Case.Name);
    }
}
bool FPrototypeScopeUnsupportedAxes::RunTest(const FString& Name)
{
    using namespace AbilityPrototypeScopeScalarTests;
    for (const auto& Case : Unsupported)
    {
        if (Name != Case.Name)
        {
            continue;
        }
        FAbilityActualMageFrontendFixture F;
        FString Error;
        const auto* Row = F.Definition(Case.ClassPath, Error);
        if (!TestNotNull(TEXT("Concrete original class exists in current actual catalog"), Row))
        {
            AddInfo(Error);
            return false;
        }
        const int32 Index = ScopeIndex(*Row->Base.DefaultGraph);
        if (!TestTrue(TEXT("Current actual default exporter exposes its real root Scope"), Index != INDEX_NONE) ||
            !TestTrue(TEXT("Actual directory preview starts without manual base registration"),
                      F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const auto* Parameters =
            CastChecked<UAbilityPrototypeScopeNodeConfig>(Row->Base.DefaultGraph->Nodes[Index].Config);
        if (Name == TEXT("MixedFollowRootDuration"))
        {
            const auto* Field = Cast<UBatch6FieldPrototype>(Parameters->Parameters);
            TestTrue(TEXT("The concrete current asset really is a mixed root toggle follow field"),
                     Field && Field->bFollowCasterToggle && Parameters->LogicalDepth == 0 &&
                         Parameters->Profile == TEXT("PrototypeFine.Mixed.v1"));
        }
        const auto Before = F.Developer->GetDeveloperState();
        FAbilityRuntimeRecipeSnapshot Snapshot;
        if (!TestTrue(TEXT("Accepted real preview produces an initial snapshot"),
                      F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Snapshot, Error)))
        {
            return false;
        }
        const FString BeforeHash = AbilityRuntimeRecipes::Fingerprint(*Snapshot.Graph);
        const FString OriginalAsset = AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph);
        const float Damage = Parameters->Parameters->Damage;
        const float Radius = Parameters->Parameters->Radius;
        const float Duration = Parameters->Parameters->Duration;
        AddInfo(FString(Case.ClassPath) + TEXT(" | ") + Case.Modifier +
                TEXT(" | Gate=actual catalog ApplyEdit transaction; no frontend/network claim"));
        TestFalse(TEXT("Unsupported semantic axis is rejected rather than accepted as a no-effect edit"),
                  F.Developer->ApplyEdit(Edit(F, *Row, Index, Case.Modifier, 2.f), Error));
        TestEqual(TEXT("Rejected edit preserves accepted recipe revision"), F.Developer->GetDeveloperState().Revision,
                  Before.Revision);
        TestEqual(TEXT("Rejected edit preserves accepted content hash"), F.Developer->GetDeveloperState().ContentHash,
                  Before.ContentHash);
        TestEqual(TEXT("Rejected edit preserves accepted base hash"), F.Developer->GetDeveloperState().BaseHash,
                  Before.BaseHash);
        TestFalse(TEXT("Rejected edit creates no hidden modifier source"),
                  F.Developer->HasContributions(Row->Base.AbilityTag));
        FAbilityRuntimeRecipeSnapshot After;
        TestTrue(TEXT("Same accepted version remains available after rejection"),
                 F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, After, Error));
        if (After.Graph)
        {
            TestEqual(TEXT("Rejected edit preserves every actual accepted graph parameter"),
                      AbilityRuntimeRecipes::Fingerprint(*After.Graph), BeforeHash);
        }
        TestEqual(TEXT("Original damage parameter is unchanged"), Parameters->Parameters->Damage, Damage);
        TestEqual(TEXT("Original radius parameter is unchanged"), Parameters->Parameters->Radius, Radius);
        TestEqual(TEXT("Original duration parameter is unchanged"), Parameters->Parameters->Duration, Duration);
        TestEqual(TEXT("Original exported definition remains immutable"),
                  AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph), OriginalAsset);
        F.Developer->EndPreview(Error);
        return true;
    }
    return false;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FPrototypeScopeSupportedAxes,
                                  "test_25d.DynamicMage.ScopeScalars.SupportedAxisRealConsumption",
                                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FPrototypeScopeSupportedAxes::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const TCHAR* Name :
         {TEXT("BurstDamage"), TEXT("FieldRadius"), TEXT("ProjectileDuration"), TEXT("SingleMultishotDamage")})
    {
        Names.Add(Name);
        Commands.Add(Name);
    }
}
bool FPrototypeScopeSupportedAxes::RunTest(const FString& Name)
{
    using namespace AbilityPrototypeScopeScalarTests;
    const bool bField = Name == TEXT("FieldRadius");
    const bool bDuration = Name == TEXT("ProjectileDuration");
    const bool bPellet = Name == TEXT("SingleMultishotDamage");
    const TCHAR* ClassName = bField      ? TEXT("Field")
                             : bDuration ? TEXT("Projectile")
                             : bPellet   ? TEXT("Multishot")
                                         : TEXT("Burst");
    const FString Path = FString::Printf(
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_%s.GA_Prototype_%s_C"), ClassName,
        ClassName);
    float OriginalDamage = 0.f;
    bool bOriginalAlive = false;
    for (const bool bScaled : {false, true})
    {
        FAbilityActualMageFrontendFixture F;
        FString Error;
        const auto* Row = F.Definition(Path, Error);
        if (!TestNotNull(TEXT("Actual original positive-control class is in catalog"), Row) ||
            !TestTrue(TEXT("Actual positive-control preview starts"),
                      F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const int32 Index = ScopeIndex(*Row->Base.DefaultGraph, bPellet);
        if (!TestTrue(TEXT("Exact intended root or single-pellet Scope exists"), Index != INDEX_NONE))
        {
            return false;
        }
        const auto* Scope = CastChecked<UAbilityPrototypeScopeNodeConfig>(Row->Base.DefaultGraph->Nodes[Index].Config);
        const auto& Parameters = *Scope->Parameters;
        F.Target->SetActorLocation(bField      ? FVector(Parameters.Radius * 1.5f, 0, 0)
                                   : bDuration ? FVector(100000, 0, 0)
                                   : bPellet   ? FVector(500, 0, 0)
                                               : FVector(150, 0, 0));
        if (bScaled)
        {
            const FName Modifier = bField      ? FName(TEXT("Scale.Radius"))
                                   : bDuration ? FName(TEXT("Scale.Duration"))
                                               : FName(TEXT("Scale.Damage"));
            if (!TestTrue(TEXT("Existing consumed axis remains accepted by actual panel transaction"),
                          F.Developer->ApplyEdit(Edit(F, *Row, Index, Modifier, bDuration ? .25f : 2.f), Error)))
            {
                AddInfo(Error);
                return false;
            }
        }
        FAbilityRuntimeRecipeSnapshot Snapshot;
        if (!TestTrue(TEXT("Use the actual accepted snapshot, never an alternative hand-installed graph"),
                      F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Snapshot, Error)))
        {
            return false;
        }
        FMath::RandInit(291472);
        const TStrongObjectPtr<UAbilityGraphInstance> Run(
            StartActualSnapshot(*this, F, *Row, *Snapshot.Graph, bDuration || bPellet));
        if (!Run.IsValid())
        {
            return false;
        }
        AddInfo(Path + TEXT(" | consumed actual snapshot: ") + Name +
                TEXT(" | action-level GE/health/lifetime, not frontend input/network evidence"));
        const float Observation =
            bDuration ? FMath::Min(Parameters.Duration * .5f, Parameters.Range / Parameters.Speed * .75f)
                      : Parameters.Duration + 1.f;
        for (float Time = 0.f; Time + .001f < Observation; Time += .01f)
        {
            ++GFrameCounter;
            Run->Advance(FMath::Min(.01f, Observation - Time));
        }
        const float Damage =
            10000.f - F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        if (bDuration)
        {
            TestTrue(TEXT("Duration observation really lies between scaled cutoff and original range/duration"),
                     Observation > Parameters.Duration * .25f + .02f);
            if (bScaled)
            {
                TestTrue(TEXT("Shortened actual projectile duration ends flight before the original cutoff"),
                         Run->IsFinished());
                TestTrue(TEXT("Original unscaled projectile still existed at exactly that observation"),
                         bOriginalAlive);
            }
            else
            {
                bOriginalAlive = !Run->IsFinished();
                TestTrue(TEXT("Unmodified actual projectile is still in genuine flight"), bOriginalAlive);
            }
        }
        else if (bField)
        {
            if (bScaled)
            {
                TestTrue(TEXT("Scaled actual field geometry reaches and damages the previously outside Actor"),
                         Damage > 0.f);
            }
            else
            {
                TestEqual(TEXT("Original field geometry cannot damage the boundary-control Actor"), Damage, 0.f);
            }
        }
        else if (bScaled)
        {
            TestTrue(TEXT("Actual Burst/single-pellet damage consumes its own multiplier exactly once"),
                     OriginalDamage > 0.f && FMath::IsNearlyEqual(Damage, OriginalDamage * 2.f, .02f));
        }
        else
        {
            OriginalDamage = Damage;
            TestTrue(TEXT("Actual unmodified damage control applies its original GE to real Health"), Damage > 0.f);
        }
        Run->Cancel();
        F.Developer->EndPreview(Error);
    }
    return true;
}
#endif
