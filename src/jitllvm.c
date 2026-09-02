// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "vm.h"
#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define popenpipe _popen
#define pclosepipe _pclose
#else
#include <dlfcn.h>
#define popenpipe popen
#define pclosepipe pclose
#endif

static int GetLLVMOpLen(uint8_t op) {
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

extern void *GetJITCompiledFn(VM *vm, FnProto *fn, int targetoffset);

void *CompileLLVMTrace(VM *vm, FnProto *fn, int startoffset, TraceStep *trace, int stepcount, int startdepth) {
    char dllfile[128];
#ifdef _WIN32
    snprintf(dllfile, sizeof(dllfile), "jittrace%d%d.dll", fn->id, startoffset);
    char compilecmd[256];
    snprintf(compilecmd, sizeof(compilecmd), "clang -x ir -shared -o %s - 2>nul", dllfile);
#else
    snprintf(dllfile, sizeof(dllfile), "jittrace%d%d.so", fn->id, startoffset);
    char compilecmd[256];
    snprintf(compilecmd, sizeof(compilecmd), "clang -x ir -shared -fPIC -o %s - 2>nul", dllfile);
#endif

    FILE *f = popenpipe(compilecmd, "w");
    if (!f) return NULL;

    fprintf(f, "; LLVM JIT Trace compilation\n");
    fprintf(f, "declare void @JITCleanupFrame(i64*, i64*, i64)\n");
    fprintf(f, "define i32 @jitfn(i64* %%stack, i32* %%stacktop, i64* %%locals, i8* %%vm) {\n");
    fprintf(f, "entry:\n");
    fprintf(f, "  %%stackint = ptrtoint i64* %%stack to i64\n");
    fprintf(f, "  %%localsint = ptrtoint i64* %%locals to i64\n");
    fprintf(f, "  %%diff = sub i64 %%localsint, %%stackint\n");
    fprintf(f, "  %%slotsbase = sdiv i64 %%diff, 8\n");
    fprintf(f, "  %%slotsbasei32 = trunc i64 %%slotsbase to i32\n");

    int vtop = startdepth;

    for (int i = 0; i < stepcount; i++) {
        TraceStep *step = &trace[i];
        fprintf(f, "  ; Step %d: Opcode %d\n", i, step->op);
        switch (step->op) {
            case OPPUSHINT:
                fprintf(f, "  %%ptrpush%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  store i64 %lld, i64* %%ptrpush%d\n", (long long)ValInt(step->pushival), i);
                vtop++;
                break;
            case OPPUSHFLOAT:
                fprintf(f, "  %%ptrpush%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  store i64 %lld, i64* %%ptrpush%d\n", (long long)ValFloat(step->pushfval), i);
                vtop++;
                break;
            case OPPUSHBOOL:
                fprintf(f, "  %%ptrpush%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  store i64 %lld, i64* %%ptrpush%d\n", (long long)ValBool((int)step->pushival), i);
                vtop++;
                break;
            case OPPUSHNULL:
                fprintf(f, "  %%ptrpush%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  store i64 %lld, i64* %%ptrpush%d\n", (long long)ValNull(), i);
                vtop++;
                break;
            case OPLOAD:
                fprintf(f, "  %%ptrloc%d = getelementptr i64, i64* %%locals, i32 %d\n", i, step->slot);
                fprintf(f, "  %%valloc%d = load i64, i64* %%ptrloc%d\n", i, i);
                fprintf(f, "  %%ptrst%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  store i64 %%valloc%d, i64* %%ptrst%d\n", i, i);
                vtop++;
                break;
            case OPSTORE:
                vtop--;
                fprintf(f, "  %%ptrst%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  %%valst%d = load i64, i64* %%ptrst%d\n", i, i);
                fprintf(f, "  %%ptrloc%d = getelementptr i64, i64* %%locals, i32 %d\n", i, step->slot);
                fprintf(f, "  store i64 %%valst%d, i64* %%ptrloc%d\n", i, i);
                break;
            case OPADD:
            case OPSUB:
            case OPMUL:
            case OPDIV: {
                vtop--;
                fprintf(f, "  %%ptra%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop - 1);
                fprintf(f, "  %%vala%d = load i64, i64* %%ptra%d\n", i, i);
                fprintf(f, "  %%ptrb%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  %%valb%d = load i64, i64* %%ptrb%d\n", i, i);
                
                fprintf(f, "  %%ashl%d = shl i64 %%vala%d, 16\n", i, i);
                fprintf(f, "  %%araw%d = ashr i64 %%ashl%d, 16\n", i, i);
                fprintf(f, "  %%bshl%d = shl i64 %%valb%d, 16\n", i, i);
                fprintf(f, "  %%braw%d = ashr i64 %%bshl%d, 16\n", i, i);
                
                if (step->op == OPADD) {
                    fprintf(f, "  %%resraw%d = add i64 %%araw%d, %%braw%d\n", i, i, i);
                } else if (step->op == OPSUB) {
                    fprintf(f, "  %%resraw%d = sub i64 %%araw%d, %%braw%d\n", i, i, i);
                } else if (step->op == OPMUL) {
                    fprintf(f, "  %%resraw%d = mul i64 %%araw%d, %%braw%d\n", i, i, i);
                } else {
                    fprintf(f, "  %%resraw%d = sdiv i64 %%araw%d, %%braw%d\n", i, i, i);
                }
                
                fprintf(f, "  %%resmasked%d = and i64 %%resraw%d, 281474976710655\n", i, i);
                fprintf(f, "  %%resboxed%d = or i64 %%resmasked%d, -4222124650659840\n", i, i);
                fprintf(f, "  store i64 %%resboxed%d, i64* %%ptra%d\n", i, i);
                break;
            }
            case OPLT:
            case OPGT:
            case OPEQ: {
                vtop--;
                fprintf(f, "  %%ptra%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop - 1);
                fprintf(f, "  %%vala%d = load i64, i64* %%ptra%d\n", i, i);
                fprintf(f, "  %%ptrb%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  %%valb%d = load i64, i64* %%ptrb%d\n", i, i);
                
                fprintf(f, "  %%ashl%d = shl i64 %%vala%d, 16\n", i, i);
                fprintf(f, "  %%araw%d = ashr i64 %%ashl%d, 16\n", i, i);
                fprintf(f, "  %%bshl%d = shl i64 %%valb%d, 16\n", i, i);
                fprintf(f, "  %%braw%d = ashr i64 %%bshl%d, 16\n", i, i);
                
                if (step->op == OPLT) {
                    fprintf(f, "  %%cmp%d = icmp slt i64 %%araw%d, %%braw%d\n", i, i, i);
                } else if (step->op == OPGT) {
                    fprintf(f, "  %%cmp%d = icmp sgt i64 %%araw%d, %%braw%d\n", i, i, i);
                } else {
                    fprintf(f, "  %%cmp%d = icmp eq i64 %%araw%d, %%braw%d\n", i, i, i);
                }
                
                fprintf(f, "  %%cmpi64%d = zext i1 %%cmp%d to i64\n", i, i);
                fprintf(f, "  %%resboxed%d = or i64 %%cmpi64%d, -3940649673949184\n", i, i);
                fprintf(f, "  store i64 %%resboxed%d, i64* %%ptra%d\n", i, i);
                break;
            }
            case OPDUP:
                fprintf(f, "  %%ptrd%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop - 1);
                fprintf(f, "  %%vald%d = load i64, i64* %%ptrd%d\n", i, i);
                fprintf(f, "  %%ptrst%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  store i64 %%vald%d, i64* %%ptrst%d\n", i, i);
                vtop++;
                break;
            case OPPOP:
                vtop--;
                break;
            case OPINC:
                fprintf(f, "  %%ptri%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop - 1);
                fprintf(f, "  %%vali%d = load i64, i64* %%ptri%d\n", i, i);
                fprintf(f, "  %%ishl%d = shl i64 %%vali%d, 16\n", i, i);
                fprintf(f, "  %%iraw%d = ashr i64 %%ishl%d, 16\n", i, i);
                fprintf(f, "  %%resraw%d = add i64 %%iraw%d, 1\n", i, i);
                fprintf(f, "  %%resmasked%d = and i64 %%resraw%d, 281474976710655\n", i, i);
                fprintf(f, "  %%resboxed%d = or i64 %%resmasked%d, -4222124650659840\n", i, i);
                fprintf(f, "  store i64 %%resboxed%d, i64* %%ptri%d\n", i, i);
                break;
            case OPDEC:
                fprintf(f, "  %%ptri%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop - 1);
                fprintf(f, "  %%vali%d = load i64, i64* %%ptri%d\n", i, i);
                fprintf(f, "  %%ishl%d = shl i64 %%vali%d, 16\n", i, i);
                fprintf(f, "  %%iraw%d = ashr i64 %%ishl%d, 16\n", i, i);
                fprintf(f, "  %%resraw%d = sub i64 %%iraw%d, 1\n", i, i);
                fprintf(f, "  %%resmasked%d = and i64 %%resraw%d, 281474976710655\n", i, i);
                fprintf(f, "  %%resboxed%d = or i64 %%resmasked%d, -4222124650659840\n", i, i);
                fprintf(f, "  store i64 %%resboxed%d, i64* %%ptri%d\n", i, i);
                break;
            case OPJUMP: {
                int targetoffset = step->jumpoffset < 0 ? (step->ipoffset + 5 + step->jumpoffset) : (step->ipoffset + 5);
                void *targetfn = GetJITCompiledFn(vm, fn, targetoffset);
                fprintf(f, "  %%absvtop%d = add i32 %%slotsbasei32, %d\n", i, vtop);
                fprintf(f, "  store i32 %%absvtop%d, i32* %%stacktop\n", i);
                if (targetfn) {
                    fprintf(f, "  %%targetfnptr%d = inttoptr i64 %lld to i32 (i64*, i32*, i64*, i8*)*\n", i, (long long)targetfn);
                    fprintf(f, "  %%res%d = call i32 %%targetfnptr%d(i64* %%stack, i32* %%stacktop, i64* %%locals, i8* %%vm)\n", i, i);
                    fprintf(f, "  ret i32 %%res%d\n", i);
                } else {
                    fprintf(f, "  ret i32 %d\n", targetoffset);
                }
                break;
            }
            case OPJUMPIFFALSE: {
                vtop--;
                fprintf(f, "  %%ptrcond%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  %%valcond%d = load i64, i64* %%ptrcond%d\n", i, i);
                
                fprintf(f, "  %%shifted%d = lshr i64 %%valcond%d, 48\n", i, i);
                fprintf(f, "  %%tag%d = trunc i64 %%shifted%d to i16\n", i, i);
                
                fprintf(f, "  %%isbool%d = icmp eq i16 %%tag%d, 65522\n", i, i);
                fprintf(f, "  %%payload%d = and i64 %%valcond%d, 1\n", i, i);
                fprintf(f, "  %%isfalsebool%d = icmp eq i64 %%payload%d, 0\n", i, i);
                fprintf(f, "  %%condboolf%d = and i1 %%isbool%d, %%isfalsebool%d\n", i, i, i);
                
                fprintf(f, "  %%isint%d = icmp eq i16 %%tag%d, 65521\n", i, i);
                fprintf(f, "  %%payloadint%d = and i64 %%valcond%d, 281474976710655\n", i, i);
                fprintf(f, "  %%iszeroint%d = icmp eq i64 %%payloadint%d, 0\n", i, i);
                fprintf(f, "  %%condintz%d = and i1 %%isint%d, %%iszeroint%d\n", i, i, i);
                
                fprintf(f, "  %%isnull%d = icmp eq i16 %%tag%d, 65523\n", i, i);
                
                fprintf(f, "  %%tmpf%d = or i1 %%condboolf%d, %%condintz%d\n", i, i, i);
                fprintf(f, "  %%falsy%d = or i1 %%tmpf%d, %%isnull%d\n", i, i, i);
                fprintf(f, "  %%truthy%d = xor i1 %%falsy%d, true\n", i, i);
                
                int targetoffset = (step->guardoutcome == 1) ? (step->ipoffset + 5) : (step->ipoffset + 5 + step->jumpoffset);
                void *targetfn = GetJITCompiledFn(vm, fn, targetoffset);
                
                if (step->guardoutcome == 1) {
                    fprintf(f, "  br i1 %%truthy%d, label %%deoptf%d, label %%cont%d\n", i, i, i);
                    fprintf(f, "deoptf%d:\n", i);
                    fprintf(f, "  %%absvtopf%d = add i32 %%slotsbasei32, %d\n", i, vtop);
                    fprintf(f, "  store i32 %%absvtopf%d, i32* %%stacktop\n", i);
                    if (targetfn) {
                        fprintf(f, "  %%targetfnptr%d = inttoptr i64 %lld to i32 (i64*, i32*, i64*, i8*)*\n", i, (long long)targetfn);
                        fprintf(f, "  %%res%d = call i32 %%targetfnptr%d(i64* %%stack, i32* %%stacktop, i64* %%locals, i8* %%vm)\n", i, i);
                        fprintf(f, "  ret i32 %%res%d\n", i);
                    } else {
                        fprintf(f, "  ret i32 %d\n", targetoffset);
                    }
                } else {
                    fprintf(f, "  br i1 %%falsy%d, label %%deoptt%d, label %%cont%d\n", i, i, i);
                    fprintf(f, "deoptt%d:\n", i);
                    fprintf(f, "  %%absvtopt%d = add i32 %%slotsbasei32, %d\n", i, vtop);
                    fprintf(f, "  store i32 %%absvtopt%d, i32* %%stacktop\n", i);
                    if (targetfn) {
                        fprintf(f, "  %%targetfnptr%d = inttoptr i64 %lld to i32 (i64*, i32*, i64*, i8*)*\n", i, (long long)targetfn);
                        fprintf(f, "  %%res%d = call i32 %%targetfnptr%d(i64* %%stack, i32* %%stacktop, i64* %%locals, i8* %%vm)\n", i, i);
                        fprintf(f, "  ret i32 %%res%d\n", i);
                    } else {
                        fprintf(f, "  ret i32 %d\n", targetoffset);
                    }
                }
                fprintf(f, "cont%d:\n", i);
                break;
            }
            case OPRETURNVAL: {
                vtop--;
                fprintf(f, "  %%ptrret%d = getelementptr i64, i64* %%locals, i32 %d\n", i, vtop);
                fprintf(f, "  %%valret%d = load i64, i64* %%ptrret%d\n", i, i);
                fprintf(f, "  %%ptrdest%d = getelementptr i64, i64* %%locals, i32 -1\n", i);
                fprintf(f, "  store i64 %%valret%d, i64* %%ptrdest%d\n", i, i);
                fprintf(f, "  call void @JITCleanupFrame(i64* %%stack, i64* %%locals, i64 %d)\n", vtop);
                fprintf(f, "  store i32 %%slotsbasei32, i32* %%stacktop\n");
                fprintf(f, "  ret i32 0\n");
                break;
            }
            case OPRETURN: {
                fprintf(f, "  %%ptrdest%d = getelementptr i64, i64* %%locals, i32 -1\n", i);
                fprintf(f, "  store i64 %lld, i64* %%ptrdest%d\n", (long long)ValNull(), i);
                fprintf(f, "  call void @JITCleanupFrame(i64* %%stack, i64* %%locals, i64 %d)\n", vtop);
                fprintf(f, "  store i32 %%slotsbasei32, i32* %%stacktop\n");
                fprintf(f, "  ret i32 0\n");
                break;
            }

            default:
                break;
        }
    }

    int lastoffset = startoffset;
    if (stepcount > 0) {
        lastoffset = trace[stepcount - 1].ipoffset + GetLLVMOpLen(trace[stepcount - 1].op);
    }
    fprintf(f, "  %%absvtopfinal = add i32 %%slotsbasei32, %d\n", vtop);
    fprintf(f, "  store i32 %%absvtopfinal, i32* %%stacktop\n");
    fprintf(f, "  ret i32 %d\n", lastoffset);
    fprintf(f, "}\n");

    int res = pclosepipe(f);

    if (res == 0) {
#ifdef _WIN32
        void *lib = LoadLibraryA(dllfile);
        if (lib) {
            void *func = GetProcAddress((HMODULE)lib, "jitfn");
            return func;
        }
#else
        void *lib = dlopen(dllfile, RTLD_NOW);
        if (lib) {
            void *func = dlsym(lib, "jitfn");
            return func;
        }
#endif
    }
    return NULL;
}
