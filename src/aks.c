// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "aks.h"
#include "aks_plat.h"
#include "vm.h"
#include "ast.h"
#include "compiler.h"
#include "parser.h"
#include "value.h"
#include "opcode.h"


const char *gProgramName = "aks_lib";

extern const char *AKSPRELUDE;

extern int gImmutableByDefault;
extern int gTypeCheckingEnabled;
extern int gBorrowCheckerEnabled;

AksVmT *AKSNewVM(void) {
    return (AksVmT*)VMNew();
}

void AKSFreeVM(AksVmT *vm) {
    VMFree((VM*)vm);
}

int AKSRunSource(AksVmT *vm, const char *source) {
    VM *aksVM = (VM*)vm;
    Compiler *aksCompiler = CompNew();
    int parseError = 0;
    
    int savedImmutable = gImmutableByDefault;
    int savedType = gTypeCheckingEnabled;
    int savedBorrow = gBorrowCheckerEnabled;
    gImmutableByDefault = 0;
    gTypeCheckingEnabled = 0;
    gBorrowCheckerEnabled = 0;

    Node *preludeAst = Parse(AKSPRELUDE, &parseError);
    if (parseError || !preludeAst) {
        if (preludeAst) NodeFree(preludeAst);
        CompFree(aksCompiler);
        gImmutableByDefault = savedImmutable;
        gTypeCheckingEnabled = savedType;
        gBorrowCheckerEnabled = savedBorrow;
        return -1;
    }
    if (!CompCompile(aksCompiler, preludeAst)) {
        NodeFree(preludeAst);
        CompFree(aksCompiler);
        gImmutableByDefault = savedImmutable;
        gTypeCheckingEnabled = savedType;
        gBorrowCheckerEnabled = savedBorrow;
        return -2;
    }
    NodeFree(preludeAst);
    if (aksCompiler->fns[0]->chunk.count > 0 && aksCompiler->fns[0]->chunk.code[aksCompiler->fns[0]->chunk.count - 1] == OPHALT) {
        aksCompiler->fns[0]->chunk.count--;
    }

    gImmutableByDefault = savedImmutable;
    gTypeCheckingEnabled = savedType;
    gBorrowCheckerEnabled = savedBorrow;

    Node *programast = Parse(source, &parseError);
    if (parseError || !programast) {
        if (programast) NodeFree(programast);
        CompFree(aksCompiler);
        return -3;
    }
    if (!CompCompile(aksCompiler, programast)) {
        NodeFree(programast);
        CompFree(aksCompiler);
        return -4;
    }
    NodeFree(programast);

    if (!VMLoadMem(aksVM, aksCompiler)) {
        CompFree(aksCompiler);
        return -5;
    }
    CompFree(aksCompiler);

    int res = VMRun(aksVM);
    return res ? 0 : 1;
}

int AKSLoadBinaryFile(AksVmT *vm, const char *path) {
    return VMLoad((VM*)vm, path);
}

int AKSLoadBinaryBuffer(AksVmT *vm, const uint8_t *buffer, size_t size) {
    extern int VMLoadBuffer(VM *vm, const uint8_t *buffer, size_t size);
    return VMLoadBuffer((VM*)vm, buffer, size);
}

int AKSRunVM(AksVmT *vm) {
    int res = VMRun((VM*)vm);
    return res ? 0 : 1;
}

// Thread-safe wrapper execution
struct OneShotArgs {
    const char *source;
    int result;
};

#ifdef _WIN32
#if defined(_MSC_VER)
static DWORD WINAPI OneShotThreadFunc(LPVOID lpParam) {
    struct OneShotArgs *args = (struct OneShotArgs *)lpParam;
    __try {
        VM *vm = VMNew();
        args->result = AKSRunSource((AksVmT*)vm, args->source);
        VMFree(vm);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        args->result = -99;
    }
    return 0;
}
#else
static DWORD WINAPI OneShotThreadFunc(LPVOID lpParam) {
    struct OneShotArgs *args = (struct OneShotArgs *)lpParam;
    VM *vm = VMNew();
    args->result = AKSRunSource((AksVmT*)vm, args->source);
    VMFree(vm);
    return 0;
}
#endif
#else
static void *OneShotThreadFunc(void *lpParam) {
    struct OneShotArgs *args = (struct OneShotArgs *)lpParam;
    VM *vm = VMNew();
    args->result = AKSRunSource((AksVmT*)vm, args->source);
    VMFree(vm);
    return NULL;
}
#endif

int AKSRunSourceOneshot(const char *source) {
    struct OneShotArgs args;
    args.source = source;
    args.result = -1;

    HANDLE hThread = CreateThread(NULL, 16 * 1024 * 1024, OneShotThreadFunc, &args, 0, NULL);
    if (!hThread) {
        return -5;
    }
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    return args.result;
}

void AKSRegisterNative(AksVmT *vm, const char *name, AksNativeFn func) {
    VM *aksVM = (VM*)vm;
    if (aksVM->registered_natives_count < 512) {
        char *name_copy = malloc(strlen(name) + 1);
        if (name_copy) {
            strcpy(name_copy, name);
            aksVM->registered_natives[aksVM->registered_natives_count].name = name_copy;
            aksVM->registered_natives[aksVM->registered_natives_count].func = (void*)func;
            aksVM->registered_natives_count++;
        }
    } else {
        fprintf(stderr, "AKSRegisterNative warning: maximum registered natives capacity reached (512). Symbol '%s' not registered.\n", name ? name : "");
    }
}

AksValType AKSValType(AKSValue v) {
    return (AksValType)ValKind(v);
}

AKSValue AKSValInt(int64_t i) {
    return ValInt(i);
}

int64_t AKSValToInt(AKSValue v) {
    return ValToInt(v);
}

AKSValue AKSValFloat(double f) {
    return ValFloat(f);
}

double AKSValToFloat(AKSValue v) {
    return ValToFloat(v);
}

AKSValue AKSValBool(int b) {
    return ValBool(b);
}

int AKSValToBool(AKSValue v) {
    return ValToBool(v);
}

AKSValue AKSValNull(void) {
    return ValNull();
}

AKSValue AKSValStr(const char *s) {
    return ValStr(s);
}

const char *AKSValToStr(AKSValue v) {
    if (ValKind(v) != VALSTR) return NULL;
    return ValToStrPtr(v)->data;
}

void AKSValFree(AKSValue *v) {
    ValFree(v);
}
