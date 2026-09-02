// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef AKS_H
#define AKS_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#ifdef AKS_BUILD_DLL
#define AKS_API __declspec(dllexport)
#elif defined(AKS_STATIC)
#define AKS_API
#else
#define AKS_API __declspec(dllimport)
#endif
#else
#define AKS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VM AksVmT;
typedef struct VM AksVM;
typedef struct VM AksVmT;

// Creates a new VM instance.
AKS_API AksVmT *AKSNewVM(void);

// Frees the VM instance.
AKS_API void AKSFreeVM(AksVmT *vm);

// Run AKS source code string in the VM.
// Returns 0 on success, non-zero on error.
AKS_API int AKSRunSource(AksVmT *vm, const char *source);

// Load AKS bytecode binary from a file.
// Returns 1 on success, 0 on error.
AKS_API int AKSLoadBinaryFile(AksVmT *vm, const char *path);

// Load AKS bytecode binary from a memory buffer.
// Returns 1 on success, 0 on error.
AKS_API int AKSLoadBinaryBuffer(AksVmT *vm, const uint8_t *buffer, size_t size);

// Run the loaded VM bytecode.
// Returns 0 on success, non-zero on error.
AKS_API int AKSRunVM(AksVmT *vm);

// High-level wrapper function to run AKS source code in a new, separate VM thread.
// Ensures 16MB thread stack size requirement is met.
// Returns 0 on success, non-zero on error/crash.
AKS_API int AKSRunSourceOneshot(const char *source);

typedef uint64_t AKSValue;

typedef enum {
    AKS_VALFLOAT = 0,
    AKS_VALINT = 1,
    AKS_VALBOOL = 2,
    AKS_VALNULL = 3,
    AKS_VALSTR = 4,
    AKS_VALARR = 5,
    AKS_VALSTRUCT = 6
} AksValType;

typedef AksValType AksValType;

typedef AKSValue (*AksNativeFn)(AksVmT *vm, int argc, AKSValue *args);
typedef AksNativeFn AksNativeFn;

// Registers a native C/C++ function into the VM.
// Scripts can call this function by name.
AKS_API void AKSRegisterNative(AksVmT *vm, const char *name, AksNativeFn func);

// Value constructors and accessors
AKS_API AksValType AKSValType(AKSValue v);
AKS_API AKSValue AKSValInt(int64_t i);
AKS_API int64_t AKSValToInt(AKSValue v);
AKS_API AKSValue AKSValFloat(double f);
AKS_API double AKSValToFloat(AKSValue v);
AKS_API AKSValue AKSValBool(int b);
AKS_API int AKSValToBool(AKSValue v);
AKS_API AKSValue AKSValNull(void);
AKS_API AKSValue AKSValStr(const char *s);
AKS_API const char *AKSValToStr(AKSValue v);
AKS_API void AKSValFree(AKSValue *v);

#ifdef __cplusplus
}
#endif

#endif // Aks_h
