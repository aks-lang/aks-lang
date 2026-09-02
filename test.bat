@echo off
if exist .akscache rmdir /s /q .akscache

echo Building compiler
call build.bat
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo Running tests
aks.exe run tests\TestNullPtr.aks || exit /b 1
aks.exe run tests\TestBounds.aks || exit /b 1
aks.exe run --no-bounds-check tests\TestBounds.aks || exit /b 1
aks.exe run tests\TestOverflow.aks || exit /b 1
aks.exe run --strict-overflow tests\TestOverflow.aks 2>nul
ver > nul
aks.exe run tests\TestUaf.aks || exit /b 1
aks.exe run tests\TestTypeError.aks 2>nul
ver > nul
aks.exe run --immutable-by-default tests\TestConstError.aks 2>nul
ver > nul
aks.exe run tests\TestBorrowError.aks 2>nul
ver > nul
aks.exe run tests\TestStaticRecursion.aks 2>nul
ver > nul
aks.exe run tests\TestOpts.aks || exit /b 1
aks.exe run tests\TestRaii.aks || exit /b 1
aks.exe run tests\SecurityTest.aks || exit /b 1
aks.exe run tests\PlatformTest.aks || exit /b 1
aks.exe run tests\TestCollections.aks || exit /b 1
aks.exe run tests\TestCrypto.aks || exit /b 1
aks.exe run tests\TestSerialization.aks || exit /b 1
aks.exe run tests\TestStringUnicode.aks || exit /b 1
aks.exe run tests\TestNewFeatures.aks || exit /b 1
aks.exe run tests\TestSimdSwizzle.aks || exit /b 1
aks.exe run tests\TestPhysicalUnits.aks || exit /b 1
aks.exe run tests\TestActorModel.aks || exit /b 1
aks.exe run tests\TestAutoDiff.aks || exit /b 1
aks.exe run tests\TestMatrixDualDeepSuite.aks || exit /b 1
aks.exe run tests\TestContractsVerified.aks || exit /b 1
aks.exe run tests\TestAlgebraicEffects.aks || exit /b 1

echo Running strict type tests
aks.exe run --strict-types tests\TestStrict.aks || exit /b 1
aks.exe run --strict-types tests\TestStrictFail.aks 2>nul
aks.exe run --strict-types tests\TestAdvancedTypes.aks || exit /b 1
aks.exe run --strict-types tests\TestFailOpaque.aks && exit /b 1
aks.exe run --strict-types tests\TestFailRefine.aks && exit /b 1
aks.exe run --strict-types tests\TestFailArray.aks && exit /b 1
aks.exe run --strict-types tests\TestFailEffect.aks && exit /b 1
ver > nul

echo Running package tests
call tests\PackagingTest.bat || exit /b 1

echo Running tools tests
call tests\TestTools.bat || exit /b 1

echo Running Editor Protocol tests (LSP and DAP)
tests\TestProtocols.exe || exit /b 1

del /f /q akscoredump.json 2>nul
del /f /q jittrace*.* 2>nul
del /f /q outtarget.c.map 2>nul
del /f /q *.svg 2>nul
del /f /q tests\*.svg 2>nul
rd /s /q LibraryTestDir 2>nul
rd /s /q templibrarydir 2>nul

echo All tests completed.
