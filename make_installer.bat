@echo off
setlocal
echo Building AKS Native GUI Installer (aks-installer.exe)...

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
        echo Error: cl.exe not found in PATH and Visual Studio was not found.
        exit /b 1
    )
)

set CFLAGS=/O2 /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /nologo
set GUI_LIBS=user32.lib gdi32.lib comctl32.lib shell32.lib advapi32.lib ole32.lib gdiplus.lib

set RES_FILE=
if exist src\installer\installer.rc (
    rc.exe /nologo /fo src\installer\installer.res src\installer\installer.rc >nul 2>nul
    if exist src\installer\installer.res set RES_FILE=src\installer\installer.res
)

echo [1/4] Compiling native GUI installer engine (src\installer\gui_installer.cpp)...
cl.exe %CFLAGS% /Isrc\installer /Fe:gui_installer_stub.exe src\installer\gui_installer.cpp %RES_FILE% %GUI_LIBS% /link /SUBSYSTEM:WINDOWS
if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile GUI installer stub.
    exit /b 1
)

echo [2/4] Compiling package bundler (src\installer\pack_installer.c)...
cl.exe %CFLAGS% /Fe:pack_installer.exe src\installer\pack_installer.c
if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile packager.
    exit /b 1
)

set INSTALLER_OUT=aks-installer-v1.0.0.exe
if not "%~1"=="" set INSTALLER_OUT=%~1

echo [3/4] Packaging distribution components into %INSTALLER_OUT%...
pack_installer.exe gui_installer_stub.exe %INSTALLER_OUT%
if %errorlevel% neq 0 (
    echo [ERROR] Packaging failed.
    exit /b 1
)
copy /y %INSTALLER_OUT% aks-installer.exe >nul 2>nul

echo [4/4] Cleaning build artifacts...
del /f /q gui_installer_stub.exe gui_installer_stub.obj gui_installer_stub.exe.manifest pack_installer.exe pack_installer.obj src\installer\installer.res 2>nul

echo.
echo GUI Installer Build Complete: %INSTALLER_OUT%
echo Also available as: aks-installer.exe
echo Double-click '%INSTALLER_OUT%' to launch the Installer, or run '%INSTALLER_OUT% --silent' for headless installation.

