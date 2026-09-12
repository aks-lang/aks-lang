@echo off
setlocal enabledelayedexpansion

echo Running Cross-Backend Differential Testing Harness...

REM 1. Standard VM execution
.\aks.exe run tests\TestDifferentialSuite.aks > tests\diff_vm.out
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Standard VM execution failed
    del /q tests\diff_vm.out 2>nul
    exit /b 1
)

REM 2. Tracing JIT execution
.\aks.exe run --jit tests\TestDifferentialSuite.aks > tests\diff_jit.out
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] JIT execution failed
    del /q tests\diff_vm.out tests\diff_jit.out 2>nul
    exit /b 1
)

REM 3. Parallel compilation execution
.\aks.exe run --parallel tests\TestDifferentialSuite.aks > tests\diff_parallel.out
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Parallel compilation execution failed
    del /q tests\diff_vm.out tests\diff_jit.out tests\diff_parallel.out 2>nul
    exit /b 1
)

REM 4. Verify semantic identity between VM and JIT
fc /b tests\diff_vm.out tests\diff_jit.out >nul
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Semantic divergence between VM and JIT!
    del /q tests\diff_vm.out tests\diff_jit.out tests\diff_parallel.out 2>nul
    exit /b 1
)

REM 5. Verify semantic identity between VM and Parallel compilation
fc /b tests\diff_vm.out tests\diff_parallel.out >nul
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Semantic divergence between VM and Parallel compilation!
    del /q tests\diff_vm.out tests\diff_jit.out tests\diff_parallel.out 2>nul
    exit /b 1
)

REM 6. Verify MLIR code generator emission
.\aks.exe compile-mlir tests\TestDifferentialSuite.aks tests\TestDifferentialSuite.mlir 1>nul
if not exist tests\TestDifferentialSuite.mlir (
    echo [FAIL] MLIR compilation failed
    del /q tests\diff_vm.out tests\diff_jit.out tests\diff_parallel.out 2>nul
    exit /b 1
)

REM 7. Verify LLVM code generator emission
.\aks.exe compile-llvm tests\TestDifferentialSuite.aks tests\TestDifferentialSuite.ll 1>nul
if not exist tests\TestDifferentialSuite.ll (
    echo [FAIL] LLVM compilation failed
    del /q tests\diff_vm.out tests\diff_jit.out tests\diff_parallel.out tests\TestDifferentialSuite.mlir 2>nul
    exit /b 1
)

REM Clean up temporary verification artifacts
del /q tests\diff_vm.out tests\diff_jit.out tests\diff_parallel.out tests\TestDifferentialSuite.mlir tests\TestDifferentialSuite.ll 2>nul

echo [PASS] Cross-Backend Differential Verification: 100%% Semantic Parity across VM, JIT, Parallel, MLIR, and LLVM!
exit /b 0
