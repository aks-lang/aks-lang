// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// High-resolution nanosecond Benchmark & Profiler Engine.
#ifndef BENCHH
#define BENCHH

#include <stdint.h>
#include <stdio.h>

typedef struct BenchResult {
    const char *name;
    uint64_t    iterations;
    double      total_time_ms;
    double      ns_per_op;
    double      ops_per_sec;
    double      mean_ns;
    double      median_ns;
    double      stddev_ns;
    double      min_ns;
    double      max_ns;
    double      p95_ns;
    double      p99_ns;
    int         samples_count;
} BenchResult;

typedef struct BenchStats {
    const char *name;
    uint64_t    iterations;
    int         samples_count;
    double      total_time_ms;
    double      mean_ns;
    double      median_ns;
    double      stddev_ns;
    double      min_ns;
    double      max_ns;
    double      p95_ns;
    double      p99_ns;
    double      ops_per_sec;
} BenchStats;

void BenchStart(const char *name);
BenchResult BenchEnd(const char *name, uint64_t iterations);
BenchStats BenchComputeStats(const char *name, const double *samples_ns, int count, uint64_t iters_per_sample);
void BenchPrintSummary(const BenchResult *res);
void BenchPrintStats(const BenchStats *stats);

#endif // Benchh
