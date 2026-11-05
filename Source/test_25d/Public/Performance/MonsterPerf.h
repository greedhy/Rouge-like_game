#pragma once
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

CSV_DECLARE_CATEGORY_EXTERN(MonsterPerf);
// CSV values are inclusive wall-clock time summed per frame, not exclusive CPU cost.
#define MONSTER_PERF_SCOPE(Name) \
    TRACE_CPUPROFILER_EVENT_SCOPE(Name); \
    CSV_SCOPED_TIMING_STAT(MonsterPerf, Name)
