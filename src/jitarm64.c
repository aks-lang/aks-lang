// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "jitarm64.h"
#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// Allocates memory with write-only permissions
static void *AllocExecMem(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
#endif
}

// Transitions memory to execute-only and flushes instruction cache
static int MarkExecMem(void *ptr, size_t size) {
    if (!ptr || size == 0) return 0;
#ifdef _WIN32
    DWORD old_protect;
    int ok = VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &old_protect) ? 1 : 0;
    FlushInstructionCache(GetCurrentProcess(), ptr, size);
    return ok;
#else
    int ok = (mprotect(ptr, size, PROT_READ | PROT_EXEC) == 0) ? 1 : 0;
#if defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache((char *)ptr, (char *)ptr + size);
#endif
    return ok;
#endif
}

void JITArm64BufferInit(JITArm64Buffer *buf) {
    buf->cap = 1024;
    buf->size = 0;
    buf->code = (uint8_t *)malloc(buf->cap);
}

void JITArm64BufferFree(JITArm64Buffer *buf) {
    if (buf->code) {
        free(buf->code);
        buf->code = NULL;
    }
}

void JITArm64Emit32(JITArm64Buffer *buf, uint32_t inst) {
    if (buf->size + 4 > buf->cap) {
        buf->cap *= 2;
        buf->code = (uint8_t *)realloc(buf->code, buf->cap);
    }
    buf->code[buf->size++] = (uint8_t)(inst & 0xFF);
    buf->code[buf->size++] = (uint8_t)((inst >> 8) & 0xFF);
    buf->code[buf->size++] = (uint8_t)((inst >> 16) & 0xFF);
    buf->code[buf->size++] = (uint8_t)((inst >> 24) & 0xFF);
}

// Helpers called from ARM64 JIT
static AKSValue JITArm64HelperAdd(AKSValue a, AKSValue b) {
    if (ValIsInt(a) && ValIsInt(b)) {
        return ValAddIntInt(a, b);
    }
    if (ValIsFloat(a) && ValIsFloat(b)) {
        return ValAddFloatFloat(a, b);
    }
    return ValAdd(a, b);
}

static AKSValue JITArm64HelperSub(AKSValue a, AKSValue b) {
    if (ValIsInt(a) && ValIsInt(b)) {
        return ValSubIntInt(a, b);
    }
    if (ValIsFloat(a) && ValIsFloat(b)) {
        return ValSubFloatFloat(a, b);
    }
    return ValSub(a, b);
}

static AKSValue JITArm64HelperMul(AKSValue a, AKSValue b) {
    if (ValIsInt(a) && ValIsInt(b)) {
        return ValMulIntInt(a, b);
    }
    if (ValIsFloat(a) && ValIsFloat(b)) {
        return ValMulFloatFloat(a, b);
    }
    return ValMul(a, b);
}

static AKSValue JITArm64HelperDiv(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        int64_t div = ValToInt(b);
        if (div != 0) return ValInt(ValToInt(a) / div);
        return ValInt(0);
    }
    double divisor = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    if (divisor != 0.0) {
        double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
        return ValFloat(fa / divisor);
    }
    return ValFloat(0.0);
}

static AKSValue JITArm64HelperMod(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        int64_t div = ValToInt(b);
        if (div != 0) return ValInt(ValToInt(a) % div);
    }
    return ValInt(0);
}

static AKSValue JITArm64HelperLt(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValBool(ValToInt(a) < ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa < fb);
}

static AKSValue JITArm64HelperGt(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValBool(ValToInt(a) > ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa > fb);
}

static AKSValue JITArm64HelperEq(AKSValue a, AKSValue b) {
    if (ValKind(a) == ValKind(b)) {
        if (ValKind(a) == VALINT) return ValBool(ValToInt(a) == ValToInt(b));
        if (ValKind(a) == VALFLOAT) return ValBool(ValToFloat(a) == ValToFloat(b));
        if (ValKind(a) == VALBOOL) return ValBool(ValToBool(a) == ValToBool(b));
        if (ValKind(a) == VALNULL) return ValBool(1);
    }
    return ValBool(a == b);
}

// AArch64 instruction builders
static inline void EmitMovImm64(JITArm64Buffer *buf, int rd, uint64_t val) {
    // MOVZ Xd, imm16, LSL 0
    uint32_t inst = 0xD2800000 | ((val & 0xFFFF) << 5) | (rd & 31);
    JITArm64Emit32(buf, inst);

    for (int hw = 1; hw < 4; hw++) {
        uint16_t part = (uint16_t)((val >> (hw * 16)) & 0xFFFF);
        if (part != 0) {
            // MOVK Xd, imm16, LSL (hw * 16)
            uint32_t movk = 0xF2800000 | (hw << 21) | (part << 5) | (rd & 31);
            JITArm64Emit32(buf, movk);
        }
    }
}

static inline void EmitLdr64(JITArm64Buffer *buf, int rt, int rn, int imm) {
    // LDR Xt, [Xn, #imm] where imm % 8 == 0
    uint32_t inst = 0xF9400000 | (((imm / 8) & 0xFFF) << 10) | ((rn & 31) << 5) | (rt & 31);
    JITArm64Emit32(buf, inst);
}

static inline void EmitStr64(JITArm64Buffer *buf, int rt, int rn, int imm) {
    // STR Xt, [Xn, #imm] where imm % 8 == 0
    uint32_t inst = 0xF9000000 | (((imm / 8) & 0xFFF) << 10) | ((rn & 31) << 5) | (rt & 31);
    JITArm64Emit32(buf, inst);
}

static inline void EmitStr32(JITArm64Buffer *buf, int wt, int xn, int imm) {
    // STR Wt, [Xn, #imm]
    uint32_t inst = 0xB9000000 | (((imm / 4) & 0xFFF) << 10) | ((xn & 31) << 5) | (wt & 31);
    JITArm64Emit32(buf, inst);
}

static inline void EmitBlr(JITArm64Buffer *buf, int rn) {
    // BLR Xn
    uint32_t inst = 0xD63F0000 | ((rn & 31) << 5);
    JITArm64Emit32(buf, inst);
}

static int IsArm64SupportedOp(uint8_t op) {
    switch (op) {
        case OPPUSHINT:
        case OPPUSHFLOAT:
        case OPPUSHBOOL:
        case OPPUSHNULL:
        case OPLOAD:
        case OPSTORE:
        case OPADD:
        case OPSUB:
        case OPMUL:
        case OPDIV:
        case OPMOD:
        case OPLT:
        case OPGT:
        case OPEQ:
        case OPJUMP:
            return 1;
        default:
            return 0;
    }
}

void *CompileArm64Trace(VM *vm, FnProto *fn, int startoffset, TraceStep *trace, int stepcount, int startdepth) {
    if (!trace || stepcount <= 0) return NULL;
    for (int i = 0; i < stepcount; i++) {
        if (!IsArm64SupportedOp(trace[i].op)) return NULL;
    }

    JITArm64Buffer buf;
    JITArm64BufferInit(&buf);

    // Prologue:
    // STP X19, X20, [SP, #-48]!
    JITArm64Emit32(&buf, 0xA9BD53F3);
    // STP X21, X22, [SP, #16]
    JITArm64Emit32(&buf, 0xA9015BF5);
    // STP X29, X30, [SP, #32]
    JITArm64Emit32(&buf, 0xA9027BFD);
    // ADD X29, SP, #32
    JITArm64Emit32(&buf, 0x910083FD);

    // Copy incoming args:
    // X0 (stack) -> X19 : MOV X19, X0 -> ORR X19, XZR, X0
    JITArm64Emit32(&buf, 0xAA0003F3);
    // X1 (vtopptr) -> X20 : MOV X20, X1
    JITArm64Emit32(&buf, 0xAA0103F4);
    // X2 (locals) -> X21 : MOV X21, X2
    JITArm64Emit32(&buf, 0xAA0203F5);
    // X3 (vm) -> X22 : MOV X22, X3
    JITArm64Emit32(&buf, 0xAA0303F6);

    int vtop = startdepth;

    for (int i = 0; i < stepcount; i++) {
        TraceStep *step = &trace[i];
        switch (step->op) {
            case OPPUSHINT: {
                EmitMovImm64(&buf, 9, (uint64_t)ValInt(step->pushival));
                EmitStr64(&buf, 9, 19, vtop * 8);
                vtop++;
                break;
            }
            case OPPUSHFLOAT: {
                EmitMovImm64(&buf, 9, (uint64_t)ValFloat(step->pushfval));
                EmitStr64(&buf, 9, 19, vtop * 8);
                vtop++;
                break;
            }
            case OPPUSHBOOL: {
                EmitMovImm64(&buf, 9, (uint64_t)ValBool((int)step->pushival));
                EmitStr64(&buf, 9, 19, vtop * 8);
                vtop++;
                break;
            }
            case OPPUSHNULL: {
                EmitMovImm64(&buf, 9, (uint64_t)ValNull());
                EmitStr64(&buf, 9, 19, vtop * 8);
                vtop++;
                break;
            }
            case OPLOAD: {
                EmitLdr64(&buf, 9, 21, step->slot * 8);
                EmitStr64(&buf, 9, 19, vtop * 8);
                vtop++;
                break;
            }
            case OPSTORE: {
                vtop--;
                EmitLdr64(&buf, 9, 19, vtop * 8);
                EmitStr64(&buf, 9, 21, step->slot * 8);
                break;
            }
            case OPADD: {
                vtop--;
                EmitLdr64(&buf, 0, 19, (vtop - 1) * 8);
                EmitLdr64(&buf, 1, 19, vtop * 8);
                EmitMovImm64(&buf, 9, (uint64_t)(uintptr_t)JITArm64HelperAdd);
                EmitBlr(&buf, 9);
                EmitStr64(&buf, 0, 19, (vtop - 1) * 8);
                break;
            }
            case OPSUB: {
                vtop--;
                EmitLdr64(&buf, 0, 19, (vtop - 1) * 8);
                EmitLdr64(&buf, 1, 19, vtop * 8);
                EmitMovImm64(&buf, 9, (uint64_t)(uintptr_t)JITArm64HelperSub);
                EmitBlr(&buf, 9);
                EmitStr64(&buf, 0, 19, (vtop - 1) * 8);
                break;
            }
            case OPMUL: {
                vtop--;
                EmitLdr64(&buf, 0, 19, (vtop - 1) * 8);
                EmitLdr64(&buf, 1, 19, vtop * 8);
                EmitMovImm64(&buf, 9, (uint64_t)(uintptr_t)JITArm64HelperMul);
                EmitBlr(&buf, 9);
                EmitStr64(&buf, 0, 19, (vtop - 1) * 8);
                break;
            }
            case OPDIV: {
                vtop--;
                EmitLdr64(&buf, 0, 19, (vtop - 1) * 8);
                EmitLdr64(&buf, 1, 19, vtop * 8);
                EmitMovImm64(&buf, 9, (uint64_t)(uintptr_t)JITArm64HelperDiv);
                EmitBlr(&buf, 9);
                EmitStr64(&buf, 0, 19, (vtop - 1) * 8);
                break;
            }
            case OPMOD: {
                vtop--;
                EmitLdr64(&buf, 0, 19, (vtop - 1) * 8);
                EmitLdr64(&buf, 1, 19, vtop * 8);
                EmitMovImm64(&buf, 9, (uint64_t)(uintptr_t)JITArm64HelperMod);
                EmitBlr(&buf, 9);
                EmitStr64(&buf, 0, 19, (vtop - 1) * 8);
                break;
            }
            case OPLT: {
                vtop--;
                EmitLdr64(&buf, 0, 19, (vtop - 1) * 8);
                EmitLdr64(&buf, 1, 19, vtop * 8);
                EmitMovImm64(&buf, 9, (uint64_t)(uintptr_t)JITArm64HelperLt);
                EmitBlr(&buf, 9);
                EmitStr64(&buf, 0, 19, (vtop - 1) * 8);
                break;
            }
            case OPGT: {
                vtop--;
                EmitLdr64(&buf, 0, 19, (vtop - 1) * 8);
                EmitLdr64(&buf, 1, 19, vtop * 8);
                EmitMovImm64(&buf, 9, (uint64_t)(uintptr_t)JITArm64HelperGt);
                EmitBlr(&buf, 9);
                EmitStr64(&buf, 0, 19, (vtop - 1) * 8);
                break;
            }
            case OPEQ: {
                vtop--;
                EmitLdr64(&buf, 0, 19, (vtop - 1) * 8);
                EmitLdr64(&buf, 1, 19, vtop * 8);
                EmitMovImm64(&buf, 9, (uint64_t)(uintptr_t)JITArm64HelperEq);
                EmitBlr(&buf, 9);
                EmitStr64(&buf, 0, 19, (vtop - 1) * 8);
                break;
            }
            default:
                break;
        }
    }

    // Write back vtop to *vtopptr:
    // MOV W9, #vtop
    EmitMovImm64(&buf, 9, (uint64_t)vtop);
    EmitStr32(&buf, 9, 20, 0);

    // Return offset in W0/X0
    int lastoffset = startoffset;
    if (stepcount > 0) {
        lastoffset = trace[stepcount - 1].ipoffset + 1;
    }
    EmitMovImm64(&buf, 0, (uint64_t)lastoffset);

    // Epilogue:
    // LDP X29, X30, [SP, #32]
    JITArm64Emit32(&buf, 0xA9427BFD);
    // LDP X21, X22, [SP, #16]
    JITArm64Emit32(&buf, 0xA9415BF5);
    // LDP X19, X20, [SP], #48
    JITArm64Emit32(&buf, 0xA8C353F3);
    // RET
    JITArm64Emit32(&buf, 0xD65F03C0);

    uint8_t *exec = (uint8_t *)AllocExecMem(buf.size);
    if (exec) {
        memcpy(exec, buf.code, buf.size);
        MarkExecMem(exec, buf.size);
    }
    JITArm64BufferFree(&buf);
    return exec;
}

void *CompileArm64Method(VM *vm, FnProto *fn) {
    if (!vm || !fn) return NULL;
    return NULL;
}
