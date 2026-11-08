// 调用链：Automation RunTests test_25d.AbilityTimeline -> 配置解析/事件入口；无伤害和资产写入。
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Timeline/AbilityTimelineDefinition.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TimelineTestHit, "Test.AbilityTimeline.Hit");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TimelineTestTail, "Test.AbilityTimeline.Tail");

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    /** 0.6 秒前摇压缩到 0.3 秒，0.2 秒命中不变，0.4 秒后摇延长到 0.8 秒。 */
    UAbilityTimelineDefinition* MakeTimeline()
    {
        auto* Definition = NewObject<UAbilityTimelineDefinition>();
        for (const auto& Pair : {TPair<float, float>(0.6f, 0.3f), {0.8f, 0.2f}, {1.2f, 0.8f}})
        {
            auto& Phase = Definition->Phases.AddDefaulted_GetRef();
            Phase.Name = FName(*FString::FromInt(Definition->Phases.Num()));
            Phase.SourceEnd = Pair.Key;
            Phase.Duration = Pair.Value;
        }
        auto& Event = Definition->Events.AddDefaulted_GetRef();
        Event.EventTag = TAG_TimelineTestHit;
        Event.SourceStart = 0.6f;
        Event.SourceEnd = 0.8f;
        return Definition;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineMapping, "test_25d.AbilityTimeline.MappingAndSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 验证分段变速不移动源命中位置，且读表得到值快照。 */
bool FTimelineMapping::RunTest(const FString&)
{
    auto* Definition = MakeTimeline();
    auto* Table = NewObject<UDataTable>();
    Table->RowStruct = FAbilityTimelineTimingRow::StaticStruct();
    FAbilityTimelineTimingRow Row;
    Row.Duration = 0.3f;
    Table->AddRow(TEXT("Windup"), Row);
    Definition->Phases[0].Timing.DataTable = Table;
    Definition->Phases[0].Timing.RowName = TEXT("Windup");
    FAbilityTimelineSnapshot Snapshot;
    FString Error;
    if (!TestTrue(TEXT("Resolve valid timeline"), Definition->Resolve(1.2f, Snapshot, Error)))
        return false;
    TestEqual(TEXT("Windup plays at 2x"), Snapshot.RateAt(0.2f), 2.f);
    TestTrue(TEXT("Impact remains at source .6 but happens after .3 seconds"),
             FMath::IsNearlyEqual(Snapshot.ElapsedAt(.6f), .3f));
    TestEqual(TEXT("Boundary belongs to next phase"), Snapshot.PhaseAt(.6f), 1);
    TestTrue(TEXT("Recovery plays at .5x"), FMath::IsNearlyEqual(Snapshot.RateAt(1.f), .5f, KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Full actual duration"), FMath::IsNearlyEqual(Snapshot.ElapsedAt(1.2f), 1.3f));
    Row.Duration = 9;
    Table->AddRow(TEXT("Windup"), Row);
    TestEqual(TEXT("Active cast ignores table edits"), Snapshot.RateAt(.2f), 2.f);
    Definition->Phases[0].Timing.RowName = TEXT("Missing");
    TestFalse(TEXT("Missing timing row rejected before commit"), Definition->Resolve(1.2f, Snapshot, Error));
    Definition->Phases[0].Timing = {};
    Definition->Phases[1].SourceEnd = .5f;
    TestFalse(TEXT("Reverse source segment rejected"), Definition->Resolve(1.2f, Snapshot, Error));
    Definition = MakeTimeline();
    Definition->Phases[0].Duration = 0;
    TestFalse(TEXT("Zero duration rejected"), Definition->Resolve(1.2f, Snapshot, Error));
    Definition = MakeTimeline();
    TestFalse(TEXT("Partial montage coverage rejected"), Definition->Resolve(2.f, Snapshot, Error));
    const FAbilityTimelineEventWindow Duplicate = Definition->Events[0];
    Definition->Events.Add(Duplicate);
    TestFalse(TEXT("Duplicate event identities rejected"), Definition->Resolve(1.2f, Snapshot, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineEventGate, "test_25d.AbilityTimeline.EventIdentityAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 模拟通知重发、旧动画混出、取消及对象池下一次激活，拒绝错误代次。 */
bool FTimelineEventGate::RunTest(const FString&)
{
    auto* Definition = MakeTimeline();
    FAbilityTimelineSnapshot Snapshot;
    FString Error;
    if (!TestTrue(TEXT("Resolve"), Definition->Resolve(1.2f, Snapshot, Error)))
        return false;
    Snapshot.MontageInstanceId = 17;
    const FGuid OldCast = Snapshot.CastId;
    TestFalse(TEXT("Early event rejected"), Snapshot.AcceptEvent(TAG_TimelineTestHit, OldCast, 17, .2f));
    TestFalse(TEXT("Old montage instance rejected"), Snapshot.AcceptEvent(TAG_TimelineTestHit, OldCast, 16, .6f));
    TestFalse(TEXT("Old cast rejected"), Snapshot.AcceptEvent(TAG_TimelineTestHit, FGuid::NewGuid(), 17, .6f));
    TestTrue(TEXT("Current event accepted"), Snapshot.AcceptEvent(TAG_TimelineTestHit, OldCast, 17, .6f));
    TestFalse(TEXT("Duplicate event cannot deal damage twice"),
              Snapshot.AcceptEvent(TAG_TimelineTestHit, OldCast, 17, .6f));
    Snapshot.Close();
    Snapshot.Close();
    TestFalse(TEXT("Cancelled cast rejects notifications"),
              Snapshot.AcceptEvent(TAG_TimelineTestHit, OldCast, 17, .7f));
    Definition->Resolve(1.2f, Snapshot, Error);
    Snapshot.MontageInstanceId = 18;
    TestFalse(TEXT("Reused GA rejects previous activation"),
              Snapshot.AcceptEvent(TAG_TimelineTestHit, OldCast, 18, .6f));
    TestTrue(TEXT("Fresh activation can hit again"),
             Snapshot.AcceptEvent(TAG_TimelineTestHit, Snapshot.CastId, 18, .6f));
    return true;
}
#endif
