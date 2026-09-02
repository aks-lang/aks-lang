@echo on
setlocal

rmdir /s /q mylib myapp 2>nul

echo Running package manager tests

.\aks.exe pkg init mylib >nul
if not exist mylib\aks.json (
    echo Init failed
    exit /b 1
)

echo { "name": "mylib", "version": "1.0.0", "entrypoint": "src/main.aks", "dependencies": {} } > mylib\aks.json
echo print("Hello from mylib"); > mylib\src\main.aks

cd mylib
..\aks.exe pkg publish >nul || exit /b 1
cd ..

.\aks.exe pkg init myapp >nul || exit /b 1
echo { "name": "myapp", "version": "1.0.0", "entrypoint": "src/main.aks", "dependencies": {} } > myapp\aks.json

cd myapp
..\aks.exe pkg add mylib@^1.0.0 >nul || exit /b 1
cd ..

cd myapp
..\aks.exe pkg install >nul || exit /b 1
cd ..

if not exist myapp\aks.lock (
    echo Lockfile failed
    exit /b 1
)

echo import "mylib"; > myapp\src\main.aks
echo print("Hello from myapp"); >> myapp\src\main.aks

cd myapp
..\aks.exe pkg run || exit /b 1
cd ..

cd myapp
..\aks.exe pkg build --bundle >nul || exit /b 1
cd ..

if not exist myapp\dist\myapp.aks (
    echo Bundle failed
    exit /b 1
)

.\aks.exe run myapp\dist\myapp.aks || exit /b 1

.\aks.exe compile-aot myapp\dist\myapp.aks outtarget.c --target c-code >nul || exit /b 1

rmdir /s /q mylib myapp 2>nul
del /q outtarget.c 2>nul

echo Package manager tests passed
