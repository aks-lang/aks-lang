// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// WebAssembly (WAT/WASM) code generator implementation.
#include "wasmcodegen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void EmitWasmNode(WasmCtx *ctx, Node *node) {
    if (!node) return;
    switch (node->type) {
    case NINT:
        fprintf(ctx->out, "    i64.const %lld\n", (long long)node->ival);
        break;
    case NFLOAT:
        fprintf(ctx->out, "    f64.const %g\n", node->fval);
        break;
    case NBOOL:
        fprintf(ctx->out, "    i32.const %d\n", node->ival ? 1 : 0);
        break;
    case NIDENT:
        fprintf(ctx->out, "    local.get $%s\n", node->decl.name ? node->decl.name : "v");
        break;
    case NVARDECL:
        if (node->decl.init) {
            EmitWasmNode(ctx, node->decl.init);
            fprintf(ctx->out, "    local.set $%s\n", node->decl.name);
        }
        break;
    case NASSIGN:
        if (node->assign.value) {
            EmitWasmNode(ctx, node->assign.value);
            if (node->assign.target && node->assign.target->type == NIDENT) {
                fprintf(ctx->out, "    local.set $%s\n", node->assign.target->decl.name);
            }
        }
        break;
    case NBINOP:
        EmitWasmNode(ctx, node->binop.left);
        EmitWasmNode(ctx, node->binop.right);
        if (strcmp(node->binop.op, "+") == 0) {
            fprintf(ctx->out, "    i64.add\n");
        } else if (strcmp(node->binop.op, "-") == 0) {
            fprintf(ctx->out, "    i64.sub\n");
        } else if (strcmp(node->binop.op, "*") == 0) {
            fprintf(ctx->out, "    i64.mul\n");
        } else if (strcmp(node->binop.op, "/") == 0) {
            fprintf(ctx->out, "    i64.div_s\n");
        } else if (strcmp(node->binop.op, "<") == 0) {
            fprintf(ctx->out, "    i64.lt_s\n");
        } else if (strcmp(node->binop.op, ">") == 0) {
            fprintf(ctx->out, "    i64.gt_s\n");
        } else if (strcmp(node->binop.op, "==") == 0) {
            fprintf(ctx->out, "    i64.eq\n");
        }
        break;
    case NIF:
        EmitWasmNode(ctx, node->ifn.cond);
        fprintf(ctx->out, "    if (result i64)\n");
        if (node->ifn.then) EmitWasmNode(ctx, node->ifn.then);
        if (node->ifn.els) {
            fprintf(ctx->out, "    else\n");
            EmitWasmNode(ctx, node->ifn.els);
        } else {
            fprintf(ctx->out, "      i64.const 0\n");
        }
        fprintf(ctx->out, "    end\n");
        break;
    case NWHILE:
        fprintf(ctx->out, "    block\n    loop\n");
        EmitWasmNode(ctx, node->whilen.cond);
        fprintf(ctx->out, "      i64.eqz\n      br_if 1\n");
        if (node->whilen.body) EmitWasmNode(ctx, node->whilen.body);
        fprintf(ctx->out, "      br 0\n    end\n    end\n");
        break;
    case NCALL:
        for (int i = 0; i < node->call.argc; i++) {
            EmitWasmNode(ctx, node->call.args[i]);
        }
        if (node->call.callee && node->call.callee->type == NIDENT) {
            fprintf(ctx->out, "    call $%s\n", node->call.callee->decl.name);
        }
        break;
    case NRETURN:
        if (node->binop.left) EmitWasmNode(ctx, node->binop.left);
        fprintf(ctx->out, "    return\n");
        break;
    case NBLOCK:
    case NPROGRAM:
        for (int i = 0; i < node->block.count; i++) {
            EmitWasmNode(ctx, node->block.stmts[i]);
        }
        break;
    case NFNDECL:
        fprintf(ctx->out, "  (func $%s (export \"%s\")", node->fn.name, node->fn.name);
        for (int i = 0; i < node->fn.pc; i++) {
            fprintf(ctx->out, " (param $%s i64)", node->fn.pnames[i]);
        }
        fprintf(ctx->out, " (result i64)\n");
        if (node->fn.body) EmitWasmNode(ctx, node->fn.body);
        fprintf(ctx->out, "    i64.const 0\n  )\n");
        break;
    default:
        break;
    }
}

int EmitWasmModule(Compiler *comp, Node *ast_root, const char *out_filepath) {
    FILE *f = fopen(out_filepath, "w");
    if (!f) return 0;

    WasmCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.comp = comp;
    ctx.out = f;

    fprintf(f, ";; WebAssembly Text Format (WAT) emitted by AKS WebAssembly Backend\n");
    fprintf(f, "(module\n");
    fprintf(f, "  (memory (export \"memory\") 1)\n");

    if (ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        for (int i = 0; i < ast_root->block.count; i++) {
            if (ast_root->block.stmts[i]->type == NFNDECL) {
                EmitWasmNode(&ctx, ast_root->block.stmts[i]);
            }
        }
    }

    fprintf(f, ")\n");
    fclose(f);
    return ctx.had_error ? 0 : 1;
}
