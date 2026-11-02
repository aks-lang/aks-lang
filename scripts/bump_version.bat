@echo off
if "%~1"=="" (
    echo Usage: scripts\bump_version.bat ^<new_version^>   e.g. scripts\bump_version.bat 1.1.0
    exit /b 1
)
python "%~dp0bump_version.py" %1

