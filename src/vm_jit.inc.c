extern void *CompileX64Trace(VM *vm, FnProto *fn, int startoffset,
                             TraceStep *trace, int stepcount, int startdepth);
extern void *CompileLLVMTrace(VM *vm, FnProto *fn, int startoffset,
                              TraceStep *trace, int stepcount, int startdepth);
extern void *CompileQBETrace(VM *vm, FnProto *fn, int startoffset,
                             TraceStep *trace, int stepcount, int startdepth);

VMProfilingStats gVMProfiling = {0};

int DetectHotFunction(VM *vm, FnProto *fn) {
  if (!vm || !fn || fn->id < 0 || fn->id >= 4096) return 0;
  int thresh = vm->hot_func_threshold > 0 ? vm->hot_func_threshold : 50;
  return (vm->fncallcounts[fn->id] >= thresh);
}

int DetectHotLoop(VM *vm, FnProto *fn, int offset) {
  if (!vm || !fn || !fn->chunk.loopcounters || offset < 0 || offset >= fn->chunk.count) return 0;
  int thresh = vm->hot_loop_threshold > 0 ? vm->hot_loop_threshold : 50;
  return (fn->chunk.loopcounters[offset] >= thresh);
}

static void TriggerJIT(VM *vm, FnProto *fn, int startoffset, int endoffset) {
  if (vm->sandboxenabled)
    return;
  gVMProfiling.hot_loop_triggers++;
  MutexLockNative(vm->globallock);
  if (vm->jitloopcount >= 128) {
    MutexUnlockNative(vm->globallock);
    return;
  }

  for (int i = 0; i < vm->jitloopcount; i++) {
    if (vm->jitloops[i].fnid == fn->id &&
        vm->jitloops[i].targetoffset == startoffset) {
      MutexUnlockNative(vm->globallock);
      return;
    }
  }

  /* In-memory native JIT compilation if JIT mode is active (x64, LLVM, or QBE) or auto-tiering on x86_64 */
#if defined(__x86_64__) || defined(_M_X64)
  int effective_jitmode = (vm->jitmode > 0) ? vm->jitmode : 1;
#else
  int effective_jitmode = vm->jitmode;
#endif
  if (effective_jitmode > 0 && fn->chunk.code && fn->chunk.count > 0) {
    int stepcount = 0;
    int looplen = endoffset - startoffset + 1;
    if (looplen <= 0 || looplen > 2048) looplen = 256;
    TraceStep *trace = (TraceStep *)malloc(sizeof(TraceStep) * (looplen + 16));
    if (trace) {
      int ip = startoffset;
      int limit = (endoffset > startoffset && endoffset <= fn->chunk.count) ? endoffset : fn->chunk.count;
      int can_jit = 1;
      while (ip < limit && stepcount < looplen) {
        uint8_t op = fn->chunk.code[ip];
        if (op == OPCALL || op == OPCALLDYNAMIC || op == OPSPAWN || op == OPAWAIT ||
            op == OPTRYPUSH || op == OPTRYPOP || op == OPTAILCALL) {
          can_jit = 0;
          break;
        }
        trace[stepcount].op = op;
        trace[stepcount].ipoffset = ip;
        trace[stepcount].jumpoffset = 0;
        trace[stepcount].guardoutcome = 0;
        trace[stepcount].pushival = 0;
        trace[stepcount].pushfval = 0.0;
        trace[stepcount].slot = 0;

        int len = GetInstrLen(op, &fn->chunk.code[ip]);
        if (op == OPPUSHINT) {
          if (ip + 8 < fn->chunk.count) {
            int64_t val = 0;
            for (int b = 0; b < 8; b++) {
              val = (val << 8) | fn->chunk.code[ip + 1 + b];
            }
            trace[stepcount].pushival = val;
          }
        } else if (op == OPPUSHFLOAT) {
          if (ip + 8 < fn->chunk.count) {
            uint64_t val = 0;
            for (int b = 0; b < 8; b++) {
              val = (val << 8) | fn->chunk.code[ip + 1 + b];
            }
            double fval;
            memcpy(&fval, &val, 8);
            trace[stepcount].pushfval = fval;
          }
        } else if (op == OPPUSHBOOL) {
          if (ip + 1 < fn->chunk.count) {
            trace[stepcount].pushival = fn->chunk.code[ip + 1];
          }
        } else if (op == OPLOAD || op == OPSTORE) {
          if (ip + 2 < fn->chunk.count) {
            trace[stepcount].slot =
                (fn->chunk.code[ip + 1] << 8) | fn->chunk.code[ip + 2];
          }
        } else if (op == OPJUMP || op == OPJUMPIFFALSE || op == OPJUMPIFNULL ||
                   op == OPCOALESCE || op == OPTRYPUSH) {
          if (ip + 4 < fn->chunk.count) {
            int32_t offset =
                (int32_t)((fn->chunk.code[ip + 1] << 24) |
                          (fn->chunk.code[ip + 2] << 16) |
                          (fn->chunk.code[ip + 3] << 8) | fn->chunk.code[ip + 4]);
            trace[stepcount].jumpoffset = offset;
          }
        }
        ip += len;
        stepcount++;
      }

      if (can_jit && stepcount > 0) {
        void *func = NULL;
        int reldepth = fn->maxregisters > 0 ? fn->maxregisters : ((fn->localcount > fn->paramcount) ? fn->localcount : fn->paramcount);
        if (effective_jitmode == 1) {
          func = CompileX64Trace(vm, fn, startoffset, trace, stepcount, reldepth);
        } else if (effective_jitmode == 2) {
          func = CompileLLVMTrace(vm, fn, startoffset, trace, stepcount, reldepth);
        } else if (effective_jitmode == 3) {
          func = CompileQBETrace(vm, fn, startoffset, trace, stepcount, reldepth);
        }
        if (func) {
          int idx = vm->jitloopcount++;
          vm->jitloops[idx].fnid = fn->id;
          vm->jitloops[idx].targetoffset = startoffset;
          vm->jitloops[idx].libhandle = NULL;
          vm->jitloops[idx].jitfn = (void (*)(AKSValue *, int *, AKSValue *, void *))func;
          gVMProfiling.jit_tierings++;
          fprintf(stderr, "[JIT] In-memory compiled hot loop at offset %d (mode %d)\n",
                  startoffset, effective_jitmode);
          free(trace);
          MutexUnlockNative(vm->globallock);
          return;
        }
      }
      free(trace);
    }
    MutexUnlockNative(vm->globallock);
    return;
  }

  char cfile[128];
  char dllfile[128];
  snprintf(cfile, sizeof(cfile), "jitloop%d%d.c", fn->id, startoffset);
#ifdef _WIN32
  snprintf(dllfile, sizeof(dllfile), "jitloop%d%d.dll", fn->id, startoffset);
#else
  snprintf(dllfile, sizeof(dllfile), "jitloop%d%d.so", fn->id, startoffset);
#endif

  FILE *f = fopen(cfile, "w");
  if (!f) {
    MutexUnlockNative(vm->globallock);
    return;
  }

  fprintf(f, "#include <stdint.h>\n");
  fprintf(f, "#include <string.h>\n");
  fprintf(f, "#include <stdio.h>\n");
  fprintf(f, "#include <stdlib.h>\n");
  fprintf(f, "\n");

  fprintf(f, "typedef enum {\n");
  fprintf(f, "    VALFLOAT = 0, VALINT = 1, VALBOOL = 2, VALNULL = 3, VALSTR = "
             "4, VALARR = 5, VALSTRUCT = 6, VALFN = 7, VALTASK = 8, VALCHANNEL "
             "= 9, VALMUTEX = 10, VALSEM = 11, VALBOUNDMETHOD = 12\n");
  fprintf(f, "} ValType;\n\n");

  fprintf(f, "typedef uint64_t AKSValue;\n\n");

  fprintf(f, "#define VALFLOATLIMIT 0xFFF1000000000000ULL\n\n");

  fprintf(f, "static inline ValType valtype(AKSValue v) {\n");
  fprintf(f, "    if (v < VALFLOATLIMIT) return VALFLOAT;\n");
  fprintf(f, "    return (ValType)((v >> 48) - 0xFFF0);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline int64_t valtoint(AKSValue v) {\n");
  fprintf(f, "    uint64_t payload = v & 0x0000FFFFFFFFFFFFULL;\n");
  fprintf(f, "    if (payload & 0x0000800000000000ULL) {\n");
  fprintf(f, "        return (int64_t)(payload | 0xFFFF000000000000ULL);\n");
  fprintf(f, "    }\n");
  fprintf(f, "    return (int64_t)payload;\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline double valtofloat(AKSValue v) {\n");
  fprintf(f, "    union { uint64_t u; double d; } u;\n");
  fprintf(f, "    u.u = v;\n");
  fprintf(f, "    return u.d;\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline int valtobool(AKSValue v) {\n");
  fprintf(f, "    return (int)(v & 1);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline AKSValue valint(int64_t i) {\n");
  fprintf(f, "    return (0xFFF1ULL << 48) | ((uint64_t)i & "
             "0x0000FFFFFFFFFFFFULL);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline AKSValue valfloat(double f) {\n");
  fprintf(f, "    union { uint64_t u; double d; } u;\n");
  fprintf(f, "    u.d = f;\n");
  fprintf(
      f, "    if ((u.u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) {\n");
  fprintf(f, "        u.u = 0x7FF8000000000000ULL;\n");
  fprintf(f, "    }\n");
  fprintf(f, "    return u.u;\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline AKSValue valbool(int b) {\n");
  fprintf(f, "    return (0xFFF2ULL << 48) | (b ? 1 : 0);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline AKSValue valnull(void) {\n");
  fprintf(f, "    return (0xFFF3ULL << 48);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "#ifdef _WIN32\n");
  fprintf(f, "__declspec(dllexport) int jitfn(AKSValue *stack, int "
             "*stacktopptr, AKSValue *locals, void *vmptr, AKSValue "
             "(*valcopy)(AKSValue), void (*valfree_ptr)(AKSValue*)) {\n");
  fprintf(f, "#else\n");
  fprintf(f, "int jitfn(AKSValue *stack, int *stacktopptr, AKSValue *locals, "
             "void *vmptr, AKSValue (*valcopy)(AKSValue), void "
             "(*valfree_ptr)(AKSValue*)) {\n");
  fprintf(f, "#endif\n");
  fprintf(f, "#define valfree(v) do { if (valtype(v) >= VALSTR) "
             "valfree_ptr(&(v)); } while(0)\n\n");

  for (int s = 0; s < fn->localcount; s++) {
    fprintf(f, "    AKSValue local_%d = locals[%d];\n", s, s);
  }

  fprintf(f, "    int top = *stacktopptr;\n");
  fprintf(f, "    int64_t val = 0;\n");

  int ipoffset = startoffset;
  while (ipoffset < endoffset) {
    uint8_t op = fn->chunk.code[ipoffset];
    fprintf(f, "label_%d:\n", ipoffset);

    switch (op) {
    case OPPUSHINT: {
      int64_t val = 0;
      for (int k = 0; k < 8; k++) {
        val = (val << 8) | fn->chunk.code[ipoffset + 1 + k];
      }
      fprintf(f, "    stack[top++] = valint(%lld);\n", (long long)val);
      ipoffset += 9;
      break;
    }
    case OPPUSHFLOAT: {
      uint64_t bits = 0;
      for (int k = 0; k < 8; k++) {
        bits = (bits << 8) | fn->chunk.code[ipoffset + 1 + k];
      }
      double dval;
      memcpy(&dval, &bits, 8);
      fprintf(f, "    stack[top++] = valfloat(%g);\n", dval);
      ipoffset += 9;
      break;
    }
    case OPLOAD: {
      uint16_t slot =
          (fn->chunk.code[ipoffset + 1] << 8) | fn->chunk.code[ipoffset + 2];
      fprintf(f, "    stack[top++] = valcopy(local_%d);\n", slot);
      ipoffset += 3;
      break;
    }
    case OPSTORE: {
      uint16_t slot =
          (fn->chunk.code[ipoffset + 1] << 8) | fn->chunk.code[ipoffset + 2];
      fprintf(f, "    valfree(local_%d);\n", slot);
      fprintf(f, "    local_%d = stack[--top];\n", slot);
      ipoffset += 3;
      break;
    }
    case OPADD:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valint(valtoint(stack[top-2]) + "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(
          f,
          "        stack[top-2] = valfloat((valtype(stack[top-2]) == VALFLOAT "
          "? valtofloat(stack[top-2]) : (double)valtoint(stack[top-2])) + "
          "(valtype(stack[top-1]) == VALFLOAT ? valtofloat(stack[top-1]) : "
          "(double)valtoint(stack[top-1]))); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPSUB:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valint(valtoint(stack[top-2]) - "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(
          f,
          "        stack[top-2] = valfloat((valtype(stack[top-2]) == VALFLOAT "
          "? valtofloat(stack[top-2]) : (double)valtoint(stack[top-2])) - "
          "(valtype(stack[top-1]) == VALFLOAT ? valtofloat(stack[top-1]) : "
          "(double)valtoint(stack[top-1]))); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPMUL:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valint(valtoint(stack[top-2]) * "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(
          f,
          "        stack[top-2] = valfloat((valtype(stack[top-2]) == VALFLOAT "
          "? valtofloat(stack[top-2]) : (double)valtoint(stack[top-2])) * "
          "(valtype(stack[top-1]) == VALFLOAT ? valtofloat(stack[top-1]) : "
          "(double)valtoint(stack[top-1]))); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPDIV:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        if (valtoint(stack[top-1]) != 0) { stack[top-2] = "
                 "valint(valtoint(stack[top-2]) / valtoint(stack[top-1])); "
                 "top--; }\n");
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double divisor = valtype(stack[top-1]) == VALFLOAT ? "
              "valtofloat(stack[top-1]) : (double)valtoint(stack[top-1]);\n");
      fprintf(f, "        if (divisor != 0.0) { stack[top-2] = "
                 "valfloat((valtype(stack[top-2]) == VALFLOAT ? "
                 "valtofloat(stack[top-2]) : (double)valtoint(stack[top-2])) / "
                 "divisor); top--; }\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPINC:
      fprintf(f, "    if (valtype(stack[top-1]) == VALINT) { stack[top-1] = "
                 "valint(valtoint(stack[top-1]) + 1); }\n");
      fprintf(f,
              "    else if (valtype(stack[top-1]) == VALFLOAT) { stack[top-1] "
              "= valfloat(valtofloat(stack[top-1]) + 1.0); }\n");
      ipoffset += 1;
      break;
    case OPDEC:
      fprintf(f, "    if (valtype(stack[top-1]) == VALINT) { stack[top-1] = "
                 "valint(valtoint(stack[top-1]) - 1); }\n");
      fprintf(f,
              "    else if (valtype(stack[top-1]) == VALFLOAT) { stack[top-1] "
              "= valfloat(valtofloat(stack[top-1]) - 1.0); }\n");
      ipoffset += 1;
      break;
    case OPLT:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valbool(valtoint(stack[top-2]) < "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = valtype(stack[top-2]) == VALFLOAT ? "
              "valtofloat(stack[top-2]) : (double)valtoint(stack[top-2]);\n");
      fprintf(f,
              "        double rv = valtype(stack[top-1]) == VALFLOAT ? "
              "valtofloat(stack[top-1]) : (double)valtoint(stack[top-1]);\n");
      fprintf(f, "        stack[top-2] = valbool(lv < rv); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPGT:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valbool(valtoint(stack[top-2]) > "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = valtype(stack[top-2]) == VALFLOAT ? "
              "valtofloat(stack[top-2]) : (double)valtoint(stack[top-2]);\n");
      fprintf(f,
              "        double rv = valtype(stack[top-1]) == VALFLOAT ? "
              "valtofloat(stack[top-1]) : (double)valtoint(stack[top-1]);\n");
      fprintf(f, "        stack[top-2] = valbool(lv > rv); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPLTE:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valbool(valtoint(stack[top-2]) <= "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = valtype(stack[top-2]) == VALFLOAT ? "
              "valtofloat(stack[top-2]) : (double)valtoint(stack[top-2]);\n");
      fprintf(f,
              "        double rv = valtype(stack[top-1]) == VALFLOAT ? "
              "valtofloat(stack[top-1]) : (double)valtoint(stack[top-1]);\n");
      fprintf(f, "        stack[top-2] = valbool(lv <= rv); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPGTE:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valbool(valtoint(stack[top-2]) >= "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = valtype(stack[top-2]) == VALFLOAT ? "
              "valtofloat(stack[top-2]) : (double)valtoint(stack[top-2]);\n");
      fprintf(f,
              "        double rv = valtype(stack[top-1]) == VALFLOAT ? "
              "valtofloat(stack[top-1]) : (double)valtoint(stack[top-1]);\n");
      fprintf(f, "        stack[top-2] = valbool(lv >= rv); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPEQ:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valbool(valtoint(stack[top-2]) == "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = valtype(stack[top-2]) == VALFLOAT ? "
              "valtofloat(stack[top-2]) : (double)valtoint(stack[top-2]);\n");
      fprintf(f,
              "        double rv = valtype(stack[top-1]) == VALFLOAT ? "
              "valtofloat(stack[top-1]) : (double)valtoint(stack[top-1]);\n");
      fprintf(f, "        stack[top-2] = valbool(lv == rv); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPNEQ:
      fprintf(f, "    if (valtype(stack[top-2]) == VALINT && "
                 "valtype(stack[top-1]) == VALINT) {\n");
      fprintf(f, "        stack[top-2] = valbool(valtoint(stack[top-2]) != "
                 "valtoint(stack[top-1])); top--;\n");
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = valtype(stack[top-2]) == VALFLOAT ? "
              "valtofloat(stack[top-2]) : (double)valtoint(stack[top-2]);\n");
      fprintf(f,
              "        double rv = valtype(stack[top-1]) == VALFLOAT ? "
              "valtofloat(stack[top-1]) : (double)valtoint(stack[top-1]);\n");
      fprintf(f, "        stack[top-2] = valbool(lv != rv); top--;\n");
      fprintf(f, "    }\n");
      ipoffset += 1;
      break;
    case OPJUMPIFFALSE: {
      int32_t offset = (int32_t)((fn->chunk.code[ipoffset + 1] << 24) |
                                 (fn->chunk.code[ipoffset + 2] << 16) |
                                 (fn->chunk.code[ipoffset + 3] << 8) |
                                 fn->chunk.code[ipoffset + 4]);
      int destaddr = ipoffset + 5 + offset;
      if (destaddr < startoffset || destaddr >= endoffset) {
        fprintf(f, "    if (!valtobool(stack[--top])) { *stacktopptr = top; ");
        for (int s = 0; s < fn->localcount; s++) {
          fprintf(f, "locals[%d] = local_%d; ", s, s);
        }
        fprintf(f, "return %d; }\n", destaddr);
      } else {
        fprintf(f, "    if (!valtobool(stack[--top])) goto label_%d;\n",
                destaddr);
      }
      ipoffset += 5;
      break;
    }
    case OPJUMP: {
      int32_t offset = (int32_t)((fn->chunk.code[ipoffset + 1] << 24) |
                                 (fn->chunk.code[ipoffset + 2] << 16) |
                                 (fn->chunk.code[ipoffset + 3] << 8) |
                                 fn->chunk.code[ipoffset + 4]);
      int destaddr = ipoffset + 5 + offset;
      if (destaddr < startoffset || destaddr >= endoffset) {
        fprintf(f, "    *stacktopptr = top; ");
        for (int s = 0; s < fn->localcount; s++) {
          fprintf(f, "locals[%d] = local_%d; ", s, s);
        }
        fprintf(f, "return %d;\n", destaddr);
      } else {
        fprintf(f, "    goto label_%d;\n", destaddr);
      }
      ipoffset += 5;
      break;
    }
    case OPPOP:
      fprintf(f, "    valfree(stack[top-1]);\n");
      fprintf(f, "    top--;\n");
      ipoffset += 1;
      break;
    case OPDUP:
      fprintf(f, "    stack[top] = valcopy(stack[top-1]); top++;\n");
      ipoffset += 1;
      break;
    case OPPUSHNULL:
      fprintf(f, "    stack[top++] = valnull();\n");
      ipoffset += 1;
      break;
    case OPPUSHBOOL: {
      uint8_t val = fn->chunk.code[ipoffset + 1];
      fprintf(f, "    stack[top++] = valbool(%d);\n", val);
      ipoffset += 2;
      break;
    }
    default:
      fprintf(f, "    *stacktopptr = top; ");
      for (int s = 0; s < fn->localcount; s++) {
        fprintf(f, "locals[%d] = local_%d; ", s, s);
      }
      fprintf(f, "return %d;\n", ipoffset);
      ipoffset += 1;
      break;
    }
  }

  fprintf(f, "    *stacktopptr = top;\n");
  for (int s = 0; s < fn->localcount; s++) {
    fprintf(f, "    locals[%d] = local_%d;\n", s, s);
  }
  fprintf(f, "    return %d;\n", endoffset);
  fprintf(f, "}\n");
  fclose(f);

#ifdef _WIN32
  char batname[256];
  snprintf(batname, sizeof(batname), "compilejit_%d_%d.bat", fn->id,
           startoffset);
  FILE *bat = fopen(batname, "w");
  if (bat) {
    fprintf(bat, "@echo off\n");
    fprintf(
        bat,
        "if exist \"C:\\Program Files\\Microsoft Visual "
        "Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat\" (\n");
    fprintf(bat, "    call \"C:\\Program Files\\Microsoft Visual "
                 "Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall."
                 "bat\" x64 >nul 2>nul\n");
    fprintf(bat, ") else (\n");
    fprintf(bat, "    call \"C:\\Program Files\\Microsoft Visual "
                 "Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat\" "
                 "x64 >nul 2>nul\n");
    fprintf(bat, ")\n");
    fprintf(bat,
            "cl.exe /LD /O2 \"%s\" /Fe:\"%s\" /link /DYNAMICBASE "
            "/HIGHENTROPYVA >nul 2>nul\n",
            cfile, dllfile);
    fclose(bat);
    system(batname);
    remove(batname);
  }
#else
  char compilecmd[512];
  snprintf(compilecmd, sizeof(compilecmd),
           "gcc -O3 -shared -fPIC \"%s\" -o \"%s\" >/dev/null 2>&1", cfile,
           dllfile);
  system(compilecmd);
#endif

  // clean up temporary C and object files safely using C remove()
  remove(cfile);
#ifdef _WIN32
  char tmpobj[256], tmplib[256], tmpexp[256];
  snprintf(tmpobj, sizeof(tmpobj), "jitloop%d%d.obj", fn->id, startoffset);
  snprintf(tmplib, sizeof(tmplib), "jitloop%d%d.lib", fn->id, startoffset);
  snprintf(tmpexp, sizeof(tmpexp), "jitloop%d%d.exp", fn->id, startoffset);
  remove(tmpobj);
  remove(tmplib);
  remove(tmpexp);
#endif

  void *lib = NULL;
#ifdef _WIN32
  lib = (void *)LoadLibraryA(dllfile);
#else
  lib = dlopen(dllfile, RTLD_LAZY);
#endif

  if (lib) {
    void *jitfnptr = NULL;
#ifdef _WIN32
    jitfnptr = (void *)GetProcAddress((HMODULE)lib, "jitfn");
#else
    jitfnptr = dlsym(lib, "jitfn");
#endif
    if (jitfnptr) {
      int idx = vm->jitloopcount++;
      vm->jitloops[idx].fnid = fn->id;
      vm->jitloops[idx].targetoffset = startoffset;
      vm->jitloops[idx].libhandle = lib;
      vm->jitloops[idx].jitfn =
          (void (*)(AKSValue *, int *, AKSValue *, void *))jitfnptr;
      fprintf(stderr, "[JIT] Compiled hot loop at offset %d\n", startoffset);
    } else {
#ifdef _WIN32
      FreeLibrary((HMODULE)lib);
#else
      dlclose(lib);
#endif
    }
  }
  MutexUnlockNative(vm->globallock);
}

static void TriggerTracingJIT(VM *vm, VMTask *task, FnProto *fn,
                              int startoffset, int targetoffset,
                              TraceStep *trace, int stepcount) {
  if (vm->sandboxenabled)
    return;
  MutexLockNative(vm->globallock);
  if (vm->jitloopcount >= 128) {
    MutexUnlockNative(vm->globallock);
    return;
  }

  for (int i = 0; i < vm->jitloopcount; i++) {
    if (vm->jitloops[i].fnid == fn->id &&
        vm->jitloops[i].targetoffset == targetoffset) {
      MutexUnlockNative(vm->globallock);
      return;
    }
  }

  for (int s = 0; s < stepcount; s++) {
    uint8_t op = trace[s].op;
    if (op == OPCALL || op == OPCALLDYNAMIC || op == OPSPAWN || op == OPAWAIT ||
        op == OPTRYPUSH || op == OPTRYPOP || op == OPTAILCALL) {
      MutexUnlockNative(vm->globallock);
      return;
    }
  }

  if (vm->jitcacheenabled && vm->jitmode == 1) {
    NewDir(".akscache");
    NewDir(".akscache/jitcache");
    char cachefile[256];
    snprintf(cachefile, sizeof(cachefile),
             ".akscache/jitcache/trace_%d_%d_%d.bin", fn->id, startoffset,
             stepcount);
    int size = 0;
    void *exec = LoadX64TraceFromCache(cachefile, &size);
    if (exec) {
      int idx = vm->jitloopcount++;
      vm->jitloops[idx].fnid = fn->id;
      vm->jitloops[idx].targetoffset = targetoffset;
      vm->jitloops[idx].libhandle = NULL;
      vm->jitloops[idx].jitfn =
          (void (*)(AKSValue *, int *, AKSValue *, void *))exec;
      MutexUnlockNative(vm->globallock);
      return;
    }
  }

#if defined(__x86_64__) || defined(_M_X64)
  int effective_mode = (vm->jitmode > 0) ? vm->jitmode : 1;
#else
  int effective_mode = vm->jitmode;
#endif
  if (effective_mode > 0) {
    void *func = NULL;
    int base_reg = fn->maxregisters > 0 ? fn->maxregisters : ((fn->localcount > fn->paramcount) ? fn->localcount : fn->paramcount);
    int reldepth = (targetoffset == -1)
                       ? base_reg
                       : (task ? (task->stacktop -
                                  task->frames[task->framecount - 1].slotsbase)
                               : base_reg);
    if (reldepth < base_reg) reldepth = base_reg;
    if (effective_mode == 1) {
      func = CompileX64Trace(vm, fn, startoffset, trace, stepcount, reldepth);
    } else if (effective_mode == 2) {
      func = CompileLLVMTrace(vm, fn, startoffset, trace, stepcount, reldepth);
    } else if (effective_mode == 3) {
      func = CompileQBETrace(vm, fn, startoffset, trace, stepcount, reldepth);
    }

    if (func) {
      int idx = vm->jitloopcount++;
      vm->jitloops[idx].fnid = fn->id;
      vm->jitloops[idx].targetoffset = targetoffset;
      vm->jitloops[idx].libhandle = NULL;
      vm->jitloops[idx].jitfn =
          (void (*)(AKSValue *, int *, AKSValue *, void *))func;
      fprintf(stderr, "[Tracing JIT] Compiled hot trace at offset %d (mode %d)\n",
             startoffset, vm->jitmode);
      MutexUnlockNative(vm->globallock);
      return;
    }
    MutexUnlockNative(vm->globallock);
    return;
  }

  char cfile[128];
  CallFrame *frame = &task->frames[task->framecount - 1];
  char dllfile[128];
  snprintf(cfile, sizeof(cfile), "jittrace%d%d.c", fn->id, startoffset);
#ifdef _WIN32
  snprintf(dllfile, sizeof(dllfile), "jittrace%d%d.dll", fn->id, startoffset);
#else
  snprintf(dllfile, sizeof(dllfile), "jittrace%d%d.so", fn->id, startoffset);
#endif

  FILE *f = fopen(cfile, "w");
  if (!f) {
    MutexUnlockNative(vm->globallock);
    return;
  }

  fprintf(f, "#include <stdint.h>\n");
  fprintf(f, "#include <string.h>\n");
  fprintf(f, "#include <stdio.h>\n");
  fprintf(f, "#include <stdlib.h>\n");
  fprintf(f, "\n");

  fprintf(f, "typedef enum {\n");
  fprintf(f, "    VALFLOAT = 0, VALINT = 1, VALBOOL = 2, VALNULL = 3, VALSTR = "
             "4, VALARR = 5, VALSTRUCT = 6, VALFN = 7, VALTASK = 8, VALCHANNEL "
             "= 9, VALMUTEX = 10, VALSEM = 11, VALBOUNDMETHOD = 12\n");
  fprintf(f, "} ValType;\n\n");

  fprintf(f, "typedef uint64_t AKSValue;\n\n");

  fprintf(f, "#define VALFLOATLIMIT 0xFFF1000000000000ULL\n\n");

  fprintf(f, "static inline ValType valtype(AKSValue v) {\n");
  fprintf(f, "    if (v < VALFLOATLIMIT) return VALFLOAT;\n");
  fprintf(f, "    return (ValType)((v >> 48) - 0xFFF0);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline int64_t valtoint(AKSValue v) {\n");
  fprintf(f, "    uint64_t payload = v & 0x0000FFFFFFFFFFFFULL;\n");
  fprintf(f, "    if (payload & 0x0000800000000000ULL) {\n");
  fprintf(f, "        return (int64_t)(payload | 0xFFFF000000000000ULL);\n");
  fprintf(f, "    }\n");
  fprintf(f, "    return (int64_t)payload;\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline double valtofloat(AKSValue v) {\n");
  fprintf(f, "    union { uint64_t u; double d; } u;\n");
  fprintf(f, "    u.u = v;\n");
  fprintf(f, "    return u.d;\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline int valtobool(AKSValue v) {\n");
  fprintf(f, "    return (int)(v & 1);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline AKSValue valint(int64_t i) {\n");
  fprintf(f, "    return (0xFFF1ULL << 48) | ((uint64_t)i & "
             "0x0000FFFFFFFFFFFFULL);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline AKSValue valfloat(double f) {\n");
  fprintf(f, "    union { uint64_t u; double d; } u;\n");
  fprintf(f, "    u.d = f;\n");
  fprintf(
      f, "    if ((u.u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) {\n");
  fprintf(f, "        u.u = 0x7FF8000000000000ULL;\n");
  fprintf(f, "    }\n");
  fprintf(f, "    return u.u;\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline AKSValue valbool(int b) {\n");
  fprintf(f, "    return (0xFFF2ULL << 48) | (b ? 1 : 0);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "static inline AKSValue valnull(void) {\n");
  fprintf(f, "    return (0xFFF3ULL << 48);\n");
  fprintf(f, "}\n\n");

  fprintf(f, "#ifdef _WIN32\n");
  fprintf(f, "__declspec(dllexport) int jitfn(AKSValue *stack, int "
             "*stacktopptr, AKSValue *locals, void *vmptr, AKSValue "
             "(*valcopy)(AKSValue), void (*valfree_ptr)(AKSValue*)) {\n");
  fprintf(f, "#else\n");
  fprintf(f, "int jitfn(AKSValue *stack, int *stacktopptr, AKSValue *locals, "
             "void *vmptr, AKSValue (*valcopy)(AKSValue), void "
             "(*valfree_ptr)(AKSValue*)) {\n");
  fprintf(f, "#endif\n");
  fprintf(f, "#define valfree(v) do { if (valtype(v) >= VALSTR) "
             "valfree_ptr(&(v)); } while(0)\n\n");

  // Declare VM registers
  fprintf(f, "    AKSValue r0 = valnull()");
  for (int i = 1; i < 128; i++) {
    fprintf(f, ", r%d = valnull()", i);
  }
  fprintf(f, ";\n");

  // load local variables
  for (int s = 0; s < fn->localcount; s++) {
    fprintf(f, "    AKSValue local_%d = valcopy(locals[%d]);\n", s, s);
  }

  // load existing stack elements into registers (frame-relative)
  fprintf(f, "    int startdepth = *stacktopptr - (int)(locals - stack);\n");
  fprintf(f, "    if (startdepth > 128) startdepth = 128;\n");
  fprintf(f, "    switch (startdepth) {\n");
  for (int i = 128; i >= 1; i--) {
    fprintf(f, "        case %d: r%d = valcopy(locals[%d]);\n", i, i - 1,
            i - 1);
  }
  fprintf(f, "        default: break;\n");
  fprintf(f, "    }\n");

  fprintf(f, "    int vtop = startdepth;\n");

  // truthiness check - null, false, 0, 0.0 all count as falsy
  fprintf(f,
          "#define ISTRUTHY(v) (valtype(v) != VALNULL && !(valtype(v) == "
          "VALBOOL && !valtobool(v)) && !(valtype(v) == VALINT && valtoint(v) "
          "== 0) && !(valtype(v) == VALFLOAT && valtofloat(v) == 0.0))\n");

  // flush locals + stack back before bailing out to the interpreter
  fprintf(f, "#define WRITEBACKANDRETURN(retval, final_vtop) do { \\\n");
  fprintf(f, "    *stacktopptr = (int)(locals - stack) + (final_vtop); \\\n");
  for (int s = 0; s < fn->localcount; s++) {
    fprintf(f, "    valfree(locals[%d]); locals[%d] = local_%d; \\\n", s, s, s);
  }
  fprintf(f, "    switch (final_vtop) { \\\n");
  for (int i = 128; i >= 1; i--) {
    fprintf(f,
            "        case %d: if ((%d) >= startdepth) { valfree(locals[%d]); "
            "locals[%d] = r%d; } else { valfree(r%d); } \\\n",
            i, i - 1, i - 1, i - 1, i - 1, i - 1);
  }
  fprintf(f, "        default: break; \\\n");
  fprintf(f, "    } \\\n");
  for (int i = 0; i < 128; i++) {
    fprintf(f, "    if ((%d) >= final_vtop) { valfree(r%d); } \\\n", i, i);
  }
  fprintf(f, "    return (retval); \\\n");
  fprintf(f, "} while(0)\n\n");

  int vtop_track = (targetoffset == -1) ? fn->localcount
                                        : (task->stacktop - frame->slotsbase);
  for (int i = 0; i < stepcount; i++) {
    TraceStep *step = &trace[i];
    fprintf(f, "    // Step %d: Opcode %d at IP offset %d\n", i, step->op,
            step->ipoffset);
    switch (step->op) {
    case OPPUSHINT:
      fprintf(f, "    r%d = valint(%lld);\n", vtop_track,
              (long long)step->pushival);
      vtop_track++;
      break;
    case OPPUSHFLOAT:
      fprintf(f, "    r%d = valfloat(%g);\n", vtop_track, step->pushfval);
      vtop_track++;
      break;
    case OPPUSHBOOL:
      fprintf(f, "    r%d = valbool(%d);\n", vtop_track, (int)step->pushival);
      vtop_track++;
      break;
    case OPPUSHNULL:
      fprintf(f, "    r%d = valnull();\n", vtop_track);
      vtop_track++;
      break;
    case OPLOAD:
      fprintf(f, "    r%d = valcopy(local_%d);\n", vtop_track, step->slot);
      vtop_track++;
      break;
    case OPSTORE:
      vtop_track--;
      fprintf(f, "    valfree(local_%d);\n", step->slot);
      fprintf(f, "    local_%d = r%d;\n", step->slot, vtop_track);
      fprintf(f, "    r%d = valnull();\n", vtop_track);
      break;
    case OPADD:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valint(valtoint(r%d) + valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        r%d = valfloat((valtype(r%d) == VALFLOAT ? "
              "valtofloat(r%d) : (double)valtoint(r%d)) + (valtype(r%d) == "
              "VALFLOAT ? valtofloat(r%d) : (double)valtoint(r%d)));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1, vtop_track - 1,
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "    }\n");
      break;
    case OPSUB:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valint(valtoint(r%d) - valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        r%d = valfloat((valtype(r%d) == VALFLOAT ? "
              "valtofloat(r%d) : (double)valtoint(r%d)) - (valtype(r%d) == "
              "VALFLOAT ? valtofloat(r%d) : (double)valtoint(r%d)));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1, vtop_track - 1,
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "    }\n");
      break;
    case OPMUL:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valint(valtoint(r%d) * valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        r%d = valfloat((valtype(r%d) == VALFLOAT ? "
              "valtofloat(r%d) : (double)valtoint(r%d)) * (valtype(r%d) == "
              "VALFLOAT ? valtofloat(r%d) : (double)valtoint(r%d)));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1, vtop_track - 1,
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "    }\n");
      break;
    case OPDIV:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f,
              "        if (valtoint(r%d) != 0) r%d = valint(valtoint(r%d) / "
              "valtoint(r%d));\n",
              vtop_track, vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double divisor = (valtype(r%d) == VALFLOAT ? "
              "valtofloat(r%d) : (double)valtoint(r%d));\n",
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "        if (divisor != 0.0) {\n");
      fprintf(f,
              "            r%d = valfloat((valtype(r%d) == VALFLOAT ? "
              "valtofloat(r%d) : (double)valtoint(r%d)) / divisor);\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f, "        }\n");
      fprintf(f, "    }\n");
      break;
    case OPMOD:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f,
              "        if (valtoint(r%d) != 0) r%d = valint(valtoint(r%d) %% "
              "valtoint(r%d));\n",
              vtop_track, vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    }\n");
      break;
    case OPSHL:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) r%d = "
              "valint(valtoint(r%d) << valtoint(r%d));\n",
              vtop_track - 1, vtop_track, vtop_track - 1, vtop_track - 1,
              vtop_track);
      break;
    case OPSHR:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) r%d = "
              "valint(valtoint(r%d) >> valtoint(r%d));\n",
              vtop_track - 1, vtop_track, vtop_track - 1, vtop_track - 1,
              vtop_track);
      break;
    case OPBITAND:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) r%d = "
              "valint(valtoint(r%d) & valtoint(r%d));\n",
              vtop_track - 1, vtop_track, vtop_track - 1, vtop_track - 1,
              vtop_track);
      break;
    case OPBITOR:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) r%d = "
              "valint(valtoint(r%d) | valtoint(r%d));\n",
              vtop_track - 1, vtop_track, vtop_track - 1, vtop_track - 1,
              vtop_track);
      break;
    case OPBITXOR:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) r%d = "
              "valint(valtoint(r%d) ^ valtoint(r%d));\n",
              vtop_track - 1, vtop_track, vtop_track - 1, vtop_track - 1,
              vtop_track);
      break;
    case OPBITNOT:
      fprintf(f,
              "    if (valtype(r%d) == VALINT) r%d = valint(~valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      break;
    case OPINC:
      fprintf(
          f,
          "    if (valtype(r%d) == VALINT) r%d = valint(valtoint(r%d) + 1);\n",
          vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f,
              "    else if (valtype(r%d) == VALFLOAT) r%d = "
              "valfloat(valtofloat(r%d) + 1.0);\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      break;
    case OPDEC:
      fprintf(
          f,
          "    if (valtype(r%d) == VALINT) r%d = valint(valtoint(r%d) - 1);\n",
          vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f,
              "    else if (valtype(r%d) == VALFLOAT) r%d = "
              "valfloat(valtofloat(r%d) - 1.0);\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      break;
    case OPLT:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valbool(valtoint(r%d) < valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f,
              "        double rv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "        r%d = valbool(lv < rv);\n", vtop_track - 1);
      fprintf(f, "    }\n");
      break;
    case OPGT:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valbool(valtoint(r%d) > valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f,
              "        double rv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "        r%d = valbool(lv > rv);\n", vtop_track - 1);
      fprintf(f, "    }\n");
      break;
    case OPLTE:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valbool(valtoint(r%d) <= valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f,
              "        double rv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "        r%d = valbool(lv <= rv);\n", vtop_track - 1);
      fprintf(f, "    }\n");
      break;
    case OPGTE:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valbool(valtoint(r%d) >= valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f,
              "        double rv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "        r%d = valbool(lv >= rv);\n", vtop_track - 1);
      fprintf(f, "    }\n");
      break;
    case OPEQ:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valbool(valtoint(r%d) == valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f,
              "        double rv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "        r%d = valbool(lv == rv);\n", vtop_track - 1);
      fprintf(f, "    }\n");
      break;
    case OPNEQ:
      vtop_track--;
      fprintf(f,
              "    if (valtype(r%d) == VALINT && valtype(r%d) == VALINT) {\n",
              vtop_track - 1, vtop_track);
      fprintf(f, "        r%d = valbool(valtoint(r%d) != valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track);
      fprintf(f, "    } else {\n");
      fprintf(f,
              "        double lv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track - 1, vtop_track - 1, vtop_track - 1);
      fprintf(f,
              "        double rv = (valtype(r%d) == VALFLOAT ? valtofloat(r%d) "
              ": (double)valtoint(r%d));\n",
              vtop_track, vtop_track, vtop_track);
      fprintf(f, "        r%d = valbool(lv != rv);\n", vtop_track - 1);
      fprintf(f, "    }\n");
      break;
    case OPDUP:
      fprintf(f, "    r%d = valcopy(r%d);\n", vtop_track, vtop_track - 1);
      vtop_track++;
      break;
    case OPPOP:
      vtop_track--;
      fprintf(f, "    valfree(r%d);\n", vtop_track);
      fprintf(f, "    r%d = valnull();\n", vtop_track);
      break;
    case OPJUMPIFFALSE: {
      vtop_track--;
      int destaddr = step->ipoffset + 5 + step->jumpoffset;
      int fallthroughaddr = step->ipoffset + 5;
      fprintf(f, "    int cond_truthy = ISTRUTHY(r%d);\n", vtop_track);
      fprintf(f, "    valfree(r%d);\n", vtop_track);
      fprintf(f, "    r%d = valnull();\n", vtop_track);
      if (step->guardoutcome == 1) {
        fprintf(f, "    if (cond_truthy) { WRITEBACKANDRETURN(%d, %d); }\n",
                fallthroughaddr, vtop_track);
      } else {
        fprintf(f, "    if (!cond_truthy) { WRITEBACKANDRETURN(%d, %d); }\n",
                destaddr, vtop_track);
      }
      break;
    }
    case OPJUMP: {
      int destaddr = step->ipoffset + 5 + step->jumpoffset;
      fprintf(f, "    WRITEBACKANDRETURN(%d, %d);\n", destaddr, vtop_track);
      break;
    }
    default:
      fprintf(f, "    WRITEBACKANDRETURN(%d, %d);\n", step->ipoffset,
              vtop_track);
      break;
    }
  }

  fprintf(f, "    WRITEBACKANDRETURN(%d, %d);\n", startoffset, vtop_track);
  fprintf(f, "}\n");
  fclose(f);

#ifdef _WIN32
  char batname[256];
  snprintf(batname, sizeof(batname), "compilejit_tr_%d_%d.bat", fn->id,
           startoffset);
  FILE *bat = fopen(batname, "w");
  if (bat) {
    fprintf(bat, "@echo off\n");
    fprintf(
        bat,
        "if exist \"C:\\Program Files\\Microsoft Visual "
        "Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat\" (\n");
    fprintf(bat, "    call \"C:\\Program Files\\Microsoft Visual "
                 "Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall."
                 "bat\" x64 >nul 2>nul\n");
    fprintf(bat, ") else (\n");
    fprintf(bat, "    call \"C:\\Program Files\\Microsoft Visual "
                 "Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat\" "
                 "x64 >nul 2>nul\n");
    fprintf(bat, ")\n");
    fprintf(bat,
            "cl.exe /LD /O2 \"%s\" /Fe:\"%s\" /link /DYNAMICBASE "
            "/HIGHENTROPYVA >nul 2>nul\n",
            cfile, dllfile);
    fclose(bat);
    system(batname);
    remove(batname);
  }
#else
  char compilecmd[512];
  snprintf(compilecmd, sizeof(compilecmd),
           "gcc -O3 -shared -fPIC \"%s\" -o \"%s\" >/dev/null 2>&1", cfile,
           dllfile);
  system(compilecmd);
#endif

  // clean up temporary C and object files safely using C remove()
  remove(cfile);
#ifdef _WIN32
  char tmpobj[256], tmplib[256], tmpexp[256];
  snprintf(tmpobj, sizeof(tmpobj), "jittrace%d%d.obj", fn->id, startoffset);
  snprintf(tmplib, sizeof(tmplib), "jittrace%d%d.lib", fn->id, startoffset);
  snprintf(tmpexp, sizeof(tmpexp), "jittrace%d%d.exp", fn->id, startoffset);
  remove(tmpobj);
  remove(tmplib);
  remove(tmpexp);
#endif

  void *lib = NULL;
#ifdef _WIN32
  lib = (void *)LoadLibraryA(dllfile);
#else
  lib = dlopen(dllfile, RTLD_LAZY);
#endif

  if (lib) {
    void *jitfnptr = NULL;
#ifdef _WIN32
    jitfnptr = (void *)GetProcAddress((HMODULE)lib, "jitfn");
#else
    jitfnptr = dlsym(lib, "jitfn");
#endif
    if (jitfnptr) {
      int idx = vm->jitloopcount++;
      vm->jitloops[idx].fnid = fn->id;
      vm->jitloops[idx].targetoffset = targetoffset;
      vm->jitloops[idx].libhandle = lib;
      vm->jitloops[idx].jitfn =
          (void (*)(AKSValue *, int *, AKSValue *, void *))jitfnptr;
      if (stepcount > 0) {
        extern const char *StackOpName(int op);
        fprintf(stderr, "[Tracing JIT] Compiled hot loop for function %s (ID %d) at "
               "offset %d with %d trace steps\n",
               fn->name, fn->id, startoffset, stepcount);
        for (int s = 0; s < stepcount; s++) {
          fprintf(stderr, "  Step %d: Opcode %d (%s) at IP offset %d\n", s, trace[s].op,
                 StackOpName(trace[s].op), trace[s].ipoffset);
        }
        fflush(stderr);
      }
    } else {
#ifdef _WIN32
      FreeLibrary((HMODULE)lib);
#else
      dlclose(lib);
#endif
    }
  }
  MutexUnlockNative(vm->globallock);
}
static void TriggerMethodJIT(VM *vm, VMTask *task, FnProto *fn) {
  if (vm->sandboxenabled)
    return;
  gVMProfiling.hot_function_triggers++;
  if (!fn->chunk.code || fn->chunk.count == 0)
    return; /* skip empty stdlib stubs */
  if (vm->jitloopcount >= 128)
    return;

  for (int i = 0; i < vm->jitloopcount; i++) {
    if (vm->jitloops[i].fnid == fn->id && vm->jitloops[i].targetoffset == -1) {
      return;
    }
  }

  int stepcount = 0;
  TraceStep *trace = malloc(sizeof(TraceStep) * (fn->chunk.count + 1));
  if (!trace)
    return;

  int ip = 0;
  while (ip < fn->chunk.count) {
    uint8_t op = fn->chunk.code[ip];
    if (op == OPCALL || op == OPCALLDYNAMIC || op == OPSPAWN || op == OPAWAIT ||
        op == OPTRYPUSH || op == OPTRYPOP || op == OPTAILCALL ||
        op == OPJUMP || op == OPJUMPIFFALSE || op == OPJUMPIFNULL) {
      free(trace);
      return;
    }
    trace[stepcount].op = op;
    trace[stepcount].ipoffset = ip;
    trace[stepcount].jumpoffset = 0;
    trace[stepcount].guardoutcome = 0;
    trace[stepcount].pushival = 0;
    trace[stepcount].pushfval = 0.0;
    trace[stepcount].slot = 0;

    int len = GetInstrLen(op, &fn->chunk.code[ip]);
    if (op == OPPUSHINT) {
      if (ip + 8 < fn->chunk.count) {
        int64_t val = 0;
        for (int b = 0; b < 8; b++) {
          val = (val << 8) | fn->chunk.code[ip + 1 + b];
        }
        trace[stepcount].pushival = val;
      }
    } else if (op == OPPUSHFLOAT) {
      if (ip + 8 < fn->chunk.count) {
        uint64_t val = 0;
        for (int b = 0; b < 8; b++) {
          val = (val << 8) | fn->chunk.code[ip + 1 + b];
        }
        double fval;
        memcpy(&fval, &val, 8);
        trace[stepcount].pushfval = fval;
      }
    } else if (op == OPPUSHBOOL) {
      if (ip + 1 < fn->chunk.count) {
        trace[stepcount].pushival = fn->chunk.code[ip + 1];
      }
    } else if (op == OPLOAD || op == OPSTORE) {
      if (ip + 2 < fn->chunk.count) {
        trace[stepcount].slot =
            (fn->chunk.code[ip + 1] << 8) | fn->chunk.code[ip + 2];
      }
    } else if (op == OPJUMP || op == OPJUMPIFFALSE || op == OPJUMPIFNULL ||
               op == OPCOALESCE || op == OPTRYPUSH) {
      if (ip + 4 < fn->chunk.count) {
        int32_t offset =
            (int32_t)((fn->chunk.code[ip + 1] << 24) |
                      (fn->chunk.code[ip + 2] << 16) |
                      (fn->chunk.code[ip + 3] << 8) | fn->chunk.code[ip + 4]);
        trace[stepcount].jumpoffset = offset;
      }
    }
    ip += len;
    stepcount++;
  }

  TriggerTracingJIT(vm, task, fn, 0, -1, trace, stepcount);
  free(trace);
}

static int HasJITCompiled(FnProto *fn, int targetoffset, VM *vm) {
  for (int i = 0; i < vm->jitloopcount; i++) {
    if (vm->jitloops[i].fnid == fn->id &&
        vm->jitloops[i].targetoffset == targetoffset) {
      return 1;
    }
  }
  return 0;
}

static void ExecuteJITCompiled(VM *vm, VMTask *task, FnProto *fn,
                               int targetoffset, AKSValue **stacktopptr,
                               CallFrame **frameptr, AKSValue **frameslotsptr,
                               uint8_t **ipptr) {
  if (!fn->chunk.code || fn->chunk.count == 0)
    return; /* guard against empty stubs */
  for (int i = 0; i < vm->jitloopcount; i++) {
    if (vm->jitloops[i].fnid == fn->id &&
        vm->jitloops[i].targetoffset == targetoffset) {
      int top = (int)(*stacktopptr - task->stack);
      typedef int (*jitloopfnt)(AKSValue *, int *, AKSValue *, void *,
                                AKSValue (*)(AKSValue), void (*)(AKSValue *));
      int resumeoffset = ((jitloopfnt)vm->jitloops[i].jitfn)(
          task->stack, &top, task->stack + (*frameptr)->slotsbase, vm, ValCopy,
          ValFreeFunc);
      *stacktopptr = task->stack + top;
      gVMProfiling.native_executions++;
      if (resumeoffset >= 0 && resumeoffset < fn->chunk.count) {
        *ipptr = fn->chunk.code + resumeoffset;
      }
      return;
    }
  }
}
