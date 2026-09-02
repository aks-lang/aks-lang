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
cl.exe /Od /Zi /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /arch:AVX2 /Iinclude /Fosrc\ /Fe:aks.exe src\main.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib dbghelp.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /DEBUG /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /arch:AVX2 /Iinclude /Fosrc\ /Fe:aksls.exe src\lsp.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /arch:AVX2 /Iinclude /Fosrc\ /Fe:aksfmt.exe src\aksfmt.c
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /arch:AVX2 /Iinclude /Fosrc\ /Fe:akslint.exe src\akslint.c
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /arch:AVX2 /DAKS_BUILD_DLL /Iinclude /Fosrc\ /Fe:aks.dll /LD src\aks.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fe:tests\TestEmbed.exe tests\TestEmbed.c aks.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fe:tests\TestPythonDllTest.exe tests\TestPythonDllTest.c /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fe:tests\TestProtocols.exe tests\TestProtocols.c ws2_32.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fosrc\ /Fe:tests\BorrowCheckerTests.exe tests\BorrowCheckerTests.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fosrc\ /Fe:tests\TypeSystemTests.exe tests\TypeSystemTests.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fosrc\ /Fe:tests\PerformanceTests.exe tests\PerformanceTests.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fosrc\ /Fe:tests\StdlibTests.exe tests\StdlibTests.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fosrc\ /Fe:tests\EcosystemTests.exe tests\EcosystemTests.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fosrc\ /Fe:tests\UniqueFeaturesTests.exe tests\UniqueFeaturesTests.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
cl.exe /Od /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fosrc\ /Fe:tests\StaticModeTests.exe tests\StaticModeTests.c src\prelude.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c src\typer.c src\staticcodegen.c src\wasmcodegen.c src\bench.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib cabinet.lib psapi.lib rpcrt4.lib advapi32.lib iphlpapi.lib winmm.lib dnsapi.lib pdh.lib winhttp.lib /link /STACK:16777216
if errorlevel 1 exit /b 1
