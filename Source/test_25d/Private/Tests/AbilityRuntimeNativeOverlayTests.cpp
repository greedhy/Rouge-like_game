// 测试链：真实FireBolt目录/BeginPreview -> 可信Set参数 -> 生产解析快照/原一次装备准备 -> 实际BP弹体。
// 此处不声称真实输入或网络；原前端已有独立病例，这批隔离原生准备帧与typed修改的实际消费。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityNativeProjectileParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDistributionNodes.h"
#include "Actor/MyProjectile.h"
#include "Components/SphereComponent.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

namespace AbilityRuntimeNativeOverlayTests
{
    TArray<AMyProjectile*> Shots(UWorld* World)
    {
        TArray<AMyProjectile*> Result;
        for (TActorIterator<AMyProjectile> It(World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed())
            {
                Result.Add(*It);
            }
        }
        return Result;
    }
    /** 原装备动作只作用一次；绝不手工将结果写回原技能或共享配置。 */
    void Gear(FAbilityActualMageFrontendFixture& F, FGameplayTag Tag)
    {
        auto* Effect = NewObject<UUniqueEquipmentGameplayEffect>(F.World);
        Effect->ProjectileRule.AbilityTag = Tag;
        Effect->ProjectileRule.ExtraProjectiles = FScalableFloat(1.f);
        Effect->ProjectileRule.DamageMultiplier = FScalableFloat(.5f);
        FGameplayEffectSpec Spec(Effect, F.PlayerASC->MakeEffectContext(), 1.f);
        F.PlayerASC->ApplyGameplayEffectSpecToSelf(Spec);
    }
    struct FOriginalConstruction
    {
        UClass* Class = nullptr;
        float InitialSpeed = 0.f;
        float MaximumSpeed = 0.f;
        float Radius = 0.f;
        float LifeSpan = 0.f;
        float Damage = 0.f;
    };
    /** 原Notify使用的共享发射动作/真实BP构造作为对照；不从CDO或通用默认值猜测构造后参数。 */
    FOriginalConstruction OriginalConstruction(FAbilityActualMageFrontendFixture& F,
                                               const UAbilityNativeProjectilePrepareConfig& Prepare,
                                               int32 ExpectedCount, FAutomationTestBase& Test)
    {
        FOriginalConstruction Result;
        FAbilitySourceToken Source;
        FString Error;
        if (!AbilityExecutionIdentity::CaptureSource(F.Pawn, Source, Error))
        {
            Test.AddError(Error);
            return Result;
        }
        FDamageEffectParams Damage;
        Damage.SourceAbilitySystemComponent = F.PlayerASC;
        Damage.BaseDamage = 25.f;
        auto Call = Prepare.Settings;
        Call.bOverridePitch = true;
        Call.PitchOverride = 25.f;
        F.World->SetBegunPlay(true);
        const auto Original = AbilityTraditionalActions::SpawnProjectiles(
            Source, Damage, Call, 1.f, FVector::ZeroVector, FVector(5000, 0, 0), nullptr);
        Test.TestEqual(TEXT("True original native shared Notify action produces its own expected physical count"),
                       Original.Num(), ExpectedCount);
        if (!Original.IsEmpty())
        {
            const auto* First = Original[0];
            Result = {First->GetClass(),
                      First->ProjectileMovement->InitialSpeed,
                      First->ProjectileMovement->MaxSpeed,
                      First->Sphere->GetScaledSphereRadius(),
                      First->GetLifeSpan(),
                      First->DamageEffectParams.BaseDamage};
            Test.AddInfo(FString::Printf(TEXT("OriginalNativeBP=%s Initial=%g Maximum=%g Radius=%g Life=%g"),
                                         *GetPathNameSafe(Result.Class), Result.InitialSpeed, Result.MaximumSpeed,
                                         Result.Radius, Result.LifeSpan));
        }
        for (auto* Shot : Original)
        {
            Shot->Destroy();
        }
        return Result;
    }
    bool Set(FAbilityActualMageFrontendFixture& F, const FAbilityRuntimeDeveloperDefinition& Row, int32 Index,
             const TCHAR* Id, float Value, FString& Error)
    {
        FAbilityRuntimeDeveloperEdit Request;
        Request.DefinitionId = Row.DefinitionId;
        Request.NodeIndex = Index;
        Request.ModifierId = Id;
        Request.Value = Value;
        Request.ExpectedBaseHash = Row.BaseHash;
        Request.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
        return F.Developer->ApplyEdit(Request, Error);
    }
    /** 只把实际已接受配方交给原执行器；保留Notify方向作为明确测试输入，不称为真实Notify证据。 */
    bool Launch(FAbilityActualMageFrontendFixture& F, const FAbilityRuntimeDeveloperDefinition& Row,
                UAbilityGraphInstance& Run, FString& Error)
    {
        FAbilityRuntimeRecipeSnapshot Snapshot;
        if (!F.Developer->CreateCastSnapshot(Row.Base.AbilityTag, F.World, Snapshot, Error))
        {
            return false;
        }
        FAbilityNodeInput Input;
        if (!AbilityExecutionIdentity::CaptureSource(F.Pawn, Input.SourceToken, Error))
        {
            return false;
        }
        Input.Direction = FRotator(25.f, 0.f, 0.f).Vector();
        Input.EventCenter = FVector(5000, 0, 0);
        Input.Level = 1.f;
        Input.DamageParams.SourceAbilitySystemComponent = F.PlayerASC;
        Input.DamageParams.BaseDamage = 25.f;
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Snapshot.Graph->CompatibilityProfile;
        Options.BudgetPolicy = Snapshot.Graph->BudgetPolicy;
        Options.OwnerLease = F.Developer->GetPreviewOwnerLease(F.Developer->GetPreviewHandle());
        F.World->SetBegunPlay(true);
        return Run.StartWithInput(Snapshot.Graph, Input, Options, Error);
    }
} // namespace AbilityRuntimeNativeOverlayTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeNativePreparedCountSpread,
    "test_25d.DynamicMage.Task9.NativeOverlay.PreparedCountSpreadKeepOriginalBPAndEquipmentOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeNativePreparedCountSpread::RunTest(const FString&)
{
    using namespace AbilityRuntimeNativeOverlayTests;
    for (const bool Edited : {false, true})
    {
        FAbilityActualMageFrontendFixture F;
        ON_SCOPE_EXIT
        {
            F.World->SetBegunPlay(false);
        };
        F.Target->Destroy();
        FString Error;
        const auto* Row = F.Definition(
            TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"), Error);
        if (!TestNotNull(TEXT("Actual FireBolt catalog row exists"), Row) ||
            !TestTrue(TEXT("Actual preview begins without RegisterBase"),
                      F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            return false;
        }
        const int32 Scatter = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Distribution.Scatter");
            });
        const auto* PrepareNode = Row->Base.DefaultGraph->Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Prepare.NativeFireBolt");
            });
        const auto* Prepare = PrepareNode ? Cast<UAbilityNativeProjectilePrepareConfig>(PrepareNode->Config) : nullptr;
        if (!TestTrue(TEXT("Actual native default exposes independent Scatter"), Scatter != INDEX_NONE) ||
            !TestNotNull(TEXT("Actual default retains original native launch settings"), Prepare))
        {
            return false;
        }
        Gear(F, Row->Base.AbilityTag);
        const auto Original = OriginalConstruction(F, *Prepare, 2, *this);
        if (!TestNotNull(TEXT("True original native BP control actually constructed"), Original.Class))
        {
            return false;
        }
        if (Edited && (!TestTrue(TEXT("Actual native frame accepts final prepared Count override"),
                                 Set(F, *Row, Scatter, TEXT("Set.Count"), 3.f, Error)) ||
                       !TestTrue(TEXT("Actual native frame accepts orthogonal Spread override"),
                                 Set(F, *Row, Scatter, TEXT("Set.Spread"), 0.f, Error))))
        {
            AddInfo(Error);
            return false;
        }
        const TStrongObjectPtr<UAbilityGraphInstance> Run(NewObject<UAbilityGraphInstance>(F.World));
        if (!TestTrue(TEXT("Actual parsed native recipe launches real original BP class"),
                      Launch(F, *Row, *Run, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const auto Physical = Shots(F.World);
        TestEqual(Edited ? TEXT("Explicit final Count is three, without applying the same gear extra again")
                         : TEXT("Original min(level) then one equipment extra still produces two real bolts"),
                  Physical.Num(), Edited ? 3 : 2);
        for (const auto* Shot : Physical)
        {
            TestTrue(TEXT("Unselected original BP class is preserved"), Shot->GetClass() == Original.Class);
            TestEqual(TEXT("Unselected InitialSpeed matches the actually constructed original BP"),
                      Shot->ProjectileMovement->InitialSpeed, Original.InitialSpeed);
            TestEqual(TEXT("Unselected MaxSpeed matches its independent original BP value"),
                      Shot->ProjectileMovement->MaxSpeed, Original.MaximumSpeed);
            TestEqual(TEXT("Unselected radius matches actual original world geometry"),
                      Shot->Sphere->GetScaledSphereRadius(), Original.Radius);
            TestEqual(TEXT("Equipment damage multiplier is consumed exactly once"), Shot->DamageEffectParams.BaseDamage,
                      12.5f);
            TestTrue(TEXT("Prepared direction retains original Notify pitch"),
                     FMath::IsNearlyEqual(Shot->GetActorForwardVector().Rotation().Pitch, 25.f, .01f));
        }
        if (Edited && Physical.Num() > 1)
        {
            TestTrue(TEXT("Zero spread really aligns all native launches"),
                     Physical[0]->GetActorForwardVector().Equals(Physical.Last()->GetActorForwardVector(), .0001f));
        }
        Run->Cancel();
        F.Developer->EndPreview(Error);
        F.World->SetBegunPlay(false);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeNativeWorldRadius,
    "test_25d.DynamicMage.Task9.NativeOverlay.WorldRadiusDoesNotOverwriteUnselectedConstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeNativeWorldRadius::RunTest(const FString&)
{
    using namespace AbilityRuntimeNativeOverlayTests;
    for (const bool Edited : {false, true})
    {
        FAbilityActualMageFrontendFixture F;
        ON_SCOPE_EXIT
        {
            F.World->SetBegunPlay(false);
        };
        F.Target->Destroy();
        FString Error;
        const auto* Row = F.Definition(
            TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"), Error);
        if (!TestNotNull(TEXT("Exact original FireBolt directory row exists"), Row) ||
            !TestTrue(TEXT("Actual preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            return false;
        }
        const int32 Flight = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Delivery.ProjectileAwait");
            });
        if (!TestTrue(TEXT("Native delivery axis exists"), Flight != INDEX_NONE))
        {
            return false;
        }
        const auto* PrepareNode = Row->Base.DefaultGraph->Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Prepare.NativeFireBolt");
            });
        const auto* Prepare = PrepareNode ? Cast<UAbilityNativeProjectilePrepareConfig>(PrepareNode->Config) : nullptr;
        if (!TestNotNull(TEXT("Actual default retains original native launch settings"), Prepare))
        {
            return false;
        }
        const auto Original = OriginalConstruction(F, *Prepare, 1, *this);
        if (!TestNotNull(TEXT("True original native BP control actually constructed"), Original.Class))
        {
            return false;
        }
        if (Edited && !TestTrue(TEXT("Actual trusted CollisionRadius edit is accepted on native prepared delivery"),
                                Set(F, *Row, Flight, TEXT("Set.CollisionRadius"), 64.f, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const TStrongObjectPtr<UAbilityGraphInstance> Run(NewObject<UAbilityGraphInstance>(F.World));
        if (!TestTrue(TEXT("Actual parsed snapshot spawns the native BP"), Launch(F, *Row, *Run, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const auto Physical = Shots(F.World);
        if (!TestEqual(TEXT("Original level1 cast still creates one physical projectile"), Physical.Num(), 1))
        {
            return false;
        }
        const auto* Shot = Physical[0];
        TestEqual(TEXT("Only selected collision radius changes in world centimetres"),
                  Shot->Sphere->GetScaledSphereRadius(), Edited ? 64.f : Original.Radius);
        TestEqual(TEXT("InitialSpeed remains the actually constructed original BP value"),
                  Shot->ProjectileMovement->InitialSpeed, Original.InitialSpeed);
        TestEqual(TEXT("MaxSpeed remains the actually constructed independent original value"),
                  Shot->ProjectileMovement->MaxSpeed, Original.MaximumSpeed);
        TestTrue(TEXT("Actual BP original configured lifetime remains when duration is unselected"),
                 FMath::IsNearlyEqual(Shot->GetLifeSpan(), Shot->GetConfiguredGraphFlightLifeSpan(), .01f));
        Run->Cancel();
        F.Developer->EndPreview(Error);
        F.World->SetBegunPlay(false);
    }
    return true;
}
#endif
