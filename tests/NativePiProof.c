// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Native empirical benchmark for 1 billion values of Pi calculation

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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

int main(void) {
    const int64_t steps = 1000000000LL;
    const double step = 1.0 / (double)steps;

    printf("Starting Native Baseline Pi Benchmark (1,000,000,000 values)\n\n");

    // Method 1: Midpoint numerical integration
    double best_mid_ms = 1e9;
    double final_mid_pi = 0.0;
    for (int run = 0; run < 3; run++) {
        double t0 = GetTimeSeconds();
        __m256d vstep = _mm256_set1_pd(step);
        __m256d vfour = _mm256_set1_pd(4.0);
        __m256d vone = _mm256_set1_pd(1.0);
        __m256d vacc0 = _mm256_setzero_pd();
        __m256d vacc1 = _mm256_setzero_pd();
        __m256d vidx0 = _mm256_set_pd(3.5, 2.5, 1.5, 0.5);
        __m256d vidx1 = _mm256_set_pd(7.5, 6.5, 5.5, 4.5);
        __m256d veight = _mm256_set1_pd(8.0);

        for (int64_t i = 0; i < steps; i += 8) {
            __m256d vx0 = _mm256_mul_pd(vidx0, vstep);
            __m256d vx1 = _mm256_mul_pd(vidx1, vstep);
            __m256d denom0 = _mm256_fmadd_pd(vx0, vx0, vone);
            __m256d denom1 = _mm256_fmadd_pd(vx1, vx1, vone);
            vacc0 = _mm256_add_pd(vacc0, _mm256_div_pd(vfour, denom0));
            vacc1 = _mm256_add_pd(vacc1, _mm256_div_pd(vfour, denom1));
            vidx0 = _mm256_add_pd(vidx0, veight);
            vidx1 = _mm256_add_pd(vidx1, veight);
        }
        vacc0 = _mm256_add_pd(vacc0, vacc1);
        double lanes[4];
        _mm256_storeu_pd(lanes, vacc0);
        double pi_calc = (lanes[0] + lanes[1] + lanes[2] + lanes[3]) * step;
        double t1 = GetTimeSeconds();
        double elapsed_ms = (t1 - t0) * 1000.0;
        if (elapsed_ms < best_mid_ms) best_mid_ms = elapsed_ms;
        final_mid_pi = pi_calc;
    }
    printf("Native Midpoint Pi: %.10f in %.4f ms\n", final_mid_pi, best_mid_ms);

    // Method 2: Gregory-Leibniz alternating series
    double best_leib_ms = 1e9;
    double final_leib_pi = 0.0;
    for (int run = 0; run < 3; run++) {
        double t0 = GetTimeSeconds();
        __m256d vone = _mm256_set1_pd(1.0);
        __m256d vpos_acc = _mm256_setzero_pd();
        __m256d vneg_acc = _mm256_setzero_pd();
        __m256d vpos_den = _mm256_set_pd(13.0, 9.0, 5.0, 1.0);
        __m256d vneg_den = _mm256_set_pd(15.0, 11.0, 7.0, 3.0);
        __m256d vstep_den = _mm256_set1_pd(16.0);

        for (int64_t i = 0; i < steps; i += 8) {
            vpos_acc = _mm256_add_pd(vpos_acc, _mm256_div_pd(vone, vpos_den));
            vneg_acc = _mm256_add_pd(vneg_acc, _mm256_div_pd(vone, vneg_den));
            vpos_den = _mm256_add_pd(vpos_den, vstep_den);
            vneg_den = _mm256_add_pd(vneg_den, vstep_den);
        }
        vpos_acc = _mm256_sub_pd(vpos_acc, vneg_acc);
        double lanes[4];
        _mm256_storeu_pd(lanes, vpos_acc);
        double pi_calc = (lanes[0] + lanes[1] + lanes[2] + lanes[3]) * 4.0;
        double t1 = GetTimeSeconds();
        double elapsed_ms = (t1 - t0) * 1000.0;
        if (elapsed_ms < best_leib_ms) best_leib_ms = elapsed_ms;
        final_leib_pi = pi_calc;
    }
    printf("Native Leibniz  Pi: %.10f in %.4f ms\n", final_leib_pi, best_leib_ms);

    return 0;
}
