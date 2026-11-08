/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 管理链：DungeonRunDirector维护Actor池与PendingPoolReturns -> 死亡进入等待队列 -> DrainPoolReturns准备休眠/取消能力 -> CanFinishPoolReturn -> Retire并SleepForDungeonPool -> 后续借出复用。
 * 本文件中的池记录是Director持有的普通C++状态，不是独立UObject子系统或全局单例；Actor寿命仍由UWorld管理。非池管理Actor在ReturnEnemyToPool直接Destroy。
 * 死亡与回收分开：先清一次生命的逻辑状态，再等待尸体阶段结束后转为可复用；移动队列后再处理，是为了防止取消技能或广播回调重入当前队列。
 * 池账本不复制；网络可见的怪物表现由EnemyCharacter自身复制/表现通路负责。清理世界时应走Director/Actor生命周期，不能仅清空池记录而假定Actor已销毁。
 */
// 主要调用链：借出或接纳 → 死亡等待 → 归还休眠 → 后续房间复用 → 世界退出销毁。
#include "Roguelike/DungeonEnemyPool.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonRunProfile.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// 非池对象直接销毁；池化对象登记闲置后休眠并累计归还耗时。
void ADungeonRunDirector::ReturnEnemyToPool(AEnemyCharacter* Enemy)
{
    if (!IsValid(Enemy))
        return;
    LastSafeLocations.Remove(Enemy);
    if (!Enemy->ActorHasTag(TEXT("DungeonPoolManaged")))
    {
        Enemy->Destroy();
        return;
    }
    if (!EnemyPool.Retire(Enemy))
        return;
    TRACE_CPUPROFILER_EVENT_SCOPE(DungeonEnemyPool_Return);
    const double Started = FPlatformTime::Seconds();
    Enemy->SleepForDungeonPool();
    ReturnTiming.Add((FPlatformTime::Seconds() - Started) * 1000.0);
}

// 先转移队列避免重入，再准备休眠并等待尸体阶段完成后正式归还。
void ADungeonRunDirector::DrainPoolReturns()
{
    // Move first: ability cancellation/delegates can re-enter gameplay code.
    auto Returns = MoveTemp(PendingPoolReturns);
    PendingPoolReturns.Reset();
    for (auto Weak : Returns)
        if (auto* Enemy = Weak.Get())
        {
            if (!Enemy->HasPreparedPoolReturn())
            {
                const double Started = FPlatformTime::Seconds();
                Enemy->SleepForDungeonPool(true);
                ResetTiming.Add((FPlatformTime::Seconds() - Started) * 1000.0);
            }
            if (Enemy->CanFinishPoolReturn())
                ReturnEnemyToPool(Enemy);
            else
                PendingPoolReturns.AddUnique(Enemy);
        }
    EnemyPool.Prune();
}

// 按间隔采样进程内存、帧耗时和池占用，保留有限长度快照。
void ADungeonRunDirector::SamplePerformance(float DeltaSeconds)
{
    if (!Profile || !Profile->bSamplePerformance)
        return;
    TRACE_CPUPROFILER_EVENT_SCOPE(DungeonPerformance_Sample);
    const double FrameMs = FMath::Max(0.f, DeltaSeconds) * 1000.0;
    ++FrameCount;
    FrameTotalMs += FrameMs;
    MaxFrameMs = FMath::Max(MaxFrameMs, FrameMs);
    const double Now = FPlatformTime::Seconds();
    if (Now < NextPerformanceSample)
        return;
    NextPerformanceSample = Now + FMath::Max(.1f, Profile->PerformanceSampleInterval);
    const auto Memory = FPlatformMemory::GetStats();
    PeakObservedProcessBytes = FMath::Max(PeakObservedProcessBytes, uint64(Memory.UsedPhysical));
    auto Sample = MakeShared<FJsonObject>();
    Sample->SetNumberField(TEXT("sample_index"), PerformanceSampleCount++);
    Sample->SetNumberField(TEXT("world_seconds"), GetWorld()->GetTimeSeconds());
    Sample->SetStringField(TEXT("phase"), StaticEnum<EDungeonRunPhase>()->GetNameStringByValue(int64(Phase)));
    Sample->SetNumberField(TEXT("process_physical_bytes"), double(Memory.UsedPhysical));
    Sample->SetNumberField(TEXT("process_virtual_bytes"), double(Memory.UsedVirtual));
    Sample->SetNumberField(TEXT("interval_max_frame_ms"), MaxFrameMs);
    Sample->SetNumberField(TEXT("interval_mean_frame_ms"), FrameCount ? FrameTotalMs / FrameCount : 0);
    Sample->SetNumberField(TEXT("active_enemy_count"), ActorTargets.Num());
    Sample->SetNumberField(TEXT("pending_enemy_spawns"), SpawnQueue.Num());
    Sample->SetNumberField(TEXT("pool_total"), EnemyPool.Entries.Num());
    Sample->SetNumberField(TEXT("pool_idle"), EnemyPool.IdleCount());
    Sample->SetNumberField(TEXT("pool_hits"), PoolHits);
    Sample->SetNumberField(TEXT("room_count"), Generation ? Generation->Manifest.Layout.Rooms.Num() : 0);
    if (PerformanceSamples.Num() >= 600)
        PerformanceSamples.RemoveAt(0);
    PerformanceSamples.Add(MakeShared<FJsonValueObject>(Sample));
    FrameCount = 0;
    FrameTotalMs = 0;
    MaxFrameMs = 0;
}

// 汇总池分类占用、命中统计和创建复用归还耗时。
TSharedPtr<FJsonObject> ADungeonRunDirector::MakePerformanceReport() const
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("schema_version"), 1);
    Result->SetStringField(
        TEXT("memory_scope"),
        TEXT("whole process; includes editor/all PIE worlds; not per-room allocation or GPU memory"));
    Result->SetStringField(TEXT("frame_scope"), TEXT("World tick DeltaSeconds, not exclusive GameThread CPU duration"));
    Result->SetNumberField(TEXT("pool_capacity"), Profile ? FMath::Clamp(Profile->NormalPoolCapacity, 0, 64) : 0);
    Result->SetNumberField(TEXT("pool_total"), EnemyPool.Entries.Num());
    Result->SetNumberField(TEXT("pool_idle"), EnemyPool.IdleCount());
    Result->SetNumberField(TEXT("pool_peak"), PoolPeak);
    Result->SetNumberField(TEXT("pool_hits"), PoolHits);
    Result->SetNumberField(TEXT("pool_misses"), PoolMisses);
    Result->SetNumberField(TEXT("pool_overflow"), PoolOverflow);
    TArray<TSharedPtr<FJsonValue>> ByType;
    if (Profile)
        for (const auto& Entry : Profile->NormalRoster)
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("id"), Entry.Id.ToString());
            Row->SetStringField(TEXT("class"), Entry.EnemyClass.ToSoftObjectPath().ToString());
            Row->SetNumberField(TEXT("quota"), Profile->NormalPoolQuota(Entry.EnemyClass.Get()));
            Row->SetNumberField(TEXT("total"), EnemyPool.CountClass(Entry.EnemyClass.Get()));
            int32 Idle = 0;
            for (const auto& P : EnemyPool.Entries)
                if (P.Idle && P.Actor.IsValid() && P.Actor->GetClass() == Entry.EnemyClass.Get())
                    ++Idle;
            Row->SetNumberField(TEXT("idle"), Idle);
            ByType.Add(MakeShared<FJsonValueObject>(Row));
        }
    Result->SetArrayField(TEXT("pool_by_type"), ByType);
    Result->SetNumberField(TEXT("sample_count_total"), PerformanceSampleCount);
    Result->SetNumberField(TEXT("peak_observed_process_physical_bytes"), double(PeakObservedProcessBytes));
    const auto AddTiming = [&](const TCHAR* Name, const FTiming& Timing)
    {
        auto Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("count"), Timing.Count);
        Object->SetNumberField(TEXT("total_ms"), Timing.TotalMs);
        Object->SetNumberField(TEXT("max_ms"), Timing.MaxMs);
        Object->SetNumberField(TEXT("mean_ms"), Timing.Count ? Timing.TotalMs / Timing.Count : 0);
        Result->SetObjectField(Name, Object);
    };
    AddTiming(TEXT("create"), CreateTiming);
    AddTiming(TEXT("reuse"), ReuseTiming);
    AddTiming(TEXT("return"), ReturnTiming);
    AddTiming(TEXT("gas_reset"), ResetTiming);
    Result->SetNumberField(TEXT("settling_corpses"), PendingPoolReturns.Num());
    Result->SetArrayField(TEXT("samples"), PerformanceSamples);
    return Result;
}

// 以运行、世界、网络角色和进程标识命名并保存性能报告。
bool ADungeonRunDirector::ExportPerformanceReport() const
{
    if (!Profile || !Profile->bSamplePerformance || !RunId.IsValid())
        return false;
    auto Result = MakePerformanceReport();
    Result->SetStringField(TEXT("run_id"), RunId.ToString());
    Result->SetStringField(TEXT("world"), GetWorld()->GetName());
    Result->SetBoolField(TEXT("authority"), HasAuthority());
    Result->SetStringField(TEXT("phase"), StaticEnum<EDungeonRunPhase>()->GetNameStringByValue(int64(Phase)));
    Result->SetNumberField(TEXT("seed"), Generation ? Generation->Seed : 0);
    FString Json;
    FJsonSerializer::Serialize(Result.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("RoguelikeGeneration/Performance");
    IFileManager::Get().MakeDirectory(*Directory, true);
    // PIE worlds share the World object's name; package + role + PID prevents overwrites.
    const FString Filename =
        FString::Printf(TEXT("%s-%s-%s-%u.json"), *RunId.ToString(EGuidFormats::Digits),
                        *FPaths::GetCleanFilename(GetWorld()->GetOutermost()->GetName()),
                        HasAuthority() ? TEXT("server") : TEXT("client"), FPlatformProcess::GetCurrentProcessId());
    return FFileHelper::SaveStringToFile(Json, *(Directory / Filename));
}
