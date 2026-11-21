// 测试调用链：纯motion/contact合同 -> 分段/倍率观察；不使用伪造命中替代投递行为测试。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Unified/AbilityProjectileMotion.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectileMotionSegmentTest, "test_25d.DynamicMage.Motion.SegmentsAndHoming",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectileMotionSegmentTest::RunTest(const FString&)
{
    FAbilityProjectileMotionConfig Config;
    Config.Speed = 100.f;
    Config.Range = 50.f;
    Config.CollisionRadius = 2.f;
    FAbilityProjectileMotionState State;
    State.Position = FVector(40, 0, 0);
    State.OutboundDistance = 40.f;
    FAbilityFlightSegment Segment;
    TestTrue(TEXT("Finite outbound segment is computed"),
             AbilityProjectileMotion::ComputeFlightSegment(Config, State, .3f, FVector::ZeroVector, {}, Segment));
    TestTrue(TEXT("Segment clips exactly to remaining outbound range"), Segment.End.Equals(FVector(50, 0, 0)));
    TestTrue(TEXT("Outbound endpoint is explicit"), Segment.bReachesPhaseEnd);
    TestTrue(TEXT("Only actual travel time is consumed"), FMath::IsNearlyEqual(Segment.SecondsUsed, .1f));
    AddInfo(FString::Printf(TEXT("Motion timing: used=%.12f remaining=%.12f expected=%.12f error=%.12g"),
                            static_cast<double>(Segment.SecondsUsed), static_cast<double>(Segment.RemainingSeconds),
                            static_cast<double>(.2f), static_cast<double>(FMath::Abs(Segment.RemainingSeconds - .2f))));
    // UE运行诊断确认差值为1 ULP（1.49011611938e-08）；两次float时间运算采用显式精度容差。
    TestTrue(TEXT("Remainder survives a phase transition"),
             FMath::IsNearlyEqual(Segment.RemainingSeconds, .2f, 1.e-7f));
    Config.Motion = EAbilityProjectileMotion::Returning;
    State.Phase = EAbilityProjectilePhase::Returning;
    State.Position = FVector(50, 0, 0);
    TestTrue(TEXT("Return heads to current source position"),
             AbilityProjectileMotion::ComputeFlightSegment(Config, State, .2f, FVector(10, 0, 0), {}, Segment));
    TestTrue(TEXT("Return distance uses the live source point"), Segment.End.Equals(FVector(30, 0, 0)));
    Config.Motion = EAbilityProjectileMotion::LegacyTurnHoming;
    Config.HomingTurnRate = 90.f;
    State.Phase = EAbilityProjectilePhase::Outbound;
    State.Position = FVector::ZeroVector;
    State.OutboundDistance = 0.f;
    State.Direction = FVector::ForwardVector;
    const FVector Target(0, 100, 0);
    TestTrue(TEXT("Turn-limited homing computes a segment"),
             AbilityProjectileMotion::ComputeFlightSegment(Config, State, .1f, FVector::ZeroVector, Target, Segment));
    const FVector Expected = FMath::VInterpNormalRotationTo(FVector::ForwardVector, FVector::RightVector, .1f, 90.f);
    TestTrue(TEXT("Legacy turn model uses original angular interpolation"), Segment.Direction.Equals(Expected));
    TestFalse(TEXT("Native acceleration stays in original movement component rather than reimplementing it"),
              [&]()
              {
                  Config.Motion = EAbilityProjectileMotion::NativeHoming;
                  return AbilityProjectileMotion::ComputeFlightSegment(Config, State, .1f, FVector::ZeroVector, Target,
                                                                       Segment);
              }());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectileTinyReturnSegment,
                                 "test_25d.DynamicMage.Motion.TinyTurnRemainderDoesNotReachSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectileTinyReturnSegment::RunTest(const FString&)
{
    FAbilityProjectileMotionConfig Config;
    Config.Motion = EAbilityProjectileMotion::Returning;
    Config.Speed = Config.Range = 400.f;
    Config.CollisionRadius = 12.f;
    FAbilityProjectileMotionState State;
    FAbilityFlightSegment Segment;
    const FVector Source(-100, 0, 0);
    TestTrue(TEXT("Actual first .6f outbound phase is computed"),
             AbilityProjectileMotion::ComputeFlightSegment(Config, State, .6f, Source, {}, Segment));
    State.OutboundDistance += static_cast<float>(FVector::Distance(Segment.Start, Segment.End));
    State.Position = Segment.End;
    TestTrue(TEXT("Actual .4f turn frame is computed"),
             AbilityProjectileMotion::ComputeFlightSegment(Config, State, .4f, Source, {}, Segment));
    const float Remainder = Segment.RemainingSeconds;
    TestTrue(TEXT("Floating-point turn remainder still exceeds the caller's zero-time guard"),
             Remainder > SMALL_NUMBER);
    State.Position = Segment.End;
    State.Phase = EAbilityProjectilePhase::Returning;
    TestTrue(TEXT("Tiny actual return segment is computed"),
             AbilityProjectileMotion::ComputeFlightSegment(Config, State, Remainder, Source, {}, Segment));
    TestTrue(TEXT("Tiny physical motion can compare equal under FVector's default coordinate tolerance"),
             Segment.Start.Equals(Segment.End));
    TestFalse(TEXT("Coordinate equality cannot assert reaching a caster still500cm away"), Segment.bReachesPhaseEnd);
    TestTrue(TEXT("Actual return distance remains far outside collision radius"),
             FVector::Distance(Segment.End, Source) > Config.CollisionRadius);
    State.Position = Source + FVector(1, 0, 0);
    TestTrue(TEXT("Actual already-at-source case is computed"),
             AbilityProjectileMotion::ComputeFlightSegment(Config, State, Remainder, Source, {}, Segment));
    TestTrue(TEXT("True at-source result explicitly identifies the completed phase"), Segment.bReachesPhaseEnd);
    TestTrue(TEXT("True at-source result intentionally has no motion"), Segment.Start.Equals(Segment.End));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectileImpactAdmissionTest, "test_25d.DynamicMage.Motion.ImpactPolicy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectileImpactAdmissionTest::RunTest(const FString&)
{
    const auto First = AbilityProjectileMotion::AdmitImpact(EAbilityProjectileImpactPolicy::Pierce,
                                                            EAbilityProjectilePhase::Outbound, 0, false, 3, .5f);
    TestTrue(TEXT("Piercing accepts a new first contact without terminating"),
             First.bAccepted && !First.bTerminateAfterImpact);
    TestEqual(TEXT("First hit keeps full payload"), First.DamageScale, 1.f);
    const auto Last = AbilityProjectileMotion::AdmitImpact(EAbilityProjectileImpactPolicy::Pierce,
                                                           EAbilityProjectilePhase::Outbound, 2, false, 3, .5f);
    TestTrue(TEXT("Piercing limit terminates after actual final contact"),
             Last.bAccepted && Last.bTerminateAfterImpact);
    TestEqual(TEXT("Piercing scales by unique ordinal"), Last.DamageScale, .25f);
    const auto Repeated = AbilityProjectileMotion::AdmitImpact(EAbilityProjectileImpactPolicy::Pierce,
                                                               EAbilityProjectilePhase::Outbound, 1, true, 3, .5f);
    TestFalse(TEXT("Repeated contact does not consume another hit"), Repeated.bAccepted);
    const auto Return = AbilityProjectileMotion::AdmitImpact(EAbilityProjectileImpactPolicy::ReturnPerPhase,
                                                             EAbilityProjectilePhase::Returning, 1, false, 2, .5f);
    TestTrue(TEXT("Return accepts per-phase contact without stopping return flight"),
             Return.bAccepted && !Return.bTerminateAfterImpact);
    TestEqual(TEXT("Return multiplier is per phase, not exponential per target"), Return.DamageScale, .5f);
    TestFalse(TEXT("Return phase count rejects additional contact without ending motion"),
              AbilityProjectileMotion::AdmitImpact(EAbilityProjectileImpactPolicy::ReturnPerPhase,
                                                   EAbilityProjectilePhase::Returning, 2, false, 2, .5f)
                  .bAccepted);
    return true;
}
#endif
