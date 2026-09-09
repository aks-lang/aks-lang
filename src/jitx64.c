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
    void *helperistruthy;
    void *jitcleanupframe;
} JITHelperTable;

// Allocates executable memory
static void *AllocExecMem(int size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    return mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
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
        return ValInt(ValToInt(a) + ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValFloat(fa + fb);
}

static AKSValue JITHelperSub(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValInt(ValToInt(a) - ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValFloat(fa - fb);
}

static AKSValue JITHelperMul(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValInt(ValToInt(a) * ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValFloat(fa * fb);
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
    if (ValKind(a) == VALINT) {
        return ValInt(ValToInt(a) - 1);
    } else if (ValKind(a) == VALFLOAT) {
        return ValFloat(ValToFloat(a) - 1.0);
    }
    return a;
}

static AKSValue JITHelperLt(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValBool(ValToInt(a) < ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa < fb);
}

static AKSValue JITHelperGt(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValBool(ValToInt(a) > ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa > fb);
}

static AKSValue JITHelperEq(AKSValue a, AKSValue b) {
    if (ValKind(a) == ValKind(b)) {
        if (ValKind(a) == VALINT) {
            return ValBool(ValToInt(a) == ValToInt(b));
        } else if (ValKind(a) == VALFLOAT) {
            return ValBool(ValToFloat(a) == ValToFloat(b));
        } else if (ValKind(a) == VALBOOL) {
            return ValBool(ValToBool(a) == ValToBool(b));
        } else if (ValKind(a) == VALNULL) {
            return ValBool(1);
        }
    }
    return ValBool(a == b);
}

static AKSValue JITHelperLte(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValBool(ValToInt(a) <= ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa <= fb);
}

static AKSValue JITHelperGte(AKSValue a, AKSValue b) {
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        return ValBool(ValToInt(a) >= ValToInt(b));
    }
    double fa = (ValKind(a) == VALFLOAT) ? ValToFloat(a) : (double)ValToInt(a);
    double fb = (ValKind(b) == VALFLOAT) ? ValToFloat(b) : (double)ValToInt(b);
    return ValBool(fa >= fb);
}

static AKSValue JITHelperNeq(AKSValue a, AKSValue b) {
    AKSValue eq = JITHelperEq(a, b);
    return ValBool(!ValToBool(eq));
}

static int JITHelperIsTruthy(AKSValue v) {
    return (ValKind(v) != VALNULL && 
            !(ValKind(v) == VALBOOL && !ValToBool(v)) && 
            !(ValKind(v) == VALINT && ValToInt(v) == 0) && 
            !(ValKind(v) == VALFLOAT && ValToFloat(v) == 0.0));
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

    // r14: vm (fourth arg)
#ifdef _WIN32
    JITEmitBytes(&buf, (const uint8_t[]){0x4D, 0x89, 0xCE}, 3); // mov r14, r9
#else
    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0xCE}, 3); // mov r14, rcx
#endif

    int patches[256];
    int patchcount = 0;

    int vtop = startdepth;

    // Linear scan register allocation:
    // Determine the hottest local slot accessed across the trace
    int slot_freq[256];
    memset(slot_freq, 0, sizeof(slot_freq));
    for (int i = 0; i < stepcount; i++) {
        if ((trace[i].op == OPLOAD || trace[i].op == OPSTORE) && trace[i].slot >= 0 && trace[i].slot < 256) {
            slot_freq[trace[i].slot]++;
        }
    }
    int hot_slot = -1;
    int max_freq = 1;
    for (int s = 0; s < 256; s++) {
        if (slot_freq[s] > max_freq) {
            max_freq = slot_freq[s];
            hot_slot = s;
        }
    }
    if (hot_slot >= 0) {
        // Pre-load hot slot into rbx: mov rbx, [r13 + hot_slot * 8]
        JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0x9D}, 3);
        JITEmitDword(&buf, (uint32_t)(hot_slot * 8));
    }
    
    // Process trace steps
    for (int i = 0; i < stepcount; i++) {
        TraceStep *step = &trace[i];
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
                if (step->slot == hot_slot) {
                    // mov [r13 + vtop * 8], rbx
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0x9D}, 3);
                    JITEmitDword(&buf, (uint32_t)(vtop * 8));
                } else {
                    JITEmitLoadLocal(&buf, step->slot, vtop);
                }
                vtop++;
                break;
            case OPSTORE:
                vtop--;
                if (step->slot == hot_slot) {
                    // mov rbx, [r13 + vtop * 8]
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x8B, 0x9D}, 3);
                    JITEmitDword(&buf, (uint32_t)(vtop * 8));
                    // mov [r13 + hot_slot * 8], rbx
                    JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0x9D}, 3);
                    JITEmitDword(&buf, (uint32_t)(hot_slot * 8));
                } else {
                    JITEmitStoreLocal(&buf, step->slot, vtop);
                }
                break;
            case OPADD:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperAdd);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPSUB:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperSub);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPMUL:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperMul);
                JITEmitSaveRax(&buf, vtop - 1);
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
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitCall(&buf, (void*)JITHelperInc);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPDEC:
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitCall(&buf, (void*)JITHelperDec);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPLT:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperLt);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPGT:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperGt);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPEQ:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperEq);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPNEQ:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperNeq);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPLTE:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperLte);
                JITEmitSaveRax(&buf, vtop - 1);
                break;
            case OPGTE:
                vtop--;
                JITEmitLoadArg(&buf, vtop - 1, 0);
                JITEmitLoadArg(&buf, vtop, 1);
                JITEmitCall(&buf, (void*)JITHelperGte);
                JITEmitSaveRax(&buf, vtop - 1);
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
                JITEmitStoreAbsoluteVtop(&buf, vtop);
                JITEmitByte(&buf, 0xB8);
                JITEmitDword(&buf, (uint32_t)targetoffset);
                JITEmitByte(&buf, 0xE9);
                patches[patchcount++] = buf.size;
                JITEmitDword(&buf, 0);
                break;
            }
            case OPJUMPIFFALSE: {
                vtop--;
                JITEmitLoadArg(&buf, vtop, 0);
                JITEmitCall(&buf, (void*)JITHelperIsTruthy);
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
    if (hot_slot >= 0) {
        // Flush rbx back to memory slot: mov [r13 + hot_slot * 8], rbx
        JITEmitBytes(&buf, (const uint8_t[]){0x49, 0x89, 0x9D}, 3);
        JITEmitDword(&buf, (uint32_t)(hot_slot * 8));
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
                (void*)JITHelperIsTruthy, (void*)JITCleanupFrame
            };
            fwrite(&table, sizeof(table), 1, cf);
            fwrite(buf.code, 1, buf.size, cf);
            fclose(cf);
        }
    }

    // Map to executable memory
    uint8_t *exec = AllocExecMem(buf.size);
    if (exec) {
        memcpy(exec, buf.code, buf.size);
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
    fread(&oldtable, sizeof(oldtable), 1, cf);
    
    int codesize = filesize - sizeof(oldtable);
    uint8_t *code = malloc(codesize);
    fread(code, 1, codesize, cf);
    fclose(cf);
    
    // Current helpers
    JITHelperTable newtable = {
        (void*)JITHelperAdd, (void*)JITHelperSub, (void*)JITHelperMul, (void*)JITHelperDiv,
        (void*)JITHelperMod, (void*)JITHelperShl, (void*)JITHelperShr, (void*)JITHelperBitAnd,
        (void*)JITHelperBitOr, (void*)JITHelperBitXor, (void*)JITHelperBitNot, (void*)JITHelperInc,
        (void*)JITHelperDec, (void*)JITHelperLt, (void*)JITHelperGt, (void*)JITHelperEq,
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
    
    // Allocate executable memory
    uint8_t *exec = AllocExecMem(codesize);
    if (exec) {
        memcpy(exec, code, codesize);
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
