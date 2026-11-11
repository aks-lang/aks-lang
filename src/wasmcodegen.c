// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// WebAssembly (WAT/WASM) code generator implementation.
#include "wasmcodegen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void CollectWasmLocals(Node *n, char **locals, int *count, int max) {
    if (!n || *count >= max) return;
    if ((n->type == NVARDECL || n->type == NCONSTDECL) && n->decl.name) {
        int exists = 0;
        for (int i = 0; i < *count; i++) {
            if (strcmp(locals[i], n->decl.name) == 0) { exists = 1; break; }
        }
        if (!exists && *count < max) {
            locals[(*count)++] = n->decl.name;
        }
    }
    if (n->type == NBLOCK || n->type == NPROGRAM) {
        for (int i = 0; i < n->block.count; i++) CollectWasmLocals(n->block.stmts[i], locals, count, max);
    } else if (n->type == NIF) {
        CollectWasmLocals(n->ifn.then, locals, count, max);
        CollectWasmLocals(n->ifn.els, locals, count, max);
    } else if (n->type == NFOR) {
        CollectWasmLocals(n->forn.init, locals, count, max);
        CollectWasmLocals(n->forn.body, locals, count, max);
    } else if (n->type == NWHILE) {
        CollectWasmLocals(n->whilen.body, locals, count, max);
    }
}

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
        fprintf(ctx->out, "    local.get $%s\n", node->sval ? node->sval : "v");
        break;
    case NVARDECL:
    case NCONSTDECL:
        if (node->decl.init) {
            EmitWasmNode(ctx, node->decl.init);
            fprintf(ctx->out, "    local.set $%s\n", node->decl.name);
        }
        break;
    case NASSIGN:
        if (node->assign.value) {
            EmitWasmNode(ctx, node->assign.value);
            if (node->assign.target && node->assign.target->type == NIDENT) {
                fprintf(ctx->out, "    local.set $%s\n", node->assign.target->sval ? node->assign.target->sval : "v");
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
        } else if (strcmp(node->binop.op, "%") == 0) {
            fprintf(ctx->out, "    i64.rem_s\n");
        } else if (strcmp(node->binop.op, "<") == 0) {
            fprintf(ctx->out, "    i64.lt_s\n");
        } else if (strcmp(node->binop.op, ">") == 0) {
            fprintf(ctx->out, "    i64.gt_s\n");
        } else if (strcmp(node->binop.op, "<=") == 0) {
            fprintf(ctx->out, "    i64.le_s\n");
        } else if (strcmp(node->binop.op, ">=") == 0) {
            fprintf(ctx->out, "    i64.ge_s\n");
        } else if (strcmp(node->binop.op, "==") == 0) {
            fprintf(ctx->out, "    i64.eq\n");
        } else if (strcmp(node->binop.op, "!=") == 0) {
            fprintf(ctx->out, "    i64.ne\n");
        } else if (strcmp(node->binop.op, "&") == 0) {
            fprintf(ctx->out, "    i64.and\n");
        } else if (strcmp(node->binop.op, "|") == 0) {
            fprintf(ctx->out, "    i64.or\n");
        } else if (strcmp(node->binop.op, "^") == 0) {
            fprintf(ctx->out, "    i64.xor\n");
        } else if (strcmp(node->binop.op, "<<") == 0) {
            fprintf(ctx->out, "    i64.shl\n");
        } else if (strcmp(node->binop.op, ">>") == 0) {
            fprintf(ctx->out, "    i64.shr_s\n");
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
        if (node->whilen.cond) {
            EmitWasmNode(ctx, node->whilen.cond);
            fprintf(ctx->out, "      i64.eqz\n      br_if 1\n");
        }
        if (node->whilen.body) EmitWasmNode(ctx, node->whilen.body);
        fprintf(ctx->out, "      br 0\n    end\n    end\n");
        break;
    case NFOR:
        if (node->forn.init) EmitWasmNode(ctx, node->forn.init);
        fprintf(ctx->out, "    block\n    loop\n");
        if (node->forn.cond) {
            EmitWasmNode(ctx, node->forn.cond);
            fprintf(ctx->out, "      i64.eqz\n      br_if 1\n");
        }
        if (node->forn.body) EmitWasmNode(ctx, node->forn.body);
        if (node->forn.post) {
            EmitWasmNode(ctx, node->forn.post);
            fprintf(ctx->out, "      drop\n");
        }
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
        if (node->single.expr) EmitWasmNode(ctx, node->single.expr);
        else if (node->binop.left) EmitWasmNode(ctx, node->binop.left);
        else fprintf(ctx->out, "    i64.const 0\n");
        fprintf(ctx->out, "    return\n");
        break;
    case NEXPRSTMT:
        if (node->single.expr) {
            EmitWasmNode(ctx, node->single.expr);
            fprintf(ctx->out, "    drop\n");
        }
        break;
    case NBLOCK:
    case NPROGRAM:
        for (int i = 0; i < node->block.count; i++) {
            EmitWasmNode(ctx, node->block.stmts[i]);
        }
        break;
    case NFNDECL: {
        fprintf(ctx->out, "  (func $%s (export \"%s\")", node->fn.name, node->fn.name);
        for (int i = 0; i < node->fn.pc; i++) {
            fprintf(ctx->out, " (param $%s i64)", node->fn.pnames[i]);
        }
        fprintf(ctx->out, " (result i64)\n");

        char *locals[64];
        int lcount = 0;
        CollectWasmLocals(node->fn.body, locals, &lcount, 64);
        for (int i = 0; i < lcount; i++) {
            int is_param = 0;
            for (int p = 0; p < node->fn.pc; p++) {
                if (strcmp(node->fn.pnames[p], locals[i]) == 0) { is_param = 1; break; }
            }
            if (!is_param) {
                fprintf(ctx->out, "    (local $%s i64)\n", locals[i]);
            }
        }
        if (node->fn.body) EmitWasmNode(ctx, node->fn.body);
        fprintf(ctx->out, "    i64.const 0\n  )\n");
        break;
    }
    default:
        break;
    }
}

static void CollectWasmCalls(Node *n, char **calls, int *count, int max) {
    if (!n || *count >= max) return;
    if (n->type == NCALL && n->call.callee) {
        const char *name = NULL;
        if (n->call.callee->type == NIDENT && n->call.callee->decl.name) {
            name = n->call.callee->decl.name;
        } else if (n->call.callee->type == NIDENT && n->call.callee->sval) {
            name = n->call.callee->sval;
        }
        if (name) {
            int exists = 0;
            for (int i = 0; i < *count; i++) {
                if (strcmp(calls[i], name) == 0) { exists = 1; break; }
            }
            if (!exists && *count < max) {
                calls[(*count)++] = (char*)name;
            }
        }
    }
    if (n->type == NBLOCK || n->type == NPROGRAM) {
        for (int i = 0; i < n->block.count; i++) CollectWasmCalls(n->block.stmts[i], calls, count, max);
    } else if (n->type == NIF) {
        CollectWasmCalls(n->ifn.cond, calls, count, max);
        CollectWasmCalls(n->ifn.then, calls, count, max);
        CollectWasmCalls(n->ifn.els, calls, count, max);
    } else if (n->type == NFOR) {
        CollectWasmCalls(n->forn.init, calls, count, max);
        CollectWasmCalls(n->forn.cond, calls, count, max);
        CollectWasmCalls(n->forn.post, calls, count, max);
        CollectWasmCalls(n->forn.body, calls, count, max);
    } else if (n->type == NWHILE) {
        CollectWasmCalls(n->whilen.cond, calls, count, max);
        CollectWasmCalls(n->whilen.body, calls, count, max);
    } else if (n->type == NBINOP) {
        CollectWasmCalls(n->binop.left, calls, count, max);
        CollectWasmCalls(n->binop.right, calls, count, max);
    } else if (n->type == NASSIGN) {
        CollectWasmCalls(n->assign.value, calls, count, max);
    } else if (n->type == NVARDECL || n->type == NCONSTDECL) {
        CollectWasmCalls(n->decl.init, calls, count, max);
    } else if (n->type == NCALL) {
        for (int i = 0; i < n->call.argc; i++) CollectWasmCalls(n->call.args[i], calls, count, max);
    } else if (n->type == NEXPRSTMT || n->type == NRETURN) {
        CollectWasmCalls(n->single.expr, calls, count, max);
    }
}


// Binary WASM Byte Buffer helpers
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} WasmBinBuf;

static void WbbInit(WasmBinBuf *b) {
    b->size = 0;
    b->capacity = 256;
    b->data = (uint8_t*)malloc(b->capacity);
}

static void WbbFree(WasmBinBuf *b) {
    if (b->data) free(b->data);
    b->data = NULL;
    b->size = b->capacity = 0;
}

static void WbbWriteByte(WasmBinBuf *b, uint8_t byte) {
    if (b->size >= b->capacity) {
        b->capacity = b->capacity ? b->capacity * 2 : 256;
        b->data = (uint8_t*)realloc(b->data, b->capacity);
    }
    b->data[b->size++] = byte;
}

static void WbbWriteBytes(WasmBinBuf *b, const uint8_t *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        WbbWriteByte(b, bytes[i]);
    }
}

static void WbbWriteU32(WasmBinBuf *b, uint32_t val) {
    do {
        uint8_t byte = (uint8_t)(val & 0x7F);
        val >>= 7;
        if (val != 0) byte |= 0x80;
        WbbWriteByte(b, byte);
    } while (val != 0);
}

static void WbbWriteI64(WasmBinBuf *b, int64_t val) {
    int more = 1;
    while (more) {
        uint8_t byte = (uint8_t)(val & 0x7F);
        val >>= 7;
        if ((val == 0 && (byte & 0x40) == 0) || (val == -1 && (byte & 0x40) != 0)) {
            more = 0;
        } else {
            byte |= 0x80;
        }
        WbbWriteByte(b, byte);
    }
}

static void WbbWriteI32(WasmBinBuf *b, int32_t val) {
    int more = 1;
    while (more) {
        uint8_t byte = (uint8_t)(val & 0x7F);
        val >>= 7;
        if ((val == 0 && (byte & 0x40) == 0) || (val == -1 && (byte & 0x40) != 0)) {
            more = 0;
        } else {
            byte |= 0x80;
        }
        WbbWriteByte(b, byte);
    }
}

static void WbbWriteF64(WasmBinBuf *b, double val) {
    uint8_t bytes[8];
    memcpy(bytes, &val, 8);
    WbbWriteBytes(b, bytes, 8);
}

static void WbbWriteString(WasmBinBuf *b, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    WbbWriteU32(b, len);
    WbbWriteBytes(b, (const uint8_t*)s, len);
}

static void WbbWriteSection(FILE *f, uint8_t sec_id, WasmBinBuf *payload) {
    fputc(sec_id, f);
    WasmBinBuf len_buf;
    WbbInit(&len_buf);
    WbbWriteU32(&len_buf, (uint32_t)payload->size);
    fwrite(len_buf.data, 1, len_buf.size, f);
    WbbFree(&len_buf);
    if (payload->size > 0) {
        fwrite(payload->data, 1, payload->size, f);
    }
}

typedef struct {
    char name[128];
    int pc;
    char pnames[32][64];
    char locals[64][64];
    int local_count;
    Node *body;
    int is_main;
} WasmBinFn;

static int WbbFindLocal(WasmBinFn *fn, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < fn->pc; i++) {
        if (strcmp(fn->pnames[i], name) == 0) return i;
    }
    for (int i = 0; i < fn->local_count; i++) {
        if (strcmp(fn->locals[i], name) == 0) return fn->pc + i;
    }
    return -1;
}

static int WbbFindFunc(WasmBinFn *fns, int fn_count, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < fn_count; i++) {
        if (strcmp(fns[i].name, name) == 0) return i;
    }
    return -1;
}

static void EmitWasmBinNode(WasmBinBuf *bb, Node *node, WasmBinFn *cur_fn, WasmBinFn *all_fns, int fn_count) {
    if (!node) return;
    switch (node->type) {
    case NINT:
        WbbWriteByte(bb, 0x42); // i64.const
        WbbWriteI64(bb, (int64_t)node->ival);
        break;
    case NFLOAT:
        WbbWriteByte(bb, 0x44); // f64.const
        WbbWriteF64(bb, node->fval);
        break;
    case NBOOL:
        WbbWriteByte(bb, 0x41); // i32.const
        WbbWriteI32(bb, node->ival ? 1 : 0);
        break;
    case NIDENT: {
        const char *id = node->sval ? node->sval : "v";
        int idx = WbbFindLocal(cur_fn, id);
        if (idx >= 0) {
            WbbWriteByte(bb, 0x20); // local.get
            WbbWriteU32(bb, (uint32_t)idx);
        } else {
            WbbWriteByte(bb, 0x42); // i64.const 0
            WbbWriteI64(bb, 0);
        }
        break;
    }
    case NVARDECL:
    case NCONSTDECL:
        if (node->decl.init) {
            EmitWasmBinNode(bb, node->decl.init, cur_fn, all_fns, fn_count);
            int idx = WbbFindLocal(cur_fn, node->decl.name);
            if (idx >= 0) {
                WbbWriteByte(bb, 0x21); // local.set
                WbbWriteU32(bb, (uint32_t)idx);
            }
        }
        break;
    case NASSIGN:
        if (node->assign.value) {
            EmitWasmBinNode(bb, node->assign.value, cur_fn, all_fns, fn_count);
            if (node->assign.target && node->assign.target->type == NIDENT) {
                const char *id = node->assign.target->sval ? node->assign.target->sval : "v";
                int idx = WbbFindLocal(cur_fn, id);
                if (idx >= 0) {
                    WbbWriteByte(bb, 0x21); // local.set
                    WbbWriteU32(bb, (uint32_t)idx);
                }
            }
        }
        break;
    case NBINOP:
        EmitWasmBinNode(bb, node->binop.left, cur_fn, all_fns, fn_count);
        EmitWasmBinNode(bb, node->binop.right, cur_fn, all_fns, fn_count);
        if (strcmp(node->binop.op, "+") == 0) WbbWriteByte(bb, 0x7C);
        else if (strcmp(node->binop.op, "-") == 0) WbbWriteByte(bb, 0x7D);
        else if (strcmp(node->binop.op, "*") == 0) WbbWriteByte(bb, 0x7E);
        else if (strcmp(node->binop.op, "/") == 0) WbbWriteByte(bb, 0x7F);
        else if (strcmp(node->binop.op, "%") == 0) WbbWriteByte(bb, 0x81);
        else if (strcmp(node->binop.op, "<") == 0) { WbbWriteByte(bb, 0x53); WbbWriteByte(bb, 0xAD); }
        else if (strcmp(node->binop.op, "<=") == 0) { WbbWriteByte(bb, 0x57); WbbWriteByte(bb, 0xAD); }
        else if (strcmp(node->binop.op, ">") == 0) { WbbWriteByte(bb, 0x55); WbbWriteByte(bb, 0xAD); }
        else if (strcmp(node->binop.op, ">=") == 0) { WbbWriteByte(bb, 0x59); WbbWriteByte(bb, 0xAD); }
        else if (strcmp(node->binop.op, "==") == 0) { WbbWriteByte(bb, 0x51); WbbWriteByte(bb, 0xAD); }
        else if (strcmp(node->binop.op, "!=") == 0) { WbbWriteByte(bb, 0x52); WbbWriteByte(bb, 0xAD); }
        else if (strcmp(node->binop.op, "&") == 0) WbbWriteByte(bb, 0x83);
        else if (strcmp(node->binop.op, "|") == 0) WbbWriteByte(bb, 0x84);
        else if (strcmp(node->binop.op, "^") == 0) WbbWriteByte(bb, 0x85);
        else if (strcmp(node->binop.op, "<<") == 0) WbbWriteByte(bb, 0x86);
        else if (strcmp(node->binop.op, ">>") == 0) WbbWriteByte(bb, 0x87);
        break;
    case NIF:
        EmitWasmBinNode(bb, node->ifn.cond, cur_fn, all_fns, fn_count);
        WbbWriteByte(bb, 0x42); WbbWriteI64(bb, 0); // i64.const 0
        WbbWriteByte(bb, 0x52); // i64.ne (produces i32 bool)
        WbbWriteByte(bb, 0x04); // if
        WbbWriteByte(bb, 0x7E); // result i64
        if (node->ifn.then) EmitWasmBinNode(bb, node->ifn.then, cur_fn, all_fns, fn_count);
        WbbWriteByte(bb, 0x05); // else
        if (node->ifn.els) {
            EmitWasmBinNode(bb, node->ifn.els, cur_fn, all_fns, fn_count);
        } else {
            WbbWriteByte(bb, 0x42); // i64.const 0
            WbbWriteI64(bb, 0);
        }
        WbbWriteByte(bb, 0x0B); // end
        break;
    case NWHILE:
        WbbWriteByte(bb, 0x02); WbbWriteByte(bb, 0x40); // block
        WbbWriteByte(bb, 0x03); WbbWriteByte(bb, 0x40); // loop
        if (node->whilen.cond) {
            EmitWasmBinNode(bb, node->whilen.cond, cur_fn, all_fns, fn_count);
            WbbWriteByte(bb, 0x50); // i64.eqz
            WbbWriteByte(bb, 0x0D); WbbWriteU32(bb, 1); // br_if 1
        }
        if (node->whilen.body) EmitWasmBinNode(bb, node->whilen.body, cur_fn, all_fns, fn_count);
        WbbWriteByte(bb, 0x0C); WbbWriteU32(bb, 0); // br 0
        WbbWriteByte(bb, 0x0B); // end loop
        WbbWriteByte(bb, 0x0B); // end block
        break;
    case NFOR:
        if (node->forn.init) EmitWasmBinNode(bb, node->forn.init, cur_fn, all_fns, fn_count);
        WbbWriteByte(bb, 0x02); WbbWriteByte(bb, 0x40); // block
        WbbWriteByte(bb, 0x03); WbbWriteByte(bb, 0x40); // loop
        if (node->forn.cond) {
            EmitWasmBinNode(bb, node->forn.cond, cur_fn, all_fns, fn_count);
            WbbWriteByte(bb, 0x50); // i64.eqz
            WbbWriteByte(bb, 0x0D); WbbWriteU32(bb, 1); // br_if 1
        }
        if (node->forn.body) EmitWasmBinNode(bb, node->forn.body, cur_fn, all_fns, fn_count);
        if (node->forn.post) {
            EmitWasmBinNode(bb, node->forn.post, cur_fn, all_fns, fn_count);
            if (node->forn.post->type != NASSIGN && node->forn.post->type != NVARDECL) {
                WbbWriteByte(bb, 0x1A); // drop
            }
        }
        WbbWriteByte(bb, 0x0C); WbbWriteU32(bb, 0); // br 0
        WbbWriteByte(bb, 0x0B); // end loop
        WbbWriteByte(bb, 0x0B); // end block
        break;
    case NCALL: {
        for (int i = 0; i < node->call.argc; i++) {
            EmitWasmBinNode(bb, node->call.args[i], cur_fn, all_fns, fn_count);
        }
        const char *target_name = NULL;
        if (node->call.callee && node->call.callee->type == NIDENT) {
            target_name = node->call.callee->decl.name ? node->call.callee->decl.name : node->call.callee->sval;
        }
        int fidx = WbbFindFunc(all_fns, fn_count, target_name);
        if (fidx >= 0) {
            WbbWriteByte(bb, 0x10); // call
            WbbWriteU32(bb, (uint32_t)fidx);
        } else {
            WbbWriteByte(bb, 0x42); // i64.const 0
            WbbWriteI64(bb, 0);
        }
        break;
    }
    case NRETURN:
        if (node->single.expr) EmitWasmBinNode(bb, node->single.expr, cur_fn, all_fns, fn_count);
        else if (node->binop.left) EmitWasmBinNode(bb, node->binop.left, cur_fn, all_fns, fn_count);
        else {
            WbbWriteByte(bb, 0x42); // i64.const 0
            WbbWriteI64(bb, 0);
        }
        WbbWriteByte(bb, 0x0F); // return
        break;
    case NEXPRSTMT:
        if (node->single.expr) {
            EmitWasmBinNode(bb, node->single.expr, cur_fn, all_fns, fn_count);
            if (node->single.expr->type != NASSIGN && node->single.expr->type != NVARDECL && node->single.expr->type != NCONSTDECL) {
                WbbWriteByte(bb, 0x1A); // drop
            }
        }
        break;
    case NBLOCK:
    case NPROGRAM:
        for (int i = 0; i < node->block.count; i++) {
            if (node->block.stmts[i]->type != NFNDECL) {
                EmitWasmBinNode(bb, node->block.stmts[i], cur_fn, all_fns, fn_count);
            }
        }
        break;
    default:
        break;
    }
}

static int EmitWasmBinaryModule(Compiler *comp, Node *ast_root, const char *out_filepath) {
    (void)comp;
    FILE *f = fopen(out_filepath, "wb");
    if (!f) return 0;

    // Magic number: \0asm
    const uint8_t magic[4] = { 0x00, 0x61, 0x73, 0x6D };
    fwrite(magic, 1, 4, f);
    // Version: 1
    const uint8_t ver[4] = { 0x01, 0x00, 0x00, 0x00 };
    fwrite(ver, 1, 4, f);

    // Collect functions
    static WasmBinFn fns[128];
    int fn_count = 0;
    memset(fns, 0, sizeof(fns));

    int has_main_stmts = 0;
    if (ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        for (int i = 0; i < ast_root->block.count; i++) {
            if (ast_root->block.stmts[i]->type != NFNDECL) {
                has_main_stmts++;
            }
        }
    }

    if (ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        for (int i = 0; i < ast_root->block.count && fn_count < 120; i++) {
            Node *st = ast_root->block.stmts[i];
            if (st->type == NFNDECL && st->fn.name) {
                WasmBinFn *wfn = &fns[fn_count++];
                snprintf(wfn->name, sizeof(wfn->name), "%s", st->fn.name);
                wfn->pc = st->fn.pc;
                for (int p = 0; p < st->fn.pc && p < 32; p++) {
                    snprintf(wfn->pnames[p], sizeof(wfn->pnames[p]), "%s", st->fn.pnames[p]);
                }
                char *raw_locals[64];
                int lcount = 0;
                CollectWasmLocals(st->fn.body, raw_locals, &lcount, 64);
                wfn->local_count = 0;
                for (int l = 0; l < lcount && wfn->local_count < 64; l++) {
                    int is_param = 0;
                    for (int p = 0; p < st->fn.pc; p++) {
                        if (strcmp(st->fn.pnames[p], raw_locals[l]) == 0) { is_param = 1; break; }
                    }
                    if (!is_param) {
                        snprintf(wfn->locals[wfn->local_count++], 64, "%s", raw_locals[l]);
                    }
                }
                wfn->body = st->fn.body;
                wfn->is_main = 0;
            }
        }
    }

    if (has_main_stmts && fn_count < 128) {
        WasmBinFn *wfn = &fns[fn_count++];
        snprintf(wfn->name, sizeof(wfn->name), "main");
        wfn->pc = 0;
        char *raw_locals[64];
        int lcount = 0;
        CollectWasmLocals(ast_root, raw_locals, &lcount, 64);
        wfn->local_count = 0;
        for (int l = 0; l < lcount && wfn->local_count < 64; l++) {
            snprintf(wfn->locals[wfn->local_count++], 64, "%s", raw_locals[l]);
        }
        wfn->body = ast_root;
        wfn->is_main = 1;
    }

    // Section 1: Type Section
    WasmBinBuf type_sec;
    WbbInit(&type_sec);
    WbbWriteU32(&type_sec, (uint32_t)fn_count);
    for (int i = 0; i < fn_count; i++) {
        WbbWriteByte(&type_sec, 0x60); // func type
        WbbWriteU32(&type_sec, (uint32_t)fns[i].pc);
        for (int p = 0; p < fns[i].pc; p++) {
            WbbWriteByte(&type_sec, 0x7E); // i64
        }
        WbbWriteU32(&type_sec, 1); // 1 result
        WbbWriteByte(&type_sec, 0x7E); // i64
    }
    WbbWriteSection(f, 1, &type_sec);
    WbbFree(&type_sec);

    // Section 3: Function Section
    WasmBinBuf func_sec;
    WbbInit(&func_sec);
    WbbWriteU32(&func_sec, (uint32_t)fn_count);
    for (int i = 0; i < fn_count; i++) {
        WbbWriteU32(&func_sec, (uint32_t)i); // type index i
    }
    WbbWriteSection(f, 3, &func_sec);
    WbbFree(&func_sec);

    // Section 5: Memory Section
    WasmBinBuf mem_sec;
    WbbInit(&mem_sec);
    WbbWriteU32(&mem_sec, 1); // 1 memory
    WbbWriteByte(&mem_sec, 0x00); // min only
    WbbWriteU32(&mem_sec, 1); // min 1 page
    WbbWriteSection(f, 5, &mem_sec);
    WbbFree(&mem_sec);

    // Section 7: Export Section
    WasmBinBuf exp_sec;
    WbbInit(&exp_sec);
    WbbWriteU32(&exp_sec, (uint32_t)(fn_count + 1)); // fns + memory
    for (int i = 0; i < fn_count; i++) {
        WbbWriteString(&exp_sec, fns[i].name);
        WbbWriteByte(&exp_sec, 0x00); // func
        WbbWriteU32(&exp_sec, (uint32_t)i);
    }
    WbbWriteString(&exp_sec, "memory");
    WbbWriteByte(&exp_sec, 0x02); // memory
    WbbWriteU32(&exp_sec, 0);
    WbbWriteSection(f, 7, &exp_sec);
    WbbFree(&exp_sec);

    // Section 10: Code Section
    WasmBinBuf code_sec;
    WbbInit(&code_sec);
    WbbWriteU32(&code_sec, (uint32_t)fn_count);
    for (int i = 0; i < fn_count; i++) {
        WasmBinBuf body;
        WbbInit(&body);
        if (fns[i].local_count > 0) {
            WbbWriteU32(&body, 1); // 1 local group
            WbbWriteU32(&body, (uint32_t)fns[i].local_count);
            WbbWriteByte(&body, 0x7E); // i64
        } else {
            WbbWriteU32(&body, 0);
        }
        if (fns[i].body) {
            EmitWasmBinNode(&body, fns[i].body, &fns[i], fns, fn_count);
        }
        // Ensure stack has return value
        WbbWriteByte(&body, 0x42); // i64.const 0
        WbbWriteI64(&body, 0);
        WbbWriteByte(&body, 0x0B); // end

        // Write func size + body into code_sec
        WbbWriteU32(&code_sec, (uint32_t)body.size);
        WbbWriteBytes(&code_sec, body.data, body.size);
        WbbFree(&body);
    }
    WbbWriteSection(f, 10, &code_sec);
    WbbFree(&code_sec);

    // Section 0: Custom Section for WebAssembly Component Model & WASI Preview 2 WIT Metadata
    WasmBinBuf custom_sec;
    WbbInit(&custom_sec);
    WbbWriteString(&custom_sec, "component-type:wit");
    char wit_desc[1024];
    snprintf(wit_desc, sizeof(wit_desc), "package aks:component@0.2.0;\nworld component-target {\n  import wasi:cli/environment@0.2.0;\n  import wasi:io/streams@0.2.0;\n");
    for (int i = 0; i < fn_count; i++) {
        if (fns[i].name[0] != '\0' && strcmp(fns[i].name, "memory") != 0) {
            char fnbuf[128];
            snprintf(fnbuf, sizeof(fnbuf), "  export %.96s: func() -> s64;\n", fns[i].name);
            strncat(wit_desc, fnbuf, sizeof(wit_desc) - strlen(wit_desc) - 1);
        }
    }
    strncat(wit_desc, "}\n", sizeof(wit_desc) - strlen(wit_desc) - 1);
    WbbWriteBytes(&custom_sec, (const uint8_t *)wit_desc, strlen(wit_desc));
    WbbWriteSection(f, 0, &custom_sec);
    WbbFree(&custom_sec);

    fclose(f);
    return 1;
}

int EmitWasmModule(Compiler *comp, Node *ast_root, const char *out_filepath) {
    if (!out_filepath) return 0;
    size_t outlen = strlen(out_filepath);
    if (outlen >= 5 && (strcmp(out_filepath + outlen - 5, ".wasm") == 0 ||
                        strcmp(out_filepath + outlen - 5, ".WASM") == 0)) {
        return EmitWasmBinaryModule(comp, ast_root, out_filepath);
    }
    FILE *f = fopen(out_filepath, "w");
    if (!f) return 0;

    WasmCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.comp = comp;
    ctx.out = f;

    fprintf(f, ";; WebAssembly Text Format (WAT) emitted by AKS WebAssembly Backend\n");
    fprintf(f, "(module\n");
    fprintf(f, "  (memory (export \"memory\") 1)\n");

    int has_main_stmts = 0;
    if (ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        for (int i = 0; i < ast_root->block.count; i++) {
            if (ast_root->block.stmts[i]->type != NFNDECL) {
                has_main_stmts++;
            }
        }
    }

    char *reachable_fns[128];
    int reachable_count = 0;

    if (has_main_stmts > 0 && ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        // Collect calls from top-level main statements
        for (int i = 0; i < ast_root->block.count; i++) {
            if (ast_root->block.stmts[i]->type != NFNDECL) {
                CollectWasmCalls(ast_root->block.stmts[i], reachable_fns, &reachable_count, 128);
            }
        }

        // Transitive closure: reach all functions called directly or indirectly
        int changed = 1;
        while (changed) {
            changed = 0;
            int prev_count = reachable_count;
            for (int i = 0; i < ast_root->block.count; i++) {
                Node *st = ast_root->block.stmts[i];
                if (st->type == NFNDECL && st->fn.name) {
                    int is_reach = 0;
                    for (int r = 0; r < reachable_count; r++) {
                        if (strcmp(reachable_fns[r], st->fn.name) == 0) {
                            is_reach = 1;
                            break;
                        }
                    }
                    if (is_reach) {
                        CollectWasmCalls(st->fn.body, reachable_fns, &reachable_count, 128);
                        if (reachable_count > prev_count) {
                            changed = 1;
                            prev_count = reachable_count;
                        }
                    }
                }
            }
        }
    }

    if (ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        for (int i = 0; i < ast_root->block.count; i++) {
            if (ast_root->block.stmts[i]->type == NFNDECL) {
                Node *fn = ast_root->block.stmts[i];
                if (has_main_stmts > 0 && fn->fn.name) {
                    int is_reach = 0;
                    if (strcmp(fn->fn.name, "main") == 0) is_reach = 1;
                    for (int r = 0; r < reachable_count; r++) {
                        if (strcmp(reachable_fns[r], fn->fn.name) == 0) {
                            is_reach = 1;
                            break;
                        }
                    }
                    if (!is_reach) {
                        // Tree-shaken: dead function eliminated
                        continue;
                    }
                }
                EmitWasmNode(&ctx, fn);
            }
        }
    }

    if (has_main_stmts) {
        fprintf(ctx.out, "  (func $main (export \"main\") (result i64)\n");
        char *locals[64];
        int lcount = 0;
        CollectWasmLocals(ast_root, locals, &lcount, 64);
        for (int i = 0; i < lcount; i++) {
            fprintf(ctx.out, "    (local $%s i64)\n", locals[i]);
        }
        for (int i = 0; i < ast_root->block.count; i++) {
            if (ast_root->block.stmts[i]->type != NFNDECL) {
                EmitWasmNode(&ctx, ast_root->block.stmts[i]);
            }
        }
        fprintf(ctx.out, "    i64.const 0\n  )\n");
    }

    fprintf(f, ")\n");
    fclose(f);
    return ctx.had_error ? 0 : 1;
}
