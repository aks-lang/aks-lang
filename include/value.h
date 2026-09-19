// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef VALH
#define VALH


#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _MSC_VER
#include <intrin.h>
#define AtomicIncrement(ptr) _InterlockedIncrement((volatile long*)(ptr))
#define AtomicDecrement(ptr) _InterlockedDecrement((volatile long*)(ptr))
#elif defined(__GNUC__) || defined(__clang__)
#define AtomicIncrement(ptr) __sync_add_and_fetch(ptr, 1)
#define AtomicDecrement(ptr) __sync_sub_and_fetch(ptr, 1)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define AtomicIncrement(ptr) atomic_fetch_add((atomic_int*)(ptr), 1)
#define AtomicDecrement(ptr) atomic_fetch_sub((atomic_int*)(ptr), 1)
#else
#error "Compiler does not support atomic operations. Use GCC, Clang, MSVC, or a C11 compiler with stdatomic.h support."
#endif

#define SAFE_ALLOC(count, size) \
    (((count) > 0 && (size) > 0 && (count) > (size_t)-1 / (size)) ? NULL : malloc((count) * (size)))

typedef enum {
    VALFLOAT = 0,
    VALINT = 1,
    VALBOOL = 2,
    VALNULL = 3,
    VALSTR = 4,
    VALARR = 5,
    VALSTRUCT = 6,
    VALFN = 7,
    VALTASK = 8,
    VALCHANNEL = 9,
    VALMUTEX = 10,
    VALSEM = 11,
    VALBOUNDMETHOD = 12,
    VALARENA = 13,
    VALTUPLE = 14
} ValType;

#define ValToTuple(v) ValToArr(v)

typedef uint64_t AKSValue;
typedef struct AKSString AKSString;
typedef struct AKSArray AKSArray;
typedef struct AKSStruct AKSStruct;
typedef struct VMTask VMTask;
typedef struct VMChannel VMChannel;
typedef struct VMMutex VMMutex;
typedef struct VMSemaphore VMSemaphore;
typedef struct AKSBoundMethod AKSBoundMethod;

struct AKSString {
    char *data;
    int len;
    int refcount;
};

struct AKSBoundMethod {
    int refcount;
    AKSValue *receiver;
    char *name;
};

typedef struct AKSArena AKSArena;
struct AKSArena {
    int refcount;
    void **allocations;
    int *alloc_types;
    int count;
    int capacity;
    int invalidated;
};

#if defined(_MSC_VER)
#define AKS_INLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define AKS_INLINE static inline __attribute__((always_inline))
#else
#define AKS_INLINE static inline
#endif

AKS_INLINE AKSArena *ValToArena(AKSValue v) {
    return (AKSArena*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE AKSValue ValArena(AKSArena *a) {
    return (0xFFFDULL << 48) | ((uintptr_t)a & 0x0000FFFFFFFFFFFFULL);
}

#define VALFLOATLIMIT 0xFFF1000000000000ULL
#define VAL_PRIMITIVE_LIMIT 0xFFF4000000000000ULL

AKS_INLINE ValType ValKind(AKSValue v) {
    if (v < VALFLOATLIMIT) return VALFLOAT;
    uint64_t tag = (v >> 48) - 0xFFF0ULL;
    if (tag > 14) return VALFLOAT;
    return (ValType)tag;
}

AKS_INLINE int ValIsFloat(AKSValue v) {
    return v < VALFLOATLIMIT;
}

AKS_INLINE int ValIsInt(AKSValue v) {
    return (v >> 48) == 0xFFF1ULL;
}

AKS_INLINE int ValIsBool(AKSValue v) {
    return (v >> 48) == 0xFFF2ULL;
}

AKS_INLINE int ValIsNull(AKSValue v) {
    return (v >> 48) == 0xFFF3ULL;
}

AKS_INLINE int ValIsArr(AKSValue v) {
    return (v >> 48) == 0xFFF5ULL;
}

AKS_INLINE int ValIsStruct(AKSValue v) {
    return (v >> 48) == 0xFFF6ULL;
}

AKS_INLINE int ValIsPrimitive(AKSValue v) {
    return v < VAL_PRIMITIVE_LIMIT;
}

AKS_INLINE int64_t ValToInt(AKSValue v) {
    return ((int64_t)(v << 16)) >> 16;
}

AKS_INLINE int64_t ValToIntUnchecked(AKSValue v) {
    return ((int64_t)(v << 16)) >> 16;
}

AKS_INLINE double ValToFloat(AKSValue v) {
    union { uint64_t u; double d; } u;
    u.u = v;
    return u.d;
}

AKS_INLINE int ValToBool(AKSValue v) {
    return (int)(v & 1);
}

AKS_INLINE int ValToFn(AKSValue v) {
    return (int)(v & 0xFFFFFFFFULL);
}

AKS_INLINE AKSString *ValToStrPtr(AKSValue v) {
    return (AKSString*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE AKSArray *ValToArr(AKSValue v) {
    return (AKSArray*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE AKSStruct *ValToObj(AKSValue v) {
    return (AKSStruct*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE VMTask *ValToTask(AKSValue v) {
    return (VMTask*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE VMChannel *ValToChan(AKSValue v) {
    return (VMChannel*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE VMMutex *ValToMutex(AKSValue v) {
    return (VMMutex*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE VMSemaphore *ValToSem(AKSValue v) {
    return (VMSemaphore*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE AKSBoundMethod *ValToBoundMethod(AKSValue v) {
    return (AKSBoundMethod*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE AKSValue ValInt(int64_t i) {
    return (0xFFF1ULL << 48) | ((uint64_t)i & 0x0000FFFFFFFFFFFFULL);
}

AKS_INLINE AKSValue ValFloat(double f) {
    union { uint64_t u; double d; } u;
    u.d = f;
    if ((u.u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) {
        u.u = 0x7FF8000000000000ULL;
    }
    return u.u;
}

AKS_INLINE AKSValue ValBool(int b) {
    return (0xFFF2ULL << 48) | (b ? 1 : 0);
}

AKS_INLINE AKSValue ValNull(void) {
    return (0xFFF3ULL << 48);
}

AKS_INLINE AKSValue ValFn(int fnid) {
    return (0xFFF7ULL << 48) | (uint32_t)fnid;
}

#include <limits.h>

struct AKSArray {
    AKSValue *items;
    int count;
    int capacity;
    int refcount;
    int invalidated;
    int isstack;
    int isslice;
    AKSValue parent;
    void *arena;
};

struct AKSStruct {
    char *typename;
    int fieldcount;
    char **fieldnames;
    AKSValue *fieldvals;
    int refcount;
    int invalidated;
    int isstack;
    void *arena;
};

static inline void AksArrayEnsureCapacity(AKSArray *a, int needed) {
    if (a->capacity < needed) {
        int new_cap = a->capacity == 0 ? 4 : a->capacity * 2;
        if (new_cap < needed) new_cap = needed;
        if (a->isstack == 1) {
            AKSValue *new_items = (AKSValue*)malloc(new_cap * sizeof(AKSValue));
            if (a->count > 0 && a->items) {
                memcpy(new_items, a->items, a->count * sizeof(AKSValue));
            }
            a->items = new_items;
            a->capacity = new_cap;
            a->isstack = 2; // header on stack arena, items migrated to CRT heap
        } else {
            a->capacity = new_cap;
            a->items = (AKSValue*)realloc(a->items, a->capacity * sizeof(AKSValue));
        }
    }
}

#define AKS_INT48_MAX ((int64_t)0x00007FFFFFFFFFFFULL)
#define AKS_INT48_MIN (-((int64_t)0x0000800000000000ULL))

static inline int ChkAddOvf(int64_t a, int64_t b, int64_t *res) {
    if ((b > 0 && a > AKS_INT48_MAX - b) || (b < 0 && a < AKS_INT48_MIN - b)) {
        return 1;
    }
    *res = a + b;
    return 0;
}

static inline int ChkSubOvf(int64_t a, int64_t b, int64_t *res) {
    if ((b < 0 && a > AKS_INT48_MAX + b) || (b > 0 && a < AKS_INT48_MIN + b)) {
        return 1;
    }
    *res = a - b;
    return 0;
}

static inline int ChkMulOvf(int64_t a, int64_t b, int64_t *res) {
    if (a == 0 || b == 0) {
        *res = 0;
        return 0;
    }
    if (a > 0) {
        if (b > 0) {
            if (b > AKS_INT48_MAX / a) return 1;
        } else {
            if (b < AKS_INT48_MIN / a) return 1;
        }
    } else {
        if (b > 0) {
            if (a < AKS_INT48_MIN / b) return 1;
        } else {
            if (b < AKS_INT48_MAX / a) return 1;
        }
    }
    *res = a * b;
    return 0;
}

AKS_INLINE AKSValue ValAddIntInt(AKSValue a, AKSValue b) {
    int64_t res;
    if (ChkAddOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &res)) {
        return ValFloat((double)ValToIntUnchecked(a) + (double)ValToIntUnchecked(b));
    }
    return ValInt(res);
}

AKS_INLINE AKSValue ValAddFloatFloat(AKSValue a, AKSValue b) {
    return ValFloat(ValToFloat(a) + ValToFloat(b));
}

AKS_INLINE AKSValue ValSubFloatFloat(AKSValue a, AKSValue b) {
    return ValFloat(ValToFloat(a) - ValToFloat(b));
}

AKS_INLINE AKSValue ValMulFloatFloat(AKSValue a, AKSValue b) {
    return ValFloat(ValToFloat(a) * ValToFloat(b));
}

AKS_INLINE AKSValue ValDivFloatFloat(AKSValue a, AKSValue b) {
    return ValFloat(ValToFloat(a) / ValToFloat(b));
}

AKS_INLINE AKSValue ValSubIntInt(AKSValue a, AKSValue b) {
    int64_t res;
    if (ChkSubOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &res)) {
        return ValFloat((double)ValToIntUnchecked(a) - (double)ValToIntUnchecked(b));
    }
    return ValInt(res);
}

AKS_INLINE AKSValue ValMulIntInt(AKSValue a, AKSValue b) {
    int64_t res;
    if (ChkMulOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &res)) {
        return ValFloat((double)ValToIntUnchecked(a) * (double)ValToIntUnchecked(b));
    }
    return ValInt(res);
}

AKS_INLINE AKSValue ValDivIntInt(AKSValue a, AKSValue b) {
    int64_t ib = ValToIntUnchecked(b);
    if (ib == 0) {
        fprintf(stderr, "Runtime error: division by zero\n");
        return ValNull();
    }
    return ValInt(ValToIntUnchecked(a) / ib);
}

AKS_INLINE AKSValue ValModIntInt(AKSValue a, AKSValue b) {
    int64_t ib = ValToIntUnchecked(b);
    if (ib == 0) {
        fprintf(stderr, "Runtime error: modulo by zero\n");
        return ValNull();
    }
    return ValInt(ValToIntUnchecked(a) % ib);
}

extern int gStaticSafeMode;

static inline int ValIsInvalidated(AKSValue v) {
    if (gStaticSafeMode) return 0;
    if (ValKind(v) == VALARR && ValToArr(v) && ValToArr(v)->invalidated) return 1;
    if (ValKind(v) == VALSTRUCT && ValToObj(v) && ValToObj(v)->invalidated) return 1;
    return 0;
}

extern volatile int gActiveStrs;
extern volatile int gactivearrs;
extern volatile int gactivestructs;

AKSValue ValInt(int64_t i);
AKSValue ValFloat(double f);
AKSValue ValBool(int b);
AKSValue ValNull(void);
AKSValue ValStr(const char *s);
AKSValue ValStrLen(const char *s, int len);
AKSValue ValArr(int cap);
AKSValue ValStruct(const char *typename);
AKSValue ValArrStack(VMTask *task, int cap);
AKSValue ValTuple(int cap);
AKSValue ValTupleStack(VMTask *task, int cap);
AKSValue ValStructStack(VMTask *task, const char *typename);
AKSValue ValFn(int fnid);
AKSValue ValTask(VMTask *t);
AKSValue ValChannel(VMChannel *c);
AKSValue ValMutex(VMMutex *m);
AKSValue ValSem(VMSemaphore *s);
AKSValue ValBoundmethod(AKSValue receiver, const char *name);
AKSValue ValTypeof(AKSValue v);
AKSValue ValCastInt(AKSValue val, int *ok);
AKSValue ValCastFloat(AKSValue val, int *ok);
AKSValue ValCastBool(AKSValue val, int *ok);
AKSValue ValCastStr(AKSValue val);

void ValFreeFunc(AKSValue *v);
#define ValFree(v) do { if ((v) && !ValIsPrimitive(*(v))) ValFreeFunc(v); } while(0)
AKSValue ValCopySlow(AKSValue v);
static inline AKSValue ValCopy(AKSValue v) {
    if (ValIsPrimitive(v)) return v;
    uint64_t tag = (v >> 48) - 0xFFF0ULL;
    if (tag > 14) return v;
    return ValCopySlow(v);
}
AKS_INLINE int ValIsTruthy(AKSValue v) {
    uint64_t hi = v >> 48;
    if (hi == 0xFFF2ULL) return (int)(v & 1);
    if (hi == 0xFFF1ULL) return (((int64_t)(v << 16)) >> 16) != 0;
    if (hi == 0xFFF3ULL) return 0;
    if (v < VALFLOATLIMIT) {
        union { uint64_t u; double d; } u;
        u.u = v;
        return u.d != 0.0;
    }
    if (hi == 0xFFF4ULL) {
        AKSString *s = ValToStrPtr(v);
        return s && s->len > 0;
    }
    return 1;
}

AKSValue ValAdd(AKSValue a, AKSValue b);
AKSValue ValSub(AKSValue a, AKSValue b);
AKSValue ValMul(AKSValue a, AKSValue b);
AKSValue ValDiv(AKSValue a, AKSValue b);
AKSValue ValMod(AKSValue a, AKSValue b);
AKSValue ValNeg(AKSValue a);

AKSValue ValLt(AKSValue a, AKSValue b);
AKSValue ValGt(AKSValue a, AKSValue b);
AKSValue ValLte(AKSValue a, AKSValue b);
AKSValue ValGte(AKSValue a, AKSValue b);
AKSValue ValEq(AKSValue a, AKSValue b);
AKSValue ValNeq(AKSValue a, AKSValue b);
AKSValue ValAnd(AKSValue a, AKSValue b);
AKSValue ValOr(AKSValue a, AKSValue b);
AKSValue ValNot(AKSValue a);

AKSValue ValToStr(AKSValue a);
void ValPrint(AKSValue v);
void ValPrintln(AKSValue v);
const char *ValTypeName(ValType t);
AKSValue ValArrArena(int cap, void *arena);
AKSValue ValStructArena(const char *typename, void *arena);

#if defined(AKS_RELEASE_BUILD)
static inline void *AslrmAllocFast(size_t size) { return (malloc)(size); }
static inline void *AslrReAllocFast(void *ptr, size_t size) { return (realloc)(ptr, size); }
static inline void AslrFreeFast(void *ptr) { (free)(ptr); }
static inline void *AslrcAllocFast(size_t num, size_t size) { return (calloc)(num, size); }
static inline char *AslrStrDupFast(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *r = (char*)(malloc)(len + 1);
    if (r) memcpy(r, s, len + 1);
    return r;
}
#define AslrmAlloc(sz) AslrmAllocFast(sz)
#define AslrReAlloc(ptr, sz) AslrReAllocFast(ptr, sz)
#define AslrFree(ptr) AslrFreeFast(ptr)
#define AslrcAlloc(num, sz) AslrcAllocFast(num, sz)
#define AslrStrDup(s) AslrStrDupFast(s)
#else
void *AslrmAlloc(size_t size);
void *AslrReAlloc(void *ptr, size_t size);
void AslrFree(void *ptr);
char *AslrStrDup(const char *s);
void *AslrcAlloc(size_t num, size_t size);

#define malloc(sz) AslrmAlloc(sz)
#define realloc(ptr, sz) AslrReAlloc(ptr, sz)
#define free(ptr) AslrFree(ptr)
#define strdup(s) AslrStrDup(s)
#define calloc(n, sz) AslrcAlloc(n, sz)
#endif

#endif // Valh
