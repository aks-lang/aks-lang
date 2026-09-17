void FreeStackArenaWithGuard(void *ptr, size_t size) {
  if (!ptr)
    return;
#ifdef _WIN32
  VirtualFree(ptr, 0, MEM_RELEASE);
#else
  size_t page_size = sysconf(_SC_PAGESIZE);
  size = (size + page_size - 1) & ~(page_size - 1);
  munmap(ptr, size + page_size);
#endif
}

static inline void StackEnsure(VMTask *t, int needed) {
  if (needed >= t->stackcap) {
    int newcap = t->stackcap ? t->stackcap * 2 : MINSTACK;
    while (newcap <= needed) newcap *= 2;
    if (newcap > MAXSTACK) newcap = MAXSTACK;
    AKSValue *newstack = realloc(t->stack, newcap * sizeof(AKSValue));
    if (newstack) {
      for (int i = t->stackcap; i < newcap; i++) newstack[i] = ValNull();
      t->stack = newstack;
      t->stackcap = newcap;
      if (newcap > t->stack_peak) t->stack_peak = newcap; /* watermark */
    }
  }
}

static inline void FrameEnsure(VMTask *t, int needed) {
  if (needed >= t->framecap) {
    int newcap = t->framecap ? t->framecap * 2 : MINFRAMES;
    while (newcap <= needed) newcap *= 2;
    if (newcap > MAXFRAMES) newcap = MAXFRAMES;
    CallFrame *newframes = realloc(t->frames, newcap * sizeof(CallFrame));
    if (newframes) {
      memset(newframes + t->framecap, 0, (newcap - t->framecap) * sizeof(CallFrame));
      t->frames = newframes;
      t->framecap = newcap;
    }
  }
}

// vMTask lifecycle
VMTask *TaskNew(VM *vm, FnProto *fn, int startipoffset) {
  VMTask *t = calloc(1, sizeof(VMTask));
  if (!t) return NULL;
  t->refcount = 1;
  t->select_woken_index = -1;
  t->state = 0; // Ready
  t->tracingmode = 0;
  t->tracestartoffset = 0;
  t->recordedtrace = NULL;
  t->tracestepcount = 0;

  // Start with minimal stack and let StackEnsure/FrameEnsure grow on demand.
  // This reduces initial per-task memory from 512KB to ~8KB (97% reduction),
  // dramatically improving cache locality and memory efficiency for workloads
  // with many concurrent tasks.
  size_t allocated_size = 0;
  t->stackarena = AllocStackArenaWithGuard(65536, &allocated_size);
  t->stackarena_size = allocated_size;
  t->stackarenatop = 0;

  t->stackcap = MINSTACK;
  t->stack = calloc(t->stackcap, sizeof(AKSValue));
  t->framecap = MINFRAMES;
  t->frames = calloc(t->framecap, sizeof(CallFrame));
  if (!t->stack || !t->frames) {
    if (t->stack) free(t->stack);
    if (t->frames) free(t->frames);
    if (t->stackarena) FreeStackArenaWithGuard(t->stackarena, t->stackarena_size ? t->stackarena_size : 65536);
    free(t);
    return NULL;
  }

  t->stacktop = fn->maxregisters > 0 ? fn->maxregisters : fn->localcount;
  for (int i = 0; i < t->stacktop; i++)
    t->stack[i] = ValNull();
  t->frames[0].fn = fn;
  t->frames[0].ip = fn->chunk.code + startipoffset;
  t->frames[0].slotsbase = 0;
  t->framecount = 1;

  MutexLockNative(vm->globallock);
  t->id = vm->nexttaskid++;
  if (vm->alltaskscount < 4096) {
    vm->alltasks[vm->alltaskscount++] = t;
    t->refcount++;
  }
  MutexUnlockNative(vm->globallock);
  return t;
}

void TaskFree(VMTask *t) {
  if (!t)
    return;
  for (int i = 0; i < t->stacktop; i++) {
    ValFree(&t->stack[i]);
  }
  for (int i = 0; i < t->effect_handler_count; i++) {
    ValFree(&t->effect_handlers[i]);
  }
  t->effect_handler_count = 0;
  ValFree(&t->result);
  free(t->awaiters);
  if (t->recordedtrace)
    free(t->recordedtrace);
  FreeStackArenaWithGuard(t->stackarena, t->stackarena_size ? t->stackarena_size : 65536);
  if (t->stack)
    free(t->stack);
  if (t->frames)
    free(t->frames);
  free(t);
}

void TaskReLease(VM *vm, VMTask *t) {
  if (!t)
    return;
  MutexLockNative(vm->globallock);
  for (int i = 0; i < vm->alltaskscount; i++) {
    if (vm->alltasks[i] == t) {
      for (int j = i; j < vm->alltaskscount - 1; j++) {
        vm->alltasks[j] = vm->alltasks[j + 1];
      }
      vm->alltaskscount--;
      break;
    }
  }
  MutexUnlockNative(vm->globallock);
  TaskFree(t);
}

// types defined in vm.h

void ChanFree(VMChannel *c) {
  if (!c)
    return;
  for (int i = 0; i < c->count; i++) {
    ValFree(&c->buffer[(c->head + i) % c->capacity]);
  }
  free(c->buffer);
  MutexFreeNative(c->lock);
  free(c);
}
void MutexFree(VMMutex *m) {
  if (!m)
    return;
  MutexFreeNative(m->lock);
  free(m);
}
void SemFree(VMSemaphore *s) {
  if (!s)
    return;
  MutexFreeNative(s->lock);
  free(s);
}

VMChannel *ChanNew(int capacity) {
  VMChannel *c = calloc(1, sizeof(VMChannel));
  c->refcount = 1;
  c->capacity = capacity >= 0 ? capacity : 1;
  c->buffer = c->capacity > 0 ? calloc(c->capacity, sizeof(AKSValue)) : NULL;
  c->lock = MutexNewNative();
  return c;
}
VMMutex *MutexNew(void) {
  VMMutex *m = calloc(1, sizeof(VMMutex));
  m->refcount = 1;
  m->lock = MutexNewNative();
  return m;
}
VMSemaphore *SemNew(int initialcount) {
  VMSemaphore *s = calloc(1, sizeof(VMSemaphore));
  s->refcount = 1;
  s->count = initialcount;
  s->lock = MutexNewNative();
  return s;
}

// deque operations
static void WorkerPushTail(VMWorker *w, VMTask *t) {
  MutexLockNative(w->lock);
  while (w->tail - w->head >= 1024) {
    MutexUnlockNative(w->lock);
    SleepMs(1);
    MutexLockNative(w->lock);
  }
  w->deque[w->tail % 1024] = t;
  w->tail++;
  MutexUnlockNative(w->lock);
}
static VMTask *WorkerPopTail(VMWorker *w) {
  VMTask *t = NULL;
  MutexLockNative(w->lock);
  if (w->tail > w->head) {
    w->tail--;
    t = w->deque[w->tail % 1024];
  }
  MutexUnlockNative(w->lock);
  return t;
}
static VMTask *WorkerSteal(VMWorker *w) {
  VMTask *t = NULL;
  MutexLockNative(w->lock);
  if (w->tail > w->head) {
    t = w->deque[w->head % 1024];
    w->head++;
  }
  MutexUnlockNative(w->lock);
  return t;
}

// forward decl for runtaskslice
int RunTaskSlice(VM *vm, VMTask *task, int maxinstructions);
static void RaiseEx(VM *vm, VMTask *task, const char *msg, uint8_t **ipptr,
                    AKSValue **stacktopptr, CallFrame **frameptr,
                    AKSValue **frameslotsptr);
static void TriggerPanic(VM *vm, VMTask *task, const char *msg,
                         uint8_t **ipptr);
static void RaiseExceptionValue(VM *vm, VMTask *task, AKSValue exception_val,
                                uint8_t **ipptr, AKSValue **stacktopptr,
                                CallFrame **frameptr, AKSValue **frameslotsptr);

static int GetBytecodeOffset(FnProto *fn, int regidx) {
  if (!fn->offsettomap)
    return 0;
  for (int i = 0; i < fn->chunk.count; i++) {
    if (fn->offsettomap[i] == regidx)
      return i;
  }
  for (int i = fn->chunk.count - 1; i >= 0; i--) {
    if (fn->offsettomap[i] >= 0 && fn->offsettomap[i] <= regidx)
      return i;
  }
  return 0;
}

static int RegCheckOverload(VM *vm, VMTask *task, const char *opname, int dst,
                            int src1, int src2, RegInst **ipptr,
                            AKSValue **stacktopptr, CallFrame **frameptr,
                            AKSValue **frameslotsptr) {
  AKSValue a = (*frameslotsptr)[src1];
  if (ValKind(a) != VALSTRUCT || !ValToObj(a))
    return 0;
  AKSStruct *s = ValToObj(a);
  int ovidx = -1;
  for (int i = 0; i < s->fieldcount; i++) {
    if (strcmp(s->fieldnames[i], opname) == 0) {
      ovidx = i;
      break;
    }
  }
  if (ovidx < 0)
    return 0;
  AKSValue method = s->fieldvals[ovidx];
  if (ValKind(method) != VALFN && ValKind(method) != VALBOUNDMETHOD)
    return 0;

  int caller_maxreg = (*frameptr)->fn->maxregisters;
  if (caller_maxreg == 0)
    caller_maxreg = (*frameptr)->fn->localcount;

  /* Ensure stack has space above the caller's register window */
  int needed = (*frameptr)->slotsbase + caller_maxreg + 4;
  if (needed >= task->stackcap) {
    ptrdiff_t _so = *stacktopptr - task->stack;
    ptrdiff_t _fo = *frameslotsptr - task->stack;
    StackEnsure(task, needed + 16);
    *stacktopptr = task->stack + _so;
    *frameslotsptr = task->stack + _fo;
  }

  AKSValue *st = task->stack + (*frameptr)->slotsbase + caller_maxreg;
  *st++ = ValCopy((*frameslotsptr)[src1]);
  *st++ = (src2 >= 0) ? ValCopy((*frameslotsptr)[src2]) : ValNull();

  FnProto *fn = (*frameptr)->fn;
  int regidx = (int)(*ipptr - 1 - (RegInst *)fn->regcode);
  uint8_t *bytecodeip = fn->chunk.code + GetBytecodeOffset(fn, regidx);
  (*frameptr)->ip = bytecodeip;

  int origframes = task->framecount;
  if (ChkopOverload(vm, task, &bytecodeip, &st, frameptr, frameslotsptr,
                    opname)) {
    if (task->framecount > origframes) {
      /* Overloaded function frame was pushed */
      CallFrame *newframe = *frameptr;
      int newmaxreg = newframe->fn->maxregisters;
      if (newmaxreg == 0)
        newmaxreg = newframe->fn->localcount;
      int newneeded = newframe->slotsbase + newmaxreg + 1;
      if (newneeded >= task->stackcap) {
        ptrdiff_t _fo = *frameslotsptr - task->stack;
        StackEnsure(task, newneeded + 16);
        *frameptr = &task->frames[task->framecount - 1];
        newframe = *frameptr;
        *frameslotsptr = task->stack + _fo;
      }
      for (int i = 2; i < newmaxreg; i++) {
        task->stack[newframe->slotsbase + i] = ValNull();
      }
      *stacktopptr = task->stack + newframe->slotsbase + newmaxreg;
      if (newframe->fn->regcode) {
        *ipptr = (RegInst *)newframe->fn->regcode;
      } else {
        *ipptr = NULL;
      }
    } else {
      /* Handled synchronously by ExecuteStdlib, return value left at st[-1] */
      AKSValue exval = *--st;
      ValFree(&((*frameslotsptr)[dst]));
      (*frameslotsptr)[dst] = exval;
      *stacktopptr = task->stack + (*frameptr)->slotsbase + caller_maxreg;
    }
    return 1;
  }

  /* Overload failed or not handled: clean up the 2 pushed arguments */
  ValFree(&st[-1]);
  ValFree(&st[-2]);
  *stacktopptr = task->stack + (*frameptr)->slotsbase + caller_maxreg;
  return 0;
}

static void FreeRegIfNotNull(FnProto *fn) {
  if (!fn)
    return;
  if (fn->regcode) {
    RegInst *insts = (RegInst *)fn->regcode;
    for (int i = 0; i < fn->regcount; i++) {
      if (insts[i].fieldname)
        free(insts[i].fieldname);
      if (insts[i].cachedtypename)
        free(insts[i].cachedtypename);
      ValFree(&insts[i].constval);
    }
    free(fn->regcode);
    fn->regcode = NULL;
  }
  if (fn->offsettomap) {
    free(fn->offsettomap);
    fn->offsettomap = NULL;
  }
}

static int GetInstrLen(uint8_t op, uint8_t *ip) {
  switch (op) {
  case OPHALT:
    return 1;
  case OPPUSHINT:
    return 9;
  case OPPUSHFLOAT:
    return 9;
  case OPPUSHSTR:
    return 3;
  case OPPUSHBOOL:
    return 2;
  case OPPUSHNULL:
    return 1;
  case OPLOAD:
    return 3;
  case OPLOADGLOBAL:
    return 3;
  case OPSTORE:
    return 3;
  case OPSTOREGLOBAL:
    return 3;
  case OPDUP:
    return 1;
  case OPPOP:
  case OP_STATIC_RELEASE:
    return 1;
  case OPPRINTLN:
    return 1;
  case OPINPUTSTR:
    return 3;
  case OPCASTINT:
    return 1;
  case OPCASTFLOAT:
    return 1;
  case OPCASTBOOL:
    return 1;
  case OPCASTSTR:
    return 1;
  case OPADD:
    return 1;
  case OPSUB:
    return 1;
  case OPMUL:
    return 1;
  case OPDIV:
    return 1;
  case OPMOD:
    return 1;
  case OPEQ:
    return 1;
  case OPNEQ:
    return 1;
  case OPLT:
    return 1;
  case OPGT:
    return 1;
  case OPLTE:
    return 1;
  case OPGTE:
    return 1;
  case OPAND:
    return 1;
  case OPOR:
    return 1;
  case OPNOT:
    return 1;
  case OPNEG:
    return 1;
  case OPJUMP:
    return 5;
  case OPJUMPIFFALSE:
    return 5;
  case OPCALL:
    return 4;
  case OPRETURN:
    return 1;
  case OPRETURNVAL:
    return 1;
  case OPNEWARR:
    return 3;
  case OPARRGET:
    return 1;
  case OPARRSET:
    return 1;
  case OPNEWSTRUCT:
    return 3;
  case OPGETFIELD:
    return 3;
  case OPSETFIELD:
    return 3;
  case OPINC:
    return 1;
  case OPDEC:
    return 1;
  case OPPUSHFN:
    return 3;
  case OPCALLDYNAMIC:
    return 2;
  case OPTRYPUSH:
    return 5;
  case OPTRYPOP:
    return 1;
  case OPTHROW:
    return 1;
  case OPCOALESCE:
    return 5;
  case OPRANGE:
    return 1;
  case OPJUMPIFNULL:
    return 5;
  case OPTAILCALL:
    return 4;
  case OPPRINTREPL:
    return 1;
  case OPSPAWN:
    return 4;
  case OPAWAIT:
    return 1;
  case OPSELECT: {
    if (!ip)
      return 1;
    uint16_t count = (uint16_t)((ip[1] << 8) | ip[2]);
    uint8_t has_default = ip[3];
    return 4 + count * 2 + (has_default ? 2 : 0);
  }
  case OPNEWTUPLE:
    return 3;
  case OPCLONESTRUCT:
    return 1;
  case OPSHL:
    return 1;
  case OPSHR:
    return 1;
  case OPBITAND:
    return 1;
  case OPBITOR:
    return 1;
  case OPBITXOR:
    return 1;
  case OPBITNOT:
    return 1;
  case OPAPPLY:
    return 1;
  case OPRESTARGS:
    return 2;
  default:
    return 1;
  }
}

void TranslateToRegister(VM *vm, FnProto *fn) {
  // Compute actual local slots count from OPLOAD/OPSTORE instructions
  int computedlocals = fn->paramcount;
  if (fn->chunk.code && fn->chunk.count > 0) {
    int ip = 0;
    uint8_t *bytecode = fn->chunk.code;
    while (ip < fn->chunk.count) {
      uint8_t op = bytecode[ip];
      if (op == OPSELECT || op == OPAWAIT) {
        fn->regcode = NULL;
        fn->regcount = 0;
        fn->maxregisters = fn->localcount;
        return;
      }
      int len = GetInstrLen(op, &bytecode[ip]);
      if (ip + len > fn->chunk.count)
        break;
      if (op == OPLOAD || op == OPSTORE) {
        uint16_t slot = (uint16_t)((bytecode[ip + 1] << 8) | bytecode[ip + 2]);
        if (slot + 1 > computedlocals) {
          computedlocals = slot + 1;
        }
      }
      ip += len;
    }
  }
  fn->localcount = computedlocals;
  FreeRegIfNotNull(fn);
  if (!fn->chunk.code || fn->chunk.count == 0) {
    fn->regcode = NULL;
    fn->regcount = 0;
    fn->maxregisters = fn->localcount;
    return;
  }

  int cap = fn->chunk.count;
  if (cap < 16)
    cap = 16;
  RegInst *insts = calloc(cap, sizeof(RegInst));
  int count = 0;

  int localcount = 128;
  if (fn->localcount > localcount)
    localcount = fn->localcount;
  // simstack with extra padding to avoid overflow
  int simstackcap = fn->chunk.count + 512;
  int *simstack = malloc(simstackcap * sizeof(int));
  int simstacktop = 0;
  int maxregisters = localcount;

  int *saved_simstacktops = calloc(fn->chunk.count, sizeof(int));
  int **saved_simstacks = calloc(fn->chunk.count, sizeof(int *));

  fn->offsettomap = malloc(fn->chunk.count * sizeof(int));
  for (int i = 0; i < fn->chunk.count; i++) {
    fn->offsettomap[i] = -1;
  }

  uint8_t *bytecode = fn->chunk.code;
  int ipoffset = 0;

#define RDBYTE() (bytecode[ipoffset++])
#define RDU16()                                                                \
  (ipoffset += 2,                                                              \
   (uint16_t)((bytecode[ipoffset - 2] << 8) | bytecode[ipoffset - 1]))
#define RDI32()                                                                \
  (ipoffset += 4,                                                              \
   (int32_t)((bytecode[ipoffset - 4] << 24) | (bytecode[ipoffset - 3] << 16) | \
             (bytecode[ipoffset - 2] << 8) | bytecode[ipoffset - 1]))
#define RDI64()                                                                \
  (ipoffset += 8, (int64_t)(((uint64_t)bytecode[ipoffset - 8] << 56) |         \
                            ((uint64_t)bytecode[ipoffset - 7] << 48) |         \
                            ((uint64_t)bytecode[ipoffset - 6] << 40) |         \
                            ((uint64_t)bytecode[ipoffset - 5] << 32) |         \
                            ((uint64_t)bytecode[ipoffset - 4] << 24) |         \
                            ((uint64_t)bytecode[ipoffset - 3] << 16) |         \
                            ((uint64_t)bytecode[ipoffset - 2] << 8) |          \
                            (uint64_t)bytecode[ipoffset - 1]))
/* prevent underflow */
#define SIMSPOP() (simstacktop > 0 ? simstack[--simstacktop] : 0)
/* prevent overflow */
#define SIMSPUSH(reg)                                                          \
  do {                                                                         \
    if (simstacktop < simstackcap) {                                           \
      simstack[simstacktop] = (reg);                                           \
      simstacktop++;                                                           \
    }                                                                          \
  } while (0)

  while (ipoffset < fn->chunk.count) {
    if (count >= cap) {
      cap *= 2;
      insts = realloc(insts, cap * sizeof(RegInst));
      memset(insts + count, 0, (cap - count) * sizeof(RegInst));
    }

    int start_ipoffset = ipoffset;
    insts[count].bytecodeoffset = start_ipoffset;
    fn->offsettomap[ipoffset] = count;
    if (saved_simstacks[ipoffset] != NULL) {
      simstacktop = saved_simstacktops[ipoffset];
      for (int k = 0; k < simstacktop; k++) {
        simstack[k] = saved_simstacks[ipoffset][k];
      }
      if (localcount + simstacktop > maxregisters)
        maxregisters = localcount + simstacktop;
    }
    uint8_t op = RDBYTE();

    switch (op) {
    case OPHALT: {
      insts[count].op = regophalt;
      count++;
      break;
    }
    case OPPUSHINT: {
      int64_t val = RDI64();
      int reg = localcount + simstacktop;
      insts[count].op = regoppushconst;
      insts[count].dst = reg;
      insts[count].constval = ValInt(val);
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPPUSHFLOAT: {
      int64_t rawbits = RDI64();
      double val;
      memcpy(&val, &rawbits, 8);
      int reg = localcount + simstacktop;
      insts[count].op = regoppushconst;
      insts[count].dst = reg;
      insts[count].constval = ValFloat(val);
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPPUSHSTR: {
      uint16_t sid = RDU16();
      int reg = localcount + simstacktop;
      insts[count].op = regoppushconst;
      insts[count].dst = reg;
      char *unesc = strdup(vm->strpool[sid]);
      UnEscapeStrInplace(unesc);
      insts[count].constval = ValStr(unesc);
      free(unesc);
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPPUSHBOOL: {
      uint8_t b = RDBYTE();
      int reg = localcount + simstacktop;
      insts[count].op = regoppushconst;
      insts[count].dst = reg;
      insts[count].constval = ValBool(b);
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPPUSHNULL: {
      int reg = localcount + simstacktop;
      insts[count].op = regoppushconst;
      insts[count].dst = reg;
      insts[count].constval = ValNull();
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPLOAD: {
      uint16_t slot = RDU16();
      if (ipoffset < fn->chunk.count && bytecode[ipoffset] == OPADD &&
          simstacktop > 0) {
        int left = simstack[simstacktop - 1];
        insts[count].op = regopaddreg;
        insts[count].dst = left;
        insts[count].src1 = left;
        insts[count].src2 = slot;
        insts[count].bytecodeoffset = start_ipoffset;
        count++;
        ipoffset++;
      } else {
        int reg = localcount + simstacktop;
        insts[count].op = regopload;
        insts[count].dst = reg;
        insts[count].src1 = slot;
        SIMSPUSH(reg);
        if (reg + 1 > maxregisters)
          maxregisters = reg + 1;
        count++;
      }
      break;
    }
    case OPSTORE: {
      uint16_t slot = RDU16();
      int valreg = SIMSPOP();
      insts[count].op = regopstore;
      insts[count].dst = slot;
      insts[count].src1 = valreg;
      insts[count].bytecodeoffset = start_ipoffset;
      if (slot + 1 > maxregisters)
        maxregisters = slot + 1;
      count++;
      break;
    }
    case OPLOADGLOBAL: {
      uint16_t sid = RDU16();
      int reg = localcount + simstacktop;
      insts[count].op = regoploadglobal;
      insts[count].dst = reg;
      insts[count].src1 = sid;
      insts[count].bytecodeoffset = start_ipoffset;
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPSTOREGLOBAL: {
      uint16_t sid = RDU16();
      int valreg = SIMSPOP();
      insts[count].op = regopstoreglobal;
      insts[count].src1 = valreg;
      insts[count].src2 = sid;
      insts[count].bytecodeoffset = start_ipoffset;
      count++;
      break;
    }
    case OPDUP: {
      int top = (simstacktop > 0) ? simstack[simstacktop - 1] : 0;
      int reg = localcount + simstacktop;
      insts[count].op = regopload;
      insts[count].dst = reg;
      insts[count].src1 = top;
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPPOP:
    case OP_STATIC_RELEASE: {
      if (simstacktop > 0)
        simstacktop--;
      break;
    }
    case OPPRINTLN: {
      int reg = SIMSPOP();
      insts[count].op = regopwriteln;
      insts[count].src1 = reg;
      count++;
      break;
    }
    case OPINPUTSTR: {
      uint16_t flags = RDU16();
      int promptreg = -1;
      if (flags & 1) {
        promptreg = SIMSPOP();
      }
      int reg = localcount + simstacktop;
      insts[count].op = regopinputstr;
      insts[count].dst = reg;
      insts[count].src1 = promptreg;
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPADD:
    case OPSUB:
    case OPMUL:
    case OPDIV:
    case OPMOD:
    case OPEQ:
    case OPNEQ:
    case OPLT:
    case OPGT:
    case OPLTE:
    case OPGTE:
    case OPAND:
    case OPOR:
    case OPSHL:
    case OPSHR:
    case OPBITAND:
    case OPBITOR:
    case OPBITXOR: {
      int regop = regophalt;
      switch (op) {
      case OPADD:
        regop = regopadd;
        break;
      case OPSUB:
        regop = regopsub;
        break;
      case OPMUL:
        regop = regopmul;
        break;
      case OPDIV:
        regop = regopdiv;
        break;
      case OPMOD:
        regop = regopmod;
        break;
      case OPEQ:
        regop = regopeq;
        break;
      case OPNEQ:
        regop = regopneq;
        break;
      case OPLT:
        regop = regoplt;
        break;
      case OPGT:
        regop = regopgt;
        break;
      case OPLTE:
        regop = regoplte;
        break;
      case OPGTE:
        regop = regopgte;
        break;
      case OPAND:
        regop = regopand;
        break;
      case OPOR:
        regop = regopor;
        break;
      case OPSHL:
        regop = regopshl;
        break;
      case OPSHR:
        regop = regopshr;
        break;
      case OPBITAND:
        regop = regopbitand;
        break;
      case OPBITOR:
        regop = regopbitor;
        break;
      case OPBITXOR:
        regop = regopbitxor;
        break;
      }
      int regb = SIMSPOP();
      int rega = SIMSPOP();
      int regdst = rega;
      insts[count].op = regop;
      insts[count].dst = regdst;
      insts[count].src1 = rega;
      insts[count].src2 = regb;
      SIMSPUSH(regdst);
      count++;
      break;
    }
    case OPCASTINT:
    case OPCASTFLOAT:
    case OPCASTBOOL:
    case OPCASTSTR:
    case OPNOT:
    case OPNEG:
    case OPBITNOT:
    case OPINC:
    case OPDEC:
    case OPTYPEOF: {
      int regop = regophalt;
      switch (op) {
      case OPCASTINT:
        regop = regopcastint;
        break;
      case OPCASTFLOAT:
        regop = regopcastfloat;
        break;
      case OPCASTBOOL:
        regop = regopcastbool;
        break;
      case OPCASTSTR:
        regop = regopcaststr;
        break;
      case OPNOT:
        regop = regopnot;
        break;
      case OPNEG:
        regop = regopneg;
        break;
      case OPBITNOT:
        regop = regopbitnot;
        break;
      case OPINC:
        regop = regopinc;
        break;
      case OPDEC:
        regop = regopdec;
        break;
      case OPTYPEOF:
        regop = regoptypeof;
        break;
      }
      int reg = (simstacktop > 0) ? simstack[simstacktop - 1] : localcount;
      insts[count].op = regop;
      insts[count].dst = reg;
      insts[count].src1 = reg;
      insts[count].bytecodeoffset = start_ipoffset;
      count++;
      break;
    }
    case OPJUMP: {
      int offset = RDI32();
      int target_offset = ipoffset + offset;
      if (target_offset >= 0 && target_offset < fn->chunk.count &&
          saved_simstacks[target_offset] == NULL) {
        saved_simstacktops[target_offset] = simstacktop;
        saved_simstacks[target_offset] = malloc(simstacktop * sizeof(int));
        for (int k = 0; k < simstacktop; k++)
          saved_simstacks[target_offset][k] = simstack[k];
      }
      insts[count].op = regopjmp;
      insts[count].targetip = target_offset;
      count++;
      break;
    }
    case OPJUMPIFFALSE: {
      int offset = RDI32();
      int reg = SIMSPOP();
      int target_offset = ipoffset + offset;
      if (target_offset >= 0 && target_offset < fn->chunk.count &&
          saved_simstacks[target_offset] == NULL) {
        saved_simstacktops[target_offset] = simstacktop;
        saved_simstacks[target_offset] = malloc(simstacktop * sizeof(int));
        for (int k = 0; k < simstacktop; k++)
          saved_simstacks[target_offset][k] = simstack[k];
      }
      insts[count].op = regopjmpiffalse;
      insts[count].src1 = reg;
      insts[count].targetip = target_offset;
      count++;
      break;
    }
    case OPJUMPIFNULL: {
      int offset = RDI32();
      int reg = (simstacktop > 0) ? simstack[simstacktop - 1] : 0;
      int target_offset = ipoffset + offset;
      if (target_offset >= 0 && target_offset < fn->chunk.count &&
          saved_simstacks[target_offset] == NULL) {
        saved_simstacktops[target_offset] = simstacktop;
        saved_simstacks[target_offset] = malloc(simstacktop * sizeof(int));
        for (int k = 0; k < simstacktop; k++)
          saved_simstacks[target_offset][k] = simstack[k];
      }
      insts[count].op = regopjmpifnull;
      insts[count].src1 = reg;
      insts[count].targetip = target_offset;
      count++;
      break;
    }
    case OPCALL: {
      uint16_t fnid = RDU16();
      uint8_t argc = RDBYTE();
      if (simstacktop < argc)
        simstacktop = argc;
      int dstreg = (argc > 0) ? (localcount + simstacktop - argc)
                              : (localcount + simstacktop);
      insts[count].op = regopcall;
      insts[count].dst = dstreg;
      insts[count].src1 = fnid;
      insts[count].src2 = argc;
      simstacktop -= argc;
      SIMSPUSH(dstreg);
      if (dstreg + 1 > maxregisters)
        maxregisters = dstreg + 1;
      count++;
      break;
    }
    case OPCALLDYNAMIC: {
      uint8_t argc = RDBYTE();
      int fnreg = SIMSPOP();
      if (simstacktop < argc)
        simstacktop = argc;
      int dstreg = (argc > 0) ? (localcount + simstacktop - argc)
                              : (localcount + simstacktop);
      insts[count].op = regopcalldynamic;
      insts[count].dst = dstreg;
      insts[count].src1 = fnreg;
      insts[count].src2 = argc;
      simstacktop -= argc;
      SIMSPUSH(dstreg);
      if (dstreg + 1 > maxregisters)
        maxregisters = dstreg + 1;
      count++;
      break;
    }
    case OPRETURN: {
      insts[count].op = regopret;
      count++;
      break;
    }
    case OPRETURNVAL: {
      int reg = SIMSPOP();
      insts[count].op = regopretval;
      insts[count].src1 = reg;
      count++;
      break;
    }
    case OPNEWARR: {
      uint16_t elcount = RDU16();
      uint16_t countval = elcount & 0x7FFF;
      if (simstacktop < countval)
        simstacktop = countval;
      int dstreg = (countval > 0) ? (localcount + simstacktop - countval)
                                  : (localcount + simstacktop);
      insts[count].op = regopnewarr;
      insts[count].dst = dstreg;
      insts[count].src1 = elcount;
      simstacktop -= countval;
      SIMSPUSH(dstreg);
      if (dstreg + 1 > maxregisters)
        maxregisters = dstreg + 1;
      count++;
      break;
    }
    case OPNEWTUPLE: {
      uint16_t elcount = RDU16();
      uint16_t countval = elcount & 0x7FFF;
      if (simstacktop < countval)
        simstacktop = countval;
      int dstreg = (countval > 0) ? (localcount + simstacktop - countval)
                                  : (localcount + simstacktop);
      insts[count].op = regopnewtuple;
      insts[count].dst = dstreg;
      insts[count].src1 = elcount;
      simstacktop -= countval;
      SIMSPUSH(dstreg);
      if (dstreg + 1 > maxregisters)
        maxregisters = dstreg + 1;
      count++;
      break;
    }
    case OPARRGET: {
      int idxreg = SIMSPOP();
      int arrreg = SIMSPOP();
      int dstreg = localcount + simstacktop;
      insts[count].op = regoparrget;
      insts[count].dst = dstreg;
      insts[count].src1 = arrreg;
      insts[count].src2 = idxreg;
      insts[count].bytecodeoffset = start_ipoffset;
      SIMSPUSH(dstreg);
      if (dstreg + 1 > maxregisters)
        maxregisters = dstreg + 1;
      count++;
      break;
    }
    case OPARRSET: {
      int idxreg = SIMSPOP();
      int arrreg = SIMSPOP();
      int valreg = SIMSPOP();
      insts[count].op = regoparrset;
      insts[count].dst = arrreg;
      insts[count].src1 = idxreg;
      insts[count].src2 = valreg;
      insts[count].bytecodeoffset = start_ipoffset;
      SIMSPUSH(valreg);
      count++;
      break;
    }
    case OPNEWSTRUCT: {
      uint16_t sid = RDU16();
      int reg = localcount + simstacktop;
      insts[count].op = regopnewstruct;
      insts[count].dst = reg;
      insts[count].src1 = sid;
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPCLONESTRUCT: {
      int objreg = SIMSPOP();
      int reg = localcount + simstacktop;
      insts[count].op = regopclonestruct;
      insts[count].dst = reg;
      insts[count].src1 = objreg;
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPGETFIELD: {
      uint16_t sid = RDU16();
      int objreg = SIMSPOP();
      int dstreg = objreg;
      insts[count].op = regopgetfield;
      insts[count].dst = dstreg;
      insts[count].src1 = objreg;
      insts[count].fieldname = strdup(vm->strpool[sid]);
      insts[count].cachedtypename = NULL;
      insts[count].cachedindex = -1;
      SIMSPUSH(dstreg);
      count++;
      break;
    }
    case OPSETFIELD: {
      uint16_t sid = RDU16();
      int objreg = SIMSPOP();
      int valreg = SIMSPOP();
      insts[count].op = regopsetfield;
      insts[count].dst = objreg;
      insts[count].src1 = valreg;
      insts[count].fieldname = strdup(vm->strpool[sid]);
      insts[count].cachedtypename = NULL;
      insts[count].cachedindex = -1;
      SIMSPUSH(valreg);
      count++;
      break;
    }
    case OPPUSHFN: {
      uint16_t fnid = RDU16();
      int reg = localcount + simstacktop;
      insts[count].op = regoppushfn;
      insts[count].dst = reg;
      insts[count].src1 = fnid;
      SIMSPUSH(reg);
      if (reg + 1 > maxregisters)
        maxregisters = reg + 1;
      count++;
      break;
    }
    case OPTRYPUSH: {
      int offset = RDI32();
      int catch_offset = ipoffset + offset;
      if (catch_offset >= 0 && catch_offset < fn->chunk.count) {
        saved_simstacktops[catch_offset] = simstacktop + 1;
        saved_simstacks[catch_offset] = malloc((simstacktop + 1) * sizeof(int));
        for (int k = 0; k < simstacktop; k++) {
          saved_simstacks[catch_offset][k] = simstack[k];
        }
        saved_simstacks[catch_offset][simstacktop] = localcount + simstacktop;
      }
      insts[count].op = regoptrypush;
      insts[count].targetip = catch_offset;
      count++;
      break;
    }
    case OPTRYPOP: {
      insts[count].op = regoptrypop;
      count++;
      break;
    }
    case OPTHROW: {
      int reg = SIMSPOP();
      insts[count].op = regopthrow;
      insts[count].src1 = reg;
      count++;
      break;
    }
    case OPCOALESCE: {
      int offset = RDI32();
      int target_offset = ipoffset + offset;
      if (target_offset >= 0 && target_offset < fn->chunk.count &&
          saved_simstacks[target_offset] == NULL) {
        saved_simstacktops[target_offset] = simstacktop;
        saved_simstacks[target_offset] = malloc(simstacktop * sizeof(int));
        for (int k = 0; k < simstacktop; k++)
          saved_simstacks[target_offset][k] = simstack[k];
      }
      int reg = (simstacktop > 0) ? simstack[simstacktop - 1] : 0;
      insts[count].op = regopcoalesce;
      insts[count].src1 = reg;
      insts[count].targetip = target_offset;
      count++;
      break;
    }
    case OPRANGE: {
      int stopreg = SIMSPOP();
      int startreg = SIMSPOP();
      int dstreg = startreg;
      insts[count].op = regoprange;
      insts[count].dst = dstreg;
      insts[count].src1 = startreg;
      insts[count].src2 = stopreg;
      SIMSPUSH(dstreg);
      count++;
      break;
    }
    case OPTAILCALL: {
      uint16_t fnid = RDU16();
      uint8_t argc = RDBYTE();
      if (fnid == 65535) {
        int fnreg = SIMSPOP();
        if (simstacktop < argc)
          simstacktop = argc;
        int dstreg = (argc > 0) ? (localcount + simstacktop - argc)
                                : (localcount + simstacktop);
        insts[count].op = regoptailcall;
        insts[count].dst = dstreg;
        insts[count].src1 = fnreg;
        insts[count].src2 = argc;
        insts[count].targetip = 65535;
        simstacktop -= argc;
        SIMSPUSH(dstreg);
      } else {
        if (simstacktop < argc)
          simstacktop = argc;
        int dstreg = (argc > 0) ? (localcount + simstacktop - argc)
                                : (localcount + simstacktop);
        insts[count].op = regoptailcall;
        insts[count].dst = dstreg;
        insts[count].src1 = fnid;
        insts[count].src2 = argc;
        insts[count].targetip = 0;
        simstacktop -= argc;
        SIMSPUSH(dstreg);
      }
      count++;
      break;
    }
    case OPPRINTREPL: {
      int reg = (simstacktop > 0) ? simstack[simstacktop - 1] : 0;
      insts[count].op = regopprintrepl;
      insts[count].src1 = reg;
      count++;
      break;
    }
    case OPSPAWN: {
      uint16_t fnid = RDU16();
      uint8_t argc = RDBYTE();
      int dstreg = localcount + simstacktop;
      if (fnid == 65535) {
        int fnreg = SIMSPOP();
        insts[count].op = regopspawn;
        insts[count].dst = dstreg;
        insts[count].src1 = fnreg;
        insts[count].src2 = argc;
        insts[count].targetip = fnid;
        simstacktop -= argc;
      } else {
        int argbase = localcount + simstacktop - argc;
        insts[count].op = regopspawn;
        insts[count].dst = dstreg;
        insts[count].src1 = argbase;
        insts[count].src2 = argc;
        insts[count].targetip = fnid;
        simstacktop -= argc;
      }
      SIMSPUSH(dstreg);
      if (dstreg + 1 > maxregisters)
        maxregisters = dstreg + 1;
      count++;
      break;
    }
    case OPAWAIT: {
      int reg = SIMSPOP();
      int dstreg = reg;
      insts[count].op = regopawait;
      insts[count].dst = dstreg;
      insts[count].src1 = reg;
      SIMSPUSH(dstreg);
      count++;
      break;
    }
    case OPAPPLY: {
      int calleereg = SIMSPOP();
      int argsreg = SIMSPOP();
      int dstreg = argsreg;
      insts[count].op = regopapply;
      insts[count].dst = dstreg;
      insts[count].src1 = calleereg;
      insts[count].src2 = argsreg;
      SIMSPUSH(dstreg);
      count++;
      break;
    }
    case OPRESTARGS: {
      uint8_t paramidx = RDBYTE();
      insts[count].op = regoprestargs;
      insts[count].dst = paramidx;
      if (paramidx + 1 > maxregisters)
        maxregisters = paramidx + 1;
      count++;
      break;
    }
    default:
      break;
    }
  }

#undef RDBYTE
#undef RDU16
#undef RDI32
#undef RDI64
#undef SIMSPOP
#undef SIMSPUSH

  for (int i = 0; i < count; i++) {
    if (insts[i].op == regopjmp || insts[i].op == regopjmpiffalse ||
        insts[i].op == regopjmpifnull || insts[i].op == regoptrypush ||
        insts[i].op == regopcoalesce) {
      int bytecodetarget = insts[i].targetip;
      // Guard against OOB access: bytecodetarget must be within [0,
      // chunk.count)
      if (bytecodetarget < 0 || bytecodetarget >= fn->chunk.count) {
        insts[i].targetip = count; // point to end (no-op / halt)
        continue;
      }
      int regtarget = fn->offsettomap[bytecodetarget];
      if (regtarget < 0) {
        for (int b = bytecodetarget; b < fn->chunk.count; b++) {
          if (fn->offsettomap[b] >= 0) {
            regtarget = fn->offsettomap[b];
            break;
          }
        }
        if (regtarget < 0)
          regtarget = count;
      }
      insts[i].targetip = regtarget;
    }
  }
  fn->regcode = insts;
  fn->regcount = count;
  fn->maxregisters = maxregisters;
  for (int i = 0; i < fn->chunk.count; i++) {
    if (saved_simstacks[i])
      free(saved_simstacks[i]);
  }
  free(saved_simstacks);
  free(saved_simstacktops);
  free(simstack);
}

typedef struct {
  VM *vm;
  int workerid;
} WorkerContext;

DWORD WINAPI WorkerThreadFunc(LPVOID lpParam) {
  WorkerContext *ctx = (WorkerContext *)lpParam;
  VM *vm = ctx->vm;
  int id = ctx->workerid;
  VMWorker *myworker = &vm->workers[id];
  free(ctx);
  srand((unsigned int)time(NULL) ^ id);
  while (!vm->shouldterminate) {
    VMTask *task = WorkerPopTail(myworker);
    if (!task) {
      int startidx = rand() % vm->workercount;
      for (int i = 0; i < vm->workercount; i++) {
        int victimid = (startidx + i) % vm->workercount;
        if (victimid == id)
          continue;
        task = WorkerSteal(&vm->workers[victimid]);
        if (task) {
          break;
        }
      }
    }
    if (task) {
      int res = 0;
      __try {
        res = RunTaskSlice(vm, task, 100);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr,
                "CRASH: Structured Exception in worker thread (0x%08X)\n",
                GetExceptionCode());
        fprintf(stderr, "Task ID: %d, Framecount: %d\n", task->id,
                task->framecount);
        for (int f = 0; f < task->framecount; f++) {
          CallFrame *cf = &task->frames[f];
          if (cf->fn) {
            int ipoffset = -1;
            if (cf->ip && cf->fn->chunk.code) {
              ipoffset = (int)(cf->ip - cf->fn->chunk.code);
            }
            fprintf(stderr, "  Frame %d: %s at offset %d\n", f, cf->fn->name,
                    ipoffset);
          } else {
            fprintf(stderr, "  Frame %d: <unknown>\n", f);
          }
        }
        exit(1);
      }
      if (res == 1 || res == -1) {
        MutexLockNative(vm->globallock);
        task->state = 3; // Completed
        for (int i = 0; i < task->awaitercount; i++) {
          VMTask *awaiter = task->awaiters[i];
          awaiter->state = 0; // Ready
          WorkerPushTail(myworker, awaiter);
        }
        MutexUnlockNative(vm->globallock);
      } else if (res == 0) {
        WorkerPushTail(myworker, task);
      }
    } else {
      PollIoCompletions(vm, 1);
      SleepMs(1);
    }
  }
  return 0;
}
