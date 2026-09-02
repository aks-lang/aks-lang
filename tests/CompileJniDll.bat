@echo off
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
)
cl.exe /LD /Fe:aks_jni.dll /Iinclude /DNO_MAIN src\aks_jni.c src\aks_python.c src\main.c src\bind.c src\dap.c src\aot.c src\ast.c src\compiler.c src\cross.c src\crypto_custom.c src\jitllvm.c src\jitqbe.c src\jitx64.c src\learning.c src\lexer.c src\parser.c src\pkg.c src\value.c src\vm.c user32.lib gdi32.lib shell32.lib ws2_32.lib bcrypt.lib crypt32.lib /link /STACK:16777216
