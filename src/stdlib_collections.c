// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "vm_stdlib.h"

typedef struct {
  char *buf;
  int len;
  int cap;
} AksJsonBuf;

static void AksJsonBufInit(AksJsonBuf *b) {
  b->cap = 256;
  b->len = 0;
  b->buf = (char *)malloc(b->cap);
  if (b->buf)
    b->buf[0] = '\0';
}

static void AksJsonBufEnsure(AksJsonBuf *b, int additional) {
  if (b->len + additional + 1 >= b->cap) {
    int new_cap = (b->cap * 2) + additional + 64;
    char *nb = (char *)realloc(b->buf, new_cap);
    if (nb) {
      b->buf = nb;
      b->cap = new_cap;
    }
  }
}

static void AksJsonBufAppendStr(AksJsonBuf *b, const char *str) {
  if (!str)
    return;
  int slen = (int)strlen(str);
  AksJsonBufEnsure(b, slen);
  memcpy(b->buf + b->len, str, slen);
  b->len += slen;
  b->buf[b->len] = '\0';
}

static void AksJsonBufAppendChar(AksJsonBuf *b, char c) {
  AksJsonBufEnsure(b, 1);
  b->buf[b->len++] = c;
  b->buf[b->len] = '\0';
}

static void AksJsonEscapeString(AksJsonBuf *b, const char *s, int len) {
  AksJsonBufAppendChar(b, '"');
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
    case '"':
      AksJsonBufAppendStr(b, "\\\"");
      break;
    case '\\':
      AksJsonBufAppendStr(b, "\\\\");
      break;
    case '\b':
      AksJsonBufAppendStr(b, "\\b");
      break;
    case '\f':
      AksJsonBufAppendStr(b, "\\f");
      break;
    case '\n':
      AksJsonBufAppendStr(b, "\\n");
      break;
    case '\r':
      AksJsonBufAppendStr(b, "\\r");
      break;
    case '\t':
      AksJsonBufAppendStr(b, "\\t");
      break;
    default:
      if (c < 32) {
        char uhex[8];
        snprintf(uhex, sizeof(uhex), "\\u%04x", c);
        AksJsonBufAppendStr(b, uhex);
      } else {
        AksJsonBufAppendChar(b, (char)c);
      }
      break;
    }
  }
  AksJsonBufAppendChar(b, '"');
}

static void AksJsonIndent(AksJsonBuf *b, int indent) {
  for (int i = 0; i < indent * 2; i++) {
    AksJsonBufAppendChar(b, ' ');
  }
}

static void AksJsonStringifyValue(AksJsonBuf *b, AKSValue v, int pretty,
                                  int indent) {
  if (indent > 128) {
    AksJsonBufAppendStr(b, "null");
    return;
  }
  ValType vt = ValKind(v);
  switch (vt) {
  case VALNULL:
    AksJsonBufAppendStr(b, "null");
    break;
  case VALBOOL:
    AksJsonBufAppendStr(b, ValToBool(v) ? "true" : "false");
    break;
  case VALINT: {
    char numbuf[64];
    snprintf(numbuf, sizeof(numbuf), "%lld", (long long)ValToInt(v));
    AksJsonBufAppendStr(b, numbuf);
    break;
  }
  case VALFLOAT: {
    char numbuf[64];
    double d = ValToFloat(v);
    if (isnan(d) || isinf(d)) {
      AksJsonBufAppendStr(b, "null");
    } else {
      snprintf(numbuf, sizeof(numbuf), "%.14g", d);
      if (strchr(numbuf, '.') == NULL && strchr(numbuf, 'e') == NULL &&
          strchr(numbuf, 'E') == NULL) {
        strcat(numbuf, ".0");
      }
      AksJsonBufAppendStr(b, numbuf);
    }
    break;
  }
  case VALSTR: {
    AKSString *s = ValToStrPtr(v);
    if (!s || !s->data)
      AksJsonBufAppendStr(b, "\"\"");
    else
      AksJsonEscapeString(b, s->data, s->len);
    break;
  }
  case VALARR:
  case VALTUPLE: {
    AKSArray *arr = ValToArr(v);
    if (!arr || arr->count == 0) {
      AksJsonBufAppendStr(b, "[]");
      break;
    }
    AksJsonBufAppendChar(b, '[');
    for (int i = 0; i < arr->count; i++) {
      if (i > 0)
        AksJsonBufAppendChar(b, ',');
      if (pretty) {
        AksJsonBufAppendChar(b, '\n');
        AksJsonIndent(b, indent + 1);
      }
      AksJsonStringifyValue(b, arr->items[i], pretty, indent + 1);
    }
    if (pretty) {
      AksJsonBufAppendChar(b, '\n');
      AksJsonIndent(b, indent);
    }
    AksJsonBufAppendChar(b, ']');
    break;
  }
  case VALSTRUCT: {
    AKSStruct *st = ValToObj(v);
    if (!st || st->fieldcount == 0) {
      AksJsonBufAppendStr(b, "{}");
      break;
    }
    AksJsonBufAppendChar(b, '{');
    for (int i = 0; i < st->fieldcount; i++) {
      if (i > 0)
        AksJsonBufAppendChar(b, ',');
      if (pretty) {
        AksJsonBufAppendChar(b, '\n');
        AksJsonIndent(b, indent + 1);
      }
      const char *fname =
          (st->fieldnames && st->fieldnames[i]) ? st->fieldnames[i] : "";
      AksJsonEscapeString(b, fname, (int)strlen(fname));
      AksJsonBufAppendChar(b, ':');
      if (pretty)
        AksJsonBufAppendChar(b, ' ');
      AksJsonStringifyValue(b, st->fieldvals[i], pretty, indent + 1);
    }
    if (pretty) {
      AksJsonBufAppendChar(b, '\n');
      AksJsonIndent(b, indent);
    }
    AksJsonBufAppendChar(b, '}');
    break;
  }
  default:
    AksJsonBufAppendStr(b, "null");
    break;
  }
}
int ExecuteStdlibCollections(VM *vm, VMTask *task, const char *name, int argc,
            AKSValue **stacktopptr) {
  AKSValue *stacktop = *stacktopptr;
  AKSValue *stacklimit = task->stack + (task->stackcap > 0 ? task->stackcap : MAXSTACK);
#define PUSH(val) AKS_STDLIB_PUSH(val)
#define POP() AKS_STDLIB_POP()

  if (strcmp(name, "StealingDequeNewC") == 0) {
    AKSValue root = ValStruct("StealingDeque");
    AKSStruct *s = ValToObj(root);
    s->fieldnames = malloc(sizeof(char *) * 3);
    s->fieldvals = malloc(sizeof(AKSValue) * 3);
    s->fieldnames[0] = strdup("items");
    s->fieldvals[0] = ValArr(64);
    s->fieldnames[1] = strdup("top");
    s->fieldvals[1] = ValInt(0);
    s->fieldnames[2] = strdup("bottom");
    s->fieldvals[2] = ValInt(0);
    s->fieldcount = 3;
    PUSH(root);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StealingDequePushC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: StealingDequePushC() expects 2 args\n");
      return -1;
    }
    AKSValue item = POP(), dq_v = POP();
    if (ValKind(dq_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(dq_v);
      AKSArray *arr = ValToArr(s->fieldvals[0]);
      int64_t b = ValToInt(s->fieldvals[2]);
      if (b < (int64_t)arr->count) {
        ValFree(&arr->items[b]);
        arr->items[b] = item;
      } else {
        AksArrPush(arr, item);
      }
      s->fieldvals[2] = ValInt(b + 1);
    } else {
      ValFree(&item);
    }
    ValFree(&dq_v);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StealingDequePopC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StealingDequePopC() expects 1 arg\n");
      return -1;
    }
    AKSValue dq_v = POP();
    AKSValue res = ValNull();
    if (ValKind(dq_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(dq_v);
      AKSArray *arr = ValToArr(s->fieldvals[0]);
      int64_t b = ValToInt(s->fieldvals[2]) - 1;
      s->fieldvals[2] = ValInt(b >= 0 ? b : 0);
      int64_t t = ValToInt(s->fieldvals[1]);
      if (b >= t && b < (int64_t)arr->count) {
        res = arr->items[b];
        arr->items[b] = ValNull();
      }
    }
    ValFree(&dq_v);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StealingDequeStealC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StealingDequeStealC() expects 1 arg\n");
      return -1;
    }
    AKSValue dq_v = POP();
    AKSValue res = ValNull();
    if (ValKind(dq_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(dq_v);
      AKSArray *arr = ValToArr(s->fieldvals[0]);
      int64_t t = ValToInt(s->fieldvals[1]);
      int64_t b = ValToInt(s->fieldvals[2]);
      if (t < b && t < (int64_t)arr->count) {
        res = arr->items[t];
        arr->items[t] = ValNull();
        s->fieldvals[1] = ValInt(t + 1);
      }
    }
    ValFree(&dq_v);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HazardPointerNewC") == 0) {
    AKSValue hp_v = ValStruct("HazardPointer");
    AKSStruct *s = ValToObj(hp_v);
    s->fieldnames = malloc(sizeof(char *) * 2);
    s->fieldvals = malloc(sizeof(AKSValue) * 2);
    s->fieldnames[0] = strdup("pointer");
    s->fieldvals[0] = ValInt(0);
    s->fieldnames[1] = strdup("active");
    s->fieldvals[1] = ValBool(0);
    s->fieldcount = 2;
    PUSH(hp_v);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HazardPointerAcquireC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: HazardPointerAcquireC() expects 2 args\n");
      return -1;
    }
    AKSValue ptr_v = POP(), hp_v = POP();
    if (ValKind(hp_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(hp_v);
      s->fieldvals[0] = ptr_v;
      s->fieldvals[1] = ValBool(1);
    }
    ValFree(&hp_v);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HazardPointerReleaseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: HazardPointerReleaseC() expects 1 arg\n");
      return -1;
    }
    AKSValue hp_v = POP();
    if (ValKind(hp_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(hp_v);
      s->fieldvals[0] = ValInt(0);
      s->fieldvals[1] = ValBool(0);
    }
    ValFree(&hp_v);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HAMTNewC") == 0) {
    AKSValue hm_v = ValStruct("HAMT");
    AKSStruct *s = ValToObj(hm_v);
    s->fieldnames = NULL;
    s->fieldvals = NULL;
    s->fieldcount = 0;
    PUSH(hm_v);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HAMTInsertC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: HAMTInsertC() expects 3 args\n");
      return -1;
    }
    AKSValue val = POP(), key = POP(), hm_v = POP();
    if (ValKind(hm_v) == VALSTRUCT && ValKind(key) == VALSTR) {
      AKSStruct *s = ValToObj(hm_v);
      const char *kstr = ValToStrPtr(key)->data;
      int found = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], kstr) == 0) {
          found = i;
          break;
        }
      }
      if (found >= 0) {
        ValFree(&s->fieldvals[found]);
        s->fieldvals[found] = val;
      } else {
        s->fieldnames =
            realloc(s->fieldnames, (s->fieldcount + 1) * sizeof(char *));
        s->fieldvals =
            realloc(s->fieldvals, (s->fieldcount + 1) * sizeof(AKSValue));
        s->fieldnames[s->fieldcount] = strdup(kstr);
        s->fieldvals[s->fieldcount] = val;
        s->fieldcount++;
      }
    }
    ValFree(&key);
    ValFree(&hm_v);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HAMTGetC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: HAMTGetC() expects 2 args\n");
      return -1;
    }
    AKSValue key = POP(), hm_v = POP();
    AKSValue res = ValNull();
    if (ValKind(hm_v) == VALSTRUCT && ValKind(key) == VALSTR) {
      AKSStruct *s = ValToObj(hm_v);
      const char *kstr = ValToStrPtr(key)->data;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], kstr) == 0) {
          res = s->fieldvals[i];
          break;
        }
      }
    }
    ValFree(&key);
    ValFree(&hm_v);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaveletNewC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WaveletNewC() expects 1 arg\n");
      return -1;
    }
    AKSValue arr_v = POP();
    AKSValue wt_v = ValStruct("WaveletTree");
    AKSStruct *s = ValToObj(wt_v);
    s->fieldnames = malloc(sizeof(char *));
    s->fieldvals = malloc(sizeof(AKSValue));
    s->fieldnames[0] = strdup("data");
    s->fieldvals[0] = arr_v;
    s->fieldcount = 1;
    PUSH(wt_v);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaveletRankC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: WaveletRankC() expects 3 args (wt, val, "
                      "prefix_len)\n");
      return -1;
    }
    AKSValue len_v = POP(), target_v = POP(), wt_v = POP();
    int count = 0;
    if (ValKind(wt_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(wt_v);
      if (s->fieldcount > 0 && ValKind(s->fieldvals[0]) == VALARR) {
        AKSArray *arr = ValToArr(s->fieldvals[0]);
        int64_t limit = ValToInt(len_v);
        int64_t target = ValToInt(target_v);
        if (limit > (int64_t)arr->count)
          limit = arr->count;
        for (int i = 0; i < limit; i++) {
          if (ValToInt(arr->items[i]) == target)
            count++;
        }
      }
    }
    ValFree(&len_v);
    ValFree(&target_v);
    ValFree(&wt_v);
    PUSH(ValInt(count));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaNew") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ArenaNew() expects 1 argument (capacity)\n");
      return -1;
    }
    AKSValue cap_val = POP();
    size_t cap = (size_t)ValToInt(cap_val);
    if (cap < 4096)
      cap = 4096;
    typedef struct {
      uint8_t *base;
      size_t capacity;
      size_t offset;
    } AKSArena;
    AKSArena *a = (AKSArena *)malloc(sizeof(AKSArena));
    a->capacity = cap;
    a->offset = 0;
#ifdef _WIN32
    a->base = (uint8_t *)VirtualAlloc(NULL, cap, MEM_RESERVE | MEM_COMMIT,
                                      PAGE_READWRITE);
#else
    a->base = (uint8_t *)mmap(NULL, cap, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    ValFree(&cap_val);
    PUSH(ValInt((int64_t)(uintptr_t)a));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaAlloc") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: ArenaAlloc() expects 2 arguments (arena, size)\n");
      return -1;
    }
    AKSValue sz_val = POP();
    AKSValue a_val = POP();
    size_t sz = (size_t)ValToInt(sz_val);
    size_t aligned = (sz + 7) & ~7;
    typedef struct {
      uint8_t *base;
      size_t capacity;
      size_t offset;
    } AKSArena;
    AKSArena *a = (AKSArena *)(uintptr_t)ValToInt(a_val);
    int64_t ptr = 0;
    if (a && a->base && a->offset + aligned <= a->capacity) {
      ptr = (int64_t)(uintptr_t)(a->base + a->offset);
      a->offset += aligned;
    }
    ValFree(&sz_val);
    ValFree(&a_val);
    PUSH(ValInt(ptr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaReset") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ArenaReset() expects 1 argument (arena)\n");
      return -1;
    }
    AKSValue peek_val = *(stacktop - 1);
    if (ValKind(peek_val) != VALARENA) {
      AKSValue a_val = POP();
      typedef struct {
        uint8_t *base;
        size_t capacity;
        size_t offset;
      } AKSRawArena;
      AKSRawArena *a = (AKSRawArena *)(uintptr_t)ValToInt(a_val);
      if (a) {
        a->offset = 0;
      }
      ValFree(&a_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
  }
  if (strcmp(name, "ArenaUsed") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ArenaUsed() expects 1 argument (arena)\n");
      return -1;
    }
    AKSValue a_val = POP();
    typedef struct {
      uint8_t *base;
      size_t capacity;
      size_t offset;
    } AKSArena;
    AKSArena *a = (AKSArena *)(uintptr_t)ValToInt(a_val);
    int64_t used = a ? (int64_t)a->offset : 0;
    ValFree(&a_val);
    PUSH(ValInt(used));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaFree") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ArenaFree() expects 1 argument (arena)\n");
      return -1;
    }
    AKSValue peek_val = *(stacktop - 1);
    if (ValKind(peek_val) != VALARENA) {
      AKSValue a_val = POP();
      typedef struct {
        uint8_t *base;
        size_t capacity;
        size_t offset;
      } AKSRawArena;
      AKSRawArena *a = (AKSRawArena *)(uintptr_t)ValToInt(a_val);
      if (a) {
#ifdef _WIN32
        if (a->base)
          VirtualFree(a->base, 0, MEM_RELEASE);
#else
        if (a->base)
          munmap(a->base, a->capacity);
#endif
        free(a);
      }
      ValFree(&a_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
  }
  if (strcmp(name, "LockFreeQueueNew") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: LockFreeQueueNew() expects 1 argument (capacity)\n");
      return -1;
    }
    AKSValue cap_val = POP();
    size_t cap = (size_t)ValToInt(cap_val);
    if (cap < 4)
      cap = 1024;
    typedef struct {
      int64_t *buffer;
      size_t capacity;
      volatile int64_t head;
      volatile int64_t tail;
    } AKSQueue;
    AKSQueue *q = (AKSQueue *)malloc(sizeof(AKSQueue));
    q->capacity = cap;
    q->buffer = (int64_t *)calloc(cap, sizeof(int64_t));
    q->head = 0;
    q->tail = 0;
    ValFree(&cap_val);
    PUSH(ValInt((int64_t)(uintptr_t)q));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LockFreeQueuePush") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: LockFreeQueuePush() expects 2 arguments "
                      "(queue, val)\n");
      return -1;
    }
    AKSValue val_obj = POP();
    AKSValue q_val = POP();
    int64_t val = ValToInt(val_obj);
    typedef struct {
      int64_t *buffer;
      size_t capacity;
      volatile int64_t head;
      volatile int64_t tail;
    } AKSQueue;
    AKSQueue *q = (AKSQueue *)(uintptr_t)ValToInt(q_val);
    int success = 0;
    if (q) {
      int64_t next_tail = (q->tail + 1) % q->capacity;
      if (next_tail != q->head) {
        q->buffer[q->tail] = val;
#ifdef _WIN32
        MemoryBarrier();
#else
        __sync_synchronize();
#endif
        q->tail = next_tail;
        success = 1;
      }
    }
    ValFree(&val_obj);
    ValFree(&q_val);
    PUSH(ValBool(success));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LockFreeQueuePop") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: LockFreeQueuePop() expects 1 argument (queue)\n");
      return -1;
    }
    AKSValue q_val = POP();
    typedef struct {
      int64_t *buffer;
      size_t capacity;
      volatile int64_t head;
      volatile int64_t tail;
    } AKSQueue;
    AKSQueue *q = (AKSQueue *)(uintptr_t)ValToInt(q_val);
    AKSValue res = ValNull();
    if (q && q->head != q->tail) {
      int64_t val = q->buffer[q->head];
#ifdef _WIN32
      MemoryBarrier();
#else
      __sync_synchronize();
#endif
      q->head = (q->head + 1) % q->capacity;
      res = ValInt(val);
    }
    ValFree(&q_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LockFreeQueueFree") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: LockFreeQueueFree() expects 1 argument (queue)\n");
      return -1;
    }
    AKSValue q_val = POP();
    typedef struct {
      int64_t *buffer;
      size_t capacity;
      volatile int64_t head;
      volatile int64_t tail;
    } AKSQueue;
    AKSQueue *q = (AKSQueue *)(uintptr_t)ValToInt(q_val);
    if (q) {
      if (q->buffer)
        free(q->buffer);
      free(q);
    }
    ValFree(&q_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryPoolNew") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryPoolNew() expects 2 arguments "
                      "(block_size, block_count)\n");
      return -1;
    }
    AKSValue count_val = POP();
    AKSValue size_val = POP();
    size_t blkSize = (size_t)ValToInt(size_val);
    size_t blkCount = (size_t)ValToInt(count_val);
    if (blkSize < 8)
      blkSize = 8;
    blkSize = (blkSize + 7) & ~7;
    if (blkCount < 1)
      blkCount = 64;

    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;

    AKSMemoryPool *pool = (AKSMemoryPool *)malloc(sizeof(AKSMemoryPool));
    pool->blockSize = blkSize;
    pool->blockCount = blkCount;
    pool->freeTop = (int32_t)blkCount;
    pool->freeList = (int32_t *)malloc(blkCount * sizeof(int32_t));
    for (size_t i = 0; i < blkCount; i++) {
      pool->freeList[i] = (int32_t)i;
    }
#ifdef _WIN32
    pool->slab = (uint8_t *)VirtualAlloc(
        NULL, blkSize * blkCount, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    pool->slab =
        (uint8_t *)mmap(NULL, blkSize * blkCount, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    ValFree(&count_val);
    ValFree(&size_val);
    PUSH(ValInt((int64_t)(uintptr_t)pool));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryPoolAlloc") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: MemoryPoolAlloc() expects 1 argument (pool)\n");
      return -1;
    }
    AKSValue p_val = POP();
    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;
    AKSMemoryPool *pool = (AKSMemoryPool *)(uintptr_t)ValToInt(p_val);
    int64_t ptr = 0;
    if (pool && pool->freeTop > 0) {
      int32_t idx = pool->freeList[--pool->freeTop];
      ptr = (int64_t)(uintptr_t)(pool->slab + (size_t)idx * pool->blockSize);
    }
    ValFree(&p_val);
    PUSH(ValInt(ptr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryPoolFree") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: MemoryPoolFree() expects 2 arguments (pool, ptr)\n");
      return -1;
    }
    AKSValue ptr_val = POP();
    AKSValue p_val = POP();
    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;
    AKSMemoryPool *pool = (AKSMemoryPool *)(uintptr_t)ValToInt(p_val);
    uintptr_t ptr = (uintptr_t)ValToInt(ptr_val);
    if (pool && ptr >= (uintptr_t)pool->slab &&
        ptr < (uintptr_t)(pool->slab + pool->blockCount * pool->blockSize)) {
      if ((size_t)pool->freeTop < pool->blockCount) {
        int32_t idx =
            (int32_t)((ptr - (uintptr_t)pool->slab) / pool->blockSize);
        pool->freeList[pool->freeTop++] = idx;
      }
    }
    ValFree(&ptr_val);
    ValFree(&p_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryPoolAvailable") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: MemoryPoolAvailable() expects 1 argument (pool)\n");
      return -1;
    }
    AKSValue p_val = POP();
    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;
    AKSMemoryPool *pool = (AKSMemoryPool *)(uintptr_t)ValToInt(p_val);
    int64_t avail = pool ? (int64_t)pool->freeTop : 0;
    ValFree(&p_val);
    PUSH(ValInt(avail));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryPoolDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: MemoryPoolDestroy() expects 1 argument (pool)\n");
      return -1;
    }
    AKSValue p_val = POP();
    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;
    AKSMemoryPool *pool = (AKSMemoryPool *)(uintptr_t)ValToInt(p_val);
    if (pool) {
#ifdef _WIN32
      if (pool->slab)
        VirtualFree(pool->slab, 0, MEM_RELEASE);
#else
      if (pool->slab)
        munmap(pool->slab, pool->blockSize * pool->blockCount);
#endif
      if (pool->freeList)
        free(pool->freeList);
      free(pool);
    }
    ValFree(&p_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamNew") == 0) {
    int64_t cap = 256;
    if (argc == 1) {
      AKSValue c_val = POP();
      cap = ValToInt(c_val);
      if (cap < 16)
        cap = 16;
      ValFree(&c_val);
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSMemoryStreamObj *ms =
        (AKSMemoryStreamObj *)malloc(sizeof(AKSMemoryStreamObj));
    if (ms) {
      ms->buffer = (uint8_t *)malloc((size_t)cap);
      ms->position = 0;
      ms->length = 0;
      ms->capacity = cap;
    }
    PUSH(ValInt((int64_t)(uintptr_t)ms));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamWriteByte") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamWriteByte() expects 2 "
                      "arguments (stream, byte)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue b_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms && ms->buffer) {
      if (ms->position + 1 > ms->capacity) {
        ms->capacity = (ms->capacity * 2) + 64;
        ms->buffer = (uint8_t *)realloc(ms->buffer, (size_t)ms->capacity);
      }
      ms->buffer[ms->position++] = (uint8_t)ValToInt(b_val);
      if (ms->position > ms->length)
        ms->length = ms->position;
      ok = 1;
    }
    ValFree(&b_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamWriteInt32") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamWriteInt32() expects 2 "
                      "arguments (stream, int32)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue v_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms && ms->buffer) {
      if (ms->position + 4 > ms->capacity) {
        ms->capacity = (ms->capacity * 2) + 64;
        ms->buffer = (uint8_t *)realloc(ms->buffer, (size_t)ms->capacity);
      }
      int32_t val32 = (int32_t)ValToInt(v_val);
      memcpy(ms->buffer + ms->position, &val32, 4);
      ms->position += 4;
      if (ms->position > ms->length)
        ms->length = ms->position;
      ok = 1;
    }
    ValFree(&v_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamWriteInt64") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamWriteInt64() expects 2 "
                      "arguments (stream, int64)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue v_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms && ms->buffer) {
      if (ms->position + 8 > ms->capacity) {
        ms->capacity = (ms->capacity * 2) + 64;
        ms->buffer = (uint8_t *)realloc(ms->buffer, (size_t)ms->capacity);
      }
      int64_t val64 = ValToInt(v_val);
      memcpy(ms->buffer + ms->position, &val64, 8);
      ms->position += 8;
      if (ms->position > ms->length)
        ms->length = ms->position;
      ok = 1;
    }
    ValFree(&v_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamWriteString") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamWriteString() expects 2 "
                      "arguments (stream, str)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue s_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms && ms->buffer && ValKind(s_val) == VALSTR) {
      AKSString *str = ValToStrPtr(s_val);
      if (ms->position + str->len > ms->capacity) {
        ms->capacity = (ms->capacity + str->len) * 2;
        ms->buffer = (uint8_t *)realloc(ms->buffer, (size_t)ms->capacity);
      }
      memcpy(ms->buffer + ms->position, str->data, (size_t)str->len);
      ms->position += str->len;
      if (ms->position > ms->length)
        ms->length = ms->position;
      ok = 1;
    }
    ValFree(&s_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamReadByte") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamReadByte() expects 1 "
                      "argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t bVal = -1;
    if (ms && ms->buffer && ms->position < ms->length) {
      bVal = (int64_t)ms->buffer[ms->position++];
    }
    ValFree(&h_val);
    PUSH(ValInt(bVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamReadInt32") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamReadInt32() expects 1 "
                      "argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t resVal = 0;
    if (ms && ms->buffer && ms->position + 4 <= ms->length) {
      int32_t val32 = 0;
      memcpy(&val32, ms->buffer + ms->position, 4);
      ms->position += 4;
      resVal = (int64_t)val32;
    }
    ValFree(&h_val);
    PUSH(ValInt(resVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamReadInt64") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamReadInt64() expects 1 "
                      "argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t resVal = 0;
    if (ms && ms->buffer && ms->position + 8 <= ms->length) {
      memcpy(&resVal, ms->buffer + ms->position, 8);
      ms->position += 8;
    }
    ValFree(&h_val);
    PUSH(ValInt(resVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamReadString") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamReadString() expects 2 "
                      "arguments (stream, len)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue l_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t reqLen = ValToInt(l_val);
    AKSValue res = ValNull();
    if (ms && ms->buffer && reqLen > 0) {
      int64_t avail = ms->length - ms->position;
      if (reqLen > avail)
        reqLen = avail;
      if (reqLen > 0) {
        res = ValStrLen((const char *)(ms->buffer + ms->position), (int)reqLen);
        ms->position += reqLen;
      }
    }
    if (ValKind(res) == VALNULL) {
      res = ValStr("");
    }
    ValFree(&l_val);
    ValFree(&h_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamSeek") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamSeek() expects 2 arguments "
                      "(stream, pos)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue p_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms) {
      int64_t targetPos = ValToInt(p_val);
      if (targetPos < 0)
        targetPos = 0;
      if (targetPos > ms->length)
        targetPos = ms->length;
      ms->position = targetPos;
      ok = 1;
    }
    ValFree(&p_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamPosition") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamPosition() expects 1 "
                      "argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t pos = ms ? ms->position : 0;
    ValFree(&h_val);
    PUSH(ValInt(pos));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamLength") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: MemoryStreamLength() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t len = ms ? ms->length : 0;
    ValFree(&h_val);
    PUSH(ValInt(len));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamToHex") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: MemoryStreamToHex() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    AKSValue res = ValNull();
    if (ms && ms->buffer && ms->length > 0) {
      char *hexBuf = (char *)malloc((size_t)(ms->length * 2 + 1));
      if (hexBuf) {
        for (int64_t i = 0; i < ms->length; i++) {
          sprintf(hexBuf + i * 2, "%02x", (unsigned int)ms->buffer[i]);
        }
        hexBuf[ms->length * 2] = '\0';
        res = ValStrLen(hexBuf, (int)(ms->length * 2));
        free(hexBuf);
      }
    }
    if (ValKind(res) == VALNULL) {
      res = ValStr("");
    }
    ValFree(&h_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryStreamDestroy") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: MemoryStreamDestroy() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms) {
      if (ms->buffer)
        free(ms->buffer);
      free(ms);
      ok = 1;
    }
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AlignedBufferAlloc") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AlignedBufferAlloc() expects 2 arguments "
                      "(size, alignment)\n");
      return -1;
    }
    AKSValue AlignVal = POP();
    AKSValue SizeVal = POP();
    size_t BufSize = (size_t)ValToInt(SizeVal);
    size_t Alignment = (size_t)ValToInt(AlignVal);
    if (Alignment < 8 || (Alignment & (Alignment - 1)) != 0) {
      Alignment = 64;
    }
    void *MemPtr = _aligned_malloc(BufSize, Alignment);
    if (MemPtr) {
      memset(MemPtr, 0, BufSize);
    }
    ValFree(&SizeVal);
    ValFree(&AlignVal);
    PUSH(ValInt((int64_t)(uintptr_t)MemPtr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AlignedBufferWriteBytes") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: AlignedBufferWriteBytes() expects 3 "
                      "arguments (ptr, offset, hex_str)\n");
      return -1;
    }
    AKSValue HexVal = POP();
    AKSValue OffVal = POP();
    AKSValue PtrVal = POP();
    uint8_t *BufPtr = (uint8_t *)(uintptr_t)ValToInt(PtrVal);
    size_t Offset = (size_t)ValToInt(OffVal);
    if (BufPtr && ValKind(HexVal) == VALSTR) {
      const char *HexData = ValToStrPtr(HexVal)->data;
      size_t HexLen = (size_t)ValToStrPtr(HexVal)->len;
      for (size_t Idx = 0; Idx + 1 < HexLen; Idx += 2) {
        unsigned int ByteVal = 0;
        sscanf(&HexData[Idx], "%02x", &ByteVal);
        BufPtr[Offset + (Idx / 2)] = (uint8_t)ByteVal;
      }
    }
    ValFree(&PtrVal);
    ValFree(&OffVal);
    ValFree(&HexVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AlignedBufferReadBytes") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: AlignedBufferReadBytes() expects 3 "
                      "arguments (ptr, offset, count)\n");
      return -1;
    }
    AKSValue CountVal = POP();
    AKSValue OffVal = POP();
    AKSValue PtrVal = POP();
    uint8_t *BufPtr = (uint8_t *)(uintptr_t)ValToInt(PtrVal);
    size_t Offset = (size_t)ValToInt(OffVal);
    size_t ByteCount = (size_t)ValToInt(CountVal);
    if (ByteCount > 2048)
      ByteCount = 2048;
    char HexOut[4097] = {0};
    if (BufPtr) {
      static const char HexChars[] = "0123456789abcdef";
      for (size_t Idx = 0; Idx < ByteCount; Idx++) {
        HexOut[Idx * 2] = HexChars[(BufPtr[Offset + Idx] >> 4) & 0x0F];
        HexOut[Idx * 2 + 1] = HexChars[BufPtr[Offset + Idx] & 0x0F];
      }
      HexOut[ByteCount * 2] = '\0';
    }
    ValFree(&PtrVal);
    ValFree(&OffVal);
    ValFree(&CountVal);
    PUSH(ValStr(HexOut));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AlignedBufferIsAligned") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AlignedBufferIsAligned() expects 2 "
                      "arguments (ptr, alignment)\n");
      return -1;
    }
    AKSValue AlignVal = POP();
    AKSValue PtrVal = POP();
    uintptr_t RawAddr = (uintptr_t)ValToInt(PtrVal);
    size_t Alignment = (size_t)ValToInt(AlignVal);
    BOOL IsAligned = (Alignment > 0) ? ((RawAddr % Alignment) == 0) : FALSE;
    ValFree(&PtrVal);
    ValFree(&AlignVal);
    PUSH(ValBool(IsAligned ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AlignedBufferFree") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: AlignedBufferFree() expects 1 argument (ptr)\n");
      return -1;
    }
    AKSValue PtrVal = POP();
    void *MemPtr = (void *)(uintptr_t)ValToInt(PtrVal);
    if (MemPtr) {
      _aligned_free(MemPtr);
    }
    ValFree(&PtrVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMemSearchFindByte") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMemSearchFindByte() expects 2 "
                      "arguments (hex_data, byte_val)\n");
      return -1;
    }
    AKSValue ByteVal = POP();
    AKSValue DataVal = POP();
    int64_t FoundOffset = -1;
    if (ValKind(DataVal) == VALSTR) {
      const char *HexData = ValToStrPtr(DataVal)->data;
      size_t HexLen = (size_t)ValToStrPtr(DataVal)->len;
      size_t ByteCount = HexLen / 2;
      uint8_t TargetByte = (uint8_t)ValToInt(ByteVal);
      if (ByteCount > 0) {
        uint8_t *Buf = (uint8_t *)malloc(ByteCount);
        if (Buf) {
          for (size_t i = 0; i < ByteCount; i++) {
            unsigned int B = 0;
            sscanf(&HexData[i * 2], "%02x", &B);
            Buf[i] = (uint8_t)B;
          }
          size_t Idx = 0;
#if defined(__AVX2__) || defined(_M_X64)
          __m256i TargetVec = _mm256_set1_epi8((char)TargetByte);
          for (; Idx + 32 <= ByteCount; Idx += 32) {
            __m256i Chunk = _mm256_loadu_si256((const __m256i *)(Buf + Idx));
            __m256i Cmp = _mm256_cmpeq_epi8(Chunk, TargetVec);
            int Mask = _mm256_movemask_epi8(Cmp);
            if (Mask != 0) {
              unsigned long BitIdx = 0;
              _BitScanForward(&BitIdx, (unsigned long)Mask);
              FoundOffset = (int64_t)(Idx + BitIdx);
              break;
            }
          }
#endif
          if (FoundOffset == -1) {
            for (; Idx < ByteCount; Idx++) {
              if (Buf[Idx] == TargetByte) {
                FoundOffset = (int64_t)Idx;
                break;
              }
            }
          }
          free(Buf);
        }
      }
    }
    ValFree(&DataVal);
    ValFree(&ByteVal);
    PUSH(ValInt(FoundOffset));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMemSearchFindSequence") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMemSearchFindSequence() expects 2 "
                      "arguments (hex_haystack, hex_needle)\n");
      return -1;
    }
    AKSValue NeedleVal = POP();
    AKSValue HayVal = POP();
    int64_t FoundOffset = -1;
    if (ValKind(HayVal) == VALSTR && ValKind(NeedleVal) == VALSTR) {
      const char *HayHex = ValToStrPtr(HayVal)->data;
      size_t HayLen = (size_t)ValToStrPtr(HayVal)->len / 2;
      const char *NeedleHex = ValToStrPtr(NeedleVal)->data;
      size_t NeedleLen = (size_t)ValToStrPtr(NeedleVal)->len / 2;
      if (NeedleLen > 0 && HayLen >= NeedleLen) {
        uint8_t *HayBuf = (uint8_t *)malloc(HayLen);
        uint8_t *NeedleBuf = (uint8_t *)malloc(NeedleLen);
        if (HayBuf && NeedleBuf) {
          for (size_t i = 0; i < HayLen; i++) {
            unsigned int B = 0;
            sscanf(&HayHex[i * 2], "%02x", &B);
            HayBuf[i] = (uint8_t)B;
          }
          for (size_t i = 0; i < NeedleLen; i++) {
            unsigned int B = 0;
            sscanf(&NeedleHex[i * 2], "%02x", &B);
            NeedleBuf[i] = (uint8_t)B;
          }
          for (size_t i = 0; i + NeedleLen <= HayLen; i++) {
            if (HayBuf[i] == NeedleBuf[0] &&
                memcmp(HayBuf + i, NeedleBuf, NeedleLen) == 0) {
              FoundOffset = (int64_t)i;
              break;
            }
          }
        }
        if (HayBuf)
          free(HayBuf);
        if (NeedleBuf)
          free(NeedleBuf);
      }
    }
    ValFree(&HayVal);
    ValFree(&NeedleVal);
    PUSH(ValInt(FoundOffset));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMemSearchCountByte") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMemSearchCountByte() expects 2 "
                      "arguments (hex_data, byte_val)\n");
      return -1;
    }
    AKSValue ByteVal = POP();
    AKSValue DataVal = POP();
    int64_t Count = 0;
    if (ValKind(DataVal) == VALSTR) {
      const char *HexData = ValToStrPtr(DataVal)->data;
      size_t HexLen = (size_t)ValToStrPtr(DataVal)->len;
      size_t ByteCount = HexLen / 2;
      uint8_t TargetByte = (uint8_t)ValToInt(ByteVal);
      if (ByteCount > 0) {
        uint8_t *Buf = (uint8_t *)malloc(ByteCount);
        if (Buf) {
          for (size_t i = 0; i < ByteCount; i++) {
            unsigned int B = 0;
            sscanf(&HexData[i * 2], "%02x", &B);
            Buf[i] = (uint8_t)B;
          }
          size_t Idx = 0;
#if defined(__AVX2__) || defined(_M_X64)
          __m256i TargetVec = _mm256_set1_epi8((char)TargetByte);
          for (; Idx + 32 <= ByteCount; Idx += 32) {
            __m256i Chunk = _mm256_loadu_si256((const __m256i *)(Buf + Idx));
            __m256i Cmp = _mm256_cmpeq_epi8(Chunk, TargetVec);
            int Mask = _mm256_movemask_epi8(Cmp);
#ifdef _MSC_VER
            Count += (int64_t)__popcnt((unsigned int)Mask);
#else
            Count += (int64_t)__builtin_popcount((unsigned int)Mask);
#endif
          }
#endif
          for (; Idx < ByteCount; Idx++) {
            if (Buf[Idx] == TargetByte)
              Count++;
          }
          free(Buf);
        }
      }
    }
    ValFree(&DataVal);
    ValFree(&ByteVal);
    PUSH(ValInt(Count));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitSet256NewC") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: BitSet256NewC() expects 0 arguments\n");
      return -1;
    }
    AKSValue Res = ValStruct("BitSet256");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 8;
    S->fieldnames = malloc(8 * sizeof(char *));
    S->fieldvals = malloc(8 * sizeof(AKSValue));
    for (int i = 0; i < 8; i++) {
      char fname[8];
      snprintf(fname, sizeof(fname), "w%d", i);
      S->fieldnames[i] = strdup(fname);
      S->fieldvals[i] = ValInt(0);
    }
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitSet256SetC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: BitSet256SetC() expects 3 arguments "
                      "(self, bit, val)\n");
      return -1;
    }
    AKSValue ValV = POP(), BitV = POP(), SelfV = POP();
    if (ValKind(SelfV) == VALSTRUCT) {
      AKSStruct *S = ValToObj(SelfV);
      int bit = (int)ValToInt(BitV) & 0xFF;
      int set_val = ValIsTruthy(ValV);
      int w_idx = bit / 32;
      int b_idx = bit % 32;
      if (w_idx >= 0 && w_idx < 8 && w_idx < S->fieldcount) {
        uint32_t cur = (uint32_t)ValToInt(S->fieldvals[w_idx]);
        if (set_val)
          cur |= (1U << b_idx);
        else
          cur &= ~(1U << b_idx);
        S->fieldvals[w_idx] = ValInt((int64_t)cur);
      }
    }
    ValFree(&BitV);
    ValFree(&ValV);
    PUSH(SelfV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitSet256TestC") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: BitSet256TestC() expects 2 arguments (self, bit)\n");
      return -1;
    }
    AKSValue BitV = POP(), SelfV = POP();
    int is_set = 0;
    if (ValKind(SelfV) == VALSTRUCT) {
      AKSStruct *S = ValToObj(SelfV);
      int bit = (int)ValToInt(BitV) & 0xFF;
      int w_idx = bit / 32;
      int b_idx = bit % 32;
      if (w_idx >= 0 && w_idx < 8 && w_idx < S->fieldcount) {
        uint32_t cur = (uint32_t)ValToInt(S->fieldvals[w_idx]);
        is_set = ((cur >> b_idx) & 1) ? 1 : 0;
      }
    }
    ValFree(&SelfV);
    ValFree(&BitV);
    PUSH(ValBool(is_set));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitSet256CountOnesC") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: BitSet256CountOnesC() expects 1 argument (self)\n");
      return -1;
    }
    AKSValue SelfV = POP();
    int total_ones = 0;
    if (ValKind(SelfV) == VALSTRUCT) {
      AKSStruct *S = ValToObj(SelfV);
      for (int i = 0; i < 8 && i < S->fieldcount; i++) {
        uint32_t w = (uint32_t)ValToInt(S->fieldvals[i]);
#ifdef _MSC_VER
        total_ones += (int)__popcnt(w);
#elif defined(__GNUC__) || defined(__clang__)
        total_ones += __builtin_popcount(w);
#else
        while (w) {
          total_ones += (w & 1);
          w >>= 1;
        }
#endif
      }
    }
    ValFree(&SelfV);
    PUSH(ValInt(total_ones));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitSet256BitAndC") == 0 ||
      strcmp(name, "BitSet256BitOrC") == 0 ||
      strcmp(name, "BitSet256BitXorC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s expects 2 arguments (self, other)\n",
              name);
      return -1;
    }
    AKSValue OtherV = POP(), SelfV = POP();
    AKSValue Res = ValStruct("BitSet256");
    AKSStruct *SRes = ValToObj(Res);
    SRes->fieldcount = 8;
    SRes->fieldnames = malloc(8 * sizeof(char *));
    SRes->fieldvals = malloc(8 * sizeof(AKSValue));
    for (int i = 0; i < 8; i++) {
      char fname[8];
      snprintf(fname, sizeof(fname), "w%d", i);
      SRes->fieldnames[i] = strdup(fname);
    }

    if (ValKind(SelfV) == VALSTRUCT && ValKind(OtherV) == VALSTRUCT) {
      AKSStruct *S1 = ValToObj(SelfV);
      AKSStruct *S2 = ValToObj(OtherV);
      for (int i = 0; i < 8; i++) {
        uint32_t a =
            (i < S1->fieldcount) ? (uint32_t)ValToInt(S1->fieldvals[i]) : 0;
        uint32_t b =
            (i < S2->fieldcount) ? (uint32_t)ValToInt(S2->fieldvals[i]) : 0;
        uint32_t r = 0;
        if (strcmp(name, "BitSet256BitAndC") == 0)
          r = a & b;
        else if (strcmp(name, "BitSet256BitOrC") == 0)
          r = a | b;
        else
          r = a ^ b;
        SRes->fieldvals[i] = ValInt((int64_t)r);
      }
    } else {
      for (int i = 0; i < 8; i++)
        SRes->fieldvals[i] = ValInt(0);
    }
    ValFree(&SelfV);
    ValFree(&OtherV);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBloomFilterCreateC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBloomFilterCreateC() expects 2 "
                      "arguments (capacity_bits, hash_count)\n");
      return -1;
    }
    AKSValue HashCountV = POP(), CapV = POP();
    int64_t CapBits = ValToInt(CapV);
    int64_t HashCount = ValToInt(HashCountV);
    if (CapBits < 64)
      CapBits = 64;
    if (HashCount < 1)
      HashCount = 1;
    if (HashCount > 16)
      HashCount = 16;

    int num_words = (int)((CapBits + 31) / 32);
    AKSValue BitsArr = ValArr(num_words);
    AKSArray *Arr = ValToArr(BitsArr);
    Arr->count = num_words;
    for (int i = 0; i < num_words; i++)
      Arr->items[i] = ValInt(0);

    AKSValue Res = ValStruct("FastBloomFilter");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 3;
    S->fieldnames = malloc(3 * sizeof(char *));
    S->fieldvals = malloc(3 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("bit_count");
    S->fieldvals[0] = ValInt(CapBits);
    S->fieldnames[1] = strdup("hash_count");
    S->fieldvals[1] = ValInt(HashCount);
    S->fieldnames[2] = strdup("bits");
    S->fieldvals[2] = BitsArr;

    ValFree(&CapV);
    ValFree(&HashCountV);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBloomFilterAddC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBloomFilterAddC() expects 2 "
                      "arguments (self, item_str)\n");
      return -1;
    }
    AKSValue ItemV = POP(), SelfV = POP();
    if (ValKind(SelfV) == VALSTRUCT && ValKind(ItemV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSString *Item = ValToStrPtr(ItemV);
      int64_t CapBits = ValToInt(S->fieldvals[0]);
      int64_t HashCount = ValToInt(S->fieldvals[1]);
      AKSArray *BitsArr = ValToArr(S->fieldvals[2]);

      static const uint8_t seed1[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                        0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98,
                                        0x76, 0x54, 0x32, 0x10};
      static const uint8_t seed2[16] = {0x11, 0x33, 0x55, 0x77, 0x99, 0xbb,
                                        0xdd, 0xff, 0xee, 0xcc, 0xaa, 0x88,
                                        0x66, 0x44, 0x22, 0x00};

      uint64_t h1 =
          CryptoSipHash64(seed1, (const uint8_t *)Item->data, Item->len);
      uint64_t h2 =
          CryptoSipHash64(seed2, (const uint8_t *)Item->data, Item->len);

      for (int i = 0; i < HashCount; i++) {
        uint64_t combined = (h1 + (uint64_t)i * h2) % (uint64_t)CapBits;
        int word_idx = (int)(combined / 32);
        int bit_idx = (int)(combined % 32);
        if (word_idx < BitsArr->count) {
          uint32_t cur = (uint32_t)ValToInt(BitsArr->items[word_idx]);
          cur |= (1U << bit_idx);
          BitsArr->items[word_idx] = ValInt((int64_t)cur);
        }
      }
    }
    ValFree(&SelfV);
    ValFree(&ItemV);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBloomFilterContainsC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBloomFilterContainsC() expects 2 "
                      "arguments (self, item_str)\n");
      return -1;
    }
    AKSValue ItemV = POP(), SelfV = POP();
    int contains = 1;
    if (ValKind(SelfV) == VALSTRUCT && ValKind(ItemV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSString *Item = ValToStrPtr(ItemV);
      int64_t CapBits = ValToInt(S->fieldvals[0]);
      int64_t HashCount = ValToInt(S->fieldvals[1]);
      AKSArray *BitsArr = ValToArr(S->fieldvals[2]);

      static const uint8_t seed1[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                        0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98,
                                        0x76, 0x54, 0x32, 0x10};
      static const uint8_t seed2[16] = {0x11, 0x33, 0x55, 0x77, 0x99, 0xbb,
                                        0xdd, 0xff, 0xee, 0xcc, 0xaa, 0x88,
                                        0x66, 0x44, 0x22, 0x00};

      uint64_t h1 =
          CryptoSipHash64(seed1, (const uint8_t *)Item->data, Item->len);
      uint64_t h2 =
          CryptoSipHash64(seed2, (const uint8_t *)Item->data, Item->len);

      for (int i = 0; i < HashCount; i++) {
        uint64_t combined = (h1 + (uint64_t)i * h2) % (uint64_t)CapBits;
        int word_idx = (int)(combined / 32);
        int bit_idx = (int)(combined % 32);
        if (word_idx < BitsArr->count) {
          uint32_t cur = (uint32_t)ValToInt(BitsArr->items[word_idx]);
          if (!((cur >> bit_idx) & 1)) {
            contains = 0;
            break;
          }
        } else {
          contains = 0;
          break;
        }
      }
    }
    ValFree(&SelfV);
    ValFree(&ItemV);
    PUSH(ValBool(contains));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CircularByteBufferNewC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CircularByteBufferNewC() expects 1 "
                      "argument (capacity)\n");
      return -1;
    }
    AKSValue CapV = POP();
    int cap = (int)ValToInt(CapV);
    if (cap < 16)
      cap = 16;

    AKSValue BufferArr = ValArr(cap);
    AKSArray *Arr = ValToArr(BufferArr);
    Arr->count = cap;
    for (int i = 0; i < cap; i++)
      Arr->items[i] = ValInt(0);

    AKSValue Res = ValStruct("CircularByteBuffer");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("capacity");
    S->fieldvals[0] = ValInt(cap);
    S->fieldnames[1] = strdup("read_pos");
    S->fieldvals[1] = ValInt(0);
    S->fieldnames[2] = strdup("write_pos");
    S->fieldvals[2] = ValInt(0);
    S->fieldnames[3] = strdup("count");
    S->fieldvals[3] = ValInt(0);
    S->fieldnames[4] = strdup("buffer");
    S->fieldvals[4] = BufferArr;

    ValFree(&CapV);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CircularByteBufferWriteC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CircularByteBufferWriteC() expects 2 "
                      "arguments (self, data_str)\n");
      return -1;
    }
    AKSValue DataV = POP(), SelfV = POP();
    int written = 0;
    if (ValKind(SelfV) == VALSTRUCT && ValKind(DataV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSString *Data = ValToStrPtr(DataV);
      int cap = (int)ValToInt(S->fieldvals[0]);
      int wpos = (int)ValToInt(S->fieldvals[2]);
      int count = (int)ValToInt(S->fieldvals[3]);
      AKSArray *Arr = ValToArr(S->fieldvals[4]);

      int space = cap - count;
      int to_write = (int)Data->len;
      if (to_write > space)
        to_write = space;

      for (int i = 0; i < to_write; i++) {
        Arr->items[wpos] = ValInt((uint8_t)Data->data[i]);
        wpos = (wpos + 1) % cap;
      }
      count += to_write;
      written = to_write;

      S->fieldvals[2] = ValInt(wpos);
      S->fieldvals[3] = ValInt(count);
    }
    ValFree(&SelfV);
    ValFree(&DataV);
    PUSH(ValInt(written));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CircularByteBufferReadC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CircularByteBufferReadC() expects 2 "
                      "arguments (self, max_bytes)\n");
      return -1;
    }
    AKSValue MaxV = POP(), SelfV = POP();
    char *OutStr = NULL;
    if (ValKind(SelfV) == VALSTRUCT) {
      AKSStruct *S = ValToObj(SelfV);
      int max_bytes = (int)ValToInt(MaxV);
      int cap = (int)ValToInt(S->fieldvals[0]);
      int rpos = (int)ValToInt(S->fieldvals[1]);
      int count = (int)ValToInt(S->fieldvals[3]);
      AKSArray *Arr = ValToArr(S->fieldvals[4]);

      int to_read = count;
      if (max_bytes > 0 && to_read > max_bytes)
        to_read = max_bytes;

      OutStr = malloc(to_read + 1);
      for (int i = 0; i < to_read; i++) {
        OutStr[i] = (char)(uint8_t)ValToInt(Arr->items[rpos]);
        rpos = (rpos + 1) % cap;
      }
      OutStr[to_read] = '\0';
      count -= to_read;

      S->fieldvals[1] = ValInt(rpos);
      S->fieldvals[3] = ValInt(count);
    }
    ValFree(&SelfV);
    ValFree(&MaxV);
    PUSH(ValStr(OutStr ? OutStr : ""));
    if (OutStr)
      free(OutStr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RadixTreeNewC") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: RadixTreeNewC() expects 0 arguments\n");
      return -1;
    }
    AKSValue Res = ValStruct("RadixTree");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 3;
    S->fieldnames = malloc(3 * sizeof(char *));
    S->fieldvals = malloc(3 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("count");
    S->fieldvals[0] = ValInt(0);
    S->fieldnames[1] = strdup("keys");
    S->fieldvals[1] = ValArr(0);
    S->fieldnames[2] = strdup("values");
    S->fieldvals[2] = ValArr(0);

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RadixTreeInsertC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RadixTreeInsertC() expects 3 arguments "
                      "(self, key, val)\n");
      return -1;
    }
    AKSValue ValV = POP(), KeyV = POP(), SelfV = POP();
    if (ValKind(SelfV) == VALSTRUCT && ValKind(KeyV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSArray *KeysArr = ValToArr(S->fieldvals[1]);
      AKSArray *ValsArr = ValToArr(S->fieldvals[2]);
      AKSString *KeyStr = ValToStrPtr(KeyV);
      int found_idx = -1;
      for (int i = 0; i < KeysArr->count; i++) {
        if (ValKind(KeysArr->items[i]) == VALSTR) {
          AKSString *K = ValToStrPtr(KeysArr->items[i]);
          if (strcmp(K->data, KeyStr->data) == 0) {
            found_idx = i;
            break;
          }
        }
      }
      if (found_idx >= 0) {
        ValFree(&ValsArr->items[found_idx]);
        ValsArr->items[found_idx] = ValCopy(ValV);
      } else {
        if (KeysArr->count >= KeysArr->capacity) {
          int nc = KeysArr->capacity ? KeysArr->capacity * 2 : 4;
          KeysArr->items = realloc(KeysArr->items, nc * sizeof(AKSValue));
          KeysArr->capacity = nc;
        }
        if (ValsArr->count >= ValsArr->capacity) {
          int nc = ValsArr->capacity ? ValsArr->capacity * 2 : 4;
          ValsArr->items = realloc(ValsArr->items, nc * sizeof(AKSValue));
          ValsArr->capacity = nc;
        }
        KeysArr->items[KeysArr->count++] = ValCopy(KeyV);
        ValsArr->items[ValsArr->count++] = ValCopy(ValV);
        S->fieldvals[0] = ValInt(KeysArr->count);
      }
    }
    ValFree(&SelfV);
    ValFree(&KeyV);
    ValFree(&ValV);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RadixTreeLookupC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RadixTreeLookupC() expects 2 arguments "
                      "(self, key)\n");
      return -1;
    }
    AKSValue KeyV = POP(), SelfV = POP();
    AKSValue Res = ValNull();
    if (ValKind(SelfV) == VALSTRUCT && ValKind(KeyV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSArray *KeysArr = ValToArr(S->fieldvals[1]);
      AKSArray *ValsArr = ValToArr(S->fieldvals[2]);
      AKSString *KeyStr = ValToStrPtr(KeyV);
      for (int i = 0; i < KeysArr->count; i++) {
        if (ValKind(KeysArr->items[i]) == VALSTR) {
          AKSString *K = ValToStrPtr(KeysArr->items[i]);
          if (strcmp(K->data, KeyStr->data) == 0) {
            Res = ValCopy(ValsArr->items[i]);
            break;
          }
        }
      }
    }
    ValFree(&SelfV);
    ValFree(&KeyV);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RadixTreePrefixSearchC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RadixTreePrefixSearchC() expects 2 "
                      "arguments (self, prefix)\n");
      return -1;
    }
    AKSValue PrefixV = POP(), SelfV = POP();
    AKSValue ResultArr = ValArr(0);
    AKSArray *Arr = ValToArr(ResultArr);

    if (ValKind(SelfV) == VALSTRUCT && ValKind(PrefixV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSArray *KeysArr = ValToArr(S->fieldvals[1]);
      AKSString *Prefix = ValToStrPtr(PrefixV);

      for (int i = 0; i < KeysArr->count; i++) {
        if (ValKind(KeysArr->items[i]) == VALSTR) {
          AKSString *K = ValToStrPtr(KeysArr->items[i]);
          if (strncmp(K->data, Prefix->data, Prefix->len) == 0) {
            if (Arr->count >= Arr->capacity) {
              int nc = Arr->capacity ? Arr->capacity * 2 : 4;
              Arr->items = realloc(Arr->items, nc * sizeof(AKSValue));
              Arr->capacity = nc;
            }
            Arr->items[Arr->count++] = ValCopy(KeysArr->items[i]);
          }
        }
      }
    }
    ValFree(&SelfV);
    ValFree(&PrefixV);
    PUSH(ResultArr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FenwickTreeNewC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FenwickTreeNewC() expects 1 arg\n");
      return -1;
    }
    AKSValue SzV = POP();
    int ft_n = (int)ValToInt(SzV);
    ValFree(&SzV);
    if (ft_n <= 0)
      ft_n = 1;
    AKSValue tree_arr = ValArr(ft_n + 1);
    AKSArray *ta = ValToArr(tree_arr);
    for (int i = 0; i <= ft_n; i++)
      AksArrPush(ta, ValInt(0));
    AKSValue ft_res = ValStruct("FenwickTree");
    AKSStruct *fs = ValToObj(ft_res);
    fs->fieldnames = malloc(2 * sizeof(char *));
    fs->fieldvals = malloc(2 * sizeof(AKSValue));
    fs->fieldnames[0] = strdup("n");
    fs->fieldvals[0] = ValInt(ft_n);
    fs->fieldnames[1] = strdup("tree");
    fs->fieldvals[1] = tree_arr;
    fs->fieldcount = 2;
    PUSH(ft_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FenwickTreeAddC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: FenwickTreeAddC() expects 3 args\n");
      return -1;
    }
    AKSValue DV = POP(), IV = POP(), TV = POP();
    int ft_delta = (int)ValToInt(DV), ft_i = (int)ValToInt(IV) + 1;
    AKSStruct *fs = ValToObj(TV);
    int ft_n = (int)ValToInt(fs->fieldvals[0]);
    AKSArray *ta = ValToArr(fs->fieldvals[1]);
    while (ft_i <= ft_n && ft_i < ta->count) {
      ta->items[ft_i] = ValInt(ValToInt(ta->items[ft_i]) + ft_delta);
      ft_i += ft_i & (-ft_i);
    }
    ValFree(&DV);
    ValFree(&IV);
    PUSH(TV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FenwickTreeQueryC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FenwickTreeQueryC() expects 2 args\n");
      return -1;
    }
    AKSValue IV = POP(), TV = POP();
    int ft_i = (int)ValToInt(IV) + 1;
    AKSStruct *fs = ValToObj(TV);
    AKSArray *ta = ValToArr(fs->fieldvals[1]);
    int64_t ft_sum = 0;
    while (ft_i > 0 && ft_i < ta->count) {
      ft_sum += ValToInt(ta->items[ft_i]);
      ft_i -= ft_i & (-ft_i);
    }
    ValFree(&IV);
    ValFree(&TV);
    PUSH(ValInt(ft_sum));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SegTreeNewC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SegTreeNewC() expects 1 arg\n");
      return -1;
    }
    AKSValue SzV = POP();
    int st_n = (int)ValToInt(SzV);
    ValFree(&SzV);
    if (st_n <= 0)
      st_n = 1;
    int st_sz = 4 * st_n;
    AKSValue st_arr = ValArr(st_sz);
    AKSArray *sa = ValToArr(st_arr);
    for (int i = 0; i < st_sz; i++)
      AksArrPush(sa, ValInt(0x7fffffff));
    AKSValue st_res = ValStruct("SegTree");
    AKSStruct *ss = ValToObj(st_res);
    ss->fieldnames = malloc(2 * sizeof(char *));
    ss->fieldvals = malloc(2 * sizeof(AKSValue));
    ss->fieldnames[0] = strdup("n");
    ss->fieldvals[0] = ValInt(st_n);
    ss->fieldnames[1] = strdup("tree");
    ss->fieldvals[1] = st_arr;
    ss->fieldcount = 2;
    PUSH(st_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SegTreeUpdateC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SegTreeUpdateC() expects 3 args\n");
      return -1;
    }
    AKSValue VV = POP(), PV = POP(), TV = POP();
    int st_pos = (int)ValToInt(PV), st_val = (int)ValToInt(VV);
    AKSStruct *ss = ValToObj(TV);
    int st_n = (int)ValToInt(ss->fieldvals[0]);
    AKSArray *sa = ValToArr(ss->fieldvals[1]);
    int st_node = 1, st_lo = 0, st_hi = st_n - 1;
    while (st_lo < st_hi) {
      int st_mid = (st_lo + st_hi) / 2;
      if (st_pos <= st_mid) {
        st_node = 2 * st_node;
        st_hi = st_mid;
      } else {
        st_node = 2 * st_node + 1;
        st_lo = st_mid + 1;
      }
    }
    if (st_node < sa->count)
      sa->items[st_node] = ValInt(st_val);
    st_node /= 2;
    while (st_node >= 1) {
      int l = 2 * st_node, r = 2 * st_node + 1;
      if (r < sa->count) {
        int lv = (int)ValToInt(sa->items[l]), rv = (int)ValToInt(sa->items[r]);
        sa->items[st_node] = ValInt(lv < rv ? lv : rv);
      }
      st_node /= 2;
    }
    ValFree(&VV);
    ValFree(&PV);
    PUSH(TV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SegTreeQueryC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SegTreeQueryC() expects 3 args\n");
      return -1;
    }
    AKSValue RV = POP(), LV = POP(), TV = POP();
    int stq_l = (int)ValToInt(LV), stq_r = (int)ValToInt(RV);
    AKSStruct *ss = ValToObj(TV);
    int stq_n = (int)ValToInt(ss->fieldvals[0]);
    AKSArray *sa = ValToArr(ss->fieldvals[1]);
    int stq_min = 0x7fffffff;
    typedef struct {
      int node, lo, hi;
    } StqFr;
    StqFr stq_stk[128];
    int stq_sp = 0;
    stq_stk[stq_sp].node = 1;
    stq_stk[stq_sp].lo = 0;
    stq_stk[stq_sp].hi = stq_n - 1;
    stq_sp++;
    while (stq_sp > 0) {
      StqFr fr = stq_stk[--stq_sp];
      if (fr.lo > stq_r || fr.hi < stq_l)
        continue;
      if (fr.lo >= stq_l && fr.hi <= stq_r) {
        if (fr.node < sa->count) {
          int v = (int)ValToInt(sa->items[fr.node]);
          if (v < stq_min)
            stq_min = v;
        }
        continue;
      }
      int mid = (fr.lo + fr.hi) / 2;
      if (stq_sp + 1 < 128) {
        if (2 * fr.node < sa->count) {
          stq_stk[stq_sp].node = 2 * fr.node;
          stq_stk[stq_sp].lo = fr.lo;
          stq_stk[stq_sp].hi = mid;
          stq_sp++;
        }
        if (2 * fr.node + 1 < sa->count) {
          stq_stk[stq_sp].node = 2 * fr.node + 1;
          stq_stk[stq_sp].lo = mid + 1;
          stq_stk[stq_sp].hi = fr.hi;
          stq_sp++;
        }
      }
    }
    ValFree(&RV);
    ValFree(&LV);
    ValFree(&TV);
    PUSH(ValInt(stq_min));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DSUNewC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DSUNewC() expects 1 arg\n");
      return -1;
    }
    AKSValue NV = POP();
    int dsu_n = (int)ValToInt(NV);
    ValFree(&NV);
    if (dsu_n <= 0)
      dsu_n = 1;
    AKSValue par_arr = ValArr(dsu_n);
    AKSValue rk_arr = ValArr(dsu_n);
    AKSArray *pa = ValToArr(par_arr), *ra = ValToArr(rk_arr);
    for (int i = 0; i < dsu_n; i++) {
      AksArrPush(pa, ValInt(i));
      AksArrPush(ra, ValInt(0));
    }
    AKSValue dsu_res = ValStruct("DSU");
    AKSStruct *ds = ValToObj(dsu_res);
    ds->fieldnames = malloc(3 * sizeof(char *));
    ds->fieldvals = malloc(3 * sizeof(AKSValue));
    ds->fieldnames[0] = strdup("n");
    ds->fieldvals[0] = ValInt(dsu_n);
    ds->fieldnames[1] = strdup("parent");
    ds->fieldvals[1] = par_arr;
    ds->fieldnames[2] = strdup("rank");
    ds->fieldvals[2] = rk_arr;
    ds->fieldcount = 3;
    PUSH(dsu_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DSUFindC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: DSUFindC() expects 2 args\n");
      return -1;
    }
    AKSValue XV = POP(), DV = POP();
    int dsu_x = (int)ValToInt(XV);
    AKSStruct *ds = ValToObj(DV);
    AKSArray *pa = ValToArr(ds->fieldvals[1]);
    while ((int)ValToInt(pa->items[dsu_x]) != dsu_x) {
      int par = (int)ValToInt(pa->items[dsu_x]);
      int gpar = (int)ValToInt(pa->items[par]);
      pa->items[dsu_x] = ValInt(gpar);
      dsu_x = gpar;
    }
    ValFree(&XV);
    PUSH(DV);
    PUSH(ValInt(dsu_x));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DSUUnionC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: DSUUnionC() expects 3 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV2 = POP(), DV = POP();
    AKSStruct *ds = ValToObj(DV);
    AKSArray *pa = ValToArr(ds->fieldvals[1]);
    AKSArray *ra = ValToArr(ds->fieldvals[2]);
    int dsu_a = (int)ValToInt(AV2), dsu_b = (int)ValToInt(BV);
    while ((int)ValToInt(pa->items[dsu_a]) != dsu_a) {
      int p = (int)ValToInt(pa->items[dsu_a]);
      pa->items[dsu_a] = pa->items[p];
      dsu_a = (int)ValToInt(pa->items[dsu_a]);
    }
    while ((int)ValToInt(pa->items[dsu_b]) != dsu_b) {
      int p = (int)ValToInt(pa->items[dsu_b]);
      pa->items[dsu_b] = pa->items[p];
      dsu_b = (int)ValToInt(pa->items[dsu_b]);
    }
    if (dsu_a != dsu_b) {
      if (ValToInt(ra->items[dsu_a]) < ValToInt(ra->items[dsu_b])) {
        int t = dsu_a;
        dsu_a = dsu_b;
        dsu_b = t;
      }
      pa->items[dsu_b] = ValInt(dsu_a);
      if (ValToInt(ra->items[dsu_a]) == ValToInt(ra->items[dsu_b]))
        ra->items[dsu_a] = ValInt(ValToInt(ra->items[dsu_a]) + 1);
    }
    ValFree(&AV2);
    ValFree(&BV);
    PUSH(DV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DSUSameC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: DSUSameC() expects 3 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV2 = POP(), DV = POP();
    AKSStruct *ds = ValToObj(DV);
    AKSArray *pa = ValToArr(ds->fieldvals[1]);
    int dsu_a = (int)ValToInt(AV2), dsu_b = (int)ValToInt(BV);
    while ((int)ValToInt(pa->items[dsu_a]) != dsu_a) {
      int p = (int)ValToInt(pa->items[dsu_a]);
      pa->items[dsu_a] = pa->items[p];
      dsu_a = (int)ValToInt(pa->items[dsu_a]);
    }
    while ((int)ValToInt(pa->items[dsu_b]) != dsu_b) {
      int p = (int)ValToInt(pa->items[dsu_b]);
      pa->items[dsu_b] = pa->items[p];
      dsu_b = (int)ValToInt(pa->items[dsu_b]);
    }
    int same = (dsu_a == dsu_b) ? 1 : 0;
    ValFree(&AV2);
    ValFree(&BV);
    ValFree(&DV);
    PUSH(ValBool(same));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MinMaxHeapNewC") == 0) {
    AKSValue data_arr = ValArr(8);
    AKSValue mmh_res = ValStruct("MinMaxHeap");
    AKSStruct *ms = ValToObj(mmh_res);
    ms->fieldnames = malloc(2 * sizeof(char *));
    ms->fieldvals = malloc(2 * sizeof(AKSValue));
    ms->fieldnames[0] = strdup("size");
    ms->fieldvals[0] = ValInt(0);
    ms->fieldnames[1] = strdup("data");
    ms->fieldvals[1] = data_arr;
    ms->fieldcount = 2;
    PUSH(mmh_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MinMaxHeapPushC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MinMaxHeapPushC() expects 2 args\n");
      return -1;
    }
    AKSValue VV = POP(), HV = POP();
    AKSStruct *ms = ValToObj(HV);
    int mmh_n = (int)ValToInt(ms->fieldvals[0]);
    AKSArray *da = ValToArr(ms->fieldvals[1]);
    AksArrPush(da, ValCopy(VV));
    ms->fieldvals[0] = ValInt(mmh_n + 1);
    int idx = mmh_n;
    while (idx > 0) {
      int par = (idx - 1) / 2;
      int lvl = 0;
      int tmp = idx + 1;
      while (tmp > 1) {
        tmp >>= 1;
        lvl++;
      }
      int64_t iv = ValToInt(da->items[idx]), pv = ValToInt(da->items[par]);
      if (lvl % 2 == 0) {
        if (iv > pv) {
          AKSValue t = da->items[idx];
          da->items[idx] = da->items[par];
          da->items[par] = t;
          idx = par;
        } else {
          int gp = (par - 1) / 2;
          while (idx > 2 && gp >= 0) {
            if (ValToInt(da->items[idx]) < ValToInt(da->items[gp])) {
              AKSValue t = da->items[idx];
              da->items[idx] = da->items[gp];
              da->items[gp] = t;
              idx = gp;
              par = (idx - 1) / 2;
              gp = (par - 1) / 2;
            } else
              break;
          }
          break;
        }
      } else {
        if (iv < pv) {
          AKSValue t = da->items[idx];
          da->items[idx] = da->items[par];
          da->items[par] = t;
          idx = par;
        } else {
          int gp = (par - 1) / 2;
          while (idx > 2 && gp >= 0) {
            if (ValToInt(da->items[idx]) > ValToInt(da->items[gp])) {
              AKSValue t = da->items[idx];
              da->items[idx] = da->items[gp];
              da->items[gp] = t;
              idx = gp;
              par = (idx - 1) / 2;
              gp = (par - 1) / 2;
            } else
              break;
          }
          break;
        }
      }
    }
    ValFree(&VV);
    PUSH(HV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MinMaxHeapMinC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MinMaxHeapMinC() expects 1 arg\n");
      return -1;
    }
    AKSValue HV = POP();
    AKSStruct *ms = ValToObj(HV);
    int mmh_n = (int)ValToInt(ms->fieldvals[0]);
    if (mmh_n == 0) {
      ValFree(&HV);
      PUSH(ValInt(0));
      *stacktopptr = stacktop;
      return 1;
    }
    AKSArray *da = ValToArr(ms->fieldvals[1]);
    AKSValue mmh_min = ValCopy(da->items[0]);
    ValFree(&HV);
    PUSH(mmh_min);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MinMaxHeapMaxC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MinMaxHeapMaxC() expects 1 arg\n");
      return -1;
    }
    AKSValue HV = POP();
    AKSStruct *ms = ValToObj(HV);
    int mmh_n = (int)ValToInt(ms->fieldvals[0]);
    AKSArray *da = ValToArr(ms->fieldvals[1]);
    AKSValue mmh_max;
    if (mmh_n == 0)
      mmh_max = ValInt(0);
    else if (mmh_n == 1)
      mmh_max = ValCopy(da->items[0]);
    else if (mmh_n == 2)
      mmh_max = ValCopy(da->items[1]);
    else
      mmh_max = ValCopy(ValToInt(da->items[1]) >= ValToInt(da->items[2])
                            ? da->items[1]
                            : da->items[2]);
    ValFree(&HV);
    PUSH(mmh_max);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MinMaxHeapSizeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MinMaxHeapSizeC() expects 1 arg\n");
      return -1;
    }
    AKSValue HV = POP();
    AKSStruct *ms = ValToObj(HV);
    int sz = (int)ValToInt(ms->fieldvals[0]);
    ValFree(&HV);
    PUSH(ValInt(sz));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JSONParseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: JSONParseC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    AksJP jp;
    jp.s = ValToStrPtr(SV)->data;
    jp.pos = 0;
    AKSValue result = AksJPParseValue(&jp);
    ValFree(&SV);
    PUSH(result);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JSONStringifyC") == 0) {
    if (argc < 1 || argc > 2) {
      fprintf(stderr, "Runtime error: JSONStringifyC() expects 1 or 2 args\n");
      return -1;
    }
    int pretty = 0;
    if (argc == 2) {
      AKSValue PV = POP();
      if (ValKind(PV) == VALBOOL)
        pretty = ValToBool(PV);
      else if (ValKind(PV) == VALINT)
        pretty = (ValToInt(PV) > 0);
      ValFree(&PV);
    }
    AKSValue SV = POP();
    AksJsonBuf jb;
    AksJsonBufInit(&jb);
    AksJsonStringifyValue(&jb, SV, pretty, 0);
    AKSValue res = ValStrLen(jb.buf, jb.len);
    free(jb.buf);
    ValFree(&SV);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CSVParseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CSVParseC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *csv_s = ValToStrPtr(SV)->data;
    int csv_len = (int)strlen(csv_s);
    char *cell = malloc(csv_len + 1);
    int clen = 0;
    int in_q = 0;
    AKSValue rows = ValArr(0);
    AKSArray *rows_a = ValToArr(rows);
    AKSValue cur = ValArr(0);
    AKSArray *cur_a = ValToArr(cur);
    for (int i = 0; i <= csv_len; i++) {
      char c = (i == csv_len) ? '\n' : csv_s[i];
      if (i == csv_len && clen == 0 && cur_a->count == 0)
        break;
      if (in_q) {
        if (c == '"') {
          if (i + 1 < csv_len && csv_s[i + 1] == '"') {
            cell[clen++] = '"';
            i++;
          } else
            in_q = 0;
        } else
          cell[clen++] = c;
      } else if (c == '"') {
        in_q = 1;
      } else if (c == ',') {
        cell[clen] = 0;
        AksArrPush(cur_a, ValStr(cell));
        clen = 0;
      } else if (c == '\n' || c == '\r') {
        if (c == '\r' && i + 1 < csv_len && csv_s[i + 1] == '\n')
          i++;
        cell[clen] = 0;
        AksArrPush(cur_a, ValStr(cell));
        clen = 0;
        AksArrPush(rows_a, cur);
        cur = ValArr(0);
        cur_a = ValToArr(cur);
      } else {
        cell[clen++] = c;
      }
    }
    free(cell);
    ValFree(&cur);
    ValFree(&SV);
    PUSH(rows);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TOMLParseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TOMLParseC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *toml_s = ValToStrPtr(SV)->data;
    AKSValue root = ValStruct("TOMLDoc");
    AKSStruct *rs = ValToObj(root);
    rs->fieldnames = NULL;
    rs->fieldvals = NULL;
    rs->fieldcount = 0;
    char tline[1024];
    int tli = 0;
    int cur_sec = -1;
    for (int ci = 0;; ci++) {
      char tc = toml_s[ci];
      if (tc == '\n' || tc == 0) {
        tline[tli] = 0;
        tli = 0;
        char *lp = tline;
        while (*lp == ' ' || *lp == '\t')
          lp++;
        if (!*lp || *lp == '#') {
          if (!tc)
            break;
          continue;
        }
        if (*lp == '[') {
          lp++;
          char sn[256];
          int sni = 0;
          while (*lp && *lp != ']')
            sn[sni++] = *lp++;
          sn[sni] = 0;
          int found = -1;
          for (int i = 0; i < rs->fieldcount; i++)
            if (!strcmp(rs->fieldnames[i], sn)) {
              found = i;
              break;
            }
          if (found < 0) {
            rs->fieldnames =
                realloc(rs->fieldnames, (rs->fieldcount + 1) * sizeof(char *));
            rs->fieldvals =
                realloc(rs->fieldvals, (rs->fieldcount + 1) * sizeof(AKSValue));
            rs->fieldnames[rs->fieldcount] = strdup(sn);
            rs->fieldvals[rs->fieldcount] = ValStruct(sn);
            AKSStruct *ns = ValToObj(rs->fieldvals[rs->fieldcount]);
            ns->fieldnames = NULL;
            ns->fieldvals = NULL;
            ns->fieldcount = 0;
            cur_sec = rs->fieldcount;
            rs->fieldcount++;
          } else
            cur_sec = found;
        } else {
          char *eq = strchr(lp, '=');
          if (eq) {
            char tkey[256];
            int tki = 0;
            for (char *kp = lp; kp < eq && *kp != ' ';)
              tkey[tki++] = *kp++;
            tkey[tki] = 0;
            char *vp = eq + 1;
            while (*vp == ' ')
              vp++;
            AKSValue tval;
            if (*vp == '"') {
              vp++;
              char vb[512];
              int vi = 0;
              while (*vp && *vp != '"' && vi < 511)
                vb[vi++] = *vp++;
              vb[vi] = 0;
              tval = ValStr(vb);
            } else if (!strncmp(vp, "true", 4))
              tval = ValBool(1);
            else if (!strncmp(vp, "false", 5))
              tval = ValBool(0);
            else if (strchr(vp, '.'))
              tval = ValFloat(atof(vp));
            else
              tval = ValInt((int64_t)strtoll(vp, NULL, 10));
            AKSStruct *target =
                (cur_sec >= 0) ? ValToObj(rs->fieldvals[cur_sec]) : rs;
            target->fieldnames = realloc(
                target->fieldnames, (target->fieldcount + 1) * sizeof(char *));
            target->fieldvals = realloc(
                target->fieldvals, (target->fieldcount + 1) * sizeof(AKSValue));
            target->fieldnames[target->fieldcount] = strdup(tkey);
            target->fieldvals[target->fieldcount] = tval;
            target->fieldcount++;
          }
        }
        if (!tc)
          break;
      } else {
        if (tli < 1023)
          tline[tli++] = tc;
      }
    }
    ValFree(&SV);
    PUSH(root);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexMatchC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: RegexMatchC() expects 2 args (pattern, text)\n");
      return -1;
    }
    AKSValue TV = POP(), PV = POP();
    if (ValKind(PV) != VALSTR || ValKind(TV) != VALSTR) {
      ValFree(&PV);
      ValFree(&TV);
      PUSH(ValBool(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *rx_pat = ValToStrPtr(PV)->data, *rx_txt = ValToStrPtr(TV)->data;
    int rx_found = 0;
    const char *t = rx_txt;
    if (rx_pat[0] == '^') {
      rx_found = AksRxMatchHere(rx_pat + 1, t);
    } else {
      do {
        if (AksRxMatchHere(rx_pat, t)) {
          rx_found = 1;
          break;
        }
      } while (*t++);
    }
    ValFree(&PV);
    ValFree(&TV);
    PUSH(ValBool(rx_found));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexReplaceC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RegexReplaceC() expects 3 args (pattern, "
                      "replacement, text)\n");
      return -1;
    }
    AKSValue TxV = POP(), RpV = POP(), PtV = POP();
    if (ValKind(PtV) != VALSTR || ValKind(RpV) != VALSTR ||
        ValKind(TxV) != VALSTR) {
      ValFree(&PtV);
      ValFree(&RpV);
      ValFree(&TxV);
      PUSH(ValBool(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *rxr_pat = ValToStrPtr(PtV)->data,
               *rxr_rep = ValToStrPtr(RpV)->data,
               *rxr_txt = ValToStrPtr(TxV)->data;
    size_t rxr_cap = 1024;
    size_t rxr_oi = 0;
    char *rxr_out = malloc(rxr_cap);
    int rxr_anchor = (rxr_pat[0] == '^');
    const char *pat2 = rxr_anchor ? rxr_pat + 1 : rxr_pat, *tp = rxr_txt;
    int rep_len = (int)strlen(rxr_rep);
    while (*tp) {
      int mlen = 0;
      if (AksRxMatchLen(pat2, tp, &mlen)) {
        if (rxr_oi + rep_len + 1 >= rxr_cap) {
          rxr_cap = (rxr_cap + rep_len + 1) * 2;
          rxr_out = realloc(rxr_out, rxr_cap);
        }
        memcpy(rxr_out + rxr_oi, rxr_rep, rep_len);
        rxr_oi += rep_len;
        tp += mlen > 0 ? mlen : 1;
        if (rxr_anchor)
          break;
      } else {
        if (rxr_oi + 2 >= rxr_cap) {
          rxr_cap = rxr_cap * 2 + 16;
          rxr_out = realloc(rxr_out, rxr_cap);
        }
        rxr_out[rxr_oi++] = *tp++;
      }
    }
    while (*tp) {
      if (rxr_oi + 2 >= rxr_cap) {
        rxr_cap = rxr_cap * 2 + 16;
        rxr_out = realloc(rxr_out, rxr_cap);
      }
      rxr_out[rxr_oi++] = *tp++;
    }
    rxr_out[rxr_oi] = 0;
    ValFree(&PtV);
    ValFree(&RpV);
    ValFree(&TxV);
    AKSValue res_str = ValStrLen(rxr_out, (int)rxr_oi);
    free(rxr_out);
    PUSH(res_str);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexSplitC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: RegexSplitC() expects 2 args (pattern, text)\n");
      return -1;
    }
    AKSValue TxV = POP(), PtV = POP();
    if (ValKind(PtV) != VALSTR || ValKind(TxV) != VALSTR) {
      ValFree(&PtV);
      ValFree(&TxV);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *rxs_pat = ValToStrPtr(PtV)->data,
               *rxs_txt = ValToStrPtr(TxV)->data;
    AKSValue parts = ValArr(4);
    AKSArray *pa = ValToArr(parts);
    const char *sp = rxs_txt, *seg = sp;
    while (*sp) {
      int mlen = 0;
      if (AksRxMatchLen(rxs_pat, sp, &mlen) && mlen > 0) {
        int slen = (int)(sp - seg);
        AksArrPush(pa, ValStrLen(seg, slen));
        sp += mlen;
        seg = sp;
      } else
        sp++;
    }
    int slen2 = (int)(sp - seg);
    AksArrPush(pa, ValStrLen(seg, slen2));
    ValFree(&PtV);
    ValFree(&TxV);
    PUSH(parts);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SkipListNewC") == 0) {
    AKSValue sl = ValStruct("SkipList");
    AKSStruct *ss = ValToObj(sl);
    ss->fieldnames = malloc(2 * sizeof(char *));
    ss->fieldvals = malloc(2 * sizeof(AKSValue));
    ss->fieldnames[0] = strdup("len");
    ss->fieldvals[0] = ValInt(0);
    ss->fieldnames[1] = strdup("nodes");
    ss->fieldvals[1] = ValArr(0);
    ss->fieldcount = 2;
    PUSH(sl);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SkipListInsertC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SkipListInsertC() expects 2 args\n");
      return -1;
    }
    AKSValue KV = POP(), SV = POP();
    AKSStruct *sl = ValToObj(SV);
    AKSArray *nodes = ValToArr(sl->fieldvals[1]);
    int64_t key = ValToInt(KV);
    int lo = 0, hi = nodes->count - 1, found = 0;
    while (lo <= hi) {
      int m = (lo + hi) / 2;
      AKSStruct *nd = ValToObj(nodes->items[m]);
      int64_t mk = ValToInt(nd->fieldvals[0]);
      if (mk == key) {
        found = 1;
        break;
      } else if (mk < key)
        lo = m + 1;
      else
        hi = m - 1;
    }
    if (!found) {
      AKSValue node = ValStruct("SLNode");
      AKSStruct *nd = ValToObj(node);
      nd->fieldnames = malloc(sizeof(char *));
      nd->fieldvals = malloc(sizeof(AKSValue));
      nd->fieldnames[0] = strdup("key");
      nd->fieldvals[0] = ValCopy(KV);
      nd->fieldcount = 1;
      AksArrPush(nodes, node);
      sl->fieldvals[0] = ValInt(ValToInt(sl->fieldvals[0]) + 1);
      for (int i = nodes->count - 1; i > 0; i--) {
        AKSStruct *a = ValToObj(nodes->items[i - 1]),
                  *b = ValToObj(nodes->items[i]);
        if (ValToInt(a->fieldvals[0]) > ValToInt(b->fieldvals[0])) {
          AKSValue t = nodes->items[i - 1];
          nodes->items[i - 1] = nodes->items[i];
          nodes->items[i] = t;
        } else
          break;
      }
    }
    ValFree(&KV);
    PUSH(SV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SkipListContainsC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SkipListContainsC() expects 2 args\n");
      return -1;
    }
    AKSValue KV = POP(), SV = POP();
    AKSStruct *sl = ValToObj(SV);
    AKSArray *nodes = ValToArr(sl->fieldvals[1]);
    int64_t key = ValToInt(KV);
    int lo = 0, hi = nodes->count - 1, found = 0;
    while (lo <= hi) {
      int m = (lo + hi) / 2;
      AKSStruct *nd = ValToObj(nodes->items[m]);
      int64_t mk = ValToInt(nd->fieldvals[0]);
      if (mk == key) {
        found = 1;
        break;
      } else if (mk < key)
        lo = m + 1;
      else
        hi = m - 1;
    }
    ValFree(&KV);
    ValFree(&SV);
    PUSH(ValBool(found));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SkipListLenC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SkipListLenC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    int len = (int)ValToInt(ValToObj(SV)->fieldvals[0]);
    ValFree(&SV);
    PUSH(ValInt(len));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TreapNewC") == 0) {
    AKSValue tv2 = ValStruct("Treap");
    AKSStruct *ts = ValToObj(tv2);
    ts->fieldnames = malloc(2 * sizeof(char *));
    ts->fieldvals = malloc(2 * sizeof(AKSValue));
    ts->fieldnames[0] = strdup("len");
    ts->fieldvals[0] = ValInt(0);
    ts->fieldnames[1] = strdup("keys");
    ts->fieldvals[1] = ValArr(0);
    ts->fieldcount = 2;
    PUSH(tv2);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TreapInsertC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: TreapInsertC() expects 2 args\n");
      return -1;
    }
    AKSValue KV = POP(), TV = POP();
    AKSStruct *ts = ValToObj(TV);
    AKSArray *ka = ValToArr(ts->fieldvals[1]);
    int64_t key = ValToInt(KV);
    int lo = 0, hi = ka->count - 1, found = 0;
    while (lo <= hi) {
      int m = (lo + hi) / 2;
      int64_t mk = ValToInt(ka->items[m]);
      if (mk == key) {
        found = 1;
        break;
      } else if (mk < key)
        lo = m + 1;
      else
        hi = m - 1;
    }
    if (!found) {
      AksArrPush(ka, ValCopy(KV));
      for (int i = ka->count - 1; i > 0; i--) {
        if (ValToInt(ka->items[i - 1]) > ValToInt(ka->items[i])) {
          AKSValue t2 = ka->items[i - 1];
          ka->items[i - 1] = ka->items[i];
          ka->items[i] = t2;
        } else
          break;
      }
      ts->fieldvals[0] = ValInt(ValToInt(ts->fieldvals[0]) + 1);
    }
    ValFree(&KV);
    PUSH(TV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TreapContainsC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: TreapContainsC() expects 2 args\n");
      return -1;
    }
    AKSValue KV = POP(), TV = POP();
    AKSStruct *ts = ValToObj(TV);
    AKSArray *ka = ValToArr(ts->fieldvals[1]);
    int64_t key = ValToInt(KV);
    int found = 0;
    int lo = 0, hi = ka->count - 1;
    while (lo <= hi) {
      int m = (lo + hi) / 2;
      int64_t mk = ValToInt(ka->items[m]);
      if (mk == key) {
        found = 1;
        break;
      } else if (mk < key)
        lo = m + 1;
      else
        hi = m - 1;
    }
    ValFree(&KV);
    ValFree(&TV);
    PUSH(ValBool(found));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TreapMinC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TreapMinC() expects 1 arg\n");
      return -1;
    }
    AKSValue TV = POP();
    AKSStruct *ts = ValToObj(TV);
    AKSArray *ka = ValToArr(ts->fieldvals[1]);
    AKSValue res = (ka->count > 0) ? ValCopy(ka->items[0]) : ValNull();
    ValFree(&TV);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TreapMaxC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TreapMaxC() expects 1 arg\n");
      return -1;
    }
    AKSValue TV = POP();
    AKSStruct *ts = ValToObj(TV);
    AKSArray *ka = ValToArr(ts->fieldvals[1]);
    AKSValue res =
        (ka->count > 0) ? ValCopy(ka->items[ka->count - 1]) : ValNull();
    ValFree(&TV);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IniParseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: IniParseC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *ini = ValToStrPtr(SV)->data;
    AKSValue root2 = ValStruct("IniDoc");
    AKSStruct *rs2 = ValToObj(root2);
    rs2->fieldnames = NULL;
    rs2->fieldvals = NULL;
    rs2->fieldcount = 0;
    AKSValue glo = ValStruct("global");
    AKSStruct *gs2 = ValToObj(glo);
    gs2->fieldnames = NULL;
    gs2->fieldvals = NULL;
    gs2->fieldcount = 0;
    rs2->fieldnames = malloc(sizeof(char *));
    rs2->fieldvals = malloc(sizeof(AKSValue));
    rs2->fieldnames[0] = strdup("global");
    rs2->fieldvals[0] = glo;
    rs2->fieldcount = 1;
    int ini_cur = 0;
    char iline[1024];
    int ili = 0;
    for (int ci = 0;; ci++) {
      char ic = ini[ci];
      if (ic == '\n' || ic == 0) {
        iline[ili] = 0;
        ili = 0;
        char *lp = iline;
        while (*lp == ' ' || *lp == '\t')
          lp++;
        if (!*lp || *lp == ';' || *lp == '#') {
          if (!ic)
            break;
          continue;
        }
        if (*lp == '[') {
          lp++;
          char sn[256];
          int sni = 0;
          while (*lp && *lp != ']')
            sn[sni++] = *lp++;
          sn[sni] = 0;
          int found = -1;
          for (int i = 0; i < rs2->fieldcount; i++)
            if (!strcmp(rs2->fieldnames[i], sn)) {
              found = i;
              break;
            }
          if (found < 0) {
            AKSValue ns2 = ValStruct(sn);
            AKSStruct *nss = ValToObj(ns2);
            nss->fieldnames = NULL;
            nss->fieldvals = NULL;
            nss->fieldcount = 0;
            rs2->fieldnames = realloc(rs2->fieldnames,
                                      (rs2->fieldcount + 1) * sizeof(char *));
            rs2->fieldvals = realloc(rs2->fieldvals,
                                     (rs2->fieldcount + 1) * sizeof(AKSValue));
            rs2->fieldnames[rs2->fieldcount] = strdup(sn);
            rs2->fieldvals[rs2->fieldcount] = ns2;
            ini_cur = rs2->fieldcount;
            rs2->fieldcount++;
          } else
            ini_cur = found;
        } else {
          char *eq2 = strchr(lp, '=');
          if (!eq2)
            eq2 = strchr(lp, ':');
          if (eq2) {
            char ikey[256];
            int iki = 0;
            char *kp = lp;
            while (kp < eq2 && *kp != ' ')
              ikey[iki++] = *kp++;
            ikey[iki] = 0;
            char *vp = eq2 + 1;
            while (*vp == ' ')
              vp++;
            int vlen = (int)strlen(vp);
            while (vlen > 0 && (vp[vlen - 1] == '\r' || vp[vlen - 1] == ' '))
              vlen--;
            AKSStruct *tgt = ValToObj(rs2->fieldvals[ini_cur]);
            tgt->fieldnames = realloc(tgt->fieldnames,
                                      (tgt->fieldcount + 1) * sizeof(char *));
            tgt->fieldvals = realloc(tgt->fieldvals,
                                     (tgt->fieldcount + 1) * sizeof(AKSValue));
            tgt->fieldnames[tgt->fieldcount] = strdup(ikey);
            tgt->fieldvals[tgt->fieldcount] = ValStrLen(vp, vlen);
            tgt->fieldcount++;
          }
        }
        if (!ic)
          break;
      } else {
        if (ili < 1023)
          iline[ili++] = ic;
      }
    }
    ValFree(&SV);
    PUSH(root2);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MsgPackEncodeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MsgPackEncodeC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    unsigned char *mp_buf = malloc(65536);
    int mp_len = 0;
    typedef struct {
      AKSValue v;
      int done;
    } MpFrame;
    MpFrame mp_stk[512];
    int mp_sp = 0;
    mp_stk[mp_sp].v = AV;
    mp_stk[mp_sp].done = 0;
    mp_sp++;
    while (mp_sp > 0 && mp_len < 65000) {
      MpFrame *fr = &mp_stk[--mp_sp];
      AKSValue v = fr->v;
      ValType vk = ValKind(v);
      if (vk == VALINT) {
        int64_t iv = ValToInt(v);
        if (iv >= 0 && iv <= 127) {
          mp_buf[mp_len++] = (uint8_t)iv;
        } else if (iv < 0 && iv >= -32) {
          mp_buf[mp_len++] = (uint8_t)(0xe0 | (iv & 0x1f));
        } else if (iv >= 0 && iv <= 255) {
          mp_buf[mp_len++] = 0xcc;
          mp_buf[mp_len++] = (uint8_t)iv;
        } else if (iv >= 0 && iv <= 65535) {
          mp_buf[mp_len++] = 0xcd;
          mp_buf[mp_len++] = (uint8_t)(iv >> 8);
          mp_buf[mp_len++] = (uint8_t)iv;
        } else {
          mp_buf[mp_len++] = 0xd3;
          for (int j = 7; j >= 0; j--)
            mp_buf[mp_len++] = (uint8_t)((iv >> (j * 8)) & 0xff);
        }
      } else if (vk == VALFLOAT) {
        double fv = ValToFloat(v);
        union {
          double d;
          uint64_t u;
        } u2;
        u2.d = fv;
        mp_buf[mp_len++] = 0xcb;
        for (int j = 7; j >= 0; j--)
          mp_buf[mp_len++] = (uint8_t)((u2.u >> (j * 8)) & 0xff);
      } else if (vk == VALBOOL) {
        mp_buf[mp_len++] = ValToBool(v) ? 0xc3 : 0xc2;
      } else if (vk == VALNULL) {
        mp_buf[mp_len++] = 0xc0;
      } else if (vk == VALSTR) {
        AKSString *ss3 = ValToStrPtr(v);
        int sl3 = ss3->len;
        if (sl3 <= 31) {
          mp_buf[mp_len++] = (uint8_t)(0xa0 | sl3);
        } else {
          mp_buf[mp_len++] = 0xd9;
          mp_buf[mp_len++] = (uint8_t)sl3;
        }
        if (mp_len + sl3 < 65000) {
          memcpy(mp_buf + mp_len, ss3->data, sl3);
          mp_len += sl3;
        }
      } else if (vk == VALARR) {
        AKSArray *ar = ValToArr(v);
        int ac = ar->count;
        if (ac <= 15) {
          mp_buf[mp_len++] = (uint8_t)(0x90 | ac);
        } else {
          mp_buf[mp_len++] = 0xdc;
          mp_buf[mp_len++] = (uint8_t)(ac >> 8);
          mp_buf[mp_len++] = (uint8_t)ac;
        }
        for (int i = ac - 1; i >= 0 && mp_sp < 510; i--) {
          mp_stk[mp_sp].v = ar->items[i];
          mp_stk[mp_sp].done = 0;
          mp_sp++;
        }
      } else if (vk == VALSTRUCT) {
        AKSStruct *ss4 = ValToObj(v);
        int fc4 = ss4->fieldcount;
        if (fc4 <= 15) {
          mp_buf[mp_len++] = (uint8_t)(0x80 | fc4);
        } else {
          mp_buf[mp_len++] = 0xde;
          mp_buf[mp_len++] = (uint8_t)(fc4 >> 8);
          mp_buf[mp_len++] = (uint8_t)fc4;
        }
        for (int i = fc4 - 1; i >= 0 && mp_sp < 508; i--) {
          mp_stk[mp_sp].v = ss4->fieldvals[i];
          mp_stk[mp_sp].done = 0;
          mp_sp++;
          const char *kd = ss4->fieldnames[i];
          int kl2 = (int)strlen(kd);
          if (kl2 <= 31) {
            mp_buf[mp_len++] = (uint8_t)(0xa0 | kl2);
          } else {
            mp_buf[mp_len++] = 0xd9;
            mp_buf[mp_len++] = (uint8_t)kl2;
          }
          if (mp_len + kl2 < 65000) {
            memcpy(mp_buf + mp_len, kd, kl2);
            mp_len += kl2;
          }
        }
      }
    }
    ValFree(&AV);
    char *hex_out = malloc(mp_len * 2 + 1);
    for (int i = 0; i < mp_len; i++)
      sprintf(hex_out + i * 2, "%02x", (unsigned int)mp_buf[i]);
    hex_out[mp_len * 2] = 0;
    free(mp_buf);
    PUSH(ValStr(hex_out));
    free(hex_out);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LockFreeStackNewC") == 0) {
    AKSValue lfs = ValStruct("LFStack");
    AKSStruct *ss5 = ValToObj(lfs);
    ss5->fieldnames = malloc(2 * sizeof(char *));
    ss5->fieldvals = malloc(2 * sizeof(AKSValue));
    ss5->fieldnames[0] = strdup("top");
    ss5->fieldvals[0] = ValInt(0);
    ss5->fieldnames[1] = strdup("data");
    ss5->fieldvals[1] = ValArr(8);
    ss5->fieldcount = 2;
    PUSH(lfs);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LockFreeStackPushC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: LockFreeStackPushC() expects 2 args\n");
      return -1;
    }
    AKSValue vv = POP(), sv = POP();
    AKSStruct *ss5 = ValToObj(sv);
    AKSArray *da5 = ValToArr(ss5->fieldvals[1]);
    int top5 = (int)ValToInt(ss5->fieldvals[0]);
    if (top5 >= da5->count)
      AksArrPush(da5, ValCopy(vv));
    else
      da5->items[top5] = ValCopy(vv);
    ss5->fieldvals[0] = ValInt(top5 + 1);
    ValFree(&vv);
    PUSH(sv);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LockFreeStackPopC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: LockFreeStackPopC() expects 1 arg\n");
      return -1;
    }
    AKSValue sv = POP();
    AKSStruct *ss5 = ValToObj(sv);
    AKSArray *da5 = ValToArr(ss5->fieldvals[1]);
    int top5 = (int)ValToInt(ss5->fieldvals[0]);
    AKSValue popped = ValNull();
    if (top5 > 0) {
      top5--;
      popped = ValCopy(da5->items[top5]);
      ss5->fieldvals[0] = ValInt(top5);
    }
    ValFree(&sv);
    PUSH(popped);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MpscQueueNewC") == 0) {
    AKSValue mq = ValStruct("MpscQ");
    AKSStruct *mqs = ValToObj(mq);
    mqs->fieldnames = malloc(2 * sizeof(char *));
    mqs->fieldvals = malloc(2 * sizeof(AKSValue));
    mqs->fieldnames[0] = strdup("head");
    mqs->fieldvals[0] = ValInt(0);
    mqs->fieldnames[1] = strdup("data");
    mqs->fieldvals[1] = ValArr(8);
    mqs->fieldcount = 2;
    PUSH(mq);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MpscQueuePushC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MpscQueuePushC() expects 2 args\n");
      return -1;
    }
    AKSValue vv = POP(), qv = POP();
    AKSStruct *mqs = ValToObj(qv);
    AKSArray *da = ValToArr(mqs->fieldvals[1]);
    AksArrPush(da, ValCopy(vv));
    ValFree(&vv);
    PUSH(qv);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MpscQueuePopC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MpscQueuePopC() expects 1 arg\n");
      return -1;
    }
    AKSValue qv = POP();
    AKSStruct *mqs = ValToObj(qv);
    int head = (int)ValToInt(mqs->fieldvals[0]);
    AKSArray *da = ValToArr(mqs->fieldvals[1]);
    AKSValue popped = ValNull();
    if (head < da->count) {
      popped = ValCopy(da->items[head]);
      mqs->fieldvals[0] = ValInt(head + 1);
    }
    ValFree(&qv);
    PUSH(popped);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CuckooFilterNewC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CuckooFilterNewC() expects 1 arg (capacity)\n");
      return -1;
    }
    AKSValue CV = POP();
    int cf_cap = (int)ValToInt(CV);
    ValFree(&CV);
    if (cf_cap <= 0)
      cf_cap = 256;
    AKSValue cf_bkts = ValArr(cf_cap);
    AKSArray *cf_ba = ValToArr(cf_bkts);
    for (int i = 0; i < cf_cap; i++)
      AksArrPush(cf_ba, ValInt(0));
    AKSValue cf = ValStruct("CuckooFilter");
    AKSStruct *cfs = ValToObj(cf);
    cfs->fieldnames = malloc(3 * sizeof(char *));
    cfs->fieldvals = malloc(3 * sizeof(AKSValue));
    cfs->fieldnames[0] = strdup("cap");
    cfs->fieldvals[0] = ValInt(cf_cap);
    cfs->fieldnames[1] = strdup("count");
    cfs->fieldvals[1] = ValInt(0);
    cfs->fieldnames[2] = strdup("buckets");
    cfs->fieldvals[2] = cf_bkts;
    cfs->fieldcount = 3;
    PUSH(cf);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CuckooFilterInsertC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CuckooFilterInsertC() expects 2 args\n");
      return -1;
    }
    AKSValue KV = POP(), CF = POP();
    AKSStruct *cfs = ValToObj(CF);
    int cf_cap2 = (int)ValToInt(cfs->fieldvals[0]);
    AKSArray *cf_bkts2 = ValToArr(cfs->fieldvals[2]);
    uint64_t cf_h1 = 14695981039346656037ULL;
    if (ValKind(KV) == VALSTR) {
      const char *s = ValToStrPtr(KV)->data;
      while (*s) {
        cf_h1 ^= (unsigned char)*s++;
        cf_h1 *= 1099511628211ULL;
      }
    } else {
      cf_h1 = (uint64_t)ValToInt(KV);
      cf_h1 ^= cf_h1 >> 33;
      cf_h1 *= 0xff51afd7ed558ccdULL;
      cf_h1 ^= cf_h1 >> 33;
    }
    uint32_t cf_fp = (uint32_t)(cf_h1 & 0xFF) | 1;
    int cf_i1 = (int)(cf_h1 % (uint64_t)cf_cap2);
    int cf_i2 =
        (int)((cf_h1 ^ (uint64_t)(cf_fp * 0x5bd1e995)) % (uint64_t)cf_cap2);
    int cf_ins = 0;
    if (ValToInt(cf_bkts2->items[cf_i1]) == 0) {
      cf_bkts2->items[cf_i1] = ValInt(cf_fp);
      cf_ins = 1;
    } else if (ValToInt(cf_bkts2->items[cf_i2]) == 0) {
      cf_bkts2->items[cf_i2] = ValInt(cf_fp);
      cf_ins = 1;
    }
    if (cf_ins)
      cfs->fieldvals[1] = ValInt(ValToInt(cfs->fieldvals[1]) + 1);
    ValFree(&KV);
    PUSH(CF);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CuckooFilterContainsC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: CuckooFilterContainsC() expects 2 args\n");
      return -1;
    }
    AKSValue KV = POP(), CF = POP();
    AKSStruct *cfs = ValToObj(CF);
    int cf_cap2 = (int)ValToInt(cfs->fieldvals[0]);
    AKSArray *cf_bkts2 = ValToArr(cfs->fieldvals[2]);
    uint64_t cf_h1 = 14695981039346656037ULL;
    if (ValKind(KV) == VALSTR) {
      const char *s = ValToStrPtr(KV)->data;
      while (*s) {
        cf_h1 ^= (unsigned char)*s++;
        cf_h1 *= 1099511628211ULL;
      }
    } else {
      cf_h1 = (uint64_t)ValToInt(KV);
      cf_h1 ^= cf_h1 >> 33;
      cf_h1 *= 0xff51afd7ed558ccdULL;
      cf_h1 ^= cf_h1 >> 33;
    }
    uint32_t cf_fp = (uint32_t)(cf_h1 & 0xFF) | 1;
    int cf_i1 = (int)(cf_h1 % (uint64_t)cf_cap2);
    int cf_i2 =
        (int)((cf_h1 ^ (uint64_t)(cf_fp * 0x5bd1e995)) % (uint64_t)cf_cap2);
    int cf_has = (ValToInt(cf_bkts2->items[cf_i1]) == (int64_t)cf_fp) ||
                 (ValToInt(cf_bkts2->items[cf_i2]) == (int64_t)cf_fp);
    ValFree(&KV);
    ValFree(&CF);
    PUSH(ValBool(cf_has));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SuffixArrayBuildC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SuffixArrayBuildC() expects 1 arg (str)\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *sa_s = ValToStrPtr(SV)->data;
    int sa_n = ValToStrPtr(SV)->len;
    int *sa_arr = malloc((sa_n + 1) * sizeof(int));
    for (int i = 0; i <= sa_n; i++)
      sa_arr[i] = i;
    for (int i = 1; i < sa_n; i++) {
      int key = sa_arr[i], j = i - 1;
      while (j >= 0 && strcmp(sa_s + sa_arr[j], sa_s + key) > 0) {
        sa_arr[j + 1] = sa_arr[j];
        j--;
      }
      sa_arr[j + 1] = key;
    }
    AKSValue sa_res = ValArr(sa_n);
    AKSArray *sa_ra = ValToArr(sa_res);
    for (int i = 0; i < sa_n; i++)
      AksArrPush(sa_ra, ValInt(sa_arr[i]));
    free(sa_arr);
    ValFree(&SV);
    PUSH(sa_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SuffixArraySearchC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SuffixArraySearchC() expects 3 args "
                      "(str, sa, pattern)\n");
      return -1;
    }
    AKSValue PaV = POP(), SaV = POP(), StrV = POP();
    if (ValKind(StrV) != VALSTR || ValKind(PaV) != VALSTR) {
      ValFree(&PaV);
      ValFree(&SaV);
      ValFree(&StrV);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *sa_str = ValToStrPtr(StrV)->data;
    const char *sa_pat = ValToStrPtr(PaV)->data;
    int sa_plen = (int)strlen(sa_pat);
    AKSArray *sa_a = ValToArr(SaV);
    int sa_lo = 0, sa_hi = sa_a->count - 1, sa_found = -1;
    while (sa_lo <= sa_hi) {
      int sa_mid = (sa_lo + sa_hi) / 2;
      int sa_idx = (int)ValToInt(sa_a->items[sa_mid]);
      int sa_cmp = strncmp(sa_str + sa_idx, sa_pat, sa_plen);
      if (sa_cmp == 0) {
        sa_found = sa_idx;
        break;
      } else if (sa_cmp < 0)
        sa_lo = sa_mid + 1;
      else
        sa_hi = sa_mid - 1;
    }
    ValFree(&PaV);
    ValFree(&SaV);
    ValFree(&StrV);
    PUSH(ValInt(sa_found));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Length") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Length() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) == VALSTR) {
      PUSH(ValInt(ValToStrPtr(arg)->len));
    } else if (ValKind(arg) == VALARR || ValKind(arg) == VALTUPLE) {
      PUSH(ValInt(ValToArr(arg)->count));
    } else if (ValKind(arg) == VALSTRUCT &&
               strcmp(ValToObj(arg)->typename, "Range") == 0) {
      AKSStruct *s = ValToObj(arg);
      int64_t start = 0, end = 0;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "start") == 0)
          start = ValToInt(s->fieldvals[i]);
        else if (strcmp(s->fieldnames[i], "end") == 0)
          end = ValToInt(s->fieldvals[i]);
      }
      PUSH(ValInt(end - start));
    } else {
      PUSH(ValInt(0));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "slice") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: slice expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue endval = POP();
    AKSValue startval = POP();
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR) {
      fprintf(stderr,
              "Runtime error: slice expects first argument to be an array\n");
      ValFree(&arrval);
      ValFree(&startval);
      ValFree(&endval);
      return -1;
    }
    if (ValKind(startval) != VALINT || ValKind(endval) != VALINT) {
      fprintf(stderr, "Runtime error: slice expects start and end indices to "
                      "be integers\n");
      ValFree(&arrval);
      ValFree(&startval);
      ValFree(&endval);
      return -1;
    }
    AKSArray *parentarr = ValToArr(arrval);
    int64_t start = ValToInt(startval);
    int64_t end = ValToInt(endval);
    if (start < 0)
      start = 0;
    if (end < 0)
      end = 0;
    if (start > parentarr->count)
      start = parentarr->count;
    if (end > parentarr->count)
      end = parentarr->count;
    if (start > end)
      start = end;
    int length = (int)(end - start);
    AKSArray *slicearr = malloc(sizeof(AKSArray));
    slicearr->refcount = 1;
    slicearr->invalidated = 0;
    slicearr->isstack = 0;
    slicearr->isslice = 1;
    slicearr->has_slices = 0;
    slicearr->detached = NULL;
    slicearr->arena = NULL;
    slicearr->parent = ValCopy(arrval);
    slicearr->items = parentarr->items + start;
    slicearr->count = length;
    slicearr->capacity = length;
    parentarr->has_slices = 1;
    AtomicIncrement(&gactivearrs);
    AKSValue sliceval = ((uint64_t)0xFFF5 << 48) |
                        ((uintptr_t)slicearr & 0x0000FFFFFFFFFFFFULL);
    PUSH(sliceval);
    ValFree(&arrval);
    ValFree(&startval);
    ValFree(&endval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "append") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: append() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR) {
      fprintf(stderr,
              "Runtime error: append() first argument must be an array\n");
      ValFree(&val);
      ValFree(&arr);
      return -1;
    }
    AKSArray *a = ValToArr(arr);
    AksArrayEnsureCapacity(a, a->count + 1);
    a->items[a->count++] = ValCopy(val);
    PUSH(ValNull());
    ValFree(&val);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "pop") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: pop() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR) {
      fprintf(stderr, "Runtime error: pop() argument must be an array\n");
      ValFree(&arr);
      return -1;
    }
    AKSArray *a = ValToArr(arr);
    if (a->count == 0) {
      PUSH(ValNull());
    } else {
      a->count--;
      AKSValue popped = a->items[a->count];
      PUSH(popped);
    }
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "remove") == 0 || strcmp(name, "remove_at") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: remove() expects 2 arguments (array, index), got "
              "%d\n",
              argc);
      return -1;
    }
    AKSValue idxval = POP();
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR) {
      fprintf(stderr,
              "Runtime error: remove() first argument must be an array\n");
      ValFree(&idxval);
      ValFree(&arr);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arr);
    if (idx < 0 || idx >= a->count) {
      fprintf(stderr,
              "Runtime error: remove() index %lld out of bounds (size %d)\n",
              (long long)idx, a->count);
      ValFree(&idxval);
      ValFree(&arr);
      return -1;
    }
    AKSValue removed = a->items[idx];
    for (int i = (int)idx; i < a->count - 1; i++) {
      a->items[i] = a->items[i + 1];
    }
    a->count--;
    PUSH(removed);
    ValFree(&idxval);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "insert") == 0 || strcmp(name, "insert_at") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: %s() expects 3 arguments, got %d\n", name,
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue idxval = POP();
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR || ValKind(idxval) != VALINT) {
      fprintf(stderr, "Runtime error: %s() invalid argument types\n", name);
      ValFree(&val);
      ValFree(&idxval);
      ValFree(&arr);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arr);
    if (idx < 0 || idx > a->count) {
      fprintf(stderr, "Runtime error: %s() index out of bounds: %lld\n", name,
              (long long)idx);
      ValFree(&val);
      ValFree(&idxval);
      ValFree(&arr);
      return -1;
    }
    AksArrayEnsureCapacity(a, a->count + 1);
    for (int i = a->count; i > idx; i--) {
      a->items[i] = a->items[i - 1];
    }
    a->items[idx] = ValCopy(val);
    a->count++;
    PUSH(ValNull());
    ValFree(&val);
    ValFree(&idxval);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MakeArena") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: MakeArena() expects 0 arguments, got %d\n", argc);
      return -1;
    }
    AKSArena *arena = malloc(sizeof(AKSArena));
    arena->refcount = 1;
    arena->allocations = NULL;
    arena->alloc_types = NULL;
    arena->count = 0;
    arena->capacity = 0;
    arena->invalidated = 0;

    PUSH(ValArena(arena));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaAllocArray") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ArenaAllocArray() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue cap_val = POP();
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA || ValKind(cap_val) != VALINT) {
      fprintf(stderr,
              "Runtime error: ArenaAllocArray() invalid argument types\n");
      ValFree(&arena_val);
      ValFree(&cap_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    if (arena->invalidated) {
      fprintf(stderr,
              "Runtime error: ArenaAllocArray() called on freed arena\n");
      ValFree(&arena_val);
      ValFree(&cap_val);
      return -1;
    }
    int cap = (int)ValToInt(cap_val);
    AKSValue res = ValArrArena(cap, arena);
    AKSArray *arr = ValToArr(res);
    arr->count = cap;
    for (int i = 0; i < cap; i++) {
      arr->items[i] = ValNull();
    }
    AtomicIncrement(&arr->refcount);

    if (arena->count >= arena->capacity) {
      arena->capacity = arena->capacity == 0 ? 8 : arena->capacity * 2;
      arena->allocations =
          realloc(arena->allocations, arena->capacity * sizeof(void *));
      arena->alloc_types =
          realloc(arena->alloc_types, arena->capacity * sizeof(int));
    }
    arena->allocations[arena->count] = ValToArr(res);
    arena->alloc_types[arena->count] = 1;
    arena->count++;

    PUSH(res);
    ValFree(&arena_val);
    ValFree(&cap_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaAllocStruct") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ArenaAllocStruct() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue type_val = POP();
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA || ValKind(type_val) != VALSTR) {
      fprintf(stderr,
              "Runtime error: ArenaAllocStruct() invalid argument types\n");
      ValFree(&arena_val);
      ValFree(&type_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    if (arena->invalidated) {
      fprintf(stderr,
              "Runtime error: ArenaAllocStruct() called on freed arena\n");
      ValFree(&arena_val);
      ValFree(&type_val);
      return -1;
    }
    const char *tname = ValToStrPtr(type_val)->data;
    AKSValue res = ValStructArena(tname, arena);
    AtomicIncrement(&ValToObj(res)->refcount);

    if (arena->count >= arena->capacity) {
      arena->capacity = arena->capacity == 0 ? 8 : arena->capacity * 2;
      arena->allocations =
          realloc(arena->allocations, arena->capacity * sizeof(void *));
      arena->alloc_types =
          realloc(arena->alloc_types, arena->capacity * sizeof(int));
    }
    arena->allocations[arena->count] = ValToObj(res);
    arena->alloc_types[arena->count] = 2;
    arena->count++;

    PUSH(res);
    ValFree(&arena_val);
    ValFree(&type_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ArenaFree() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA) {
      fprintf(stderr, "Runtime error: ArenaFree() argument must be an arena\n");
      ValFree(&arena_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    if (arena && !arena->invalidated) {
      for (int i = 0; i < arena->count; i++) {
        void *ptr = arena->allocations[i];
        int type = arena->alloc_types[i];
        if (type == 1) {
          AKSArray *arr = (AKSArray *)ptr;
          if (!arr->invalidated) {
            for (int j = 0; j < arr->count; j++)
              ValFree(&arr->items[j]);
            free(arr->items);
            arr->items = NULL;
            arr->count = 0;
            arr->capacity = 0;
            arr->invalidated = 1;
          }
          if (AtomicDecrement(&arr->refcount) <= 0) {
            if (!arr->isstack)
              free(arr);
            AtomicDecrement(&gactivearrs);
          }
        } else if (type == 2) {
          AKSStruct *obj = (AKSStruct *)ptr;
          if (!obj->invalidated) {
            for (int j = 0; j < obj->fieldcount; j++) {
              free(obj->fieldnames[j]);
              ValFree(&obj->fieldvals[j]);
            }
            free(obj->fieldnames);
            free(obj->fieldvals);
            free(obj->typename);
            obj->fieldnames = NULL;
            obj->fieldvals = NULL;
            obj->typename = NULL;
            obj->fieldcount = 0;
            obj->invalidated = 1;
          }
          if (AtomicDecrement(&obj->refcount) <= 0) {
            if (!obj->isstack)
              free(obj);
            AtomicDecrement(&gactivestructs);
          }
        }
      }
      if (arena->allocations) {
        free(arena->allocations);
        arena->allocations = NULL;
      }
      if (arena->alloc_types) {
        free(arena->alloc_types);
        arena->alloc_types = NULL;
      }
      arena->count = 0;
      arena->capacity = 0;
      arena->invalidated = 1;
    }
    PUSH(ValNull());
    ValFree(&arena_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaReset") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ArenaReset() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA) {
      fprintf(stderr,
              "Runtime error: ArenaReset() argument must be an arena\n");
      ValFree(&arena_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    if (arena && !arena->invalidated) {
      for (int i = 0; i < arena->count; i++) {
        void *ptr = arena->allocations[i];
        int type = arena->alloc_types[i];
        if (type == 1) {
          AKSArray *arr = (AKSArray *)ptr;
          if (!arr->invalidated) {
            for (int j = 0; j < arr->count; j++)
              ValFree(&arr->items[j]);
            free(arr->items);
            arr->items = NULL;
            arr->count = 0;
            arr->capacity = 0;
            arr->invalidated = 1;
          }
          if (AtomicDecrement(&arr->refcount) <= 0) {
            if (!arr->isstack)
              free(arr);
            AtomicDecrement(&gactivearrs);
          }
        } else if (type == 2) {
          AKSStruct *obj = (AKSStruct *)ptr;
          if (!obj->invalidated) {
            for (int j = 0; j < obj->fieldcount; j++) {
              free(obj->fieldnames[j]);
              ValFree(&obj->fieldvals[j]);
            }
            free(obj->fieldnames);
            free(obj->fieldvals);
            free(obj->typename);
            obj->fieldnames = NULL;
            obj->fieldvals = NULL;
            obj->typename = NULL;
            obj->fieldcount = 0;
            obj->invalidated = 1;
          }
          if (AtomicDecrement(&obj->refcount) <= 0) {
            if (!obj->isstack)
              free(obj);
            AtomicDecrement(&gactivestructs);
          }
        }
      }
      arena->count = 0;
    }
    PUSH(ValBool(1));
    ValFree(&arena_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaAllocCount") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ArenaAllocCount() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA) {
      fprintf(stderr,
              "Runtime error: ArenaAllocCount() argument must be an arena\n");
      ValFree(&arena_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    int64_t count = (arena && !arena->invalidated) ? (int64_t)arena->count : 0;
    PUSH(ValInt(count));
    ValFree(&arena_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaAllocatedBytes") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ArenaAllocatedBytes() expects 1 argument, got %d\n",
          argc);
      return -1;
    }
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA) {
      fprintf(
          stderr,
          "Runtime error: ArenaAllocatedBytes() argument must be an arena\n");
      ValFree(&arena_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    int64_t bytes = 0;
    if (arena && !arena->invalidated) {
      bytes += (int64_t)sizeof(AKSArena);
      bytes += (int64_t)(arena->capacity * sizeof(void *));
      bytes += (int64_t)(arena->capacity * sizeof(int));
      for (int i = 0; i < arena->count; i++) {
        int type = arena->alloc_types[i];
        if (type == 1) {
          bytes += (int64_t)sizeof(AKSArray);
          AKSArray *arr = (AKSArray *)arena->allocations[i];
          if (arr && !arr->invalidated) {
            bytes += (int64_t)(arr->capacity * sizeof(AKSValue));
          }
        } else if (type == 2) {
          bytes += (int64_t)sizeof(AKSStruct);
          AKSStruct *obj = (AKSStruct *)arena->allocations[i];
          if (obj && !obj->invalidated) {
            bytes += (int64_t)(obj->fieldcount *
                               (sizeof(char *) + sizeof(AKSValue)));
          }
        }
      }
    }
    PUSH(ValInt(bytes));
    ValFree(&arena_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ordinal") == 0 || strcmp(name, "ByteAt") == 0) {
    if (argc < 1 || argc > 2) {
      fprintf(stderr, "Runtime error: %s() expects 1 or 2 arguments, got %d\n",
              name, argc);
      return -1;
    }
    int64_t idx = 0;
    if (argc == 2) {
      AKSValue idx_val = POP();
      if (!ValIsInt(idx_val)) {
        fprintf(stderr, "Runtime error: %s() index must be an integer\n", name);
        ValFree(&idx_val);
        return -1;
      }
      idx = ValToInt(idx_val);
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: %s() argument must be a string\n", name);
      ValFree(&arg);
      return -1;
    }
    AKSString *s = ValToStrPtr(arg);
    int code = -1;
    if (s && s->data && idx >= 0 && idx < s->len) {
      code = (unsigned char)s->data[idx];
    }
    PUSH(ValInt(code));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HashCode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: HashCode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    uint64_t h = 0;
    ValType t = ValKind(arg);
    switch (t) {
    case VALINT: {
      uint64_t x = (uint64_t)ValToInt(arg);
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
      x ^= (x >> 31);
      h = x & 0x7FFFFFFFFFFFFFFFULL;
      break;
    }
    case VALFLOAT: {
      union {
        double d;
        uint64_t u;
      } u;
      u.d = ValToFloat(arg);
      if (u.d == 0.0)
        u.u = 0;
      uint64_t x = u.u;
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
      x ^= (x >> 31);
      h = x & 0x7FFFFFFFFFFFFFFFULL;
      break;
    }
    case VALBOOL:
      h = ValToBool(arg) ? 1231 : 1237;
      break;
    case VALNULL:
      h = 0;
      break;
    case VALSTR: {
      AKSString *s = ValToStrPtr(arg);
      if (s && s->data) {
        uint64_t x = 14695981039346656037ULL;
        for (int i = 0; i < s->len; i++) {
          x ^= (uint8_t)s->data[i];
          x *= 1099511628211ULL;
        }
        h = x & 0x7FFFFFFFFFFFFFFFULL;
      }
      break;
    }
    default: {
      uintptr_t ptr = (uintptr_t)(arg & 0x0000FFFFFFFFFFFFULL);
      uint64_t x = (uint64_t)ptr;
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
      x ^= (x >> 31);
      h = x & 0x7FFFFFFFFFFFFFFFULL;
      break;
    }
    }
    PUSH(ValInt((int64_t)h));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Character") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Character() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALINT) {
      fprintf(stderr,
              "Runtime error: Character() argument must be an integer\n");
      ValFree(&arg);
      return -1;
    }
    char buf[1] = {(char)ValToInt(arg)};
    PUSH(ValStrLen(buf, 1));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Substring") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: Substring() expects 3 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue lenval = POP();
    AKSValue startval = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(startval) != VALINT ||
        ValKind(lenval) != VALINT) {
      fprintf(stderr, "Runtime error: Substring() invalid argument types\n");
      ValFree(&lenval);
      ValFree(&startval);
      ValFree(&strval);
      return -1;
    }
    int64_t start = ValToInt(startval);
    int64_t length = ValToInt(lenval);
    int64_t count = ValToStrPtr(strval)->len;
    if (start < 0)
      start += count;
    if (start < 0)
      start = 0;
    if (start > count)
      start = count;
    if (length < 0)
      length = 0;
    if (start + length > count)
      length = count - start;

    char *sub = malloc(length + 1);
    memcpy(sub, ValToStrPtr(strval)->data + start, length);
    sub[length] = '\0';
    PUSH(ValStrLen(sub, (int)length));
    free(sub);
    ValFree(&lenval);
    ValFree(&startval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "clone") == 0 || strcmp(name, "deep_clone") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n", name,
              argc);
      return -1;
    }
    AKSValue arg = POP();
    AKSValue cloned = ValDeepClone(arg);
    ValFree(&arg);
    PUSH(cloned);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "split") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: split() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sep = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(sep) != VALSTR) {
      fprintf(stderr, "Runtime error: split() arguments must be strings\n");
      ValFree(&sep);
      ValFree(&strval);
      return -1;
    }
    AKSValue arrval = ValArr(4);
    const char *s = ValToStrPtr(strval)->data;
    const char *d = ValToStrPtr(sep)->data;
    int dlen = (int)strlen(d);
    if (dlen == 0) {
      for (int i = 0; i < ValToStrPtr(strval)->len; i++) {
        char tmp[2] = {s[i], '\0'};
        AKSValue item = ValStr(tmp);
        AKSArray *a = ValToArr(arrval);
        if (a->count >= a->capacity) {
          a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
          a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
        }
        a->items[a->count++] = item;
      }
    } else {
      const char *current = s;
      const char *next;
      while ((next = strstr(current, d)) != NULL) {
        int partlen = (int)(next - current);
        char *part = malloc(partlen + 1);
        memcpy(part, current, partlen);
        part[partlen] = '\0';
        AKSValue item = ValStr(part);
        free(part);
        AKSArray *a = ValToArr(arrval);
        if (a->count >= a->capacity) {
          a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
          a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
        }
        a->items[a->count++] = item;
        current = next + dlen;
      }
      AKSValue item = ValStr(current);
      AKSArray *a = ValToArr(arrval);
      if (a->count >= a->capacity) {
        a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
        a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
      }
      a->items[a->count++] = item;
    }
    PUSH(arrval);
    ValFree(&sep);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "replace") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: replace() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue newval = POP();
    AKSValue oldval = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(oldval) != VALSTR ||
        ValKind(newval) != VALSTR) {
      fprintf(stderr, "Runtime error: replace() arguments must be strings\n");
      ValFree(&newval);
      ValFree(&oldval);
      ValFree(&strval);
      return -1;
    }
    const char *s = ValToStrPtr(strval)->data;
    const char *o = ValToStrPtr(oldval)->data;
    const char *n = ValToStrPtr(newval)->data;
    int olen = (int)strlen(o);
    int nlen = (int)strlen(n);
    if (olen == 0) {
      PUSH(ValCopy(strval));
    } else {
      int occurrences = 0;
      const char *temp = s;
      while ((temp = strstr(temp, o)) != NULL) {
        occurrences++;
        temp += olen;
      }
      size_t outlen = strlen(s) + occurrences * (nlen - olen);
      char *out = malloc(outlen + 1);
      char *dest = out;
      const char *current = s;
      const char *next;
      while ((next = strstr(current, o)) != NULL) {
        int partlen = (int)(next - current);
        memcpy(dest, current, partlen);
        dest += partlen;
        memcpy(dest, n, nlen);
        dest += nlen;
        current = next + olen;
      }
      strcpy(dest, current);
      PUSH(ValStr(out));
      free(out);
    }
    ValFree(&newval);
    ValFree(&oldval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "trim") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: trim() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: trim() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    const char *s = ValToStrPtr(strval)->data;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
      s++;
    }
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
      len--;
    }
    char *trimmed = malloc(len + 1);
    memcpy(trimmed, s, len);
    trimmed[len] = '\0';
    PUSH(ValStr(trimmed));
    free(trimmed);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "trim_start") == 0 || strcmp(name, "trim_left") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: trim_start() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: trim_start() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    const char *s = ValToStrPtr(strval)->data;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
      s++;
    }
    PUSH(ValStr(s));
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "trim_end") == 0 || strcmp(name, "trim_right") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: trim_end() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: trim_end() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    const char *s = ValToStrPtr(strval)->data;
    int len = ValToStrPtr(strval)->len;
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
      len--;
    }
    PUSH(ValStrLen(s, len));
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "lines") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: lines() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: lines() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    AKSValue arrval = ValArr(4);
    const char *s = ValToStrPtr(strval)->data;
    const char *cur = s;
    while (*cur) {
      const char *next = strchr(cur, '\n');
      if (next) {
        int linelen = (int)(next - cur);
        if (linelen > 0 && cur[linelen - 1] == '\r') {
          linelen--;
        }
        AKSValue item = ValStrLen(cur, linelen);
        AKSArray *a = ValToArr(arrval);
        if (a->count >= a->capacity) {
          a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
          a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
        }
        a->items[a->count++] = item;
        cur = next + 1;
      } else {
        int linelen = (int)strlen(cur);
        if (linelen > 0 && cur[linelen - 1] == '\r') {
          linelen--;
        }
        AKSValue item = ValStrLen(cur, linelen);
        AKSArray *a = ValToArr(arrval);
        if (a->count >= a->capacity) {
          a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
          a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
        }
        a->items[a->count++] = item;
        break;
      }
    }
    PUSH(arrval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "chars") == 0 || strcmp(name, "Utf8ToChars") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: chars() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: chars() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    AKSValue arrval = ValArr(4);
    const unsigned char *s = (const unsigned char *)ValToStrPtr(strval)->data;
    int blen = ValToStrPtr(strval)->len;
    int i = 0;
    while (i < blen) {
      int step = 1;
      unsigned char c = s[i];
      if (c < 0x80)
        step = 1;
      else if ((c & 0xE0) == 0xC0)
        step = 2;
      else if ((c & 0xF0) == 0xE0)
        step = 3;
      else if ((c & 0xF8) == 0xF0)
        step = 4;
      if (i + step > blen)
        step = blen - i;
      AKSValue item = ValStrLen((const char *)(s + i), step);
      AKSArray *a = ValToArr(arrval);
      if (a->count >= a->capacity) {
        a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
        a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
      }
      a->items[a->count++] = item;
      i += step;
    }
    PUSH(arrval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "repeat_str") == 0 || strcmp(name, "repeat") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s() expects 2 arguments, got %d\n", name,
              argc);
      return -1;
    }
    AKSValue countval = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(countval) != VALINT) {
      fprintf(stderr, "Runtime error: repeat_str() expects (string, int)\n");
      ValFree(&countval);
      ValFree(&strval);
      return -1;
    }
    int64_t count = ValToInt(countval);
    if (count <= 0) {
      PUSH(ValStr(""));
    } else {
      AKSString *src = ValToStrPtr(strval);
      size_t total_len = (size_t)src->len * (size_t)count;
      char *buf = (char *)malloc(total_len + 1);
      if (buf) {
        for (int64_t k = 0; k < count; k++) {
          memcpy(buf + (size_t)k * src->len, src->data, src->len);
        }
        buf[total_len] = '\0';
        PUSH(ValStrLen(buf, (int)total_len));
        free(buf);
      } else {
        PUSH(ValStr(""));
      }
    }
    ValFree(&countval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8Len") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Utf8Len() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: Utf8Len() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    const unsigned char *s = (const unsigned char *)ValToStrPtr(strval)->data;
    int blen = ValToStrPtr(strval)->len;
    int count = 0;
    int i = 0;
    while (i < blen) {
      int step = 1;
      unsigned char c = s[i];
      if (c < 0x80)
        step = 1;
      else if ((c & 0xE0) == 0xC0)
        step = 2;
      else if ((c & 0xF0) == 0xE0)
        step = 3;
      else if ((c & 0xF8) == 0xF0)
        step = 4;
      if (i + step > blen)
        step = blen - i;
      i += step;
      count++;
    }
    PUSH(ValInt(count));
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8CharAt") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Utf8CharAt() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue idxval = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(idxval) != VALINT) {
      fprintf(stderr, "Runtime error: Utf8CharAt() expects (string, int)\n");
      ValFree(&idxval);
      ValFree(&strval);
      return -1;
    }
    int64_t target_idx = ValToInt(idxval);
    const unsigned char *s = (const unsigned char *)ValToStrPtr(strval)->data;
    int blen = ValToStrPtr(strval)->len;
    int cur_idx = 0;
    int i = 0;
    AKSValue res = ValNull();
    while (i < blen) {
      int step = 1;
      unsigned char c = s[i];
      if (c < 0x80)
        step = 1;
      else if ((c & 0xE0) == 0xC0)
        step = 2;
      else if ((c & 0xF0) == 0xE0)
        step = 3;
      else if ((c & 0xF8) == 0xF0)
        step = 4;
      if (i + step > blen)
        step = blen - i;
      if (cur_idx == target_idx) {
        res = ValStrLen((const char *)(s + i), step);
        break;
      }
      i += step;
      cur_idx++;
    }
    if (ValKind(res) == VALNULL) {
      res = ValStr("");
    }
    PUSH(res);
    ValFree(&idxval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8CodepointAt") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Utf8CodepointAt() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue idxval = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(idxval) != VALINT) {
      fprintf(stderr,
              "Runtime error: Utf8CodepointAt() expects (string, int)\n");
      ValFree(&idxval);
      ValFree(&strval);
      return -1;
    }
    int64_t target_idx = ValToInt(idxval);
    const unsigned char *s = (const unsigned char *)ValToStrPtr(strval)->data;
    int blen = ValToStrPtr(strval)->len;
    int cur_idx = 0;
    int i = 0;
    int64_t codepoint = -1;
    while (i < blen) {
      int step = 1;
      unsigned char c = s[i];
      uint32_t cp = 0;
      if (c < 0x80) {
        step = 1;
        cp = c;
      } else if ((c & 0xE0) == 0xC0) {
        step = 2;
        if (i + 1 < blen)
          cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
      } else if ((c & 0xF0) == 0xE0) {
        step = 3;
        if (i + 2 < blen)
          cp =
              ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
      } else if ((c & 0xF8) == 0xF0) {
        step = 4;
        if (i + 3 < blen)
          cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) |
               ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
      }
      if (i + step > blen)
        step = blen - i;
      if (cur_idx == target_idx) {
        codepoint = (int64_t)cp;
        break;
      }
      i += step;
      cur_idx++;
    }
    PUSH(ValInt(codepoint));
    ValFree(&idxval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8FromCodepoints") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: Utf8FromCodepoints() expects 1 argument, got %d\n",
          argc);
      return -1;
    }
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR) {
      fprintf(stderr, "Runtime error: Utf8FromCodepoints() argument must be an "
                      "array of ints\n");
      ValFree(&arrval);
      return -1;
    }
    AKSArray *a = ValToArr(arrval);
    size_t cap = a->count * 4 + 4;
    unsigned char *buf = (unsigned char *)malloc(cap);
    size_t len = 0;
    if (buf) {
      for (int k = 0; k < a->count; k++) {
        AKSValue el = a->items[k];
        if (ValKind(el) == VALINT) {
          uint32_t cp = (uint32_t)ValToInt(el);
          if (len + 4 >= cap) {
            cap = cap * 2 + 16;
            buf = (unsigned char *)realloc(buf, cap);
          }
          if (cp < 0x80) {
            buf[len++] = (unsigned char)cp;
          } else if (cp < 0x800) {
            buf[len++] = (unsigned char)(0xC0 | (cp >> 6));
            buf[len++] = (unsigned char)(0x80 | (cp & 0x3F));
          } else if (cp < 0x10000) {
            buf[len++] = (unsigned char)(0xE0 | (cp >> 12));
            buf[len++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            buf[len++] = (unsigned char)(0x80 | (cp & 0x3F));
          } else if (cp < 0x110000) {
            buf[len++] = (unsigned char)(0xF0 | (cp >> 18));
            buf[len++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            buf[len++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            buf[len++] = (unsigned char)(0x80 | (cp & 0x3F));
          }
        }
      }
      buf[len] = '\0';
      PUSH(ValStrLen((char *)buf, (int)len));
      free(buf);
    } else {
      PUSH(ValStr(""));
    }
    ValFree(&arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "lower") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: lower() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: lower() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    char *buf = strdup(ValToStrPtr(strval)->data);
    for (int i = 0; buf[i]; i++) {
      if (buf[i] >= 'A' && buf[i] <= 'Z') {
        buf[i] = buf[i] + 32;
      }
    }
    PUSH(ValStr(buf));
    free(buf);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "upper") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: upper() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: upper() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    char *buf = strdup(ValToStrPtr(strval)->data);
    for (int i = 0; buf[i]; i++) {
      if (buf[i] >= 'a' && buf[i] <= 'z') {
        buf[i] = buf[i] - 32;
      }
    }
    PUSH(ValStr(buf));
    free(buf);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "find") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: find() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sub = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(sub) != VALSTR) {
      fprintf(stderr, "Runtime error: find() arguments must be strings\n");
      ValFree(&sub);
      ValFree(&strval);
      return -1;
    }
    const char *found =
        strstr(ValToStrPtr(strval)->data, ValToStrPtr(sub)->data);
    if (found) {
      PUSH(ValInt((int64_t)(found - ValToStrPtr(strval)->data)));
    } else {
      PUSH(ValInt(-1));
    }
    ValFree(&sub);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "starts") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: starts() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue prefix = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(prefix) != VALSTR) {
      fprintf(stderr, "Runtime error: starts() arguments must be strings\n");
      ValFree(&prefix);
      ValFree(&strval);
      return -1;
    }
    int matches = (strncmp(ValToStrPtr(strval)->data, ValToStrPtr(prefix)->data,
                           ValToStrPtr(prefix)->len) == 0);
    PUSH(ValBool(matches));
    ValFree(&prefix);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ends") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ends() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue suffix = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(suffix) != VALSTR) {
      fprintf(stderr, "Runtime error: ends() arguments must be strings\n");
      ValFree(&suffix);
      ValFree(&strval);
      return -1;
    }
    int matches = 0;
    if (ValToStrPtr(strval)->len >= ValToStrPtr(suffix)->len) {
      matches = (strcmp(ValToStrPtr(strval)->data + ValToStrPtr(strval)->len -
                            ValToStrPtr(suffix)->len,
                        ValToStrPtr(suffix)->data) == 0);
    }
    PUSH(ValBool(matches));
    ValFree(&suffix);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "sort") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: sort() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR) {
      fprintf(stderr, "Runtime error: sort() argument must be an array\n");
      ValFree(&arrval);
      return -1;
    }
    AKSArray *arr = ValToArr(arrval);
    if (arr->isslice || arr->has_slices) {
      AksArrayEnsureCapacity(arr, arr->count);
    }
    qsort(arr->items, arr->count, sizeof(AKSValue), CompareAKSVals);
    PUSH(arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Concatenate") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Concatenate() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) != VALARR || ValKind(b) != VALARR) {
      fprintf(stderr,
              "Runtime error: Concatenate() arguments must be arrays\n");
      ValFree(&a);
      ValFree(&b);
      return -1;
    }
    int total = ValToArr(a)->count + ValToArr(b)->count;
    AKSValue res = ValArr(total);
    ValToArr(res)->count = total;
    for (int i = 0; i < ValToArr(a)->count; i++) {
      ValToArr(res)->items[i] = ValCopy(ValToArr(a)->items[i]);
    }
    for (int i = 0; i < ValToArr(b)->count; i++) {
      ValToArr(res)->items[ValToArr(a)->count + i] =
          ValCopy(ValToArr(b)->items[i]);
    }
    PUSH(res);
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "contains") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: contains() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR) {
      fprintf(stderr,
              "Runtime error: contains() first argument must be an array\n");
      ValFree(&val);
      ValFree(&arr);
      return -1;
    }
    int found = 0;
    for (int i = 0; i < ValToArr(arr)->count; i++) {
      AKSValue eq = ValEq(ValToArr(arr)->items[i], val);
      if (ValToBool(eq)) {
        found = 1;
        break;
      }
    }
    PUSH(ValBool(found));
    ValFree(&val);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "keys") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: keys() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue objval = POP();
    if (ValKind(objval) != VALSTRUCT) {
      fprintf(stderr, "Runtime error: keys() argument must be a struct\n");
      ValFree(&objval);
      return -1;
    }
    AKSValue arrval = ValArr(ValToObj(objval)->fieldcount);
    ValToArr(arrval)->count = ValToObj(objval)->fieldcount;
    for (int i = 0; i < ValToObj(objval)->fieldcount; i++) {
      ValToArr(arrval)->items[i] = ValStr(ValToObj(objval)->fieldnames[i]);
    }
    PUSH(arrval);
    ValFree(&objval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "values") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: values() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue objval = POP();
    if (ValKind(objval) != VALSTRUCT) {
      fprintf(stderr, "Runtime error: values() argument must be a struct\n");
      ValFree(&objval);
      return -1;
    }
    AKSValue arrval = ValArr(ValToObj(objval)->fieldcount);
    ValToArr(arrval)->count = ValToObj(objval)->fieldcount;
    for (int i = 0; i < ValToObj(objval)->fieldcount; i++) {
      ValToArr(arrval)->items[i] = ValCopy(ValToObj(objval)->fieldvals[i]);
    }
    PUSH(arrval);
    ValFree(&objval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JsonParse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: JsonParse() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: JsonParse() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *ptr = ValToStrPtr(arg)->data;
    int errorflag = 0;
    AKSValue res = ParseJSONVal(&ptr, &errorflag);
    if (errorflag) {
      ValFree(&res);
      PUSH(ValNull());
    } else {
      PUSH(res);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JsonStringify") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: JsonStringify() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    StrIfyVal(arg, &buf, &len, &cap);
    buf[len] = '\0';
    PUSH(ValStr(buf));
    free(buf);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JsonParseStream") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: JsonParseStream() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValArr(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *ptr = ValToStrPtr(arg)->data;
    AKSValue res_arr = ValArr(0);
    int errorflag = 0;
    while (*ptr) {
      while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')
        ptr++;
      if (*ptr == '\0')
        break;
      AKSValue parsed = ParseJSONVal(&ptr, &errorflag);
      if (errorflag) {
        ValFree(&parsed);
        break;
      }
      AKSArray *a = ValToArr(res_arr);
      if (a->count >= a->capacity) {
        a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
        a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
      }
      a->items[a->count++] = parsed;
    }
    PUSH(res_arr);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CborEncode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CborEncode() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue arg = POP();
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    CBOREncodeVal(arg, &buf, &len, &cap);
    PUSH(ValStrLen(buf, (int)len));
    free(buf);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CborDecode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CborDecode() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const uint8_t *ptr = (const uint8_t *)ValToStrPtr(arg)->data;
    int errorflag = 0;
    AKSValue res = CBORDecodeVal(&ptr, ptr + ValToStrPtr(arg)->len, &errorflag);
    if (errorflag) {
      ValFree(&res);
      PUSH(ValNull());
    } else {
      PUSH(res);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MessagePackEncode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: MessagePackEncode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    MsgpackEncodeVal(arg, &buf, &len, &cap);
    PUSH(ValStrLen(buf, (int)len));
    free(buf);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MessagePackDecode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: MessagePackDecode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const uint8_t *ptr = (const uint8_t *)ValToStrPtr(arg)->data;
    int errorflag = 0;
    AKSValue res =
        MsgpackDecodeVal(&ptr, ptr + ValToStrPtr(arg)->len, &errorflag);
    if (errorflag) {
      ValFree(&res);
      PUSH(ValNull());
    } else {
      PUSH(res);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CsvParse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CsvParse() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValArr(0));
      *stacktopptr = stacktop;
      return 1;
    }
    AKSValue res = ParseCSVStr(ValToStrPtr(arg)->data);
    PUSH(res);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CsvStringify") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CsvStringify() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    char *csv = StringifyCSV(arg);
    PUSH(ValStr(csv));
    free(csv);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TomlParse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TomlParse() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    AKSValue res = ParseTOMLStr(ValToStrPtr(arg)->data);
    PUSH(res);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TomlStringify") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: TomlStringify() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    char *toml = StringifyTOML(arg);
    PUSH(ValStr(toml));
    free(toml);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "XmlParse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: XmlParse() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    AKSValue res = ParseXMLStr(ValToStrPtr(arg)->data);
    PUSH(res);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "XmlStringify") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: XmlStringify() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    char *xml = StringifyXML(arg);
    PUSH(ValStr(xml));
    free(xml);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProtobufEncodeObj") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: ProtobufEncodeObj() expects 2 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue mapping = POP();
    AKSValue obj = POP();
    size_t b_len = 0;
    char *bytes = ProtobufEncodeObjFunc(obj, mapping, &b_len);
    PUSH(ValStrLen(bytes, (int)b_len));
    free(bytes);
    ValFree(&obj);
    ValFree(&mapping);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProtobufDecodeObj") == 0) {
    if (argc != 3) {
      fprintf(
          stderr,
          "Runtime error: ProtobufDecodeObj() expects 3 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue typename_val = POP();
    AKSValue mapping = POP();
    AKSValue bytes_val = POP();
    if (ValKind(bytes_val) != VALSTR || ValKind(typename_val) != VALSTR) {
      ValFree(&bytes_val);
      ValFree(&mapping);
      ValFree(&typename_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    AKSValue res = ProtobufDecodeObjFunc(ValToStrPtr(bytes_val)->data,
                                         ValToStrPtr(bytes_val)->len, mapping,
                                         ValToStrPtr(typename_val)->data);
    PUSH(res);
    ValFree(&bytes_val);
    ValFree(&mapping);
    ValFree(&typename_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8ToChars") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: Utf8ToChars() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValArr(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *str = ValToStrPtr(arg)->data;
    int len = (int)strlen(str);
    int pos = 0;
    AKSValue res_arr = ValArr(0);
    while (pos < len) {
      int consumed = 0;
      Utf8NextCodepoint((const uint8_t *)str + pos, len - pos, &consumed);
      if (consumed <= 0)
        break;
      char *ch = malloc(consumed + 1);
      memcpy(ch, str + pos, consumed);
      ch[consumed] = '\0';
      AKSArray *a = ValToArr(res_arr);
      if (a->count >= a->capacity) {
        a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
        a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
      }
      a->items[a->count++] = ValStr(ch);
      free(ch);
      pos += consumed;
    }
    PUSH(res_arr);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexMatch") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: RegexMatch() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue str_val = POP();
    AKSValue pat_val = POP();
    if (ValKind(pat_val) != VALSTR || ValKind(str_val) != VALSTR) {
      ValFree(&pat_val);
      ValFree(&str_val);
      PUSH(ValBool(0));
      *stacktopptr = stacktop;
      return 1;
    }
    ReProg prog = CompileRegex(ValToStrPtr(pat_val)->data);
    int input_len = 0;
    int *input = StrToCodepoints(ValToStrPtr(str_val)->data, &input_len);
    int saves[64];
    memset(saves, -1, sizeof(saves));
    int match = MatchNFA(prog.insts, 0, input, input_len, 0, saves,
                         2 * (prog.save_count + 1));
    int full_match = match && (saves[0] == 0 && saves[1] == input_len);
    PUSH(ValBool(full_match));
    free(input);
    FreeRegex(&prog);
    ValFree(&pat_val);
    ValFree(&str_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexSearch") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: RegexSearch() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue str_val = POP();
    AKSValue pat_val = POP();
    if (ValKind(pat_val) != VALSTR || ValKind(str_val) != VALSTR) {
      ValFree(&pat_val);
      ValFree(&str_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    ReProg prog = CompileRegex(ValToStrPtr(pat_val)->data);
    int input_len = 0;
    int *input = StrToCodepoints(ValToStrPtr(str_val)->data, &input_len);
    int saves[64];
    int match = 0;
    int match_start = -1;
    int match_end = -1;
    for (int i = 0; i <= input_len; i++) {
      memset(saves, -1, sizeof(saves));
      if (MatchNFA(prog.insts, 0, input, input_len, i, saves,
                   2 * (prog.save_count + 1))) {
        match = 1;
        match_start = saves[0];
        match_end = saves[1];
        break;
      }
    }
    if (match) {
      AKSValue res_arr = ValArr(0);
      AKSArray *a = ValToArr(res_arr);
      char *full_match_str =
          CodepointsToStr(input + match_start, match_end - match_start);
      a->items = realloc(a->items, (a->count + 1) * sizeof(AKSValue));
      a->items[a->count++] = ValStr(full_match_str);
      free(full_match_str);
      for (int g = 1; g <= prog.save_count; g++) {
        int start_idx = saves[2 * g];
        int end_idx = saves[2 * g + 1];
        if (start_idx >= 0 && end_idx >= start_idx) {
          char *sub = CodepointsToStr(input + start_idx, end_idx - start_idx);
          a->items = realloc(a->items, (a->count + 1) * sizeof(AKSValue));
          a->items[a->count++] = ValStr(sub);
          free(sub);
        } else {
          a->items = realloc(a->items, (a->count + 1) * sizeof(AKSValue));
          a->items[a->count++] = ValNull();
        }
      }
      PUSH(res_arr);
    } else {
      PUSH(ValNull());
    }
    free(input);
    FreeRegex(&prog);
    ValFree(&pat_val);
    ValFree(&str_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexReplace") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: RegexReplace() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue rep_val = POP();
    AKSValue str_val = POP();
    AKSValue pat_val = POP();
    if (ValKind(pat_val) != VALSTR || ValKind(str_val) != VALSTR ||
        ValKind(rep_val) != VALSTR) {
      ValFree(&pat_val);
      ValFree(&str_val);
      ValFree(&rep_val);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    ReProg prog = CompileRegex(ValToStrPtr(pat_val)->data);
    int input_len = 0;
    int *input = StrToCodepoints(ValToStrPtr(str_val)->data, &input_len);
    int saves[64];
    int out_cap = input_len + 16;
    int out_count = 0;
    int *out_cps = malloc(out_cap * sizeof(int));
    int idx = 0;
    while (idx <= input_len) {
      memset(saves, -1, sizeof(saves));
      if (idx < input_len && MatchNFA(prog.insts, 0, input, input_len, idx,
                                      saves, 2 * (prog.save_count + 1))) {
        int match_start = saves[0];
        int match_end = saves[1];
        int rep_len = 0;
        int *rep_cps = StrToCodepoints(ValToStrPtr(rep_val)->data, &rep_len);
        while (out_count + rep_len >= out_cap) {
          out_cap *= 2;
          out_cps = realloc(out_cps, out_cap * sizeof(int));
        }
        memcpy(out_cps + out_count, rep_cps, rep_len * sizeof(int));
        out_count += rep_len;
        free(rep_cps);
        idx = match_end;
        if (match_start == match_end) {
          if (idx < input_len) {
            if (out_count >= out_cap) {
              out_cap *= 2;
              out_cps = realloc(out_cps, out_cap * sizeof(int));
            }
            out_cps[out_count++] = input[idx];
            idx++;
          } else {
            break;
          }
        }
      } else {
        if (idx < input_len) {
          if (out_count >= out_cap) {
            out_cap *= 2;
            out_cps = realloc(out_cps, out_cap * sizeof(int));
          }
          out_cps[out_count++] = input[idx];
        }
        idx++;
      }
    }
    char *res_str = CodepointsToStr(out_cps, out_count);
    PUSH(ValStr(res_str));
    free(res_str);
    free(out_cps);
    free(input);
    FreeRegex(&prog);
    ValFree(&pat_val);
    ValFree(&str_val);
    ValFree(&rep_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UnicodeNormalize") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: UnicodeNormalize() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue form_val = POP();
    AKSValue str_val = POP();
    if (ValKind(str_val) != VALSTR || ValKind(form_val) != VALSTR) {
      ValFree(&str_val);
      ValFree(&form_val);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    char *norm = UnicodeNormalizeFunc(ValToStrPtr(str_val)->data,
                                      ValToStrPtr(form_val)->data);
    PUSH(ValStr(norm));
    free(norm);
    ValFree(&str_val);
    ValFree(&form_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LocaleCompare") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: LocaleCompare() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue loc_val = POP();
    AKSValue str2_val = POP();
    AKSValue str1_val = POP();
    if (ValKind(str1_val) != VALSTR || ValKind(str2_val) != VALSTR) {
      ValFree(&str1_val);
      ValFree(&str2_val);
      ValFree(&loc_val);
      PUSH(ValInt(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *loc =
        (ValKind(loc_val) == VALSTR) ? ValToStrPtr(loc_val)->data : "";
    int res = LocaleCompareFunc(ValToStrPtr(str1_val)->data,
                                ValToStrPtr(str2_val)->data, loc);
    PUSH(ValInt(res));
    ValFree(&str1_val);
    ValFree(&str2_val);
    ValFree(&loc_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemCompare") == 0) {
    if (argc < 3)
      return -1;
    size_t count = (size_t)ValToInt(POP());
    uintptr_t addr2 = (uintptr_t)ValToInt(POP());
    uintptr_t addr1 = (uintptr_t)ValToInt(POP());
    int res = 0;
    if (count > 0 && addr1 && addr2) {
      int r = memcmp((const void *)addr1, (const void *)addr2, count);
      res = r < 0 ? -1 : (r > 0 ? 1 : 0);
    }
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemCopy") == 0) {
    if (argc < 3)
      return -1;
    size_t count = (size_t)ValToInt(POP());
    uintptr_t src_addr = (uintptr_t)ValToInt(POP());
    uintptr_t dst_addr = (uintptr_t)ValToInt(POP());
    if (count > 0 && dst_addr && src_addr) {
      memcpy((void *)dst_addr, (const void *)src_addr, count);
    }
    PUSH(ValInt((int64_t)dst_addr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemSet") == 0) {
    if (argc < 3)
      return -1;
    size_t count = (size_t)ValToInt(POP());
    uint8_t byte_val = (uint8_t)ValToInt(POP());
    uintptr_t dst_addr = (uintptr_t)ValToInt(POP());
    if (count > 0 && dst_addr) {
      memset((void *)dst_addr, byte_val, count);
    }
    PUSH(ValInt((int64_t)dst_addr));
    *stacktopptr = stacktop;
    return 1;
  }

#undef PUSH
#undef POP
  return 0;
}
