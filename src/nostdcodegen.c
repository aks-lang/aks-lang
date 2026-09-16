// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Freestanding no_std bare-metal and firmware C code generator.
#include "nostdcodegen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct NoStdCtx {
    Compiler *comp;
    FILE     *out;
    int       had_error;
    int       indent;
} NoStdCtx;

static void WriteIndent(NoStdCtx *ctx) {
    for (int i = 0; i < ctx->indent; i++) {
        fputs("    ", ctx->out);
    }
}

static void EmitNoStdExpr(NoStdCtx *ctx, Node *node);
static void EmitNoStdStmt(NoStdCtx *ctx, Node *node);

static void EmitNoStdExpr(NoStdCtx *ctx, Node *node) {
    if (!node) return;
    switch (node->type) {
    case NINT:
        fprintf(ctx->out, "%lldLL", (long long)node->ival);
        break;
    case NFLOAT:
        fprintf(ctx->out, "%g", node->fval);
        break;
    case NBOOL:
        fprintf(ctx->out, "%d", node->ival ? 1 : 0);
        break;
    case NIDENT:
        fprintf(ctx->out, "%s", node->decl.name ? node->decl.name : "v");
        break;
    case NBINOP:
        fprintf(ctx->out, "(");
        EmitNoStdExpr(ctx, node->binop.left);
        fprintf(ctx->out, " %s ", node->binop.op ? node->binop.op : "+");
        EmitNoStdExpr(ctx, node->binop.right);
        fprintf(ctx->out, ")");
        break;
    case NCALL: {
        const char *fnname = (node->call.callee && node->call.callee->type == NIDENT) ? node->call.callee->decl.name : "";
        if (strcmp(fnname, "MmioRead8") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "MMIO8(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ")");
        } else if (strcmp(fnname, "MmioWrite8") == 0 && node->call.argc >= 2) {
            fprintf(ctx->out, "(MMIO8(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ") = (uint8_t)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "MmioRead16") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "MMIO16(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ")");
        } else if (strcmp(fnname, "MmioWrite16") == 0 && node->call.argc >= 2) {
            fprintf(ctx->out, "(MMIO16(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ") = (uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "MmioRead32") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "MMIO32(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ")");
        } else if (strcmp(fnname, "MmioWrite32") == 0 && node->call.argc >= 2) {
            fprintf(ctx->out, "(MMIO32(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ") = (uint32_t)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "MmioRead64") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "MMIO64(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ")");
        } else if (strcmp(fnname, "MmioWrite64") == 0 && node->call.argc >= 2) {
            fprintf(ctx->out, "(MMIO64(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ") = (uint64_t)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "FreestandingMemSet") == 0 && node->call.argc >= 3) {
            fprintf(ctx->out, "nostd_memset((void*)(uintptr_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "), (int)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "), (size_t)(");
            EmitNoStdExpr(ctx, node->call.args[2]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "FreestandingMemCopy") == 0 && node->call.argc >= 3) {
            fprintf(ctx->out, "nostd_memcpy((void*)(uintptr_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "), (const void*)(uintptr_t)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "), (size_t)(");
            EmitNoStdExpr(ctx, node->call.args[2]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "FreestandingAlloc") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "((int64_t)(uintptr_t)nostd_alloc((size_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, ")))");
        } else if (strcmp(fnname, "FreestandingFree") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "nostd_free((void*)(uintptr_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "CpuNop") == 0) {
            fprintf(ctx->out, "cpu_nop()");
        } else if (strcmp(fnname, "CpuPause") == 0) {
            fprintf(ctx->out, "cpu_pause()");
        } else if (strcmp(fnname, "CpuHalt") == 0) {
            fprintf(ctx->out, "cpu_halt()");
        } else if (strcmp(fnname, "InPort8") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "in_port8((uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "InPort16") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "in_port16((uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "InPort32") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "in_port32((uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "OutPort8") == 0 && node->call.argc >= 2) {
            fprintf(ctx->out, "out_port8((uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "), (uint8_t)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "OutPort16") == 0 && node->call.argc >= 2) {
            fprintf(ctx->out, "out_port16((uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "), (uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "OutPort32") == 0 && node->call.argc >= 2) {
            fprintf(ctx->out, "out_port32((uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "), (uint32_t)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "CpuCli") == 0) {
            fprintf(ctx->out, "cpu_cli()");
        } else if (strcmp(fnname, "CpuSti") == 0) {
            fprintf(ctx->out, "cpu_sti()");
        } else if (strcmp(fnname, "CpuLidt") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "cpu_lidt((uintptr_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "CpuLgdt") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "cpu_lgdt((uintptr_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "CpuReadCr3") == 0) {
            fprintf(ctx->out, "cpu_read_cr3()");
        } else if (strcmp(fnname, "CpuWriteCr3") == 0 && node->call.argc >= 1) {
            fprintf(ctx->out, "cpu_write_cr3((uintptr_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "VgaPrintChar") == 0 && node->call.argc >= 4) {
            fprintf(ctx->out, "vga_print_char((char)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "), (int)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "), (int)(");
            EmitNoStdExpr(ctx, node->call.args[2]);
            fprintf(ctx->out, "), (uint8_t)(");
            EmitNoStdExpr(ctx, node->call.args[3]);
            fprintf(ctx->out, "))");
        } else if (strcmp(fnname, "SerialWrite") == 0 && node->call.argc >= 2) {
            fprintf(ctx->out, "serial_write((uint16_t)(");
            EmitNoStdExpr(ctx, node->call.args[0]);
            fprintf(ctx->out, "), (char)(");
            EmitNoStdExpr(ctx, node->call.args[1]);
            fprintf(ctx->out, "))");
        } else {
            fprintf(ctx->out, "%s(", fnname);
            for (int i = 0; i < node->call.argc; i++) {
                if (i > 0) fprintf(ctx->out, ", ");
                EmitNoStdExpr(ctx, node->call.args[i]);
            }
            fprintf(ctx->out, ")");
        }
        break;
    }
    case NASSIGN:
        if (node->assign.target && node->assign.target->type == NIDENT) {
            fprintf(ctx->out, "%s = ", node->assign.target->decl.name);
        }
        EmitNoStdExpr(ctx, node->assign.value);
        break;
    default:
        break;
    }
}

static void EmitNoStdStmt(NoStdCtx *ctx, Node *node) {
    if (!node) return;
    switch (node->type) {
    case NVARDECL:
    case NCONSTDECL:
        WriteIndent(ctx);
        fprintf(ctx->out, "int64_t %s", node->decl.name ? node->decl.name : "temp");
        if (node->decl.init) {
            fprintf(ctx->out, " = ");
            EmitNoStdExpr(ctx, node->decl.init);
        }
        fprintf(ctx->out, ";\n");
        break;
    case NASSIGN:
        WriteIndent(ctx);
        EmitNoStdExpr(ctx, node);
        fprintf(ctx->out, ";\n");
        break;
    case NEXPRSTMT:
        if (node->single.expr) {
            WriteIndent(ctx);
            EmitNoStdExpr(ctx, node->single.expr);
            fprintf(ctx->out, ";\n");
        }
        break;
    case NRETURN:
        WriteIndent(ctx);
        fprintf(ctx->out, "return");
        if (node->single.expr) {
            fprintf(ctx->out, " ");
            EmitNoStdExpr(ctx, node->single.expr);
        } else if (node->binop.left) {
            fprintf(ctx->out, " ");
            EmitNoStdExpr(ctx, node->binop.left);
        }
        fprintf(ctx->out, ";\n");
        break;
    case NIF:
        WriteIndent(ctx);
        fprintf(ctx->out, "if (");
        EmitNoStdExpr(ctx, node->ifn.cond);
        fprintf(ctx->out, ") {\n");
        ctx->indent++;
        EmitNoStdStmt(ctx, node->ifn.then);
        ctx->indent--;
        if (node->ifn.els) {
            WriteIndent(ctx);
            fprintf(ctx->out, "} else {\n");
            ctx->indent++;
            EmitNoStdStmt(ctx, node->ifn.els);
            ctx->indent--;
        }
        WriteIndent(ctx);
        fprintf(ctx->out, "}\n");
        break;
    case NWHILE:
        WriteIndent(ctx);
        fprintf(ctx->out, "while (");
        EmitNoStdExpr(ctx, node->whilen.cond);
        fprintf(ctx->out, ") {\n");
        ctx->indent++;
        EmitNoStdStmt(ctx, node->whilen.body);
        ctx->indent--;
        WriteIndent(ctx);
        fprintf(ctx->out, "}\n");
        break;
    case NFOR:
        WriteIndent(ctx);
        fprintf(ctx->out, "for (");
        if (node->forn.init && node->forn.init->type == NVARDECL) {
            fprintf(ctx->out, "int64_t %s = ", node->forn.init->decl.name);
            EmitNoStdExpr(ctx, node->forn.init->decl.init);
        }
        fprintf(ctx->out, "; ");
        if (node->forn.cond) EmitNoStdExpr(ctx, node->forn.cond);
        fprintf(ctx->out, "; ");
        if (node->forn.post) EmitNoStdExpr(ctx, node->forn.post);
        fprintf(ctx->out, ") {\n");
        ctx->indent++;
        EmitNoStdStmt(ctx, node->forn.body);
        ctx->indent--;
        WriteIndent(ctx);
        fprintf(ctx->out, "}\n");
        break;
    case NBLOCK:
    case NPROGRAM:
        for (int i = 0; i < node->block.count; i++) {
            EmitNoStdStmt(ctx, node->block.stmts[i]);
        }
        break;
    case NFNDECL:
        fprintf(ctx->out, "\nint64_t %s(", node->fn.name ? node->fn.name : "unnamed");
        for (int i = 0; i < node->fn.pc; i++) {
            if (i > 0) fprintf(ctx->out, ", ");
            fprintf(ctx->out, "int64_t %s", node->fn.pnames[i]);
        }
        fprintf(ctx->out, ") {\n");
        ctx->indent++;
        if (node->fn.body) EmitNoStdStmt(ctx, node->fn.body);
        WriteIndent(ctx);
        fprintf(ctx->out, "return 0;\n");
        ctx->indent--;
        fprintf(ctx->out, "}\n");
        break;
    default:
        break;
    }
}

int EmitNoStdCModule(Compiler *comp, Node *ast_root, const char *out_filepath) {
    FILE *f = fopen(out_filepath, "w");
    if (!f) return 0;

    NoStdCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.comp = comp;
    ctx.out = f;
    ctx.indent = 0;

    /* Freestanding header definitions */
    fprintf(f, "/* Freestanding bare metal and microcontroller firmware generated by AKS */\n");
    fprintf(f, "#include <stdint.h>\n");
    fprintf(f, "#include <stddef.h>\n\n");

    fprintf(f, "#define MMIO8(addr)  (*(volatile uint8_t*)(uintptr_t)(addr))\n");
    fprintf(f, "#define MMIO16(addr) (*(volatile uint16_t*)(uintptr_t)(addr))\n");
    fprintf(f, "#define MMIO32(addr) (*(volatile uint32_t*)(uintptr_t)(addr))\n");
    fprintf(f, "#define MMIO64(addr) (*(volatile uint64_t*)(uintptr_t)(addr))\n\n");

    fprintf(f, "static inline void nostd_memset(void *dest, int val, size_t count) {\n");
    fprintf(f, "    volatile uint8_t *d = (volatile uint8_t *)dest;\n");
    fprintf(f, "    for (size_t i = 0; i < count; i++) d[i] = (uint8_t)val;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline void nostd_memcpy(void *dest, const void *src, size_t count) {\n");
    fprintf(f, "    volatile uint8_t *d = (volatile uint8_t *)dest;\n");
    fprintf(f, "    const volatile uint8_t *s = (const volatile uint8_t *)src;\n");
    fprintf(f, "    for (size_t i = 0; i < count; i++) d[i] = s[i];\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static uint8_t s_nostd_pool[65536];\n");
    fprintf(f, "static size_t s_nostd_pool_idx = 0;\n");
    fprintf(f, "static inline void* nostd_alloc(size_t sz) {\n");
    fprintf(f, "    if (s_nostd_pool_idx + sz > sizeof(s_nostd_pool)) return 0;\n");
    fprintf(f, "    void *p = &s_nostd_pool[s_nostd_pool_idx];\n");
    fprintf(f, "    s_nostd_pool_idx = (s_nostd_pool_idx + sz + 7) & ~7;\n");
    fprintf(f, "    return p;\n");
    fprintf(f, "}\n\n");
    fprintf(f, "static inline void nostd_free(void *p) { (void)p; }\n\n");

    fprintf(f, "static inline void cpu_nop(void) {\n");
    fprintf(f, "#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))\n");
    fprintf(f, "    __nop();\n");
    fprintf(f, "#elif defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"nop\");\n");
    fprintf(f, "#endif\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline void cpu_pause(void) {\n");
    fprintf(f, "#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))\n");
    fprintf(f, "    _mm_pause();\n");
    fprintf(f, "#elif defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"pause\");\n");
    fprintf(f, "#endif\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline void cpu_halt(void) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"hlt\");\n");
    fprintf(f, "#elif defined(_MSC_VER)\n");
    fprintf(f, "    __halt();\n");
    fprintf(f, "#endif\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline uint8_t in_port8(uint16_t port) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    uint8_t ret;\n");
    fprintf(f, "    __asm__ __volatile__(\"inb %%1, %%0\" : \"=a\"(ret) : \"Nd\"(port));\n");
    fprintf(f, "    return ret;\n");
    fprintf(f, "#else\n    (void)port; return 0;\n#endif\n}\n\n");

    fprintf(f, "static inline uint16_t in_port16(uint16_t port) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    uint16_t ret;\n");
    fprintf(f, "    __asm__ __volatile__(\"inw %%1, %%0\" : \"=a\"(ret) : \"Nd\"(port));\n");
    fprintf(f, "    return ret;\n");
    fprintf(f, "#else\n    (void)port; return 0;\n#endif\n}\n\n");

    fprintf(f, "static inline uint32_t in_port32(uint16_t port) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    uint32_t ret;\n");
    fprintf(f, "    __asm__ __volatile__(\"inl %%1, %%0\" : \"=a\"(ret) : \"Nd\"(port));\n");
    fprintf(f, "    return ret;\n");
    fprintf(f, "#else\n    (void)port; return 0;\n#endif\n}\n\n");

    fprintf(f, "static inline void out_port8(uint16_t port, uint8_t val) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"outb %%0, %%1\" : : \"a\"(val), \"Nd\"(port));\n");
    fprintf(f, "#else\n    (void)port; (void)val;\n#endif\n}\n\n");

    fprintf(f, "static inline void out_port16(uint16_t port, uint16_t val) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"outw %%0, %%1\" : : \"a\"(val), \"Nd\"(port));\n");
    fprintf(f, "#else\n    (void)port; (void)val;\n#endif\n}\n\n");

    fprintf(f, "static inline void out_port32(uint16_t port, uint32_t val) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"outl %%0, %%1\" : : \"a\"(val), \"Nd\"(port));\n");
    fprintf(f, "#else\n    (void)port; (void)val;\n#endif\n}\n\n");

    fprintf(f, "static inline void cpu_cli(void) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"cli\" : : : \"memory\");\n");
    fprintf(f, "#endif\n}\n\n");

    fprintf(f, "static inline void cpu_sti(void) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"sti\" : : : \"memory\");\n");
    fprintf(f, "#endif\n}\n\n");

    fprintf(f, "static inline void cpu_lidt(uintptr_t addr) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"lidt (%%0)\" : : \"r\"(addr) : \"memory\");\n");
    fprintf(f, "#else\n    (void)addr;\n#endif\n}\n\n");

    fprintf(f, "static inline void cpu_lgdt(uintptr_t addr) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"lgdt (%%0)\" : : \"r\"(addr) : \"memory\");\n");
    fprintf(f, "#else\n    (void)addr;\n#endif\n}\n\n");

    fprintf(f, "static inline uintptr_t cpu_read_cr3(void) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    uintptr_t val;\n");
    fprintf(f, "    __asm__ __volatile__(\"mov %%%%cr3, %%0\" : \"=r\"(val));\n");
    fprintf(f, "    return val;\n");
    fprintf(f, "#else\n    return 0;\n#endif\n}\n\n");

    fprintf(f, "static inline void cpu_write_cr3(uintptr_t val) {\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "    __asm__ __volatile__(\"mov %%0, %%%%cr3\" : : \"r\"(val) : \"memory\");\n");
    fprintf(f, "#else\n    (void)val;\n#endif\n}\n\n");

    fprintf(f, "static inline void vga_print_char(char c, int x, int y, uint8_t color) {\n");
    fprintf(f, "    volatile uint16_t *vga = (volatile uint16_t*)0xB8000;\n");
    fprintf(f, "    if (x >= 0 && x < 80 && y >= 0 && y < 25) {\n");
    fprintf(f, "        vga[y * 80 + x] = (uint16_t)(uint8_t)c | ((uint16_t)color << 8);\n");
    fprintf(f, "    }\n}\n\n");

    fprintf(f, "static inline void serial_write(uint16_t port, char c) {\n");
    fprintf(f, "    out_port8(port, (uint8_t)c);\n");
    fprintf(f, "}\n\n");

    fprintf(f, "/* Multiboot2 Header for GRUB & QEMU direct kernel boot */\n");
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(f, "__attribute__((section(\".multiboot2\"), aligned(8)))\n");
    fprintf(f, "const struct {\n");
    fprintf(f, "    uint32_t magic; uint32_t architecture; uint32_t header_length; uint32_t checksum;\n");
    fprintf(f, "    uint16_t end_type; uint16_t end_flags; uint32_t end_size;\n");
    fprintf(f, "} multiboot2_header = { 0xE85250D6, 0, 24, -(0xE85250D6 + 0 + 24), 0, 0, 8 };\n");
    fprintf(f, "#endif\n\n");

    /* Forward declarations for functions */
    if (ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        for (int i = 0; i < ast_root->block.count; i++) {
            Node *n = ast_root->block.stmts[i];
            if (n && n->type == NFNDECL && n->fn.name) {
                fprintf(f, "int64_t %s(", n->fn.name);
                for (int p = 0; p < n->fn.pc; p++) {
                    if (p > 0) fprintf(f, ", ");
                    fprintf(f, "int64_t %s", n->fn.pnames[p]);
                }
                fprintf(f, ");\n");
            }
        }
    }

    /* Emit functions */
    int has_main_stmts = 0;
    if (ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        for (int i = 0; i < ast_root->block.count; i++) {
            Node *n = ast_root->block.stmts[i];
            if (n && n->type == NFNDECL) {
                EmitNoStdStmt(&ctx, n);
            } else {
                has_main_stmts++;
            }
        }
    }

    /* Emit entrypoint */
    fprintf(f, "\nint main(void) {\n");
    ctx.indent = 1;
    if (has_main_stmts && ast_root && (ast_root->type == NPROGRAM || ast_root->type == NBLOCK)) {
        for (int i = 0; i < ast_root->block.count; i++) {
            Node *n = ast_root->block.stmts[i];
            if (n && n->type != NFNDECL) {
                EmitNoStdStmt(&ctx, n);
            }
        }
    }
    fprintf(f, "    return 0;\n");
    fprintf(f, "}\n");

    fclose(f);
    return ctx.had_error ? 0 : 1;
}
