#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "value.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "aks_plat.h"
#include "opcode.h"


#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

extern const char *gProgramName;
extern const char *AKSPRELUDE;

extern int gImmutableByDefault;
extern int gTypeCheckingEnabled;
extern int gBorrowCheckerEnabled;

static int AKSRunSourceInternal(const char *source) {
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
        return -1;
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
        return -2;
    }
    if (!CompCompile(aksCompiler, programast)) {
        NodeFree(programast);
        CompFree(aksCompiler);
        return -3;
    }
    NodeFree(programast);

    VM *aksVM = VMNew();
    if (!VMLoadMem(aksVM, aksCompiler)) {
        CompFree(aksCompiler);
        VMFree(aksVM);
        return -4;
    }

    CompFree(aksCompiler);
    int res = VMRun(aksVM);
    VMFree(aksVM);
    return res ? 0 : 1;
}

struct ThreadArgs {
    const char *source;
    int result;
};

static DWORD WINAPI RunThreadFunc(LPVOID lpParam) {
    struct ThreadArgs *args = (struct ThreadArgs *)lpParam;
#if defined(_WIN32) && defined(_MSC_VER)
    __try {
        args->result = AKSRunSourceInternal(args->source);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("CRASH in VM thread. Exception code: 0x%08X\n", GetExceptionCode()); fflush(stdout);
        args->result = -99;
    }
#else
    args->result = AKSRunSourceInternal(args->source);
#endif
    return 0;
}

EXPORT int AKSRunSource(const char *source) {
    struct ThreadArgs args;
    args.source = source;
    args.result = -1;

    HANDLE hThread = CreateThread(NULL, 16 * 1024 * 1024, RunThreadFunc, &args, 0, NULL);
    if (!hThread) {
#ifdef _WIN32
        fprintf(stderr, "Error: CreateThread failed with error code %lu\n", GetLastError());
#else
        fprintf(stderr, "Error: CreateThread failed\n");
#endif
        return -5;
    }
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    return args.result;
}
