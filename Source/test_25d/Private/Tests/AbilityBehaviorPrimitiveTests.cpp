// 调用链：Automation -> 纯行为原语 -> 几何/候选身份/调用者持有时钟；不创建世界、不触发伤害。
// 测试区分候选原生命、旧几何容差与节拍积压策略，防止拆分时统一成一种历史上不存在的行为。
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Unified/LegacyActorAbilityDriver.h"
#include "MyGameplayTags.h"
namespace AbilityBehaviorPrimitiveTests
{
    /** 内存身份只用于比较，测试无需把它当作活目标或创建世界。 */
    FAbilityTargetToken MassLife(int32 Id, int32 Index, int32 Serial)
    {
        FAbilityTargetToken Token;
        Token.Target = FPureMassCombatTarget(Id);
        Token.MassEntity.Index = Index;
        Token.MassEntity.SerialNumber = Serial;
        return Token;
    }

    FAbilityTargetCandidate Candidate(FAbilityTargetToken Token, FVector Position)
    {
        FAbilityTargetCandidate Result;
        Result.Target = Token;
        Result.Location = Position;
        return Result;
    }
} // namespace AbilityBehaviorPrimitiveTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityBehaviorSelectionTest,
                                 "test_25d.UnifiedGA.DynamicMage.Primitives.SelectionPreservesLifeAndOrder",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 相同数字目标的下一实体生命不冒充旧生命；等距和边界选择由明确兼容参数决定。 */
bool FAbilityBehaviorSelectionTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorPrimitiveTests;
    using namespace AbilityBehaviorPrimitives;
    const auto A = MassLife(7, 1, 1);
    const auto Reused = MassLife(7, 1, 2);
    const auto B = MassLife(8, 2, 1);
    TestTrue(TEXT("Copy preserves exact life identity"), SameTargetLife(A, A));
    TestFalse(TEXT("Mass serial changes target life even when public ID is unchanged"), SameTargetLife(A, Reused));
    auto DifferentRun = A;
    DifferentRun.DungeonRunId = FGuid::NewGuid();
    TestFalse(TEXT("Different run cannot reuse old identity"), SameTargetLife(A, DifferentRun));
    auto DifferentLease = A;
    DifferentLease.ActorLease = 2;
    TestFalse(TEXT("Lease is part of the captured identity"), SameTargetLife(A, DifferentLease));

    const TArray<FAbilityTargetCandidate> Candidates = {
        Candidate(A, FVector(100, 0, 0)), Candidate(B, FVector(-100, 0, 0)), Candidate(Reused, FVector(125, 0, 0))};
    auto Selected = SelectNearest(Candidates, FVector::ZeroVector, 150.f, {});
    if (TestTrue(TEXT("A candidate is selected"), Selected.IsSet()))
    {
        TestTrue(TEXT("Stable query keeps first equally near input"), SameTargetLife(Selected->Target, A));
    }
    Selected = SelectNearest(Candidates, FVector::ZeroVector, 150.f, {}, EAbilityCandidateTieBreak::Last);
    if (TestTrue(TEXT("Legacy tie query selects a candidate"), Selected.IsSet()))
    {
        TestTrue(TEXT("Legacy <= query keeps last equally near input"), SameTargetLife(Selected->Target, B));
    }
    Selected = SelectNearest(Candidates, FVector::ZeroVector, 150.f, {A, B});
    if (TestTrue(TEXT("A distinct unvisited life is eligible"), Selected.IsSet()))
    {
        TestTrue(TEXT("Visited old life does not falsely equal another serial"),
                 SameTargetLife(Selected->Target, Reused));
    }
    TestTrue(TEXT("Inclusive search accepts exact radius"),
             SelectNearest({Candidates[0]}, FVector::ZeroVector, 100.f, {}).IsSet());
    TestFalse(TEXT("Strict legacy search rejects exact radius"),
              SelectNearest({Candidates[0]}, FVector::ZeroVector, 100.f, {}, EAbilityCandidateTieBreak::First, false)
                  .IsSet());
    TestFalse(TEXT("Invalid negative search distance fails closed"),
              SelectNearest(Candidates, FVector::ZeroVector, -1.f, {}).IsSet());
    const TArray<FAbilityTargetCandidate> FloatTie = {Candidate(A, FVector(100.0000001, 0, 0)),
                                                      Candidate(B, FVector(100, 0, 0))};
    Selected = SelectNearest(FloatTie, FVector::ZeroVector, 150.f, {});
    if (TestTrue(TEXT("Legacy float distance candidates are selectable"), Selected.IsSet()))
    {
        TestTrue(TEXT("Float-rounded equality retains first input instead of silently sorting in double"),
                 SameTargetLife(Selected->Target, A));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityBehaviorGeometryTest,
                                 "test_25d.UnifiedGA.DynamicMage.Primitives.ShapeBoundaries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 在同一原语中显式保留平面/空间、圆环安全内圈、直线高度和扇形角度的合同。 */
bool FAbilityBehaviorGeometryTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorPrimitives;
    FAbilityShapeFilter Shape;
    Shape.Radius = 100.f;
    TestTrue(TEXT("Circle includes exact outer boundary"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(100, 0, 0)));
    TestFalse(TEXT("3D circle rejects above-radius target"),
              ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(90, 0, 90)));
    Shape.bUse3DDistance = false;
    TestTrue(TEXT("Explicit planar circle ignores height"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(90, 0, 90)));
    Shape.Kind = EAbilityBehaviorShape::Ring;
    Shape.InnerRadius = 40.f;
    Shape.HalfHeight = 10.f;
    Shape.bRejectOutsideHeight = true;
    TestFalse(TEXT("Ring keeps safe inner hole"),
              ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(39, 0, 0)));
    TestTrue(TEXT("Ring includes inner boundary"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(40, 0, 0)));
    TestFalse(TEXT("Ring rejects other floor"),
              ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(50, 0, 11)));
    Shape.Kind = EAbilityBehaviorShape::Line;
    Shape.Range = 200.f;
    Shape.Width = 40.f;
    TestTrue(TEXT("Line includes forward/width/height boundary"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(200, 20, 10)));
    TestFalse(TEXT("Line rejects backward point"),
              ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(-1, 0, 0)));
    TestTrue(TEXT("Line uses supplied forward direction"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::RightVector, FVector(0, 100, 0)));
    Shape.Kind = EAbilityBehaviorShape::Cone;
    Shape.SpreadDegrees = 90.f;
    Shape.bRejectOutsideHeight = false;
    Shape.AngleTolerance = KINDA_SMALL_NUMBER;
    TestTrue(TEXT("Cone includes exact angular boundary"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(50, 50, 0)));
    TestFalse(TEXT("Cone rejects outside angular boundary"),
              ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(0, 50, 0)));
    TestTrue(TEXT("Cone includes its origin"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector::ZeroVector));
    Shape.Radius = -1.f;
    TestFalse(TEXT("Invalid shape fails closed"),
              ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector::ZeroVector));
    Shape = {};
    Shape.Radius = 100.f;
    TestFalse(TEXT("Driver squared-distance profile keeps double position boundary"),
              ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(100.0000001, 0, 0)));
    Shape.DistanceMode = EAbilityShapeDistanceMode::FloatMagnitude;
    TestTrue(TEXT("Query float-magnitude profile retains its original rounded boundary"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::ForwardVector, FVector(100.0000001, 0, 0)));
    Shape.Kind = EAbilityBehaviorShape::Cone;
    Shape.SpreadDegrees = 90.f;
    Shape.bFallbackForward = false;
    TestFalse(TEXT("Query vertical direction keeps its zero planar forward"),
              ContainsPoint(Shape, FVector::ZeroVector, FVector::UpVector, FVector(50, 0, 0)));
    Shape.bFallbackForward = true;
    TestTrue(TEXT("Driver explicitly chooses horizontal forward fallback"),
             ContainsPoint(Shape, FVector::ZeroVector, FVector::UpVector, FVector(50, 0, 0)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityBehaviorCadenceTest,
                                 "test_25d.UnifiedGA.DynamicMage.Primitives.CadenceCompatibilityAndReentry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 长帧积压、单跳重置及取消后的未消费时钟分开验证，禁止一次计算所有次数再盲目执行。 */
bool FAbilityBehaviorCadenceTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorPrimitives;
    float Clock = 0.f;
    int32 Calls = 0;
    auto Pulse = [&]()
    {
        ++Calls;
        return true;
    };
    TestEqual(TEXT("Mixed profile catches up at most eight periods"),
              AdvanceCadence(Clock, 2.5f, .1f, 8, EAbilityCadenceRemainder::Preserve, Pulse), 8);
    TestEqual(TEXT("Mixed profile calls actual body eight times"), Calls, 8);
    // 八次 float 减法保留原实现约 3 ULP 的舍入误差；默认 1e-8 比该精度更严格。
    TestTrue(TEXT("Mixed profile retains unconsumed backlog"), FMath::IsNearlyEqual(Clock, 1.7f, 1.e-6f));
    Clock = 0.f;
    Calls = 0;
    TestEqual(TEXT("Extension profile catches up at most sixteen periods"),
              AdvanceCadence(Clock, 2.5f, .1f, 16, EAbilityCadenceRemainder::ClampToPeriod, Pulse), 16);
    TestTrue(TEXT("Extension profile clamps backlog to one period"), FMath::IsNearlyEqual(Clock, .1f));
    Clock = 0.f;
    Calls = 0;
    TestEqual(TEXT("Sequential propagation accepts only one jump after a hitch"),
              AdvanceCadence(Clock, 2.5f, .1f, 1, EAbilityCadenceRemainder::DiscardAfterPulse, Pulse), 1);
    TestEqual(TEXT("Sequential propagation waits a full fresh interval"), Clock, 0.f);
    Clock = 0.f;
    Calls = 0;
    TestEqual(TEXT("Synchronous stop consumes only current period"),
              AdvanceCadence(Clock, .35f, .1f, 8, EAbilityCadenceRemainder::Preserve,
                             [&]()
                             {
                                 ++Calls;
                                 return false;
                             }),
              1);
    TestEqual(TEXT("Synchronous stop does not call subsequent periods"), Calls, 1);
    TestTrue(TEXT("Synchronous stop does not preconsume future periods"), FMath::IsNearlyEqual(Clock, .25f));
    Clock = .09999f;
    TestEqual(TEXT("Actor tolerance accepts last boundary"),
              AdvanceCadence(Clock, 0.f, .1f, 1, EAbilityCadenceRemainder::Preserve, Pulse, KINDA_SMALL_NUMBER, true),
              1);
    TestEqual(TEXT("Actor subtraction explicitly clamps tolerance remainder"), Clock, 0.f);
    Clock = .2f;
    TestEqual(TEXT("Invalid period never invokes body"),
              AdvanceCadence(Clock, 1.f, 0.f, 8, EAbilityCadenceRemainder::Preserve, Pulse), 0);
    TestEqual(TEXT("Invalid input leaves owned clock untouched"), Clock, .2f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityFinishedEmitterCadenceTest,
                                 "test_25d.UnifiedGA.DynamicMage.Primitives.FinishedEmitterDoesNotEmit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 公共驱动辅助入口在正常结束后不得重新投递，即使外部迟到调用绕过了顶层 Advance。 */
bool FAbilityFinishedEmitterCadenceTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    F.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    auto* Context = NewObject<UPrototypeCastContext>(F.World);
    Context->Caster = F.Source;
    Context->DamageParams.SourceAbilitySystemComponent = F.ASC;
    auto* Config = NewObject<UAbilityPrototype>(F.World);
    Config->Kind = EAbilityPrototypeKind::Emitter;
    Config->Cooldown = 0.f;
    Config->Period = .125f;
    Config->Duration = 10.f;
    Config->Radius = 500.f;
    Config->Count = 3;
    Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
    auto* Child = NewObject<UAbilityPrototype>(F.World);
    Child->Kind = EAbilityPrototypeKind::Burst;
    Child->DamageType = FMyGameplayTags::Get().Damage_Magic;
    Config->OnPeriod = Child;
    int32 Emissions = 0;
    auto* Host =
        APrototypeRuntime::SpawnDriven(Context, Config, FVector::ZeroVector, FVector::ForwardVector, nullptr, 0, false,
                                       [&](const FLegacyAbilityChildRequest& Request)
                                       {
                                           if (Request.Event == TEXT("OnPeriod"))
                                           {
                                               ++Emissions;
                                           }
                                           return true;
                                       });
    if (!TestNotNull(TEXT("Actual emitter host is created"), Host))
    {
        return false;
    }
    const auto Driver = Host->GetDriver();
    Driver->TickEmitter(.125f);
    if (!TestEqual(TEXT("Control: live emitter produces one actual child request"), Emissions, 1))
    {
        return false;
    }
    Driver->Finish(false);
    Driver->TickEmitter(.125f);
    TestTrue(TEXT("Emitter remains finished"), Host->IsFinished());
    TestEqual(TEXT("Late helper call cannot produce another child request"), Emissions, 1);
    return true;
}
#endif
