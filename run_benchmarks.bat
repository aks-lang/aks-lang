@echo off
setlocal enabledelayedexpansion

where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
    )
)

echo Reproducible Empirical Benchmark Suite
echo.
echo Step 1: Compiling native baselines with optimizing compiler...
cl.exe /O2 /Oi /Ot /arch:AVX2 /fp:fast /DNDEBUG /W3 /Fe:tests\NativeBenchmarkProof.exe tests\NativeBenchmarkProof.c >nul 2>nul
cl.exe /O2 /Oi /Ot /arch:AVX2 /fp:fast /DNDEBUG /W3 /Fe:tests\NativePiProof.exe tests\NativePiProof.c >nul 2>nul
cl.exe /O2 /Oi /Ot /arch:AVX2 /fp:fast /DNDEBUG /W3 /Fe:tests\IndustryBenchmarksNative.exe tests\IndustryBenchmarksNative.c >nul 2>nul

echo Step 2: Executing compiled native baseline benchmarks...
echo.
tests\NativeBenchmarkProof.exe
if %errorlevel% neq 0 exit /b 1

echo.
echo Step 3: Executing AKS runtime proof suite...
echo.
aks.exe run --jit tests\TestSpeedProof.aks
if %errorlevel% neq 0 exit /b 1

echo.
echo Step 4: Executing native billion-value Pi benchmark...
echo.
tests\NativePiProof.exe
if %errorlevel% neq 0 exit /b 1

echo.
echo Step 5: Executing AKS billion-value Pi benchmark...
echo.
aks.exe run tests\TestPiProof.aks
if %errorlevel% neq 0 exit /b 1

echo.
echo Reproducible benchmark verification completed successfully.
