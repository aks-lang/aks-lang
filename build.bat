@echo off
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else (
        echo Warning: cl.exe not found in PATH and Visual Studio 2022/18 was not found in default locations.
    )
)
set CFLAGS=/O2 /Oi /Ot /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /arch:AVX2 /Iinclude /MP
set LIBS=user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib dbghelp.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib
set COMMON_SRCS=src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\nostdcodegen.c src\bench.c
set COMMON_OBJS=src\prelude.obj src\bind.obj src\dap.obj src\aot.obj src\ast.obj src\compiler.obj src\cross.obj src\crypto_custom.obj src\jitllvm.obj src\jitqbe.obj src\jitx64.obj src\learning.obj src\lexer.obj src\parser.obj src\pkg.obj src\value.obj src\vm.obj src\typer.obj src\staticcodegen.obj src\wasmcodegen.obj src\nostdcodegen.obj src\bench.obj

echo Compiling shared runtime objects...
cl.exe %CFLAGS% /c /Fosrc\ %COMMON_SRCS%
if errorlevel 1 exit /b 1

echo Linking aks.exe...
cl.exe %CFLAGS% /Zi /Fosrc\ /Fe:aks.exe src\main.c %COMMON_OBJS% %LIBS% /link /DEBUG /STACK:16777216
if errorlevel 1 exit /b 1

echo Linking aksls.exe...
cl.exe %CFLAGS% /Fosrc\ /Fe:aksls.exe src\lsp.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1

echo Building aksfmt.exe...
cl.exe %CFLAGS% /Fosrc\ /Fe:aksfmt.exe src\aksfmt.c
if errorlevel 1 exit /b 1

echo Building akslint.exe...
cl.exe %CFLAGS% /Fosrc\ /Fe:akslint.exe src\akslint.c
if errorlevel 1 exit /b 1

echo Linking aks.dll...
cl.exe %CFLAGS% /DAKS_BUILD_DLL /Fosrc\ /Fe:aks.dll /LD src\aks.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1

echo Building test helpers...
cl.exe %CFLAGS% /Fe:tests\TestEmbed.exe tests\TestEmbed.c aks.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /Fe:tests\TestPythonDllTest.exe tests\TestPythonDllTest.c /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /Fe:tests\TestProtocols.exe tests\TestProtocols.c ws2_32.lib /link /STACK:16777216
if errorlevel 1 exit /b 1

echo Linking test suites...
cl.exe %CFLAGS% /Fosrc\ /Fe:tests\BorrowCheckerTests.exe tests\BorrowCheckerTests.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /Fosrc\ /Fe:tests\TypeSystemTests.exe tests\TypeSystemTests.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /Fosrc\ /Fe:tests\PerformanceTests.exe tests\PerformanceTests.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /Fosrc\ /Fe:tests\StdlibTests.exe tests\StdlibTests.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /Fosrc\ /Fe:tests\EcosystemTests.exe tests\EcosystemTests.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /Fosrc\ /Fe:tests\UniqueFeaturesTests.exe tests\UniqueFeaturesTests.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /Fosrc\ /Fe:tests\StaticModeTests.exe tests\StaticModeTests.c %COMMON_OBJS% %LIBS% /link /STACK:16777216
if errorlevel 1 exit /b 1
echo Build completed successfully.

