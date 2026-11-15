// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Industry Standard Native Empirical Benchmark Suite
// Canonical algorithms executed with full native compiler optimization and AVX2

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && !defined(__aarch64__) && !defined(__arm64__)
#define HAS_X86_AVX2 1
#include <immintrin.h>
#define HARDWARE_BARRIER() _mm_mfence()
#else
#define HAS_X86_AVX2 0
#if defined(_MSC_VER)
#include <intrin.h>
#define HARDWARE_BARRIER() MemoryBarrier()
#else
#define HARDWARE_BARRIER() __sync_synchronize()
#endif
#endif

#ifdef _WIN32
#include <windows.h>
static int64_t GetTimeNanos(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    int64_t sec = counter.QuadPart / freq.QuadPart;
    int64_t rem = counter.QuadPart % freq.QuadPart;
    return sec * 1000000000LL + (rem * 1000000000LL) / freq.QuadPart;
}
#else
#include <time.h>
static int64_t GetTimeNanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}
#endif

// 1. Numerical Integration (Pi Midpoint Riemann Sum)
static double BenchmarkPiMidpoint(int64_t steps) {
    const double step = 1.0 / (double)steps;
#if HAS_X86_AVX2
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
    return (lanes[0] + lanes[1] + lanes[2] + lanes[3]) * step;
#else
    double s = 0.0;
    for (int64_t i = 0; i < steps; i++) {
        double x = ((double)i + 0.5) * step;
        s += 4.0 / (1.0 + x * x);
    }
    return s * step;
#endif
}

// 2. Spectral Norm
static inline double A_val(int i, int j) {
    return 1.0 / (double)((i + j) * (i + j + 1) / 2 + i + 1);
}

static void MultiplyAv(int n, const double *v, double *Av) {
#if HAS_X86_AVX2
    for (int i = 0; i < n; i++) {
        __m256d sum0 = _mm256_setzero_pd();
        int j = 0;
        for (; j + 3 < n; j += 4) {
            __m256d a = _mm256_set_pd(A_val(i, j + 3), A_val(i, j + 2), A_val(i, j + 1), A_val(i, j));
            __m256d u = _mm256_loadu_pd(&v[j]);
            sum0 = _mm256_fmadd_pd(a, u, sum0);
        }
        double lanes[4];
        _mm256_storeu_pd(lanes, sum0);
        double s = lanes[0] + lanes[1] + lanes[2] + lanes[3];
        for (; j < n; j++) {
            s += A_val(i, j) * v[j];
        }
        Av[i] = s;
    }
#else
    for (int i = 0; i < n; i++) {
        double s = 0.0;
        for (int j = 0; j < n; j++) {
            s += A_val(i, j) * v[j];
        }
        Av[i] = s;
    }
#endif
}

static void MultiplyAtv(int n, const double *v, double *Atv) {
#if HAS_X86_AVX2
    for (int i = 0; i < n; i++) {
        __m256d sum0 = _mm256_setzero_pd();
        int j = 0;
        for (; j + 3 < n; j += 4) {
            __m256d a = _mm256_set_pd(A_val(j + 3, i), A_val(j + 2, i), A_val(j + 1, i), A_val(j, i));
            __m256d u = _mm256_loadu_pd(&v[j]);
            sum0 = _mm256_fmadd_pd(a, u, sum0);
        }
        double lanes[4];
        _mm256_storeu_pd(lanes, sum0);
        double s = lanes[0] + lanes[1] + lanes[2] + lanes[3];
        for (; j < n; j++) {
            s += A_val(j, i) * v[j];
        }
        Atv[i] = s;
    }
#else
    for (int i = 0; i < n; i++) {
        double s = 0.0;
        for (int j = 0; j < n; j++) {
            s += A_val(j, i) * v[j];
        }
        Atv[i] = s;
    }
#endif
}

static double BenchmarkSpectralNorm(int n) {
    double *u = (double *)malloc(n * sizeof(double));
    double *v = (double *)malloc(n * sizeof(double));
    double *tmp = (double *)malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) u[i] = 1.0;

    for (int it = 0; it < 10; it++) {
        MultiplyAv(n, u, tmp);
        MultiplyAtv(n, tmp, v);
        MultiplyAv(n, v, tmp);
        MultiplyAtv(n, tmp, u);
    }
    double vBv = 0.0, vv = 0.0;
    for (int i = 0; i < n; i++) {
        vBv += u[i] * v[i];
        vv += v[i] * v[i];
    }
    free(u);
    free(v);
    free(tmp);
    return sqrt(vBv / vv);
}

// 3. N-Body Gravitational Simulation
typedef struct {
    double x, y, z;
    double vx, vy, vz;
    double mass;
} Body;

static double BenchmarkNBody(int steps) {
    const double pi = 3.141592653589793;
    const double days_per_year = 365.24;
    const double solar_mass = 4.0 * pi * pi;

    Body bodies[5] = {
        {0, 0, 0, 0, 0, 0, solar_mass},
        {4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
         1.66007664274403694e-03 * days_per_year, 7.69901118481405026e-03 * days_per_year, -6.90460016972063023e-05 * days_per_year,
         9.54791938424326609e-04 * solar_mass},
        {8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
         -2.76742510726862411e-03 * days_per_year, 4.99852801234917238e-03 * days_per_year, 2.30417297573763929e-05 * days_per_year,
         2.85885672722241763e-04 * solar_mass},
        {1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
         2.96460137564761618e-03 * days_per_year, 2.37847173959480950e-03 * days_per_year, -2.96589568540237556e-05 * days_per_year,
         4.36624404335156298e-05 * solar_mass},
        {1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
         2.68067772490389322e-03 * days_per_year, 1.62824170038242295e-03 * days_per_year, -9.51592254519715870e-05 * days_per_year,
         5.15138902046611451e-05 * solar_mass}
    };
    double px = 0, py = 0, pz = 0;
    for (int i = 0; i < 5; i++) {
        px += bodies[i].vx * bodies[i].mass;
        py += bodies[i].vy * bodies[i].mass;
        pz += bodies[i].vz * bodies[i].mass;
    }
    bodies[0].vx = -px / solar_mass;
    bodies[0].vy = -py / solar_mass;
    bodies[0].vz = -pz / solar_mass;

    const double dt = 0.01;
    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < 5; i++) {
            for (int j = i + 1; j < 5; j++) {
                double dx = bodies[i].x - bodies[j].x;
                double dy = bodies[i].y - bodies[j].y;
                double dz = bodies[i].z - bodies[j].z;
                double dsq = dx * dx + dy * dy + dz * dz;
                double dist = sqrt(dsq);
                double mag = dt / (dsq * dist);

                bodies[i].vx -= dx * bodies[j].mass * mag;
                bodies[i].vy -= dy * bodies[j].mass * mag;
                bodies[i].vz -= dz * bodies[j].mass * mag;

                bodies[j].vx += dx * bodies[i].mass * mag;
                bodies[j].vy += dy * bodies[i].mass * mag;
                bodies[j].vz += dz * bodies[i].mass * mag;
            }
        }
        for (int i = 0; i < 5; i++) {
            bodies[i].x += dt * bodies[i].vx;
            bodies[i].y += dt * bodies[i].vy;
            bodies[i].z += dt * bodies[i].vz;
        }
    }

    double e = 0.0;
    for (int i = 0; i < 5; i++) {
        e += 0.5 * bodies[i].mass * (bodies[i].vx * bodies[i].vx + bodies[i].vy * bodies[i].vy + bodies[i].vz * bodies[i].vz);
        for (int j = i + 1; j < 5; j++) {
            double dx = bodies[i].x - bodies[j].x;
            double dy = bodies[i].y - bodies[j].y;
            double dz = bodies[i].z - bodies[j].z;
            double dist = sqrt(dx * dx + dy * dy + dz * dz);
            e -= (bodies[i].mass * bodies[j].mass) / dist;
        }
    }
    return e;
}

// 4. Mandelbrot Set
static int64_t BenchmarkMandelbrot(int width, int height, int max_iters) {
    int64_t total = 0;
    double dx = 2.5 / (double)width;
    double dy = 2.5 / (double)height;

    for (int y = 0; y < height; y++) {
        double ci = -1.25 + (double)y * dy;
#if HAS_X86_AVX2
        int x = 0;
        for (; x + 3 < width; x += 4) {
            __m256d cr = _mm256_set_pd(-2.0 + (double)(x + 3) * dx,
                                       -2.0 + (double)(x + 2) * dx,
                                       -2.0 + (double)(x + 1) * dx,
                                       -2.0 + (double)x * dx);
            __m256d vci = _mm256_set1_pd(ci);
            __m256d zr = _mm256_setzero_pd();
            __m256d zi = _mm256_setzero_pd();
            __m256d vfour = _mm256_set1_pd(4.0);
            __m256d vtwo = _mm256_set1_pd(2.0);

            int iters[4] = {0, 0, 0, 0};
            int active = 0xF;

            for (int it = 0; it < max_iters && active; it++) {
                __m256d zr2 = _mm256_mul_pd(zr, zr);
                __m256d zi2 = _mm256_mul_pd(zi, zi);
                __m256d mag2 = _mm256_add_pd(zr2, zi2);
                __m256d mask = _mm256_cmp_pd(mag2, vfour, _CMP_LE_OQ);
                int m = _mm256_movemask_pd(mask);
                for (int k = 0; k < 4; k++) {
                    if ((m >> k) & 1) iters[k]++;
                }
                active = m;
                if (!active) break;
                __m256d new_zi = _mm256_fmadd_pd(_mm256_mul_pd(vtwo, zr), zi, vci);
                __m256d new_zr = _mm256_add_pd(_mm256_sub_pd(zr2, zi2), cr);
                zr = new_zr;
                zi = new_zi;
            }
            total += iters[0] + iters[1] + iters[2] + iters[3];
        }
        for (; x < width; x++) {
            double cr = -2.0 + (double)x * dx;
            double zr = 0.0, zi = 0.0;
            int it = 0;
            for (; it < max_iters; it++) {
                double zr2 = zr * zr;
                double zi2 = zi * zi;
                if (zr2 + zi2 > 4.0) break;
                zi = 2.0 * zr * zi + ci;
                zr = zr2 - zi2 + cr;
            }
            total += it;
        }
#else
        for (int x = 0; x < width; x++) {
            double cr = -2.0 + (double)x * dx;
            double zr = 0.0, zi = 0.0;
            int it = 0;
            for (; it < max_iters; it++) {
                double zr2 = zr * zr;
                double zi2 = zi * zi;
                if (zr2 + zi2 > 4.0) break;
                zi = 2.0 * zr * zi + ci;
                zr = zr2 - zi2 + cr;
            }
            total += it;
        }
#endif
    }
    return total;
}

// 5. Fast Fourier Transform (1D Cooley-Tukey Radix-2)
static void BenchmarkFFT(double *real, double *imag, int n) {
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            double tr = real[i]; real[i] = real[j]; real[j] = tr;
            double ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
        int k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * 3.141592653589793 / (double)len;
        double wlen_r = cos(ang), wlen_i = sin(ang);
        int half = len >> 1;
        for (int i = 0; i < n; i += len) {
            double w_r = 1.0, w_i = 0.0;
            for (int k = 0; k < half; k++) {
                double u_r = real[i + k];
                double u_i = imag[i + k];
                double v_r = real[i + k + half] * w_r - imag[i + k + half] * w_i;
                double v_i = real[i + k + half] * w_i + imag[i + k + half] * w_r;
                real[i + k] = u_r + v_r;
                imag[i + k] = u_i + v_i;
                real[i + k + half] = u_r - v_r;
                imag[i + k + half] = u_i - v_i;
                double next_wr = w_r * wlen_r - w_i * wlen_i;
                double next_wi = w_r * wlen_i + w_i * wlen_r;
                w_r = next_wr; w_i = next_wi;
            }
        }
    }
}

// 6. Binary Trees
typedef struct TreeNode {
    int item;
    struct TreeNode *left;
    struct TreeNode *right;
} TreeNode;

static TreeNode *MakeTreeNode(int item, int depth) {
    TreeNode *node = (TreeNode *)malloc(sizeof(TreeNode));
    node->item = item;
    if (depth <= 0) {
        node->left = NULL;
        node->right = NULL;
    } else {
        node->left = MakeTreeNode(2 * item - 1, depth - 1);
        node->right = MakeTreeNode(2 * item, depth - 1);
    }
    return node;
}

static int CheckTreeNode(TreeNode *node) {
    if (!node->left) return node->item;
    return node->item + CheckTreeNode(node->left) - CheckTreeNode(node->right);
}

static void FreeTreeNode(TreeNode *node) {
    if (!node) return;
    FreeTreeNode(node->left);
    FreeTreeNode(node->right);
    free(node);
}

int main(void) {
    printf("Starting Industry Standard Native Benchmark Suite\n\n");

    // Test 1: Pi numerical integration (1,000,000,000 steps)
    HARDWARE_BARRIER();
    int64_t t0 = GetTimeNanos();
    HARDWARE_BARRIER();
    volatile double pi = BenchmarkPiMidpoint(1000000000LL);
    HARDWARE_BARRIER();
    int64_t t1 = GetTimeNanos();
    HARDWARE_BARRIER();
    printf("Native Pi Numerical Integration (1B): %.4f ms (pi: %.10f)\n", (double)(t1 - t0) / 1000000.0, pi);

    // Test 2: Spectral Norm (n = 1000)
    HARDWARE_BARRIER();
    t0 = GetTimeNanos();
    HARDWARE_BARRIER();
    volatile double sn = BenchmarkSpectralNorm(1000);
    HARDWARE_BARRIER();
    t1 = GetTimeNanos();
    HARDWARE_BARRIER();
    printf("Native Spectral Norm (N=1000): %.4f ms (norm: %.9f)\n", (double)(t1 - t0) / 1000000.0, sn);

    // Test 3: N-Body Simulation (500,000 steps)
    HARDWARE_BARRIER();
    t0 = GetTimeNanos();
    HARDWARE_BARRIER();
    volatile double energy = BenchmarkNBody(500000);
    HARDWARE_BARRIER();
    t1 = GetTimeNanos();
    HARDWARE_BARRIER();
    printf("Native N-Body Gravitational (500K): %.4f ms (energy: %.9f)\n", (double)(t1 - t0) / 1000000.0, energy);

    // Test 4: Mandelbrot Set (500x500)
    HARDWARE_BARRIER();
    t0 = GetTimeNanos();
    HARDWARE_BARRIER();
    volatile int64_t mandel = BenchmarkMandelbrot(500, 500, 50);
    HARDWARE_BARRIER();
    t1 = GetTimeNanos();
    HARDWARE_BARRIER();
    printf("Native Mandelbrot Set (500x500): %.4f ms (chk: %lld)\n", (double)(t1 - t0) / 1000000.0, (long long)mandel);

    // Test 5: Fast Fourier Transform (16,384 samples)
    const int fft_n = 16384;
    double *fft_r = (double *)malloc(fft_n * sizeof(double));
    double *fft_i = (double *)malloc(fft_n * sizeof(double));
    for (int i = 0; i < fft_n; i++) {
        fft_r[i] = sin((double)i * 0.05);
        fft_i[i] = 0.0;
    }
    HARDWARE_BARRIER();
    t0 = GetTimeNanos();
    HARDWARE_BARRIER();
    BenchmarkFFT(fft_r, fft_i, fft_n);
    HARDWARE_BARRIER();
    t1 = GetTimeNanos();
    HARDWARE_BARRIER();
    printf("Native Fast Fourier Transform (16K): %.4f ms (r0: %.4f)\n", (double)(t1 - t0) / 1000000.0, fft_r[0]);
    free(fft_r);
    free(fft_i);

    // Test 6: Binary Trees (depth = 14)
    HARDWARE_BARRIER();
    t0 = GetTimeNanos();
    HARDWARE_BARRIER();
    TreeNode *root = MakeTreeNode(0, 14);
    int chk = CheckTreeNode(root);
    FreeTreeNode(root);
    HARDWARE_BARRIER();
    t1 = GetTimeNanos();
    HARDWARE_BARRIER();
    printf("Native Binary Trees (depth 14): %.4f ms (chk: %d)\n\n", (double)(t1 - t0) / 1000000.0, chk);

    printf("Industry standard native execution completed cleanly.\n");
    return 0;
}
