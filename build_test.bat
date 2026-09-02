@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cl.exe /O2 /arch:AVX2 /Iinclude /Fosrc\ /Fe:aks_test.exe src\*.c user32.lib gdi32.lib shell32.lib ws2_32.lib /link /STACK:16777216
