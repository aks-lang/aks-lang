#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "value.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "opcode.h"

#ifdef __wasm__
#define WASM_EXPORT __attribute__((visibility("default")))
#else
#define WASM_EXPORT
#endif

struct ciovec {
    const void *buf;
    size_t buf_len;
};

#ifdef __wasm__
__attribute__((import_module("wasi_snapshot_preview1")))
__attribute__((import_name("fd_write")))
int32_t WASIFDWrite(int32_t fd, const struct ciovec *iovs, int32_t iovs_len, void *nwritten);
#else
static int32_t WASIFDWrite(int32_t fd, const struct ciovec *iovs, int32_t iovs_len, void *nwritten) {
    size_t total = 0;
    for (int i = 0; i < iovs_len; i++) {
        total += fwrite(iovs[i].buf, 1, iovs[i].buf_len, fd == 1 ? stdout : stderr);
    }
    if (nwritten) *(size_t*)nwritten = total;
    return 0;
}
#endif

WASM_EXPORT void* WasmAlloc(size_t size) {
    return malloc(size);
}

WASM_EXPORT void WasmFree(void *ptr) {
    free(ptr);
}

extern const char *gProgramName;
extern const char *AKSPRELUDE;
extern int gImmutableByDefault;
extern int gTypeCheckingEnabled;
extern int gBorrowCheckerEnabled;

static int WasmRunSourceInternal(const char *source) {
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

WASM_EXPORT int WasmRunSource(const char *source) {
    if (!source) return -10;
    return WasmRunSourceInternal(source);
}
