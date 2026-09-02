// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// High-resolution nanosecond Benchmark & Profiler Engine implementation.
#include "bench.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static LARGE_INTEGER g_bench_start_time;
static LARGE_INTEGER g_freq;
#else
static struct timespec g_bench_start_time;
#endif

void BenchStart(const char *name) {
    (void)name;
#ifdef _WIN32
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_bench_start_time);
#else
    clock_gettime(CLOCK_MONOTONIC, &g_bench_start_time);
#endif
}

BenchResult BenchEnd(const char *name, uint64_t iterations) {
    BenchResult res;
    res.name = name;
    res.iterations = iterations > 0 ? iterations : 1;

#ifdef _WIN32
    LARGE_INTEGER end_time;
    QueryPerformanceCounter(&end_time);
    double elapsed_sec = (double)(end_time.QuadPart - g_bench_start_time.QuadPart) / (double)g_freq.QuadPart;
#else
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed_sec = (end_time.tv_sec - g_bench_start_time.tv_sec) + (end_time.tv_nsec - g_bench_start_time.tv_nsec) * 1e-9;
#endif

    res.total_time_ms = elapsed_sec * 1000.0;
    res.ns_per_op = (elapsed_sec * 1e9) / (double)res.iterations;
    res.ops_per_sec = (double)res.iterations / (elapsed_sec > 0 ? elapsed_sec : 1e-9);

    return res;
}

void BenchPrintSummary(const BenchResult *res) {
    printf("[bench] %-30s | %10llu ops | %8.2f ms | %8.2f ns/op | %12.0f ops/s\n",
           res->name ? res->name : "Benchmark",
           (unsigned long long)res->iterations,
           res->total_time_ms,
           res->ns_per_op,
           res->ops_per_sec);
}
