#!/bin/bash
set -e

echo "Reproducible Empirical Benchmark Suite"
echo ""

echo "Step 1: Compiling native baselines with optimizing compiler..."
if command -v gcc >/dev/null 2>&1; then
    gcc -O3 -mavx2 -mfma -DNDEBUG -Wno-unused-result tests/NativeBenchmarkProof.c -o tests/NativeBenchmarkProof -lm 2>/dev/null || gcc -O3 -DNDEBUG tests/NativeBenchmarkProof.c -o tests/NativeBenchmarkProof -lm
    gcc -O3 -mavx2 -mfma -DNDEBUG -Wno-unused-result tests/NativePiProof.c -o tests/NativePiProof -lm 2>/dev/null || gcc -O3 -DNDEBUG tests/NativePiProof.c -o tests/NativePiProof -lm
    gcc -O3 -mavx2 -mfma -DNDEBUG -Wno-unused-result tests/IndustryBenchmarksNative.c -o tests/IndustryBenchmarksNative -lm 2>/dev/null || gcc -O3 -DNDEBUG tests/IndustryBenchmarksNative.c -o tests/IndustryBenchmarksNative -lm
elif command -v clang >/dev/null 2>&1; then
    clang -O3 -DNDEBUG tests/NativeBenchmarkProof.c -o tests/NativeBenchmarkProof -lm
    clang -O3 -DNDEBUG tests/NativePiProof.c -o tests/NativePiProof -lm
    clang -O3 -DNDEBUG tests/IndustryBenchmarksNative.c -o tests/IndustryBenchmarksNative -lm
fi

echo "Step 2: Executing compiled native baseline benchmarks..."
echo ""
./tests/NativeBenchmarkProof

echo ""
echo "Step 3: Executing AKS runtime proof suite..."
echo ""
./aks run --jit tests/TestSpeedProof.aks

echo ""
echo "Step 4: Executing native billion-value Pi benchmark..."
echo ""
./tests/NativePiProof

echo ""
echo "Step 5: Executing AKS billion-value Pi benchmark..."
echo ""
./aks run tests/TestPiProof.aks

echo ""
echo "Step 6: Executing industry standard native benchmarks..."
echo ""
./tests/IndustryBenchmarksNative

echo ""
echo "Step 7: Executing AKS industry standard benchmarks..."
echo ""
./aks run tests/IndustryBenchmarks.aks

echo ""
echo "Reproducible benchmark verification completed successfully."
