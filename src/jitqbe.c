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

static int GetQBEOpLen(uint8_t op) {
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

void *CompileQBETrace(VM *vm, FnProto *fn, int startoffset, TraceStep *trace, int stepcount, int startdepth) {
    char dllfile[128];
#ifdef _WIN32
    snprintf(dllfile, sizeof(dllfile), "jittrace%d%d.dll", fn->id, startoffset);
    char compilecmd[256];
    snprintf(compilecmd, sizeof(compilecmd), "qbe | clang -x assembler -shared -o %s - 2>nul", dllfile);
#else
    snprintf(dllfile, sizeof(dllfile), "jittrace%d%d.so", fn->id, startoffset);
    char compilecmd[256];
    snprintf(compilecmd, sizeof(compilecmd), "qbe | clang -x assembler -shared -fPIC -o %s - 2>nul", dllfile);
#endif

    FILE *f = popenpipe(compilecmd, "w");
    if (!f) return NULL;

    fprintf(f, "export function w $jitfn(l %%stack, l %%stacktop, l %%locals, l %%vm) {\n");
    fprintf(f, "@start\n");
    fprintf(f, "  %%diff =l sub %%locals, %%stack\n");
    fprintf(f, "  %%slotsbase =l sar %%diff, 3\n");

    int vtop = startdepth;

    for (int i = 0; i < stepcount; i++) {
        TraceStep *step = &trace[i];
        fprintf(f, "  # Step %d: Opcode %d\n", i, step->op);
        switch (step->op) {
            case OPPUSHINT:
                fprintf(f, "  %%offpush%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrpush%d =l add %%locals, %%offpush%d\n", i, i);
                fprintf(f, "  storel %lld, %%addrpush%d\n", (long long)ValInt(step->pushival), i);
                vtop++;
                break;
            case OPPUSHFLOAT:
                fprintf(f, "  %%offpush%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrpush%d =l add %%locals, %%offpush%d\n", i, i);
                fprintf(f, "  storel %lld, %%addrpush%d\n", (long long)ValFloat(step->pushfval), i);
                vtop++;
                break;
            case OPPUSHBOOL:
                fprintf(f, "  %%offpush%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrpush%d =l add %%locals, %%offpush%d\n", i, i);
                fprintf(f, "  storel %lld, %%addrpush%d\n", (long long)ValBool((int)step->pushival), i);
                vtop++;
                break;
            case OPPUSHNULL:
                fprintf(f, "  %%offpush%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrpush%d =l add %%locals, %%offpush%d\n", i, i);
                fprintf(f, "  storel %lld, %%addrpush%d\n", (long long)ValNull(), i);
                vtop++;
                break;
            case OPLOAD:
                fprintf(f, "  %%offloc%d =l copy %d\n", i, step->slot * 8);
                fprintf(f, "  %%addrloc%d =l add %%locals, %%offloc%d\n", i, i);
                fprintf(f, "  %%valloc%d =l loadl %%addrloc%d\n", i, i);
                fprintf(f, "  %%offst%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrst%d =l add %%locals, %%offst%d\n", i, i);
                fprintf(f, "  storel %%valloc%d, %%addrst%d\n", i, i);
                vtop++;
                break;
            case OPSTORE:
                vtop--;
                fprintf(f, "  %%offst%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrst%d =l add %%locals, %%offst%d\n", i, i);
                fprintf(f, "  %%valst%d =l loadl %%addrst%d\n", i, i);
                fprintf(f, "  %%offloc%d =l copy %d\n", i, step->slot * 8);
                fprintf(f, "  %%addrloc%d =l add %%locals, %%offloc%d\n", i, i);
                fprintf(f, "  storel %%valst%d, %%addrloc%d\n", i, i);
                break;
            case OPADD:
            case OPSUB:
            case OPMUL:
            case OPDIV: {
                vtop--;
                fprintf(f, "  %%offa%d =l copy %d\n", i, (vtop - 1) * 8);
                fprintf(f, "  %%addra%d =l add %%locals, %%offa%d\n", i, i);
                fprintf(f, "  %%vala%d =l loadl %%addra%d\n", i, i);
                
                fprintf(f, "  %%offb%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrb%d =l add %%locals, %%offb%d\n", i, i);
                fprintf(f, "  %%valb%d =l loadl %%addrb%d\n", i, i);
                
                fprintf(f, "  %%ashl%d =l shl %%vala%d, 16\n", i, i);
                fprintf(f, "  %%araw%d =l sar %%ashl%d, 16\n", i, i);
                fprintf(f, "  %%bshl%d =l shl %%valb%d, 16\n", i, i);
                fprintf(f, "  %%braw%d =l sar %%bshl%d, 16\n", i, i);
                
                if (step->op == OPADD) {
                    fprintf(f, "  %%resraw%d =l add %%araw%d, %%braw%d\n", i, i, i);
                } else if (step->op == OPSUB) {
                    fprintf(f, "  %%resraw%d =l sub %%araw%d, %%braw%d\n", i, i, i);
                } else if (step->op == OPMUL) {
                    fprintf(f, "  %%resraw%d =l mul %%araw%d, %%braw%d\n", i, i, i);
                } else {
                    fprintf(f, "  %%resraw%d =l div %%araw%d, %%braw%d\n", i, i, i);
                }
                
                fprintf(f, "  %%resmasked%d =l and %%resraw%d, 281474976710655\n", i, i);
                fprintf(f, "  %%resboxed%d =l or %%resmasked%d, -4222124650659840\n", i, i);
                fprintf(f, "  storel %%resboxed%d, %%addra%d\n", i, i);
                break;
            }
            case OPLT:
            case OPGT:
            case OPEQ: {
                vtop--;
                fprintf(f, "  %%offa%d =l copy %d\n", i, (vtop - 1) * 8);
                fprintf(f, "  %%addra%d =l add %%locals, %%offa%d\n", i, i);
                fprintf(f, "  %%vala%d =l loadl %%addra%d\n", i, i);
                
                fprintf(f, "  %%offb%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrb%d =l add %%locals, %%offb%d\n", i, i);
                fprintf(f, "  %%valb%d =l loadl %%addrb%d\n", i, i);
                
                fprintf(f, "  %%ashl%d =l shl %%vala%d, 16\n", i, i);
                fprintf(f, "  %%araw%d =l sar %%ashl%d, 16\n", i, i);
                fprintf(f, "  %%bshl%d =l shl %%valb%d, 16\n", i, i);
                fprintf(f, "  %%braw%d =l sar %%bshl%d, 16\n", i, i);
                
                if (step->op == OPLT) {
                    fprintf(f, "  %%cmp%d =w csltl %%araw%d, %%braw%d\n", i, i, i);
                } else if (step->op == OPGT) {
                    fprintf(f, "  %%cmp%d =w csgtl %%araw%d, %%braw%d\n", i, i, i);
                } else {
                    fprintf(f, "  %%cmp%d =w ceql %%araw%d, %%braw%d\n", i, i, i);
                }
                
                fprintf(f, "  %%cmpl%d =l extub %%cmp%d\n", i, i);
                fprintf(f, "  %%resboxed%d =l or %%cmpl%d, -3940649673949184\n", i, i);
                fprintf(f, "  storel %%resboxed%d, %%addra%d\n", i, i);
                break;
            }
            case OPDUP:
                fprintf(f, "  %%offd%d =l copy %d\n", i, (vtop - 1) * 8);
                fprintf(f, "  %%addrd%d =l add %%locals, %%offd%d\n", i, i);
                fprintf(f, "  %%vald%d =l loadl %%addrd%d\n", i, i);
                fprintf(f, "  %%offst%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrst%d =l add %%locals, %%offst%d\n", i, i);
                fprintf(f, "  storel %%vald%d, %%addrst%d\n", i, i);
                vtop++;
                break;
            case OPPOP:
                vtop--;
                break;
            case OPINC:
                fprintf(f, "  %%offi%d =l copy %d\n", i, (vtop - 1) * 8);
                fprintf(f, "  %%addri%d =l add %%locals, %%offi%d\n", i, i);
                fprintf(f, "  %%vali%d =l loadl %%addri%d\n", i, i);
                fprintf(f, "  %%ishl%d =l shl %%vali%d, 16\n", i, i);
                fprintf(f, "  %%iraw%d =l sar %%ishl%d, 16\n", i, i);
                fprintf(f, "  %%resraw%d =l add %%iraw%d, 1\n", i, i);
                fprintf(f, "  %%resmasked%d =l and %%resraw%d, 281474976710655\n", i, i);
                fprintf(f, "  %%resboxed%d =l or %%resmasked%d, -4222124650659840\n", i, i);
                fprintf(f, "  storel %%resboxed%d, %%addri%d\n", i, i);
                break;
            case OPDEC:
                fprintf(f, "  %%offi%d =l copy %d\n", i, (vtop - 1) * 8);
                fprintf(f, "  %%addri%d =l add %%locals, %%offi%d\n", i, i);
                fprintf(f, "  %%vali%d =l loadl %%addri%d\n", i, i);
                fprintf(f, "  %%ishl%d =l shl %%vali%d, 16\n", i, i);
                fprintf(f, "  %%iraw%d =l sar %%ishl%d, 16\n", i, i);
                fprintf(f, "  %%resraw%d =l sub %%iraw%d, 1\n", i, i);
                fprintf(f, "  %%resmasked%d =l and %%resraw%d, 281474976710655\n", i, i);
                fprintf(f, "  %%resboxed%d =l or %%resmasked%d, -4222124650659840\n", i, i);
                fprintf(f, "  storel %%resboxed%d, %%addri%d\n", i, i);
                break;
            case OPJUMP: {
                int targetoffset = step->jumpoffset < 0 ? (step->ipoffset + 5 + step->jumpoffset) : (step->ipoffset + 5);
                void *targetfn = GetJITCompiledFn(vm, fn, targetoffset);
                fprintf(f, "  %%absvtop%d =l add %%slotsbase, %d\n", i, vtop);
                fprintf(f, "  storew %%absvtop%d, %%stacktop\n", i);
                if (targetfn) {
                    fprintf(f, "  %%fnptr%d =l copy %lld\n", i, (long long)targetfn);
                    fprintf(f, "  %%res%d =w call %%fnptr%d(l %%stack, l %%stacktop, l %%locals, l %%vm)\n", i, i);
                    fprintf(f, "  ret %%res%d\n", i);
                } else {
                    fprintf(f, "  ret %d\n", targetoffset);
                }
                break;
            }
            case OPJUMPIFFALSE: {
                vtop--;
                fprintf(f, "  %%offcond%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrcond%d =l add %%locals, %%offcond%d\n", i, i);
                fprintf(f, "  %%valcond%d =l loadl %%addrcond%d\n", i, i);
                
                fprintf(f, "  %%shifted%d =l sar %%valcond%d, 48\n", i, i);
                fprintf(f, "  %%tag%d =w extub %%shifted%d\n", i, i);
                
                fprintf(f, "  %%isbool%d =w ceqw %%tag%d, 65522\n", i, i);
                fprintf(f, "  %%payload%d =l and %%valcond%d, 1\n", i, i);
                fprintf(f, "  %%isfalsebool%d =w ceql %%payload%d, 0\n", i, i);
                fprintf(f, "  %%condboolf%d =w and %%isbool%d, %%isfalsebool%d\n", i, i, i);
                
                fprintf(f, "  %%isint%d =w ceqw %%tag%d, 65521\n", i, i);
                fprintf(f, "  %%payloadint%d =l and %%valcond%d, 281474976710655\n", i, i);
                fprintf(f, "  %%iszeroint%d =w ceql %%payloadint%d, 0\n", i, i);
                fprintf(f, "  %%condintz%d =w and %%isint%d, %%iszeroint%d\n", i, i, i);
                
                fprintf(f, "  %%isnull%d =w ceqw %%tag%d, 65523\n", i, i);
                
                fprintf(f, "  %%tmpf%d =w or %%condboolf%d, %%condintz%d\n", i, i, i);
                fprintf(f, "  %%falsy%d =w or %%tmpf%d, %%isnull%d\n", i, i, i);
                fprintf(f, "  %%truthy%d =w xor %%falsy%d, 1\n", i, i);
                
                int targetoffset = (step->guardoutcome == 1) ? (step->ipoffset + 5) : (step->ipoffset + 5 + step->jumpoffset);
                void *targetfn = GetJITCompiledFn(vm, fn, targetoffset);
                
                if (step->guardoutcome == 1) {
                    fprintf(f, "  jnz %%truthy%d, @deoptf%d, @cont%d\n", i, i, i);
                    fprintf(f, "@deoptf%d\n", i);
                    fprintf(f, "  %%absvtopf%d =l add %%slotsbase, %d\n", i, vtop);
                    fprintf(f, "  storew %%absvtopf%d, %%stacktop\n", i);
                    if (targetfn) {
                        fprintf(f, "  %%fnptr%d =l copy %lld\n", i, (long long)targetfn);
                        fprintf(f, "  %%res%d =w call %%fnptr%d(l %%stack, l %%stacktop, l %%locals, l %%vm)\n", i, i);
                        fprintf(f, "  ret %%res%d\n", i);
                    } else {
                        fprintf(f, "  ret %d\n", targetoffset);
                    }
                } else {
                    fprintf(f, "  jnz %%falsy%d, @deoptt%d, @cont%d\n", i, i, i);
                    fprintf(f, "@deoptt%d\n", i);
                    fprintf(f, "  %%absvtopt%d =l add %%slotsbase, %d\n", i, vtop);
                    fprintf(f, "  storew %%absvtopt%d, %%stacktop\n", i);
                    if (targetfn) {
                        fprintf(f, "  %%fnptr%d =l copy %lld\n", i, (long long)targetfn);
                        fprintf(f, "  %%res%d =w call %%fnptr%d(l %%stack, l %%stacktop, l %%locals, l %%vm)\n", i, i);
                        fprintf(f, "  ret %%res%d\n", i);
                    } else {
                        fprintf(f, "  ret %d\n", targetoffset);
                    }
                }
                fprintf(f, "@cont%d\n", i);
                break;
            }
            case OPRETURNVAL: {
                vtop--;
                fprintf(f, "  %%offret%d =l copy %d\n", i, vtop * 8);
                fprintf(f, "  %%addrret%d =l add %%locals, %%offret%d\n", i, i);
                fprintf(f, "  %%valret%d =l loadl %%addrret%d\n", i, i);
                fprintf(f, "  %%ptrdest%d =l add %%locals, -8\n", i);
                fprintf(f, "  storel %%valret%d, %%ptrdest%d\n", i, i);
                fprintf(f, "  call $JITCleanupFrame(l %%stack, l %%locals, l %d)\n", vtop);
                fprintf(f, "  storew %%slotsbase, %%stacktop\n");
                fprintf(f, "  ret 0\n");
                break;
            }
            case OPRETURN: {
                fprintf(f, "  %%ptrdest%d =l add %%locals, -8\n", i);
                fprintf(f, "  storel %lld, %%ptrdest%d\n", (long long)ValNull(), i);
                fprintf(f, "  call $JITCleanupFrame(l %%stack, l %%locals, l %d)\n", vtop);
                fprintf(f, "  storew %%slotsbase, %%stacktop\n");
                fprintf(f, "  ret 0\n");
                break;
            }

            default:
                break;
        }
    }

    int lastoffset = startoffset;
    if (stepcount > 0) {
        lastoffset = trace[stepcount - 1].ipoffset + GetQBEOpLen(trace[stepcount - 1].op);
    }
    fprintf(f, "  %%absvtopfinal =l add %%slotsbase, %d\n", vtop);
    fprintf(f, "  storew %%absvtopfinal, %%stacktop\n");
    fprintf(f, "  ret %d\n", lastoffset);
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
