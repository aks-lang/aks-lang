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
} BenchResult;

void BenchStart(const char *name);
BenchResult BenchEnd(const char *name, uint64_t iterations);
void BenchPrintSummary(const BenchResult *res);

#endif // Benchh
