// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// WebAssembly WAT/WASM code generator context definitions.
#ifndef WASMCODEGENH
#define WASMCODEGENH

#include <stdio.h>
#include <stdint.h>
#include "compiler.h"
#include "ast.h"

typedef struct WasmCtx {
    Compiler *comp;
    FILE     *out;
    int       had_error;
    int       func_count;
    char      cur_fn[128];
} WasmCtx;

int EmitWasmModule(Compiler *comp, Node *ast_root, const char *out_filepath);

#endif // Wasmcodegenh
