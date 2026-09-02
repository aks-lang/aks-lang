@echo off

if not exist aks.exe (
    echo Building compiler
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
    cl.exe /O2 /Iinclude /Fosrc\ /Fe:aks.exe src\*.c user32.lib gdi32.lib shell32.lib >nul 2>nul
    if %errorlevel% neq 0 (
        echo Build failed!
        exit /b 1
    )
)

if "%~1" == "" (
    echo Usage: run.bat ^<file.aks^>
    exit /b 0
)

set "FILE_EXT=%~x1"

if /i "%FILE_EXT%" == ".aks" (
    aks.exe run "%~1"
) else if /i "%FILE_EXT%" == ".aksb" (
    aks.exe run-bin "%~1"
) else (
    aks.exe run "%~1"
)
