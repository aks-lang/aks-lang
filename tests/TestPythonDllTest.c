#include <stdio.h>
#include <windows.h>

typedef int (*run_source_t)(const char*);

int main() {
    printf("Starting test program...\n"); fflush(stdout);
    HMODULE h = LoadLibraryA("aks.dll");
    if (!h) {
        printf("Failed to load aks.dll. Error code: %lu\n", GetLastError()); fflush(stdout);
        return 1;
    }
    printf("aks.dll loaded successfully!\n"); fflush(stdout);
    run_source_t run = (run_source_t)GetProcAddress(h, "AKSRunSourceOneshot");
    if (!run) {
        printf("Failed to find AKSRunSourceOneshot. Error code: %lu\n", GetLastError()); fflush(stdout);
        FreeLibrary(h);
        return 1;
    }
    printf("AKSRunSourceOneshot found! Calling now...\n"); fflush(stdout);
    int res = run("print(\"Hello from C/C++ DLL Interop!\");");
    printf("Result of running source: %d\n", res); fflush(stdout);
    FreeLibrary(h);
    printf("Program finished cleanly.\n"); fflush(stdout);
    return 0;
}
