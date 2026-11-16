#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <windows.h>

static double GetTimeMs(void) {
    static LARGE_INTEGER freq;
    static int init = 0;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return ((double)counter.QuadPart * 1000.0) / (double)freq.QuadPart;
}

static int64_t PureArithmeticLoop(int64_t n) {
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}

static double PurePiCalculation(int64_t n) {
    double step = 1.0 / (double)n;
    double sum = 0.0;
    for (int64_t i = 0; i < n; i++) {
        double x = ((double)i + 0.5) * step;
        sum += 4.0 / (1.0 + x * x);
    }
    return sum * step;
}

static int PureMandelbrot(int width, int height, int max_iters) {
    double dx = 2.5 / (double)width;
    double dy = 2.5 / (double)height;
    int count = 0;
    for (int y = 0; y < height; y++) {
        double ci = -1.25 + (double)y * dy;
        for (int x = 0; x < width; x++) {
            double cr = -2.0 + (double)x * dx;
            double zr = 0.0;
            double zi = 0.0;
            int it = 0;
            while (it < max_iters) {
                double zr2 = zr * zr;
                double zi2 = zi * zi;
                if (zr2 + zi2 > 4.0) break;
                zi = 2.0 * zr * zi + ci;
                zr = zr2 - zi2 + cr;
                it++;
            }
            if (it == max_iters) count++;
        }
    }
    return count;
}

static int64_t PureFib(int64_t n) {
    if (n <= 1) return n;
    return PureFib(n - 1) + PureFib(n - 2);
}

static void PureBubbleSort(int64_t *arr, int64_t n) {
    for (int64_t i = 0; i < n - 1; i++) {
        for (int64_t j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int64_t t = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = t;
            }
        }
    }
}

int main(void) {
    printf("Starting Native Baseline Measurements\n\n");

    // 1. Arithmetic Loop
    double t0 = GetTimeMs();
    int64_t sum = PureArithmeticLoop(10000000);
    double t1 = GetTimeMs();
    printf("Native Arithmetic Loop (10M): %.4f ms (sum: %lld)\n", t1 - t0, (long long)sum);

    // 2. Pi Numerical Integration
    double t2 = GetTimeMs();
    double pi = PurePiCalculation(10000000);
    double t3 = GetTimeMs();
    printf("Native Pi Numerical Integration (10M): %.4f ms (pi: %.6f)\n", t3 - t2, pi);

    // 3. Mandelbrot Set
    double t4 = GetTimeMs();
    int mandel = PureMandelbrot(100, 100, 50);
    double t5 = GetTimeMs();
    printf("Native Mandelbrot (100x100): %.4f ms (chk: %d)\n", t5 - t4, mandel);

    // 4. Recursive Fibonacci
    double t6 = GetTimeMs();
    int64_t fib = PureFib(30);
    double t7 = GetTimeMs();
    printf("Native Recursive Fib(30): %.4f ms (fib: %lld)\n", t7 - t6, (long long)fib);

    // 5. Array Sort
    int64_t sort_size = 5000;
    int64_t *arr = (int64_t *)malloc(sort_size * sizeof(int64_t));
    for (int64_t i = 0; i < sort_size; i++) {
        arr[i] = (sort_size - i);
    }
    double t8 = GetTimeMs();
    PureBubbleSort(arr, sort_size);
    double t9 = GetTimeMs();
    printf("Native Array Sort (5,000 elements): %.4f ms (first: %lld, last: %lld)\n", t9 - t8, (long long)arr[0], (long long)arr[sort_size - 1]);
    free(arr);

    return 0;
}
