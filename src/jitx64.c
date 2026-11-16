// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "jitx64.h"
#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)

typedef struct {
    void *helperadd;
    void *helpersub;
    void *helpermul;
    void *helperdiv;
    void *helpermod;
    void *helpershl;
    void *helpershr;
    void *helperbitand;
    void *helperbitor;
    void *helperbitxor;
    void *helperbitnot;
    void *helperinc;
    void *helperdec;
    void *helperlt;
    void *helpergt;
    void *helpereq;
    void *helperlte;
    void *helpergte;
    void *helperneq;
    void *helperistruthy;
    void *jitcleanupframe;
} JITHelperTable;

// Allocates memory for JIT code generation (W^X compliant)
static void *AllocExecMem(int size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
#endif
}

// Transitions JIT memory to executable (W^X enforcement)
static void MarkExecMem(void *ptr, int size) {
    if (!ptr || size <= 0) return;
#ifdef _WIN32
    DWORD oldProtect;
    VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &oldProtect);
#else
    mprotect(ptr, size, PROT_READ | PROT_EXEC);
#endif
}

void JITBufferInit(JITBuffer *buf) {
    buf->cap = 1024;
    buf->size = 0;
    buf->code = malloc(buf->cap);
}

void JITBufferFree(JITBuffer *buf) {
    if (buf->code) {
        free(buf->code);
        buf->code = NULL;
    }
}

void JITEmitByte(JITBuffer *buf, uint8_t b) {
    if (buf->size >= buf->cap) {
        buf->cap *= 2;
        buf->code = realloc(buf->code, buf->cap);
    }
    buf->code[buf->size++] = b;
}

void JITEmitBytes(JITBuffer *buf, const uint8_t *bytes, int count) {
    for (int i = 0; i < count; i++) {
        JITEmitByte(buf, bytes[i]);
    }
}

void JITEmitWord(JITBuffer *buf, uint16_t w) {
    JITEmitByte(buf, (uint8_t)(w & 0xFF));
    JITEmitByte(buf, (uint8_t)((w >> 8) & 0xFF));
}

void JITEmitDword(JITBuffer *buf, uint32_t dw) {
    JITEmitByte(buf, (uint8_t)(dw & 0xFF));
    JITEmitByte(buf, (uint8_t)((dw >> 8) & 0xFF));
    JITEmitByte(buf, (uint8_t)((dw >> 16) & 0xFF));
    JITEmitByte(buf, (uint8_t)((dw >> 24) & 0xFF));
}

void JITEmitQword(JITBuffer *buf, uint64_t qw) {
    JITEmitDword(buf, (uint32_t)(qw & 0xFFFFFFFF));
    JITEmitDword(buf, (uint32_t)((qw >> 32) & 0xFFFFFFFF));
}

// C Helper functions called from the JIT code
static AKSValue JITHelperAdd(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValAddIntInt(a, b);
    }
    if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
        return ValAddFloatFloat(a, b);
    }
    return ValAdd(a, b);
}

static AKSValue JITHelperSub(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValSubIntInt(a, b);
    }
    if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
        return ValSubFloatFloat(a, b);
    }
    return ValSub(a, b);
}

static AKSValue JITHelperMul(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValMulIntInt(a, b);
    }
    if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
        return ValMulFloatFloat(a, b);
    }
    return ValMul(a, b);
}

static AKSValue JITHelperDiv(AKSValue a, AKSValue b) {
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

static AKSValue JITHelperMod(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        int64_t div = ValToInt(b);
        if (div != 0) return ValInt(ValToInt(a) % div);
    }
    return ValInt(0);
}

static AKSValue JITHelperShl(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValInt(ValToInt(a) << ValToInt(b));
    }
    return ValInt(0);
}

static AKSValue JITHelperShr(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValInt(ValToInt(a) >> ValToInt(b));
    }
    return ValInt(0);
}

static AKSValue JITHelperBitAnd(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValInt(ValToInt(a) & ValToInt(b));
    }
    return ValInt(0);
}

static AKSValue JITHelperBitOr(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValInt(ValToInt(a) | ValToInt(b));
    }
    return ValInt(0);
}

static AKSValue JITHelperBitXor(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValInt(ValToInt(a) ^ ValToInt(b));
    }
    return ValInt(0);
}

static AKSValue JITHelperBitNot(AKSValue a) {
    if (ValKind(a) == VALINT) {
        return ValInt(~ValToInt(a));
    }
    return ValInt(0);
}

static AKSValue JITHelperInc(AKSValue a) {
    if (ValKind(a) == VALINT) {
        return ValInt(ValToInt(a) + 1);
    } else if (ValKind(a) == VALFLOAT) {
        return ValFloat(ValToFloat(a) + 1.0);
    }
    return a;
}

static AKSValue JITHelperDec(AKSValue a) {
    if (ValIsInt(a)) {
        return ValInt(ValToIntUnchecked(a) - 1);
    } else if (ValIsFloat(a)) {
        return ValFloat(ValToFloat(a) - 1.0);
    }
    return a;
}

static AKSValue JITHelperLt(AKSValue a, AKSValue b) {
    if (ValIsInt(a) && ValIsInt(b)) {
        return ValBool(ValToIntUnchecked(a) < ValToIntUnchecked(b));
    }
    double fa = ValIsFloat(a) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = ValIsFloat(b) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa < fb);
}

static AKSValue JITHelperGt(AKSValue a, AKSValue b) {
    if (ValIsInt(a) && ValIsInt(b)) {
        return ValBool(ValToIntUnchecked(a) > ValToIntUnchecked(b));
    }
    double fa = ValIsFloat(a) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = ValIsFloat(b) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa > fb);
}

static AKSValue JITHelperEq(AKSValue a, AKSValue b) {
    if (a == b) return ValBool(1);
    if (ValIsInt(a) && ValIsInt(b)) return ValBool(0);
    if (ValIsFloat(a) && ValIsFloat(b)) return ValBool(ValToFloat(a) == ValToFloat(b));
    return ValBool(ValKind(a) == ValKind(b) && a == b);
}

static AKSValue JITHelperLte(AKSValue a, AKSValue b) {
    if (ValIsInt(a) && ValIsInt(b)) {
        return ValBool(ValToIntUnchecked(a) <= ValToIntUnchecked(b));
    }
    double fa = ValIsFloat(a) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = ValIsFloat(b) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa <= fb);
}

static AKSValue JITHelperGte(AKSValue a, AKSValue b) {
    if (ValIsInt(a) && ValIsInt(b)) {
        return ValBool(ValToIntUnchecked(a) >= ValToIntUnchecked(b));
    }
    double fa = ValIsFloat(a) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = ValIsFloat(b) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa >= fb);
}

static AKSValue JITHelperNeq(AKSValue a, AKSValue b) {
    AKSValue eq = JITHelperEq(a, b);
    return ValBool(!ValToBool(eq));
}

static int JITHelperIsTruthy(AKSValue v) {
    uint64_t hi = v >> 48;
    if (hi == 0xFFF2ULL) return (int)(v & 1);
    if (hi == 0xFFF1ULL) return (((int64_t)(v << 16)) >> 16) != 0;
    if (hi == 0xFFF3ULL) return 0;
    if (v < VALFLOATLIMIT) return ValToFloat(v) != 0.0;
    return 1;
}

void JITCleanupFrame(AKSValue *stack, AKSValue *locals, int64_t vtop) {
    int localsidx = (int)(locals - stack);
    for (int i = localsidx; i < vtop; i++) {
        ValFree(&stack[i]);
    }
}

void *GetJITCompiledFn(VM *vm, FnProto *fn, int targetoffset) {
    for (int i = 0; i < vm->jitloopcount; i++) {
        if (vm->jitloops[i].fnid == fn->id && vm->jitloops[i].targetoffset == targetoffset) {
            return vm->jitloops[i].jitfn;
        }
    }
    return NULL;
}

// Emits loading a stack register value to a convention-specific target argument register
static void JITEmitLoadArg(JITBuffer *buf, int vreg, int argidx) {
    if (argidx == 0) {
#ifdef _WIN32
        // mov rcx, [r13 + vreg * 8]
        JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x8D}, 3);
#else
        // mov rdi, [r13 + vreg * 8]
        JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0xBD}, 3);
#endif
    } else {
#ifdef _WIN32
        // mov rdx, [r13 + vreg * 8]
        JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x95}, 3);
#else
        // mov rsi, [r13 + vreg * 8]
        JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0xB5}, 3);
#endif
    }
    // Emit 32-bit displacement
    JITEmitDword(buf, (uint32_t)(vreg * 8));
}

// Emits loading local variable value to stack register
static void JITEmitLoadLocal(JITBuffer *buf, int slot, int vreg) {
    // mov rax, [r13 + slot * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(slot * 8));
    // mov [r13 + vreg * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(vreg * 8));
}

// Emits storing stack register value to local variable
static void JITEmitStoreLocal(JITBuffer *buf, int slot, int vreg) {
    // mov rax, [r13 + vreg * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(vreg * 8));
    // mov [r13 + slot * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(slot * 8));
}

// Emits constant values
static void JITEmitPushConst(JITBuffer *buf, AKSValue val, int vreg) {
    // mov rax, val
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xB8}, 2);
    JITEmitQword(buf, val);
    // mov [r13 + vreg * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(vreg * 8));
}

// Emits call to helper function
static void JITEmitCall(JITBuffer *buf, void *fnptr) {
    // mov rax, fnptr
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xB8}, 2);
    JITEmitQword(buf, (uint64_t)fnptr);
    // call rax
    JITEmitBytes(buf, (const uint8_t[]){0xFF, 0xD0}, 2);
}

// Emits saving RAX to stack destination register
static void JITEmitSaveRax(JITBuffer *buf, int dstreg) {
    // mov [r13 + dstreg * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(dstreg * 8));
}

// Emits type guard check: bail out if tag does not match
static void JITEmitTypeGuard(JITBuffer *buf, int vreg, uint16_t expectedtag, int resumeoffset, int *patchptr, int *patchcount) {
    // mov rax, [r13 + vreg * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(vreg * 8));

    if (expectedtag == 0) {
        // shr rax, 48
        JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE8, 0x30}, 4);
        // cmp rax, 0xFFF1
        JITEmitBytes(buf, (const uint8_t[]){0x48, 0x3D}, 2);
        JITEmitDword(buf, 0xFFF1);
        // jae relative
        JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x83}, 2);
    } else {
        // shr rax, 48
        JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE8, 0x30}, 4);
        // cmp rax, expectedtag
        JITEmitBytes(buf, (const uint8_t[]){0x48, 0x3D}, 2);
        JITEmitDword(buf, (uint32_t)expectedtag);
        // jne relative
        JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    }
    patchptr[*patchcount] = buf->size;
    JITEmitDword(buf, 0);
    (*patchcount)++;
}


static int GetJITOpLen(uint8_t op) {
    switch (op) {
        case OPHALT: return 1;
        case OPPUSHINT: return 9;
        case OPPUSHFLOAT: return 9;
        case OPPUSHSTR: return 3;
        case OPPUSHBOOL: return 2;
        case OPPUSHNULL: return 1;
        case OPLOAD: return 3;
        case OPLOADGLOBAL: return 3;
        case OPSTORE: return 3;
        case OPSTOREGLOBAL: return 3;
        case OPDUP: return 1;
        case OPPOP: return 1;
        case OPPRINTLN: return 1;
        case OPINPUTSTR: return 3;
        case OPADD: return 1;
        case OPSUB: return 1;
        case OPMUL: return 1;
        case OPDIV: return 1;
        case OPMOD: return 1;
        case OPEQ: return 1;
        case OPNEQ: return 1;
        case OPLT: return 1;
        case OPGT: return 1;
        case OPLTE: return 1;
        case OPGTE: return 1;
        case OPAND: return 1;
        case OPOR: return 1;
        case OPNOT: return 1;
        case OPNEG: return 1;
        case OPJUMP: return 5;
        case OPJUMPIFFALSE: return 5;
        case OPCALL: return 4;
        case OPRETURN: return 1;
        case OPRETURNVAL: return 1;
        case OPNEWARR: return 3;
        case OPARRGET: return 1;
        case OPARRSET: return 1;
        case OPNEWSTRUCT: return 3;
        case OPGETFIELD: return 3;
        case OPSETFIELD: return 3;
        case OPINC: return 1;
        case OPDEC: return 1;
        case OPPUSHFN: return 3;
        case OPCALLDYNAMIC: return 2;
        case OPTRYPUSH: return 5;
        case OPTRYPOP: return 1;
        case OPTHROW: return 1;
        case OPCOALESCE: return 5;
        case OPRANGE: return 1;
        case OPJUMPIFNULL: return 5;
        case OPTAILCALL: return 4;
        case OPPRINTREPL: return 1;
        case OPSPAWN: return 4;
        case OPAWAIT: return 1;
        case OPSHL: return 1;
        case OPSHR: return 1;
        case OPBITAND: return 1;
        case OPBITOR: return 1;
        case OPBITXOR: return 1;
        case OPBITNOT: return 1;
        case OPAPPLY: return 1;
        case OPRESTARGS: return 2;
        case OPTYPEOF: return 1;
        default: return 1;
    }
}

static void JITEmitStoreAbsoluteVtop(JITBuffer *buf, int vtop) {
    // mov rax, r13
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0xC5}, 3);
    // sub rax, r12
    JITEmitBytes(buf, (const uint8_t[]){0x4C, 0x29, 0xE0}, 3);
    // sar rax, 3
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xF8, 0x03}, 4);
    // add rax, vtop
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x05}, 2);
    JITEmitDword(buf, (uint32_t)vtop);
    // mov [r15], eax
    JITEmitBytes(buf, (const uint8_t[]){0x41, 0x89, 0x07}, 3);
}

// Inlined fast integer addition with overflow guard and helper fallback
static void JITEmitInlineAdd(JITBuffer *buf, int regA, int regB) {
    // mov rax, [r13 + regA * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // mov rdx, [r13 + regB * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x95}, 3);
    JITEmitDword(buf, (uint32_t)(regB * 8));

    // Verify both operands are tagged integers:
    // mov rcx, rax; shr rcx, 48; cmp cx, 0xFFF1; jne fallback
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback1 = buf->size;
    JITEmitDword(buf, 0);

    // mov rcx, rdx; shr rcx, 48; cmp cx, 0xFFF1; jne fallback
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xD1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback2 = buf->size;
    JITEmitDword(buf, 0);

    // Sign-extend 48-bit integers to full 64-bit:
    // shl rax, 16; sar rax, 16; shl rdx, 16; sar rdx, 16
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10,
                                        0x48, 0xC1, 0xE2, 0x10, 0x48, 0xC1, 0xFA, 0x10}, 16);
    // add rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x01, 0xD0}, 3);

    // Ensure sum still fits in 48 bits signed:
    // mov rcx, rax; shl rcx, 16; sar rcx, 16; cmp rax, rcx; jne fallback
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE1, 0x10, 0x48, 0xC1, 0xF9, 0x10, 0x48, 0x39, 0xC8}, 14);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_ovf = buf->size;
    JITEmitDword(buf, 0);

    // Repack tagged integer with 0xFFF1 tag:
    // shl rax, 16; shr rax, 16
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xE8, 0x10}, 8);
    // mov rdx, 0xFFF1000000000000
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xBA}, 2);
    JITEmitQword(buf, 0xFFF1000000000000ULL);
    // or rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x09, 0xD0}, 3);
    // mov [r13 + regA * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // jmp done
    JITEmitByte(buf, 0xE9);
    int patch_done = buf->size;
    JITEmitDword(buf, 0);

    // Fallback: call helper function
    int fallback_target = buf->size;
    *(int*)(buf->code + patch_fallback1) = fallback_target - patch_fallback1 - 4;
    *(int*)(buf->code + patch_fallback2) = fallback_target - patch_fallback2 - 4;
    *(int*)(buf->code + patch_ovf) = fallback_target - patch_ovf - 4;
    JITEmitLoadArg(buf, regA, 0);
    JITEmitLoadArg(buf, regB, 1);
    JITEmitCall(buf, (void*)JITHelperAdd);
    JITEmitSaveRax(buf, regA);

    int done_target = buf->size;
    *(int*)(buf->code + patch_done) = done_target - patch_done - 4;
}

// Inlined fast integer subtraction with overflow guard and helper fallback
static void JITEmitInlineSub(JITBuffer *buf, int regA, int regB) {
    // mov rax, [r13 + regA * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // mov rdx, [r13 + regB * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x95}, 3);
    JITEmitDword(buf, (uint32_t)(regB * 8));

    // Verify both operands are tagged integers:
    // mov rcx, rax; shr rcx, 48; cmp cx, 0xFFF1; jne fallback
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback1 = buf->size;
    JITEmitDword(buf, 0);

    // mov rcx, rdx; shr rcx, 48; cmp cx, 0xFFF1; jne fallback
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xD1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback2 = buf->size;
    JITEmitDword(buf, 0);

    // Sign-extend 48-bit integers to full 64-bit:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10,
                                        0x48, 0xC1, 0xE2, 0x10, 0x48, 0xC1, 0xFA, 0x10}, 16);
    // sub rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x29, 0xD0}, 3);

    // Ensure difference fits in 48 bits signed:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE1, 0x10, 0x48, 0xC1, 0xF9, 0x10, 0x48, 0x39, 0xC8}, 14);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_ovf = buf->size;
    JITEmitDword(buf, 0);

    // Repack tagged integer with 0xFFF1 tag:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xE8, 0x10}, 8);
    // mov rdx, 0xFFF1000000000000
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xBA}, 2);
    JITEmitQword(buf, 0xFFF1000000000000ULL);
    // or rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x09, 0xD0}, 3);
    // mov [r13 + regA * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // jmp done
    JITEmitByte(buf, 0xE9);
    int patch_done = buf->size;
    JITEmitDword(buf, 0);

    // Fallback: call helper function
    int fallback_target = buf->size;
    *(int*)(buf->code + patch_fallback1) = fallback_target - patch_fallback1 - 4;
    *(int*)(buf->code + patch_fallback2) = fallback_target - patch_fallback2 - 4;
    *(int*)(buf->code + patch_ovf) = fallback_target - patch_ovf - 4;
    JITEmitLoadArg(buf, regA, 0);
    JITEmitLoadArg(buf, regB, 1);
    JITEmitCall(buf, (void*)JITHelperSub);
    JITEmitSaveRax(buf, regA);

    int done_target = buf->size;
    *(int*)(buf->code + patch_done) = done_target - patch_done - 4;
}

// Inlined fast integer multiplication with overflow guard and helper fallback
static void JITEmitInlineMul(JITBuffer *buf, int regA, int regB) {
    // mov rax, [r13 + regA * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // mov rdx, [r13 + regB * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x95}, 3);
    JITEmitDword(buf, (uint32_t)(regB * 8));

    // Verify both operands are tagged integers:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback1 = buf->size;
    JITEmitDword(buf, 0);

    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xD1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback2 = buf->size;
    JITEmitDword(buf, 0);

    // Sign-extend 48-bit integers to full 64-bit:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10,
                                        0x48, 0xC1, 0xE2, 0x10, 0x48, 0xC1, 0xFA, 0x10}, 16);
    // imul rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x0F, 0xAF, 0xC2}, 4);

    // Ensure product fits in 48 bits signed:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE1, 0x10, 0x48, 0xC1, 0xF9, 0x10, 0x48, 0x39, 0xC8}, 14);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_ovf = buf->size;
    JITEmitDword(buf, 0);

    // Repack tagged integer with 0xFFF1 tag:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xE8, 0x10}, 8);
    // mov rdx, 0xFFF1000000000000
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xBA}, 2);
    JITEmitQword(buf, 0xFFF1000000000000ULL);
    // or rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x09, 0xD0}, 3);
    // mov [r13 + regA * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // jmp done
    JITEmitByte(buf, 0xE9);
    int patch_done = buf->size;
    JITEmitDword(buf, 0);

    // Fallback: call helper function
    int fallback_target = buf->size;
    *(int*)(buf->code + patch_fallback1) = fallback_target - patch_fallback1 - 4;
    *(int*)(buf->code + patch_fallback2) = fallback_target - patch_fallback2 - 4;
    *(int*)(buf->code + patch_ovf) = fallback_target - patch_ovf - 4;
    JITEmitLoadArg(buf, regA, 0);
    JITEmitLoadArg(buf, regB, 1);
    JITEmitCall(buf, (void*)JITHelperMul);
    JITEmitSaveRax(buf, regA);

    int done_target = buf->size;
    *(int*)(buf->code + patch_done) = done_target - patch_done - 4;
}

// Inlined fast integer increment with overflow guard and helper fallback
static void JITEmitInlineInc(JITBuffer *buf, int vreg) {
    // mov rax, [r13 + vreg * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(vreg * 8));
    // mov rdx, rax; shr rdx, 48; cmp dx, 0xFFF1; jne fallback
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC2, 0x48, 0xC1, 0xEA, 0x30, 0x66, 0x81, 0xFA, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback = buf->size;
    JITEmitDword(buf, 0);

    // Sign-extend 48-bit integer:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10}, 8);
    // inc rax
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xFF, 0xC0}, 3);

    // Check overflow:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE1, 0x10, 0x48, 0xC1, 0xF9, 0x10, 0x48, 0x39, 0xC8}, 14);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_ovf = buf->size;
    JITEmitDword(buf, 0);

    // Repack tagged integer with 0xFFF1 tag:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xE8, 0x10}, 8);
    // mov rdx, 0xFFF1000000000000
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xBA}, 2);
    JITEmitQword(buf, 0xFFF1000000000000ULL);
    // or rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x09, 0xD0}, 3);
    // mov [r13 + vreg * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(vreg * 8));
    // jmp done
    JITEmitByte(buf, 0xE9);
    int patch_done = buf->size;
    JITEmitDword(buf, 0);

    // Fallback: call helper function
    int fallback_target = buf->size;
    *(int*)(buf->code + patch_fallback) = fallback_target - patch_fallback - 4;
    *(int*)(buf->code + patch_ovf) = fallback_target - patch_ovf - 4;
    JITEmitLoadArg(buf, vreg, 0);
    JITEmitCall(buf, (void*)JITHelperInc);
    JITEmitSaveRax(buf, vreg);

    int done_target = buf->size;
    *(int*)(buf->code + patch_done) = done_target - patch_done - 4;
}

// Inlined fast integer decrement with overflow guard and helper fallback
static void JITEmitInlineDec(JITBuffer *buf, int vreg) {
    // mov rax, [r13 + vreg * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(vreg * 8));
    // mov rdx, rax; shr rdx, 48; cmp dx, 0xFFF1; jne fallback
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC2, 0x48, 0xC1, 0xEA, 0x30, 0x66, 0x81, 0xFA, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback = buf->size;
    JITEmitDword(buf, 0);

    // Sign-extend 48-bit integer:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10}, 8);
    // dec rax
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xFF, 0xC8}, 3);

    // Check overflow:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE1, 0x10, 0x48, 0xC1, 0xF9, 0x10, 0x48, 0x39, 0xC8}, 14);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_ovf = buf->size;
    JITEmitDword(buf, 0);

    // Repack tagged integer with 0xFFF1 tag:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xE8, 0x10}, 8);
    // mov rdx, 0xFFF1000000000000
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xBA}, 2);
    JITEmitQword(buf, 0xFFF1000000000000ULL);
    // or rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x09, 0xD0}, 3);
    // mov [r13 + vreg * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(vreg * 8));
    // jmp done
    JITEmitByte(buf, 0xE9);
    int patch_done = buf->size;
    JITEmitDword(buf, 0);

    // Fallback: call helper function
    int fallback_target = buf->size;
    *(int*)(buf->code + patch_fallback) = fallback_target - patch_fallback - 4;
    *(int*)(buf->code + patch_ovf) = fallback_target - patch_ovf - 4;
    JITEmitLoadArg(buf, vreg, 0);
    JITEmitCall(buf, (void*)JITHelperDec);
    JITEmitSaveRax(buf, vreg);

    int done_target = buf->size;
    *(int*)(buf->code + patch_done) = done_target - patch_done - 4;
}

// Inlined fast integer comparisons with helper fallback
static void JITEmitInlineCmp(JITBuffer *buf, int regA, int regB, uint8_t setcc_byte, void *helper_fn) {
    // mov rax, [r13 + regA * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // mov rdx, [r13 + regB * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x95}, 3);
    JITEmitDword(buf, (uint32_t)(regB * 8));

    // Verify both operands are tagged integers:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback1 = buf->size;
    JITEmitDword(buf, 0);

    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xD1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback2 = buf->size;
    JITEmitDword(buf, 0);

    // Sign-extend 48-bit integers to full 64-bit:
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10,
                                        0x48, 0xC1, 0xE2, 0x10, 0x48, 0xC1, 0xFA, 0x10}, 16);
    // cmp rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x39, 0xD0}, 3);
    // setcc al
    JITEmitBytes(buf, (const uint8_t[]){0x0F, setcc_byte, 0xC0}, 3);
    // movzx rax, al
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x0F, 0xB6, 0xC0}, 4);
    // Tag boolean: mov rdx, 0xFFF2000000000000; or rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xBA}, 2);
    JITEmitQword(buf, 0xFFF2000000000000ULL);
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x09, 0xD0}, 3);
    // mov [r13 + regA * 8], rax
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // jmp done
    JITEmitByte(buf, 0xE9);
    int patch_done = buf->size;
    JITEmitDword(buf, 0);

    // Fallback: call helper function
    int fallback_target = buf->size;
    *(int*)(buf->code + patch_fallback1) = fallback_target - patch_fallback1 - 4;
    *(int*)(buf->code + patch_fallback2) = fallback_target - patch_fallback2 - 4;
    JITEmitLoadArg(buf, regA, 0);
    JITEmitLoadArg(buf, regB, 1);
    JITEmitCall(buf, helper_fn);
    JITEmitSaveRax(buf, regA);

    int done_target = buf->size;
    *(int*)(buf->code + patch_done) = done_target - patch_done - 4;
}

// Fused comparison and branch evaluation for fast native loop guards
static void JITEmitFusedCmpBranch(JITBuffer *buf, uint8_t cmp_op, TraceStep *jump_step, int regA, int regB, int *patches, int *patchcount, int vtop_after) {
    // mov rax, [r13 + regA * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
    JITEmitDword(buf, (uint32_t)(regA * 8));
    // mov rdx, [r13 + regB * 8]
    JITEmitBytes(buf, (const uint8_t[]){0x49, 0x8B, 0x95}, 3);
    JITEmitDword(buf, (uint32_t)(regB * 8));

    // Verify both operands are tagged integers (tag 0xFFF1 in bits 48-63)
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback1 = buf->size;
    JITEmitDword(buf, 0);

    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x89, 0xD1, 0x48, 0xC1, 0xE9, 0x30, 0x66, 0x81, 0xF9, 0xF1, 0xFF}, 12);
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fallback2 = buf->size;
    JITEmitDword(buf, 0);

    // Sign-extend 48-bit integers to full 64-bit
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10,
                                        0x48, 0xC1, 0xE2, 0x10, 0x48, 0xC1, 0xFA, 0x10}, 16);
    // cmp rax, rdx
    JITEmitBytes(buf, (const uint8_t[]){0x48, 0x39, 0xD0}, 3);

    // Pick near conditional jump opcode (0x0F, cc, disp32) to skip bailout when loop condition continues
    uint8_t cc = 0x85;
    switch (cmp_op) {
        case OPLT:  cc = 0x8C; break; // jl
        case OPLTE: cc = 0x8E; break; // jle
        case OPGT:  cc = 0x8F; break; // jg
        case OPGTE: cc = 0x8D; break; // jge
        case OPEQ:  cc = 0x84; break; // je
        case OPNEQ: cc = 0x85; break; // jne
        default: break;
    }

    JITEmitByte(buf, 0x0F);
    JITEmitByte(buf, cc);
    int patch_ok = buf->size;
    JITEmitDword(buf, 0);

    // Bailout path for fast integer comparison:
    int targetoffset = jump_step->jumpoffset < 0 ? (jump_step->ipoffset + 5 + jump_step->jumpoffset) : (jump_step->ipoffset + 5 + jump_step->jumpoffset);
    JITEmitStoreAbsoluteVtop(buf, vtop_after);
    JITEmitByte(buf, 0xB8);
    JITEmitDword(buf, (uint32_t)targetoffset);
    JITEmitByte(buf, 0xE9);
    patches[(*patchcount)++] = buf->size;
    JITEmitDword(buf, 0);

    // Target for label_ok:
    int ok_target = buf->size;
    *(int*)(buf->code + patch_ok) = ok_target - patch_ok - 4;

    // Skip past the fallback handler:
    JITEmitByte(buf, 0xE9);
    int patch_done = buf->size;
    JITEmitDword(buf, 0);

    // Fallback path:
    int fallback_target = buf->size;
    *(int*)(buf->code + patch_fallback1) = fallback_target - patch_fallback1 - 4;
    *(int*)(buf->code + patch_fallback2) = fallback_target - patch_fallback2 - 4;

    JITEmitLoadArg(buf, regA, 0);
    JITEmitLoadArg(buf, regB, 1);
    void *helper = NULL;
    switch (cmp_op) {
        case OPLT:  helper = (void*)JITHelperLt; break;
        case OPGT:  helper = (void*)JITHelperGt; break;
        case OPEQ:  helper = (void*)JITHelperEq; break;
        case OPNEQ: helper = (void*)JITHelperNeq; break;
        case OPLTE: helper = (void*)JITHelperLte; break;
        case OPGTE: helper = (void*)JITHelperGte; break;
        default: break;
    }
    JITEmitCall(buf, helper);
    // Test boolean truthiness: test al, 1
    JITEmitBytes(buf, (const uint8_t[]){0xA8, 0x01}, 2);
    // When truthy: jne (0x0F 0x85) skips bailout to continue loop
    JITEmitBytes(buf, (const uint8_t[]){0x0F, 0x85}, 2);
    int patch_fb_ok = buf->size;
    JITEmitDword(buf, 0);

    // Bailout path for fallback:
    JITEmitStoreAbsoluteVtop(buf, vtop_after);
    JITEmitByte(buf, 0xB8);
    JITEmitDword(buf, (uint32_t)targetoffset);
    JITEmitByte(buf, 0xE9);
    patches[(*patchcount)++] = buf->size;
    JITEmitDword(buf, 0);

    int fb_ok_target = buf->size;
    *(int*)(buf->code + patch_fb_ok) = fb_ok_target - patch_fb_ok - 4;

    int done_target = buf->size;
    *(int*)(buf->code + patch_done) = done_target - patch_done - 4;
}

static int IsX64SupportedOp(uint8_t op) {
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
        case OPSHL:
        case OPSHR:
        case OPBITAND:
        case OPBITOR:
        case OPBITXOR:
        case OPBITNOT:
        case OPINC:
        case OPDEC:
        case OPLT:
        case OPGT:
        case OPEQ:
        case OPNEQ:
        case OPLTE:
        case OPGTE:
        case OPDUP:
        case OPPOP:
        case OPJUMP:
        case OPJUMPIFFALSE:
        case OPRETURNVAL:
        case OPRETURN:
            return 1;
        default:
            return 0;
    }
}

void *CompileX64Trace(VM *vm, FnProto *fn, int startoffset, TraceStep *trace, int stepcount, int startdepth) {
    if (!trace || stepcount <= 0) return NULL;
    for (int i = 0; i < stepcount; i++) {
        if (!IsX64SupportedOp(trace[i].op)) return NULL;
    }

    JITBuffer buf;
    JITBufferInit(&buf);

    // Prologue
    // push rbp
    JITEmitByte(&buf, 0x55);
    // mov rbp, rsp
    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x89, 0xE5}, 3);
    // push registers to save
    JITEmitByte(&buf, 0x53); // push rbx
    JITEmitByte(&buf, 0x57); // push rdi
    JITEmitByte(&buf, 0x56); // push rsi
    JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x54}, 2); // push r12
    JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x55}, 2); // push r13
    JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x56}, 2); // push r14
    JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x57}, 2); // push r15
    // shadow space
    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x83, 0xEC, 0x28}, 4);

    // Map ABI registers to preserved registers:
    // r12: stack (first arg)
#ifdef _WIN32
    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x89, 0xCF}, 3); // mov rdi, rcx
#endif
    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0xFC}, 3); // mov r12, rdi (stack)

    // r15: stacktopptr (second arg)
#ifdef _WIN32
    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0xD7}, 3); // mov r15, rdx
#else
    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0xF7}, 3); // mov r15, rsi
#endif

    // r13: locals (third arg)
#ifdef _WIN32
    JITEmitBytes(&buf, (const uint8_t[]){0x4D, 0x89, 0xC5}, 3); // mov r13, r8
#else
    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0xD5}, 3); // mov r13, rdx
#endif

    int patches[2048];
    int patchcount = 0;

    int vtop = startdepth;

    // Linear scan register allocation:
    // Determine the top 2 hottest local slots accessed across the trace
    int slot_freq[256];
    memset(slot_freq, 0, sizeof(slot_freq));
    for (int i = 0; i < stepcount; i++) {
        if ((trace[i].op == OPLOAD || trace[i].op == OPSTORE) && trace[i].slot < 256) {
            slot_freq[trace[i].slot]++;
        }
    }
    int hot_slots[2] = { -1, -1 };
    int max_freq0 = 1;
    for (int s = 0; s < 256; s++) {
        if (slot_freq[s] > max_freq0) {
            max_freq0 = slot_freq[s];
            hot_slots[0] = s;
        }
    }
    int max_freq1 = 1;
    for (int s = 0; s < 256; s++) {
        if (s != hot_slots[0] && slot_freq[s] > max_freq1) {
            max_freq1 = slot_freq[s];
            hot_slots[1] = s;
        }
    }
    if (hot_slots[0] >= 0) {
        // Pre-load primary hot slot into rbx: mov rbx, [r13 + hot_slots[0] * 8]
        JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0x9D}, 3);
        JITEmitDword(&buf, (uint32_t)(hot_slots[0] * 8));
    }
    if (hot_slots[1] >= 0) {
        // Pre-load secondary hot slot into r14: mov r14, [r13 + hot_slots[1] * 8]
        JITEmitBytes(&buf, (const uint8_t[]){0x4D, 0x8B, 0xB5}, 3);
        JITEmitDword(&buf, (uint32_t)(hot_slots[1] * 8));
    }
    int loop_body_start = buf.size;
    
    // Process trace steps
    for (int i = 0; i < stepcount; i++) {
        TraceStep *step = &trace[i];

        // Fused loop guard with register slot: OPLOAD + OPLOAD/OPPUSHINT + Relational CMP + OPJUMPIFFALSE
        if (i + 3 < stepcount &&
            trace[i].op == OPLOAD &&
            (trace[i + 1].op == OPLOAD || trace[i + 1].op == OPPUSHINT) &&
            (trace[i + 2].op == OPLT || trace[i + 2].op == OPLTE || trace[i + 2].op == OPGT ||
             trace[i + 2].op == OPGTE || trace[i + 2].op == OPEQ || trace[i + 2].op == OPNEQ) &&
            trace[i + 3].op == OPJUMPIFFALSE) {

            int slotA = trace[i].slot;
            uint8_t cmp_op = trace[i + 2].op;
            TraceStep *jump_step = &trace[i + 3];

            if (slotA == hot_slots[0]) {
                if (trace[i + 1].op == OPLOAD) {
                    int slotB = trace[i + 1].slot;
                    if (slotB == hot_slots[1]) {
                        // Compare rbx with r14
                        JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x39, 0xF3}, 3);
                    } else {
                        // Compare rbx with local slot memory
                        JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x3B, 0x9D}, 3);
                        JITEmitDword(&buf, (uint32_t)(slotB * 8));
                    }
                } else {
                    // Compare rbx with tagged constant in rax
                    uint64_t tagged_val = ValInt(trace[i + 1].pushival);
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0xB8}, 2);
                    JITEmitQword(&buf, tagged_val);
                    // cmp rbx, rax
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x39, 0xC3}, 3);
                }

                // Branch to continue the loop body when the relational condition holds
                uint8_t stay_cc = 0x82; // jb
                switch (cmp_op) {
                    case OPLT:  stay_cc = 0x82; break; // jb
                    case OPLTE: stay_cc = 0x86; break; // jbe
                    case OPGT:  stay_cc = 0x87; break; // ja
                    case OPGTE: stay_cc = 0x83; break; // jae
                    case OPEQ:  stay_cc = 0x84; break; // je
                    case OPNEQ: stay_cc = 0x85; break; // jne
                }
                JITEmitByte(&buf, 0x0F);
                JITEmitByte(&buf, stay_cc);
                int patch_stay = buf.size;
                JITEmitDword(&buf, 0);

                // Bailout block executed when the loop termination condition is met
                int targetoffset = jump_step->jumpoffset < 0 ? (jump_step->ipoffset + 5 + jump_step->jumpoffset) : (jump_step->ipoffset + 5 + jump_step->jumpoffset);
                JITEmitStoreAbsoluteVtop(&buf, vtop);
                JITEmitByte(&buf, 0xB8);
                JITEmitDword(&buf, (uint32_t)targetoffset);
                JITEmitByte(&buf, 0xE9);
                patches[patchcount++] = buf.size;
                JITEmitDword(&buf, 0);

                int stay_target = buf.size;
                *(int*)(buf.code + patch_stay) = stay_target - patch_stay - 4;

                i += 3;
                continue;
            }
        }

        // Fused accumulator pattern: OPLOAD + OPLOAD + (OPADD | OPSUB) + OPSTORE (same target slot)
        if (i + 3 < stepcount &&
            trace[i].op == OPLOAD &&
            trace[i + 1].op == OPLOAD &&
            (trace[i + 2].op == OPADD || trace[i + 2].op == OPSUB) &&
            trace[i + 3].op == OPSTORE &&
            trace[i + 3].slot == trace[i].slot) {

            int slotA = trace[i].slot;
            int slotB = trace[i + 1].slot;
            uint8_t arith_op = trace[i + 2].op;

            if (slotA == hot_slots[1] && slotB == hot_slots[0]) {
                // Sign-extend 48-bit integer from rbx into rax
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x89, 0xD8, 0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10}, 11);
                if (arith_op == OPADD) {
                    // add r14, rax
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x01, 0xC6}, 3);
                } else {
                    // sub r14, rax
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x29, 0xC6}, 3);
                }
                i += 3;
                continue;
            } else if (slotA == hot_slots[0] && slotB == hot_slots[1]) {
                // Sign-extend 48-bit integer from r14 into rax
                JITEmitBytes(&buf, (const uint8_t[]){0x4C, 0x89, 0xF0, 0x48, 0xC1, 0xE0, 0x10, 0x48, 0xC1, 0xF8, 0x10}, 11);
                if (arith_op == OPADD) {
                    // add rbx, rax
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x01, 0xC3}, 3);
                } else {
                    // sub rbx, rax
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x29, 0xC3}, 3);
                }
                i += 3;
                continue;
            }
        }

        // Fused induction increment/decrement: OPLOAD + (OPINC | OPDEC) + OPSTORE (same target slot)
        if (i + 2 < stepcount &&
            trace[i].op == OPLOAD &&
            (trace[i + 1].op == OPINC || trace[i + 1].op == OPDEC) &&
            trace[i + 2].op == OPSTORE &&
            trace[i + 2].slot == trace[i].slot) {

            int slotA = trace[i].slot;
            uint8_t inc_op = trace[i + 1].op;

            if (slotA == hot_slots[0]) {
                if (inc_op == OPINC) {
                    // Direct 1-cycle integer increment on register rbx
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0xFF, 0xC3}, 3);
                } else {
                    // Direct 1-cycle integer decrement on register rbx
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0xFF, 0xCB}, 3);
                }
                i += 2;
                continue;
            } else if (slotA == hot_slots[1]) {
                if (inc_op == OPINC) {
                    // Direct 1-cycle integer increment on register r14
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0xFF, 0xC6}, 3);
                } else {
                    // Direct 1-cycle integer decrement on register r14
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0xFF, 0xCE}, 3);
                }
                i += 2;
                continue;
            }
        }

        // Fused in-place update with constant: OPLOAD + OPPUSHINT + (OPADD | OPSUB) + OPSTORE
        if (i + 3 < stepcount &&
            trace[i].op == OPLOAD &&
            trace[i + 1].op == OPPUSHINT &&
            (trace[i + 2].op == OPADD || trace[i + 2].op == OPSUB) &&
            trace[i + 3].op == OPSTORE &&
            trace[i + 3].slot == trace[i].slot) {

            int slotA = trace[i].slot;
            int64_t kval = trace[i + 1].pushival;
            uint8_t arith_op = trace[i + 2].op;

            if (slotA == hot_slots[0]) {
                if (arith_op == OPADD && kval == 1) {
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0xFF, 0xC3}, 3); // inc rbx
                    i += 3;
                    continue;
                } else if (arith_op == OPSUB && kval == 1) {
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0xFF, 0xCB}, 3); // dec rbx
                    i += 3;
                    continue;
                } else if (arith_op == OPADD && kval > 0 && kval <= 0x7FFFFFFF) {
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x81, 0xC3}, 3); // add rbx, imm32
                    JITEmitDword(&buf, (uint32_t)kval);
                    i += 3;
                    continue;
                } else if (arith_op == OPSUB && kval > 0 && kval <= 0x7FFFFFFF) {
                    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x81, 0xEB}, 3); // sub rbx, imm32
                    JITEmitDword(&buf, (uint32_t)kval);
                    i += 3;
                    continue;
                }
            } else if (slotA == hot_slots[1]) {
                if (arith_op == OPADD && kval == 1) {
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0xFF, 0xC6}, 3); // inc r14
                    i += 3;
                    continue;
                } else if (arith_op == OPSUB && kval == 1) {
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0xFF, 0xCE}, 3); // dec r14
                    i += 3;
                    continue;
                } else if (arith_op == OPADD && kval > 0 && kval <= 0x7FFFFFFF) {
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x81, 0xC6}, 3); // add r14, imm32
                    JITEmitDword(&buf, (uint32_t)kval);
                    i += 3;
                    continue;
                } else if (arith_op == OPSUB && kval > 0 && kval <= 0x7FFFFFFF) {
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x81, 0xEE}, 3); // sub r14, imm32
                    JITEmitDword(&buf, (uint32_t)kval);
                    i += 3;
                    continue;
                }
            }
        }

        // Peephole comparison-branch fusion
        if ((step->op == OPLT || step->op == OPLTE || step->op == OPGT || 
             step->op == OPGTE || step->op == OPEQ || step->op == OPNEQ) &&
            (i + 1 < stepcount) && (trace[i + 1].op == OPJUMPIFFALSE)) {
            vtop -= 2;
            JITEmitFusedCmpBranch(&buf, step->op, &trace[i + 1], vtop, vtop + 1, patches, &patchcount, vtop);
            i++; // Consume the jump instruction
            continue;
        }

        switch (step->op) {
            case OPPUSHINT:
                JITEmitPushConst(&buf, ValInt(step->pushival), vtop);
                vtop++;
                break;
            case OPPUSHFLOAT:
                JITEmitPushConst(&buf, ValFloat(step->pushfval), vtop);
                vtop++;
                break;
            case OPPUSHBOOL:
                JITEmitPushConst(&buf, ValBool((int)step->pushival), vtop);
                vtop++;
                break;
            case OPPUSHNULL:
                JITEmitPushConst(&buf, ValNull(), vtop);
                vtop++;
                break;
            case OPLOAD:
                if (step->slot == hot_slots[0]) {
                    // mov [r13 + vtop * 8], rbx
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0x9D}, 3);
                    JITEmitDword(&buf, (uint32_t)(vtop * 8));
                } else if (step->slot == hot_slots[1]) {
                    // mov [r13 + vtop * 8], r14
                    JITEmitBytes(&buf, (const uint8_t[]){0x4D, 0x89, 0xB5}, 3);
                    JITEmitDword(&buf, (uint32_t)(vtop * 8));
                } else {
                    JITEmitLoadLocal(&buf, step->slot, vtop);
                }
                vtop++;
                break;
            case OPSTORE:
                vtop--;
                if (step->slot == hot_slots[0]) {
                    // mov rbx, [r13 + vtop * 8]
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0x9D}, 3);
                    JITEmitDword(&buf, (uint32_t)(vtop * 8));
                } else if (step->slot == hot_slots[1]) {
                    // mov r14, [r13 + vtop * 8]
                    JITEmitBytes(&buf, (const uint8_t[]){0x4D, 0x8B, 0xB5}, 3);
                    JITEmitDword(&buf, (uint32_t)(vtop * 8));
                } else {
                    JITEmitStoreLocal(&buf, step->slot, vtop);
                }
                break;
            case OPADD:
                vtop--;
                JITEmitInlineAdd(&buf, vtop - 1, vtop);
                break;
            case OPSUB:
                vtop--;
                JITEmitInlineSub(&buf, vtop - 1, vtop);
                break;
            case OPMUL:
                vtop--;
                JITEmitInlineMul(&buf, vtop - 1, vtop);
                break;
            case OPDIV:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperDiv);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPMOD:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperMod);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPSHL:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperShl);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPSHR:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperShr);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPBITAND:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperBitAnd);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPBITOR:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperBitOr);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPBITXOR:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperBitXor);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPBITNOT:
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitCall(&buf, (void*)JITHelperBitNot);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPINC:
                JITEmitInlineInc(&buf, vtop - 1);
                break;
            case OPDEC:
                JITEmitInlineDec(&buf, vtop - 1);
                break;
            case OPLT:
                vtop--;
                JITEmitInlineCmp(&buf, vtop - 1, vtop, 0x9C, (void*)JITHelperLt);
                break;
            case OPGT:
                vtop--;
                JITEmitInlineCmp(&buf, vtop - 1, vtop, 0x9F, (void*)JITHelperGt);
                break;
            case OPEQ:
                vtop--;
                JITEmitInlineCmp(&buf, vtop - 1, vtop, 0x94, (void*)JITHelperEq);
                break;
            case OPNEQ:
                vtop--;
                JITEmitInlineCmp(&buf, vtop - 1, vtop, 0x95, (void*)JITHelperNeq);
                break;
            case OPLTE:
                vtop--;
                JITEmitInlineCmp(&buf, vtop - 1, vtop, 0x9E, (void*)JITHelperLte);
                break;
            case OPGTE:
                vtop--;
                JITEmitInlineCmp(&buf, vtop - 1, vtop, 0x9D, (void*)JITHelperGte);
                break;
            case OPDUP:
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
                JITEmitDword(&buf, (uint32_t)((vtop - 1) * 8));
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0x85}, 3);
                JITEmitDword(&buf, (uint32_t)(vtop * 8));
                vtop++;
                break;
            case OPPOP:
                vtop--;
                break;
            case OPJUMP: {
                int targetoffset = step->jumpoffset < 0 ? (step->ipoffset + 5 + step->jumpoffset) : (step->ipoffset + 5);
                if (step->jumpoffset < 0 && targetoffset == startoffset && loop_body_start >= 0) {
                    int next_ip = buf.size + 5;
                    int rel = loop_body_start - next_ip;
                    JITEmitByte(&buf, 0xE9);
                    JITEmitDword(&buf, (uint32_t)rel);
                } else {
                    JITEmitStoreAbsoluteVtop(&buf, vtop);
                    JITEmitByte(&buf, 0xB8);
                    JITEmitDword(&buf, (uint32_t)targetoffset);
                    JITEmitByte(&buf, 0xE9);
                    patches[patchcount++] = buf.size;
                    JITEmitDword(&buf, 0);
                }
                break;
            }
            case OPJUMPIFFALSE: {
                vtop--;
                // Fast-path boolean verification
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
                JITEmitDword(&buf, (uint32_t)(vtop * 8));
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x89, 0xC2, 0x48, 0xC1, 0xEA, 0x30, 0x66, 0x81, 0xFA, 0xF2, 0xFF}, 12);
                JITEmitBytes(&buf, (const uint8_t[]){0x0F, 0x85}, 2);
                int patch_nonbool = buf.size;
                JITEmitDword(&buf, 0);

                // Pure boolean test: test al, 1; setnz al; movzx eax, al
                JITEmitBytes(&buf, (const uint8_t[]){0xA8, 0x01, 0x0F, 0x95, 0xC0, 0x0F, 0xB6, 0xC0}, 8);
                JITEmitBytes(&buf, (const uint8_t[]){0xEB}, 1);
                int patch_after = buf.size;
                JITEmitByte(&buf, 0);

                // Fallback for non-boolean types:
                int nonbool_target = buf.size;
                *(int*)(buf.code + patch_nonbool) = nonbool_target - patch_nonbool - 4;
                JITEmitLoadArg(&buf, vtop, 0);
                JITEmitCall(&buf, (void*)JITHelperIsTruthy);

                int after_target = buf.size;
                buf.code[patch_after] = (uint8_t)(after_target - patch_after - 1);

                JITEmitBytes(&buf, (const uint8_t[]){0x83, 0xF8, 0x00}, 3);
                
                int targetoffset = (step->guardoutcome == 1) ? (step->ipoffset + 5) : (step->ipoffset + 5 + step->jumpoffset);
                
                if (step->guardoutcome == 1) {
                    JITEmitByte(&buf, 0x74); // je label_ok
                    int patchpos = buf.size;
                    JITEmitByte(&buf, 0);
                    
                    // Bailout block
                    JITEmitStoreAbsoluteVtop(&buf, vtop);
                    JITEmitByte(&buf, 0xB8);
                    JITEmitDword(&buf, (uint32_t)targetoffset);
                    JITEmitByte(&buf, 0xE9);
                    patches[patchcount++] = buf.size;
                    JITEmitDword(&buf, 0);
                    buf.code[patchpos] = (uint8_t)(buf.size - patchpos - 1);
                } else {
                    JITEmitByte(&buf, 0x75); // jne label_ok
                    int patchpos = buf.size;
                    JITEmitByte(&buf, 0);
                    
                    // Bailout block
                    JITEmitStoreAbsoluteVtop(&buf, vtop);
                    JITEmitByte(&buf, 0xB8);
                    JITEmitDword(&buf, (uint32_t)targetoffset);
                    JITEmitByte(&buf, 0xE9);
                    patches[patchcount++] = buf.size;
                    JITEmitDword(&buf, 0);
                    buf.code[patchpos] = (uint8_t)(buf.size - patchpos - 1);
                }
                break;
            }
            case OPRETURNVAL: {
                vtop--;
                // Copy return value to slotsbase - 1
                // mov rax, [r13 + vtop * 8]
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0x85}, 3);
                JITEmitDword(&buf, (uint32_t)(vtop * 8));

                // mov rcx, r13
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xCD}, 3);
                // sub rcx, 8
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x83, 0xE9, 0x08}, 4);
                // mov [rcx], rax
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x89, 0x01}, 3);

                // Call JITCleanupFrame(stack=r12, locals=r13, vtop=vtop)
#ifdef _WIN32
                // mov rcx, r12
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xCC}, 3);
                // mov rdx, r13
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xD5}, 3);
                // mov r8, vtop
                JITEmitBytes(&buf, (const uint8_t[]){0x41, 0xB8}, 2);
                JITEmitDword(&buf, (uint32_t)vtop);
#else
                // mov rdi, r12
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xFC}, 3);
                // mov rsi, r13
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xF5}, 3);
                // mov rdx, vtop
                JITEmitByte(&buf, 0xBA);
                JITEmitDword(&buf, (uint32_t)vtop);
#endif
                JITEmitCall(&buf, (void*)JITCleanupFrame);

                // Set top index to slotsbase
                // mov rax, r13
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xC5}, 3);
                // sub rax, r12
                JITEmitBytes(&buf, (const uint8_t[]){0x4C, 0x29, 0xE0}, 3);
                // sar rax, 3
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0xC1, 0xF8, 0x03}, 4);
                // mov [r15], eax
                JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x89, 0x07}, 3);

                // jmp epilogue
                JITEmitByte(&buf, 0xE9);
                patches[patchcount++] = buf.size;
                JITEmitDword(&buf, 0);
                break;
            }
            case OPRETURN: {
                // Copy ValNull() to slotsbase - 1
                // mov rax, ValNull()
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0xB8}, 2);
                JITEmitQword(&buf, ValNull());

                // mov rcx, r13
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xCD}, 3);
                // sub rcx, 8
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x83, 0xE9, 0x08}, 4);
                // mov [rcx], rax
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x89, 0x01}, 3);

                // Call JITCleanupFrame(stack=r12, locals=r13, vtop=vtop)
#ifdef _WIN32
                // mov rcx, r12
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xCC}, 3);
                // mov rdx, r13
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xD5}, 3);
                // mov r8, vtop
                JITEmitBytes(&buf, (const uint8_t[]){0x41, 0xB8}, 2);
                JITEmitDword(&buf, (uint32_t)vtop);
#else
                // mov rdi, r12
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xFC}, 3);
                // mov rsi, r13
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xF5}, 3);
                // mov rdx, vtop
                JITEmitByte(&buf, 0xBA);
                JITEmitDword(&buf, (uint32_t)vtop);
#endif
                JITEmitCall(&buf, (void*)JITCleanupFrame);

                // Set top index to slotsbase
                // mov rax, r13
                JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0xC5}, 3);
                // sub rax, r12
                JITEmitBytes(&buf, (const uint8_t[]){0x4C, 0x29, 0xE0}, 3);
                // sar rax, 3
                JITEmitBytes(&buf, (const uint8_t[]){0x48, 0xC1, 0xF8, 0x03}, 4);
                // mov [r15], eax
                JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x89, 0x07}, 3);

                // jmp epilogue
                JITEmitByte(&buf, 0xE9);
                patches[patchcount++] = buf.size;
                JITEmitDword(&buf, 0);
                break;
            }
            default:
                break;
        }
    }

    // Default exit path (success return)
    JITEmitStoreAbsoluteVtop(&buf, vtop);
    // mov eax, lastoffset
    int lastoffset = startoffset;
    if (stepcount > 0) {
        lastoffset = trace[stepcount - 1].ipoffset + GetJITOpLen(trace[stepcount - 1].op);
    }
    JITEmitByte(&buf, 0xB8);
    JITEmitDword(&buf, (uint32_t)lastoffset);

    // Epilogue label/block
    int epilogueoffset = buf.size;
    if (hot_slots[0] >= 0) {
        // Flush rbx back to memory slot: mov [r13 + hot_slots[0] * 8], rbx
        JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0x9D}, 3);
        JITEmitDword(&buf, (uint32_t)(hot_slots[0] * 8));
    }
    if (hot_slots[1] >= 0) {
        // Flush r14 back to memory slot: mov [r13 + hot_slots[1] * 8], r14
        JITEmitBytes(&buf, (const uint8_t[]){0x4D, 0x89, 0xB5}, 3);
        JITEmitDword(&buf, (uint32_t)(hot_slots[1] * 8));
    }
    // add rsp, 40
    JITEmitBytes(&buf, (const uint8_t[]){0x48, 0x83, 0xC4, 0x28}, 4);
    // pop registers
    JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x5F}, 2); // pop r15
    JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x5E}, 2); // pop r14
    JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x5D}, 2); // pop r13
    JITEmitBytes(&buf, (const uint8_t[]){0x41, 0x5C}, 2); // pop r12
    JITEmitByte(&buf, 0x5E); // pop rsi
    JITEmitByte(&buf, 0x5F); // pop rdi
    JITEmitByte(&buf, 0x5B); // pop rbx
    JITEmitByte(&buf, 0x5D); // pop rbp
    JITEmitByte(&buf, 0xC3); // ret

    // Patch deopt exits to point to epilogue
    for (int i = 0; i < patchcount; i++) {
        int offset = patches[i];
        int *ptr = (int*)(buf.code + offset);
        int relative = epilogueoffset - offset - 4;
        *ptr = relative;
    }

    if (vm->jitcacheenabled) {
        char cachepath[256];
        snprintf(cachepath, sizeof(cachepath), ".akscache/jitcache/trace_%d_%d_%d.bin", fn->id, startoffset, stepcount);
        FILE *cf = fopen(cachepath, "wb");
        if (cf) {
            JITHelperTable table = {
                (void*)JITHelperAdd, (void*)JITHelperSub, (void*)JITHelperMul, (void*)JITHelperDiv,
                (void*)JITHelperMod, (void*)JITHelperShl, (void*)JITHelperShr, (void*)JITHelperBitAnd,
                (void*)JITHelperBitOr, (void*)JITHelperBitXor, (void*)JITHelperBitNot, (void*)JITHelperInc,
                (void*)JITHelperDec, (void*)JITHelperLt, (void*)JITHelperGt, (void*)JITHelperEq,
                (void*)JITHelperLte, (void*)JITHelperGte, (void*)JITHelperNeq,
                (void*)JITHelperIsTruthy, (void*)JITCleanupFrame
            };
            fwrite(&table, sizeof(table), 1, cf);
            fwrite(buf.code, 1, buf.size, cf);
            fclose(cf);
        }
    }

    // Map to executable memory (W^X: copy then mark RX)
    uint8_t *exec = AllocExecMem(buf.size);
    if (exec) {
        memcpy(exec, buf.code, buf.size);
        MarkExecMem(exec, buf.size);
    }
    JITBufferFree(&buf);
    return exec;
}

void *CompileX64Method(VM *vm, FnProto *fn) {
    int stepcount = 0;
    TraceStep *trace = malloc(sizeof(TraceStep) * (fn->chunk.count + 1));
    if (!trace) return NULL;

    int ip = 0;
    while (ip < fn->chunk.count) {
        uint8_t op = fn->chunk.code[ip];
        trace[stepcount].op = op;
        trace[stepcount].ipoffset = ip;
        trace[stepcount].pushival = 0;
        trace[stepcount].pushfval = 0.0;
        trace[stepcount].slot = 0;

        int len = GetJITOpLen(op);
        if (op == OPPUSHINT) {
            if (ip + 8 < fn->chunk.count) {
                memcpy(&trace[stepcount].pushival, &fn->chunk.code[ip + 1], 8);
            }
        } else if (op == OPPUSHFLOAT) {
            if (ip + 8 < fn->chunk.count) {
                memcpy(&trace[stepcount].pushfval, &fn->chunk.code[ip + 1], 8);
            }
        } else if (op == OPLOAD || op == OPSTORE) {
            if (ip + 2 < fn->chunk.count) {
                trace[stepcount].slot = (fn->chunk.code[ip + 1] << 8) | fn->chunk.code[ip + 2];
            }
        }
        ip += len;
        stepcount++;
    }

    int reldepth = fn->maxregisters > 0 ? fn->maxregisters : ((fn->localcount > fn->paramcount) ? fn->localcount : fn->paramcount);
    void *exec = CompileX64Trace(vm, fn, 0, trace, stepcount, reldepth);
    free(trace);
    return exec;
}

void *LoadX64TraceFromCache(const char *cachefile, int *outsize) {
    FILE *cf = fopen(cachefile, "rb");
    if (!cf) return NULL;
    fseek(cf, 0, SEEK_END);
    int filesize = ftell(cf);
    fseek(cf, 0, SEEK_SET);
    
    if (filesize < (int)sizeof(JITHelperTable)) {
        fclose(cf);
        return NULL;
    }
    
    JITHelperTable oldtable;
    if (fread(&oldtable, sizeof(oldtable), 1, cf) != 1) {
        fclose(cf);
        return NULL;
    }

    int codesize = filesize - (int)sizeof(oldtable);
    uint8_t *code = malloc((size_t)codesize);
    if (!code || fread(code, 1, (size_t)codesize, cf) != (size_t)codesize) {
        free(code);
        fclose(cf);
        return NULL;
    }
    fclose(cf);
    
    // Current helpers
    JITHelperTable newtable = {
        (void*)JITHelperAdd, (void*)JITHelperSub, (void*)JITHelperMul, (void*)JITHelperDiv,
        (void*)JITHelperMod, (void*)JITHelperShl, (void*)JITHelperShr, (void*)JITHelperBitAnd,
        (void*)JITHelperBitOr, (void*)JITHelperBitXor, (void*)JITHelperBitNot, (void*)JITHelperInc,
        (void*)JITHelperDec, (void*)JITHelperLt, (void*)JITHelperGt, (void*)JITHelperEq,
        (void*)JITHelperLte, (void*)JITHelperGte, (void*)JITHelperNeq,
        (void*)JITHelperIsTruthy, (void*)JITCleanupFrame
    };
    
    // Relocate helpers in code
    void **oldptr = (void**)&oldtable;
    void **newptr = (void**)&newtable;
    int helpercount = sizeof(JITHelperTable) / sizeof(void*);
    
    for (int i = 0; i < codesize - 7; i++) {
        uint64_t val;
        memcpy(&val, code + i, 8);
        for (int h = 0; h < helpercount; h++) {
            if (val == (uint64_t)oldptr[h]) {
                uint64_t newval = (uint64_t)newptr[h];
                memcpy(code + i, &newval, 8);
                i += 7; // skip past this pointer
                break;
            }
        }
    }
    
    // Allocate executable memory (W^X: copy then mark RX)
    uint8_t *exec = AllocExecMem(codesize);
    if (exec) {
        memcpy(exec, code, codesize);
        MarkExecMem(exec, codesize);
    }
    free(code);
    *outsize = codesize;
    return exec;
}
#else
#include "jitx64.h"
#include <stddef.h>

void JITBufferInit(JITBuffer *buf) {
    buf->code = NULL;
    buf->size = 0;
    buf->cap = 0;
}
void JITBufferFree(JITBuffer *buf) {}
void JITEmitByte(JITBuffer *buf, uint8_t b) {}
void JITEmitBytes(JITBuffer *buf, const uint8_t *bytes, int count) {}
void JITEmitWord(JITBuffer *buf, uint16_t w) {}
void JITEmitDword(JITBuffer *buf, uint32_t dw) {}
void JITEmitQword(JITBuffer *buf, uint64_t qw) {}
void *CompileX64Trace(VM *vm, FnProto *fn, int startoffset, TraceStep *trace, int stepcount, int startdepth) {
    return NULL;
}
void *CompileX64Method(VM *vm, FnProto *fn) {
    return NULL;
}
void JITCleanupFrame(AKSValue *stack, AKSValue *locals, int64_t vtop) {}
void *GetJITCompiledFn(VM *vm, FnProto *fn, int targetoffset) {
    return NULL;
}
void *LoadX64TraceFromCache(const char *cachefile, int *outsize) {
    if (outsize) *outsize = 0;
    return NULL;
}
#endif
