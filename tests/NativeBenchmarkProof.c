// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Native empirical benchmark suite for bare-metal baseline measurements
// Measures execution latency and throughput with high-resolution timers

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>

#ifdef _WIN32
#include <windows.h>
static double GetTimeSeconds(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}
#else
#include <time.h>
static double GetTimeSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

// Standard comparator callback for native qsort
static int CompareDoubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

// Pseudo random generator matching the test suite distribution
static void GenerateData(double *arr, int count, int32_t seed) {
    int32_t state = seed;
    for (int i = 0; i < count; i++) {
        state = (int32_t)(((int64_t)state * 1103515245 + 12345) % 2147483647);
        if (state < 0) state = -state;
        arr[i] = (double)(state % 10000) / 10.0;
    }
}

static void RunBenchmarkSuite(void) {
    printf("Starting Native Baseline Empirical Proof Suite\n\n");

    // Test 1: 50,000-element sort with standard library callback
    const int sort_count = 50000;
    double *sort_buf = (double *)malloc(sort_count * sizeof(double));
    double *sort_work = (double *)malloc(sort_count * sizeof(double));
    GenerateData(sort_buf, sort_count, 9999);

    double best_sort_ms = 1e9;
    for (int run = 0; run < 5; run++) {
        memcpy(sort_work, sort_buf, sort_count * sizeof(double));
        double t0 = GetTimeSeconds();
        qsort(sort_work, sort_count, sizeof(double), CompareDoubles);
        double t1 = GetTimeSeconds();
        double elapsed_ms = (t1 - t0) * 1000.0;
        if (elapsed_ms < best_sort_ms) best_sort_ms = elapsed_ms;
    }
    printf("Native Sort (50,000 elements): %.4f ms (first: %.2f, last: %.2f)\n",
           best_sort_ms, sort_work[0], sort_work[sort_count - 1]);
    free(sort_buf);
    free(sort_work);

    // Test 2: 128x128 dense matrix multiplication with AVX2
    const int dim = 128;
    double *A = (double *)malloc(dim * dim * sizeof(double));
    double *B = (double *)malloc(dim * dim * sizeof(double));
    double *BT = (double *)malloc(dim * dim * sizeof(double));
    double *C = (double *)malloc(dim * dim * sizeof(double));
    GenerateData(A, dim * dim, 42);
    GenerateData(B, dim * dim, 84);

    // Transpose B for contiguous memory layout
    for (int r = 0; r < dim; r++) {
        for (int c = 0; c < dim; c++) {
            BT[c * dim + r] = B[r * dim + c];
        }
    }

    double best_matmul_ms = 1e9;
    for (int run = 0; run < 5; run++) {
        double t0 = GetTimeSeconds();
        for (int i = 0; i < dim; i++) {
            const double *a_row = &A[i * dim];
            for (int j = 0; j < dim; j++) {
                const double *b_col = &BT[j * dim];
                __m256d acc0 = _mm256_setzero_pd();
                __m256d acc1 = _mm256_setzero_pd();
                int k = 0;
                for (; k + 7 < dim; k += 8) {
                    __m256d va0 = _mm256_loadu_pd(&a_row[k]);
                    __m256d vb0 = _mm256_loadu_pd(&b_col[k]);
                    __m256d va1 = _mm256_loadu_pd(&a_row[k + 4]);
                    __m256d vb1 = _mm256_loadu_pd(&b_col[k + 4]);
                    acc0 = _mm256_fmadd_pd(va0, vb0, acc0);
                    acc1 = _mm256_fmadd_pd(va1, vb1, acc1);
                }
                acc0 = _mm256_add_pd(acc0, acc1);
                double lanes[4];
                _mm256_storeu_pd(lanes, acc0);
                double sum = lanes[0] + lanes[1] + lanes[2] + lanes[3];
                for (; k < dim; k++) {
                    sum += a_row[k] * b_col[k];
                }
                C[i * dim + j] = sum;
            }
        }
        double t1 = GetTimeSeconds();
        double elapsed_ms = (t1 - t0) * 1000.0;
        if (elapsed_ms < best_matmul_ms) best_matmul_ms = elapsed_ms;
    }
    printf("Native Matrix Multiply (128x128): %.4f ms (C[0][0]: %.2f)\n",
           best_matmul_ms, C[0]);
    free(A);
    free(B);
    free(BT);
    free(C);

    // Test 3: 100,000-element vector dot product with AVX2
    const int dot_count = 100000;
    double *v1 = (double *)malloc(dot_count * sizeof(double));
    double *v2 = (double *)malloc(dot_count * sizeof(double));
    GenerateData(v1, dot_count, 1234);
    GenerateData(v2, dot_count, 5678);

    double best_dot_ms = 1e9;
    double final_dot = 0.0;
    for (int run = 0; run < 5; run++) {
        double t0 = GetTimeSeconds();
        __m256d acc0 = _mm256_setzero_pd();
        __m256d acc1 = _mm256_setzero_pd();
        int k = 0;
        for (; k + 7 < dot_count; k += 8) {
            __m256d va0 = _mm256_loadu_pd(&v1[k]);
            __m256d vb0 = _mm256_loadu_pd(&v2[k]);
            __m256d va1 = _mm256_loadu_pd(&v1[k + 4]);
            __m256d vb1 = _mm256_loadu_pd(&v2[k + 4]);
            acc0 = _mm256_fmadd_pd(va0, vb0, acc0);
            acc1 = _mm256_fmadd_pd(va1, vb1, acc1);
        }
        acc0 = _mm256_add_pd(acc0, acc1);
        double lanes[4];
        _mm256_storeu_pd(lanes, acc0);
        double dot = lanes[0] + lanes[1] + lanes[2] + lanes[3];
        for (; k < dot_count; k++) {
            dot += v1[k] * v2[k];
        }
        double t1 = GetTimeSeconds();
        double elapsed_ms = (t1 - t0) * 1000.0;
        if (elapsed_ms < best_dot_ms) best_dot_ms = elapsed_ms;
        final_dot = dot;
    }
    printf("Native Dot Product (100,000 elements): %.4f ms (dot: %.2f)\n",
           best_dot_ms, final_dot);
    free(v2);

    // Test 4: 100,000-element parallel sum reduction with AVX2
    double best_red_ms = 1e9;
    double final_red = 0.0;
    for (int run = 0; run < 5; run++) {
        double t0 = GetTimeSeconds();
        __m256d acc0 = _mm256_setzero_pd();
        __m256d acc1 = _mm256_setzero_pd();
        int k = 0;
        for (; k + 7 < dot_count; k += 8) {
            acc0 = _mm256_add_pd(acc0, _mm256_loadu_pd(&v1[k]));
            acc1 = _mm256_add_pd(acc1, _mm256_loadu_pd(&v1[k + 4]));
        }
        acc0 = _mm256_add_pd(acc0, acc1);
        double lanes[4];
        _mm256_storeu_pd(lanes, acc0);
        double sum = lanes[0] + lanes[1] + lanes[2] + lanes[3];
        for (; k < dot_count; k++) {
            sum += v1[k];
        }
        double t1 = GetTimeSeconds();
        double elapsed_ms = (t1 - t0) * 1000.0;
        if (elapsed_ms < best_red_ms) best_red_ms = elapsed_ms;
        final_red = sum;
    }
    printf("Native Parallel Reduction (100,000 elements): %.4f ms (sum: %.2f)\n",
           best_red_ms, final_red);
    free(v1);

    // Test 5: 10,000,000-iteration arithmetic induction loop
    const int64_t iterations = 10000000;
    double best_loop_ms = 1e9;
    int64_t final_sum = 0;
    for (int run = 0; run < 5; run++) {
        double t0 = GetTimeSeconds();
        volatile int64_t sum = 0;
        for (int64_t i = 0; i < iterations; i++) {
            sum += i;
        }
        double t1 = GetTimeSeconds();
        double elapsed_ms = (t1 - t0) * 1000.0;
        if (elapsed_ms < best_loop_ms) best_loop_ms = elapsed_ms;
        final_sum = sum;
    }
    printf("Native Arithmetic Loop (10,000,000 iterations): %.4f ms (sum: %lld)\n\n",
           best_loop_ms, (long long)final_sum);
    printf("Native baseline execution completed cleanly.\n");
}

int main(void) {
    RunBenchmarkSuite();
    return 0;
}
