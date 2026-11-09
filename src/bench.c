// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// High-resolution nanosecond Benchmark & Profiler Engine implementation.
#include "bench.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
static LARGE_INTEGER g_bench_start_time;
static LARGE_INTEGER g_freq;
#else
static struct timespec g_bench_start_time;
#endif

static int compare_doubles_asc(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

BenchStats BenchComputeStats(const char *name, const double *samples_ns, int count, uint64_t iters_per_sample) {
    BenchStats st;
    memset(&st, 0, sizeof(st));
    st.name = name;
    st.iterations = iters_per_sample;
    st.samples_count = count;
    if (count <= 0 || !samples_ns) return st;

    double *sorted = (double *)malloc(count * sizeof(double));
    if (!sorted) return st;
    memcpy(sorted, samples_ns, count * sizeof(double));
    qsort(sorted, count, sizeof(double), compare_doubles_asc);

    st.min_ns = sorted[0];
    st.max_ns = sorted[count - 1];

    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += sorted[i];
    }
    st.mean_ns = sum / (double)count;
    st.total_time_ms = (sum * (double)iters_per_sample) / 1e6;

    if (count % 2 == 1) {
        st.median_ns = sorted[count / 2];
    } else {
        st.median_ns = (sorted[count / 2 - 1] + sorted[count / 2]) * 0.5;
    }

    int p95_idx = (int)(count * 0.95);
    if (p95_idx >= count) p95_idx = count - 1;
    st.p95_ns = sorted[p95_idx];

    int p99_idx = (int)(count * 0.99);
    if (p99_idx >= count) p99_idx = count - 1;
    st.p99_ns = sorted[p99_idx];

    double variance_sum = 0.0;
    for (int i = 0; i < count; i++) {
        double diff = sorted[i] - st.mean_ns;
        variance_sum += diff * diff;
    }
    st.stddev_ns = (count > 1) ? sqrt(variance_sum / (double)(count - 1)) : 0.0;
    st.ops_per_sec = st.mean_ns > 0.0 ? (1e9 / st.mean_ns) : 0.0;

    free(sorted);
    return st;
}

void BenchPrintStats(const BenchStats *stats) {
    printf("[bench] %-28s | %8llu iters | mean: %8.2f ns (±%6.2f) | median: %8.2f ns | p95: %8.2f ns | p99: %8.2f ns | %10.0f ops/s\n",
           stats->name ? stats->name : "Benchmark",
           (unsigned long long)stats->iterations,
           stats->mean_ns,
           stats->stddev_ns,
           stats->median_ns,
           stats->p95_ns,
           stats->p99_ns,
           stats->ops_per_sec);
}

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
