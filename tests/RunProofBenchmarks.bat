@echo off
setlocal enabledelayedexpansion

where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
    )
)

echo Building native compiled baseline...
cl.exe /O2 /Oi /Ot /arch:AVX2 /fp:fast /DNDEBUG /W3 /Fe:tests\NativeBenchmarkProof.exe tests\NativeBenchmarkProof.c >nul
if %errorlevel% neq 0 (
    echo Compilation of native benchmark failed.
    exit /b 1
)

echo Running native compiled baseline...
echo.
tests\NativeBenchmarkProof.exe
if %errorlevel% neq 0 exit /b 1

echo.
echo Running AKS runtime empirical proof...
echo.
aks.exe run --jit tests\TestSpeedProof.aks
if %errorlevel% neq 0 exit /b 1

echo.
echo Benchmark proof execution completed successfully.
