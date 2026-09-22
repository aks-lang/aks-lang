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

cd myapp
..\aks.exe pkg verify >nul || exit /b 1
cd ..

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

echo Testing local path package dependencies
.\aks.exe pkg init mypathlib >nul || exit /b 1
echo { "name": "mypathlib", "version": "1.0.0", "entrypoint": "src/main.aks", "dependencies": {} } > mypathlib\aks.json
echo print("Hello from mypathlib"); > mypathlib\src\main.aks

.\aks.exe pkg init mypathapp >nul || exit /b 1
echo { "name": "mypathapp", "version": "1.0.0", "entrypoint": "src/main.aks", "dependencies": {} } > mypathapp\aks.json
cd mypathapp
..\aks.exe pkg add path:..\mypathlib >nul || exit /b 1
..\aks.exe pkg install >nul || exit /b 1
..\aks.exe pkg verify >nul || exit /b 1
cd ..
rmdir /s /q mypathlib mypathapp 2>nul

echo Package manager tests passed
