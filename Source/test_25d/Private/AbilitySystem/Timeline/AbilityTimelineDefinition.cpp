// 调用链：GA/任务解析配置 -> 数值映射与事件校验；无世界、组件或网络副作用。
#include "AbilitySystem/Timeline/AbilityTimelineDefinition.h"
#include "Animation/AnimMontage.h"

// 暴露只读预检给蓝图，失败时调用者不应扣除本次技能资源。
bool UAbilityTimelineDefinition::ValidateForMontage(UAnimMontage* Montage, FString& Error,
                                                     bool bAllowEndBlendOut) const
{
    FAbilityTimelineSnapshot Snapshot;
    return ResolveMontage(Montage, Snapshot, Error, bAllowEndBlendOut);
}

// 半开区间避免阶段边界仍沿用前摇倍率；终点用于完成时的最终查询。
int32 FAbilityTimelineSnapshot::PhaseAt(float Position) const
{
    if (!FMath::IsFinite(Position) || Position < 0 || Phases.IsEmpty() ||
        Position > Phases.Last().SourceEnd + KINDA_SMALL_NUMBER)
    {
        return INDEX_NONE;
    }
    for (int32 Index = 0; Index < Phases.Num(); ++Index)
    {
        if (Position < Phases[Index].SourceEnd)
        {
            return Index;
        }
    }
    return Phases.Num() - 1;
}

// 按已解析阶段求和；不会依赖世界时间、当前帧率或共享数据表。
float FAbilityTimelineSnapshot::ElapsedAt(float Position) const
{
    if (PhaseAt(Position) == INDEX_NONE)
    {
        return 0;
    }
    float Start = 0;
    float Elapsed = 0;
    for (const auto& Phase : Phases)
    {
        Elapsed += Phase.Duration * FMath::Clamp((Position - Start) / (Phase.SourceEnd - Start), 0.f, 1.f);
        Start = Phase.SourceEnd;
    }
    return Elapsed;
}

// 源动画区间越长、目标秒数越短，播放倍率越高。
float FAbilityTimelineSnapshot::RateAt(float Position) const
{
    const int32 Index = PhaseAt(Position);
    if (Index == INDEX_NONE)
    {
        return 0;
    }
    const float Start = Index == 0 ? 0 : Phases[Index - 1].SourceEnd;
    return (Phases[Index].SourceEnd - Start) / Phases[Index].Duration;
}

// 先验证来源再消费 ID，错误或过早事件不能耗掉真正命中的机会。
bool FAbilityTimelineSnapshot::AcceptEvent(FGameplayTag Tag, const FGuid& InCastId, int32 InstanceId,
                                           float SourcePosition)
{
    if (bClosed || !CastId.IsValid() || InCastId != CastId || InstanceId == INDEX_NONE ||
        InstanceId != MontageInstanceId || !FMath::IsFinite(SourcePosition) || ConsumedEvents.Contains(Tag))
    {
        return false;
    }
    const auto* Window = Events.FindByPredicate(
        [Tag](const auto& Event)
        {
            return Event.EventTag == Tag;
        });
    if (!Window || SourcePosition < Window->SourceStart || SourcePosition > Window->SourceEnd)
    {
        return false;
    }
    ConsumedEvents.Add(Tag);
    return true;
}

// 先关闭入口，释放阶段资源由所属任务/GA执行；重复结束不会重放事件。
void FAbilityTimelineSnapshot::Close()
{
    bClosed = true;
}

// 失败清空输出，避免调用者误用上一次成功解析的施法状态。
bool UAbilityTimelineDefinition::Resolve(float MontageLength, FAbilityTimelineSnapshot& Out, FString& Error) const
{
    return ResolvePhases(MontageLength, Phases, Out, Error);
}

// Resolve the selected montage's phase list without copying or mutating the DA.
bool UAbilityTimelineDefinition::ResolvePhases(float MontageLength, const TArray<FAbilityTimelinePhase>& SourcePhases,
                                               FAbilityTimelineSnapshot& Out, FString& Error) const
{
    Out = {};
    Error.Reset();
    FAbilityTimelineSnapshot Resolved;
    auto Fail = [&Error](const TCHAR* Message)
    {
        Error = Message;
        return false;
    };
    if (!FMath::IsFinite(MontageLength) || MontageLength <= 0 || SourcePhases.IsEmpty())
    {
        return Fail(TEXT("Timeline requires a positive montage length and phases"));
    }
    float Start = 0;
    TSet<FName> Names;
    for (auto Phase : SourcePhases)
    {
        if (Phase.Name.IsNone() || Names.Contains(Phase.Name) || !FMath::IsFinite(Phase.SourceEnd) ||
            Phase.SourceEnd <= Start)
        {
            return Fail(TEXT("Phase names must be unique and source endpoints strictly increasing"));
        }
        if (Phase.Timing.DataTable || !Phase.Timing.RowName.IsNone())
        {
            const auto* Table = Phase.Timing.DataTable.Get();
            if (!Table || Table->GetRowStruct() != FAbilityTimelineTimingRow::StaticStruct() ||
                Phase.Timing.RowName.IsNone())
            {
                return Fail(TEXT("Timing row must reference an AbilityTimelineTimingRow table"));
            }
            const auto* Row =
                Table->FindRow<FAbilityTimelineTimingRow>(Phase.Timing.RowName, TEXT("AbilityTimeline"), false);
            if (!Row)
            {
                return Fail(TEXT("Missing timeline duration row"));
            }
            Phase.Duration = Row->Duration;
        }
        const float Rate = (Phase.SourceEnd - Start) / Phase.Duration;
        if (!FMath::IsFinite(Phase.Duration) || Phase.Duration <= 0 || !FMath::IsFinite(Rate) || Rate < .05f ||
            Rate > 10.f)
        {
            return Fail(TEXT("Phase duration must be finite and positive, with playback rate in [0.05, 10]"));
        }
        // 解析完成后切断共享表引用，避免热改数据在一次施法中途改变规则。
        Phase.Timing = {};
        Resolved.Phases.Add(Phase);
        Names.Add(Phase.Name);
        Start = Phase.SourceEnd;
    }
    if (!FMath::IsNearlyEqual(Start, MontageLength, KINDA_SMALL_NUMBER))
    {
        return Fail(TEXT("Timeline must cover the complete montage"));
    }
    TSet<FGameplayTag> EventTags;
    for (const auto& Event : Events)
    {
        if (!Event.EventTag.IsValid() || EventTags.Contains(Event.EventTag) || !FMath::IsFinite(Event.SourceStart) ||
            !FMath::IsFinite(Event.SourceEnd) || Event.SourceStart < 0 || Event.SourceEnd < Event.SourceStart ||
            Event.SourceEnd > MontageLength)
        {
            return Fail(TEXT("Events require unique tags and valid source windows inside the montage"));
        }
        EventTags.Add(Event.EventTag);
    }
    Resolved.Events = Events;
    Resolved.CastId = FGuid::NewGuid();
    Out = MoveTemp(Resolved);
    return true;
}

// 默认自然混出在最终恒定倍率阶段进行，避免已退出ActiveMontagesMap后ASC无法继续同步变速。
bool UAbilityTimelineDefinition::ResolveMontage(const UAnimMontage* Montage, FAbilityTimelineSnapshot& Out,
                                                FString& Error, bool bAllowEndBlendOut) const
{
    Out = {};
    if (!Montage || Montage->HasRootMotion() || !FMath::IsNearlyEqual(Montage->RateScale, 1.f) ||
        Montage->CompositeSections.Num() > 1 || !Montage->bEnableAutoBlendOut || (Montage->BlendOutTriggerTime >= 0 &&
         !(bAllowEndBlendOut && Montage->BlendOutTriggerTime == 0.f)) ||
        (Montage->CompositeSections.Num() == 1 && !Montage->CompositeSections[0].NextSectionName.IsNone()))
    {
        Error = TEXT(
            "Timeline requires a non-looping, non-root-motion montage with default automatic blend-out and RateScale 1");
        return false;
    }
    const auto* Variant = MontageVariants.Find(const_cast<UAnimMontage*>(Montage));
    if (!MontageVariants.IsEmpty() && !Variant)
    {
        Error = TEXT("Timeline has no phases for this montage");
        return false;
    }
    const auto& SourcePhases = Variant ? Variant->Phases : Phases;
    if (!ResolvePhases(Montage->GetPlayLength(), SourcePhases, Out, Error))
    {
        return false;
    }
    float Start = 0;
    const float BlendOutTime = Montage->BlendOut.GetBlendTime();
    for (int32 Index = 0; Index + 1 < Out.Phases.Num(); ++Index)
    {
        const auto& Phase = Out.Phases[Index];
        const float Rate = (Phase.SourceEnd - Start) / Phase.Duration;
        if ((Montage->GetPlayLength() - Phase.SourceEnd) / Rate <= BlendOutTime + KINDA_SMALL_NUMBER)
        {
            Out = {};
            Error = TEXT(
                "Automatic blend-out could begin before the final phase; shorten montage blend-out or move the last phase boundary earlier");
            return false;
        }
        Start = Phase.SourceEnd;
    }
    return true;
}
