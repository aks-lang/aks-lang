@echo off
setlocal

echo Testing CLI tools...
echo print("toolstest ok"); > sample_temp.aks

.\aks.exe run sample_temp.aks || exit /b 1
.\aks.exe compile sample_temp.aks -o sample_temp.aksb || exit /b 1
.\aks.exe run-bin sample_temp.aksb || exit /b 1
.\aks.exe highlight sample_temp.aks sample_temp.html || exit /b 1

echo fn greet() { print("hi"); } > sample_doc.aks
.\aks.exe doc sample_doc.aks sample_doc.md || exit /b 1
.\aks.exe explain "undefined variable" || exit /b 1
.\aks.exe help || exit /b 1

del /f /q sample_temp.* sample_doc.* 2>nul
echo Tools tests passed
exit /b 0
