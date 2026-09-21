// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

size_t gaslrshift = 0;

/* AKS_RELEASE_BUILD: production builds bypass all debug-allocator overhead.
 * Enable with /DAKS_RELEASE_BUILD (MSVC) or -DAKS_RELEASE_BUILD (GCC/Clang). */
#if defined(AKS_RELEASE_BUILD)
#undef AslrmAlloc
#undef AslrReAlloc
#undef AslrFree
#undef AslrStrDup
#undef AslrcAlloc

void *AslrmAlloc(size_t size) { return (malloc)(size); }
void *AslrReAlloc(void *ptr, size_t size) { return (realloc)(ptr, size); }
void  AslrFree(void *ptr) { (free)(ptr); }
char *AslrStrDup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *r = (char*)(malloc)(len + 1);
    if (r) memcpy(r, s, len + 1);
    return r;
}
void *AslrcAlloc(size_t num, size_t size) { return (calloc)(num, size); }

#else /* AKS_DEBUG_BUILD: full safety allocator below */

#define ALLOC_MAGIC 0x1A2B3C4D5E6F7A8BULL
#define FREED_MAGIC 0xDEADBEEFDEADBEEFULL

#define QUARANTINE_SIZE 2048
static void *g_quarantine[QUARANTINE_SIZE];
static int g_quarantine_head = 0;
static int g_quarantine_tail = 0;
static int g_quarantine_count = 0;

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION g_quarantine_cs;
static volatile LONG g_cs_init_flag = 0;
static inline void QuarantineLock(void) {
    if (g_cs_init_flag == 0) {
        if (InterlockedCompareExchange(&g_cs_init_flag, 1, 0) == 0) {
            InitializeCriticalSection(&g_quarantine_cs);
            g_cs_init_flag = 2;
        } else {
            while (g_cs_init_flag != 2) Sleep(0);
        }
    }
    EnterCriticalSection(&g_quarantine_cs);
}
static inline void QuarantineUnlock(void) {
    LeaveCriticalSection(&g_quarantine_cs);
}
#else
#include <pthread.h>
static pthread_mutex_t g_quarantine_mutex = PTHREAD_MUTEX_INITIALIZER;
static inline void QuarantineLock(void) {
    pthread_mutex_lock(&g_quarantine_mutex);
}
static inline void QuarantineUnlock(void) {
    pthread_mutex_unlock(&g_quarantine_mutex);
}
#endif

/* Lockless per-thread xorshift64 PRNG: avoids the global rand() mutex. */
#ifdef _WIN32
static __declspec(thread) uint64_t t_xrand_state = 0;
#elif defined(__GNUC__) || defined(__clang__)
static __thread uint64_t t_xrand_state = 0;
#else
static uint64_t t_xrand_state = 0;
#endif
static inline uint64_t xrand(void) {
    if (!t_xrand_state) {
        t_xrand_state = (uint64_t)(uintptr_t)&t_xrand_state ^ (uint64_t)time(NULL);
        if (!t_xrand_state) t_xrand_state = 0xdeadbeefcafeull;
    }
    uint64_t x = t_xrand_state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return (t_xrand_state = x);
}

void *AslrmAlloc(size_t size) {
    size_t header_size = sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t);
    if (size > (size_t)-1 - (header_size + 256)) return NULL;
    size_t shift = ((xrand() & 15) + 1) * 8;
    char *raw = (char*)(malloc)(size + shift + header_size);
    if (!raw) return NULL;
    
    char *hdr = raw + shift;
    *(size_t*)hdr = shift;
    *(size_t*)(hdr + sizeof(size_t)) = size;
    *(uint64_t*)(hdr + sizeof(size_t) + sizeof(size_t)) = ALLOC_MAGIC;
    
    return hdr + header_size;
}

void *AslrReAlloc(void *ptr, size_t size) {
    if (!ptr) return AslrmAlloc(size);
    
    size_t header_size = sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t);
    if (size > (size_t)-1 - (header_size + 256)) return NULL;
    size_t *shift_ptr = (size_t*)((char*)ptr - header_size);
    size_t *size_ptr = (size_t*)((char*)ptr - header_size + sizeof(size_t));
    uint64_t *magic_ptr = (uint64_t*)((char*)ptr - sizeof(uint64_t));
    
    size_t shift = *shift_ptr;
    size_t old_size = *size_ptr;
    uint64_t magic = *magic_ptr;
    
    if (magic == FREED_MAGIC) {
        fprintf(stderr, "CRITICAL ERROR: Reallocating already freed pointer %p!\n", ptr);
        fflush(stderr);
        exit(139);
    }
    if (magic != ALLOC_MAGIC) {
        fprintf(stderr, "CRITICAL ERROR: Reallocating corrupted pointer %p!\n", ptr);
        fflush(stderr);
        exit(139);
    }
    
    char *raw = (char*)ptr - header_size - shift;
    char *newraw = (char*)(realloc)(raw, size + shift + header_size);
    if (!newraw) return NULL;
    
    char *hdr = newraw + shift;
    *(size_t*)hdr = shift;
    *(size_t*)(hdr + sizeof(size_t)) = size;
    *(uint64_t*)(hdr + sizeof(size_t) + sizeof(size_t)) = ALLOC_MAGIC;
    
    return hdr + header_size;
}

void AslrFree(void *ptr) {
    if (!ptr) return;
    
    size_t header_size = sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t);
    size_t *shift_ptr = (size_t*)((char*)ptr - header_size);
    size_t *size_ptr = (size_t*)((char*)ptr - header_size + sizeof(size_t));
    uint64_t *magic_ptr = (uint64_t*)((char*)ptr - sizeof(uint64_t));
    
    size_t shift = *shift_ptr;
    size_t size = *size_ptr;
    uint64_t magic = *magic_ptr;
    
    if (magic == FREED_MAGIC) {
        fprintf(stderr, "CRITICAL ERROR: Double-free or Use-After-Free detected on pointer %p!\n", ptr);
        fflush(stderr);
        exit(139);
    }
    if (magic != ALLOC_MAGIC) {
        fprintf(stderr, "CRITICAL ERROR: Invalid free or corrupted pointer %p detected!\n", ptr);
        fflush(stderr);
        exit(139);
    }
    
    // Poison memory block with 0x55 (UAF detector)
    memset(ptr, 0x55, size);
    
    // Mark as freed
    *magic_ptr = FREED_MAGIC;
    
    char *raw = (char*)ptr - header_size - shift;
    
    // Thread-safe quarantine push
    QuarantineLock();
    if (g_quarantine_count < QUARANTINE_SIZE) {
        g_quarantine[g_quarantine_tail] = raw;
        g_quarantine_tail = (g_quarantine_tail + 1) % QUARANTINE_SIZE;
        g_quarantine_count++;
    } else {
        void *old_raw = g_quarantine[g_quarantine_head];
        g_quarantine[g_quarantine_head] = raw;
        g_quarantine_head = (g_quarantine_head + 1) % QUARANTINE_SIZE;
        g_quarantine_tail = (g_quarantine_tail + 1) % QUARANTINE_SIZE;
        (free)(old_raw);
    }
    QuarantineUnlock();
}

char *AslrStrDup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *r = (char*)AslrmAlloc(len + 1);
    if (r) memcpy(r, s, len + 1);
    return r;
}

void *AslrcAlloc(size_t num, size_t size) {
    if (size != 0 && num > ((size_t)-1) / size) return NULL;
    size_t total = num * size;
    void *p = AslrmAlloc(total);
    if (p) memset(p, 0, total);
    return p;
}

#endif /* AKS_RELEASE_BUILD / AKS_DEBUG_BUILD */


volatile int gActiveStrs = 0;
volatile int gactivearrs = 0;
volatile int gactivestructs = 0;

AKSValue ValTask(VMTask *t) { return ((uint64_t)0xFFF8 << 48) | ((uintptr_t)t & 0x0000FFFFFFFFFFFFULL); }
AKSValue ValChannel(VMChannel *c) { return ((uint64_t)0xFFF9 << 48) | ((uintptr_t)c & 0x0000FFFFFFFFFFFFULL); }
AKSValue ValMutex(VMMutex *m) { return ((uint64_t)0xFFFA << 48) | ((uintptr_t)m & 0x0000FFFFFFFFFFFFULL); }
AKSValue ValSem(VMSemaphore *s) { return ((uint64_t)0xFFFB << 48) | ((uintptr_t)s & 0x0000FFFFFFFFFFFFULL); }

#define INTERN_BUCKETS 1024
#define INTERN_STRIPES 32

typedef struct StringInternEntry {
    AKSString *str;
    struct StringInternEntry *next;
} StringInternEntry;

static StringInternEntry *g_intern_buckets[INTERN_BUCKETS];

#ifdef _WIN32
static CRITICAL_SECTION g_intern_cs[INTERN_STRIPES];
static volatile LONG g_intern_init_flag = 0;
static inline void EnsureInternInit(void) {
    if (g_intern_init_flag == 0) {
        if (InterlockedCompareExchange(&g_intern_init_flag, 1, 0) == 0) {
            for (int i = 0; i < INTERN_STRIPES; i++) {
                InitializeCriticalSection(&g_intern_cs[i]);
            }
            g_intern_init_flag = 2;
        } else {
            while (g_intern_init_flag != 2) Sleep(0);
        }
    }
}
static inline void InternLock(uint32_t bucket) {
    EnsureInternInit();
    EnterCriticalSection(&g_intern_cs[bucket & (INTERN_STRIPES - 1)]);
}
static inline void InternUnlock(uint32_t bucket) {
    LeaveCriticalSection(&g_intern_cs[bucket & (INTERN_STRIPES - 1)]);
}
#else
static pthread_mutex_t g_intern_mutex[INTERN_STRIPES] = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
};
static inline void InternLock(uint32_t bucket) { pthread_mutex_lock(&g_intern_mutex[bucket & (INTERN_STRIPES - 1)]); }
static inline void InternUnlock(uint32_t bucket) { pthread_mutex_unlock(&g_intern_mutex[bucket & (INTERN_STRIPES - 1)]); }
#endif

static inline uint32_t HashStr(const char *s, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

AKSValue ValStrLen(const char *s, int len) {
    if (!s) s = "";
    if (len <= 64) {
        uint32_t h = HashStr(s, len) & (INTERN_BUCKETS - 1);
        InternLock(h);
        for (StringInternEntry *e = g_intern_buckets[h]; e; e = e->next) {
            if (e->str && e->str->len == len && memcmp(e->str->data, s, len) == 0) {
                AtomicIncrement(&e->str->refcount);
                InternUnlock(h);
                return ((uint64_t)0xFFF4 << 48) | ((uintptr_t)e->str & 0x0000FFFFFFFFFFFFULL);
            }
        }
        InternUnlock(h);
    }

    AKSString *str = malloc(sizeof(AKSString));
    if (!str) return ValNull();
    str->data = malloc(len + 1);
    if (!str->data) { free(str); return ValNull(); }
    if (len > 0) memcpy(str->data, s, len);
    str->data[len] = '\0';
    str->len = len;
    str->refcount = 1;
    AtomicIncrement(&gActiveStrs);

    if (len <= 64) {
        uint32_t h = HashStr(s, len) & (INTERN_BUCKETS - 1);
        InternLock(h);
        StringInternEntry *entry = malloc(sizeof(StringInternEntry));
        if (entry) {
            entry->str = str;
            entry->next = g_intern_buckets[h];
            g_intern_buckets[h] = entry;
        }
        InternUnlock(h);
    }

    return ((uint64_t)0xFFF4 << 48) | ((uintptr_t)str & 0x0000FFFFFFFFFFFFULL);
}

void ValDumpActiveStrings(void) {
    for (int s = 0; s < INTERN_STRIPES; s++) InternLock(s);
    for (int i = 0; i < INTERN_BUCKETS; i++) {
        for (StringInternEntry *e = g_intern_buckets[i]; e; e = e->next) {
            if (e->str && e->str->refcount > 0) {
                fprintf(stderr, "[DEBUG LEAK] Leaked string: '%s' (refcount=%d)\n", e->str->data, e->str->refcount);
            }
        }
    }
    for (int s = 0; s < INTERN_STRIPES; s++) InternUnlock(s);
}

AKSValue ValStr(const char *s) {
    return ValStrLen(s, s ? (int)strlen(s) : 0);
}

AKSValue ValArr(int cap) {
    AKSArray *arr = malloc(sizeof(AKSArray));
    if (!arr) return ValNull();
    arr->items = cap > 0 ? SAFE_ALLOC(cap, sizeof(AKSValue)) : NULL;
    arr->count = 0;
    arr->capacity = cap;
    arr->refcount = 1;
    arr->invalidated = 0;
    arr->isstack = 0;
    arr->isslice = 0;
    arr->has_slices = 0;
    arr->parent = ValNull();
    arr->arena = NULL;
    arr->detached = NULL;
    AtomicIncrement(&gactivearrs);
    return ((uint64_t)0xFFF5 << 48) | ((uintptr_t)arr & 0x0000FFFFFFFFFFFFULL);
}

AKSValue ValArrArena(int cap, void *arena) {
    AKSValue res = ValArr(cap);
    if (ValKind(res) == VALARR) ValToArr(res)->arena = arena;
    return res;
}

AKSValue ValStruct(const char *typename) {
    AKSStruct *obj = malloc(sizeof(AKSStruct));
    if (!obj) return ValNull();
    obj->typename = strdup(typename ? typename : "struct");
    obj->fieldcount = 0;
    obj->fieldnames = NULL;
    obj->fieldvals = NULL;
    obj->refcount = 1;
    obj->invalidated = 0;
    obj->isstack = 0;
    obj->arena = NULL;
    AtomicIncrement(&gactivestructs);
    return ((uint64_t)0xFFF6 << 48) | ((uintptr_t)obj & 0x0000FFFFFFFFFFFFULL);
}

AKSValue ValStructArena(const char *typename, void *arena) {
    AKSValue res = ValStruct(typename);
    if (ValKind(res) == VALSTRUCT) ValToObj(res)->arena = arena;
    return res;
}


AKSValue ValBoundmethod(AKSValue receiver, const char *name) {
    AKSBoundMethod *bm = malloc(sizeof(AKSBoundMethod));
    if (!bm) return ValNull();
    bm->refcount = 1;
    bm->receiver = malloc(sizeof(AKSValue));
    if (!bm->receiver) { free(bm); return ValNull(); }
    *bm->receiver = ValCopy(receiver);
    bm->name = name ? strdup(name) : NULL;
    return ((uint64_t)0xFFFC << 48) | ((uintptr_t)bm & 0x0000FFFFFFFFFFFFULL);
}

int gStaticSafeMode = 0;

void ValFreeFunc(AKSValue *v) {
    if (!v) return;
    ValType t = ValKind(*v);
    switch (t) {
        case VALSTR: {
            AKSString *str = ValToStrPtr(*v);
            if (str) {
                int should_free = 0;
                if (str->len <= 64 && str->data) {
                    uint32_t h = HashStr(str->data, str->len) & (INTERN_BUCKETS - 1);
                    InternLock(h);
                    if (AtomicDecrement(&str->refcount) <= 0) {
                        StringInternEntry **curr = &g_intern_buckets[h];
                        while (*curr) {
                            if ((*curr)->str == str) {
                                StringInternEntry *to_free = *curr;
                                *curr = (*curr)->next;
                                free(to_free);
                                break;
                            }
                            curr = &(*curr)->next;
                        }
                        should_free = 1;
                    }
                    InternUnlock(h);
                } else {
                    if (AtomicDecrement(&str->refcount) <= 0) {
                        should_free = 1;
                    }
                }
                if (should_free) {
                    if (str->data) free(str->data);
                    free(str);
                    AtomicDecrement(&gActiveStrs);
                }
            }
            break;
        }
        case VALTUPLE:
        case VALARR: {
            AKSArray *arr = ValToArr(*v);
            if (arr) {
                if (AtomicDecrement(&arr->refcount) <= 0) {
                    if (arr->invalidated) {
                        // Arena-freed: items already freed in arena_free.
                        // Just free the header and update the counter.
                        if (!arr->isstack) free(arr);
                        AtomicDecrement(&gactivearrs);
                    } else {
                        if (arr->isslice) {
                            ValFree(&arr->parent);
                        } else {
                            for (int i = 0; i < arr->count; i++) ValFree(&arr->items[i]);
                            if (arr->isstack != 1 && arr->items) free(arr->items);
                        }
                        AKSDetachedBuffer *cur_detached = arr->detached;
                        while (cur_detached) {
                            AKSDetachedBuffer *next_detached = cur_detached->next;
                            for (int i = 0; i < cur_detached->count; i++) ValFree(&cur_detached->items[i]);
                            free(cur_detached->items);
                            free(cur_detached);
                            cur_detached = next_detached;
                        }
                        if (!arr->isstack) free(arr);
                        AtomicDecrement(&gactivearrs);
                    }
                }
            }
            break;
        }
        case VALSTRUCT: {
            AKSStruct *obj = ValToObj(*v);
            if (obj) {
                if (AtomicDecrement(&obj->refcount) <= 0) {
                    if (obj->invalidated) {
                        // Arena-freed: items and typename already freed in arena_free.
                        // Just free the header and update the counter.
                        if (!obj->isstack) free(obj);
                        AtomicDecrement(&gactivestructs);
                    } else {
                        if (obj->typename && strcmp(obj->typename, "SecureString") == 0) {
                            for (int i = 0; i < obj->fieldcount; i++) {
                                if (obj->fieldnames && strcmp(obj->fieldnames[i], "data") == 0) {
                                    AKSValue val = obj->fieldvals[i];
                                    if (ValKind(val) == VALSTR && ValToStrPtr(val)->data) {
                                        memset(ValToStrPtr(val)->data, 0, ValToStrPtr(val)->len);
                                    }
                                }
                            }
                        }
                        for (int i = 0; i < obj->fieldcount; i++) {
                            if (obj->fieldnames && obj->isstack != 1) free(obj->fieldnames[i]);
                            ValFree(&obj->fieldvals[i]);
                        }
                        if (obj->fieldnames) free(obj->fieldnames);
                        if (obj->fieldvals) free(obj->fieldvals);
                        if (!obj->isstack && obj->typename) free(obj->typename);
                        if (!obj->isstack) free(obj);
                        AtomicDecrement(&gactivestructs);
                    }
                }
            }
            break;
        }
        case VALTASK: {
            VMTask *task = ValToTask(*v);
            if (task) {
                if (AtomicDecrement(&task->refcount) <= 0) {
                    TaskFree(task);
                }
            }
            break;
        }
        case VALCHANNEL: {
            VMChannel *chan = ValToChan(*v);
            if (chan && AtomicDecrement(&chan->refcount) <= 0) {
                ChanFree(chan);
            }
            break;
        }
        case VALMUTEX: {
            VMMutex *mutex = ValToMutex(*v);
            if (mutex && AtomicDecrement(&mutex->refcount) <= 0) {
                MutexFree(mutex);
            }
            break;
        }
        case VALSEM: {
            VMSemaphore *sem = ValToSem(*v);
            if (sem && AtomicDecrement(&sem->refcount) <= 0) {
                SemFree(sem);
            }
            break;
        }
        case VALBOUNDMETHOD: {
            AKSBoundMethod *bm = ValToBoundMethod(*v);
            if (bm) {
                if (AtomicDecrement(&bm->refcount) <= 0) {
                    if (bm->receiver) {
                        ValFree(bm->receiver);
                        free(bm->receiver);
                    }
                    if (bm->name) free(bm->name);
                    free(bm);
                }
            }
            break;
        }
        case VALARENA: {
            AKSArena *arena = ValToArena(*v);
            if (arena) {
                if (AtomicDecrement(&arena->refcount) <= 0) {
                    if (!arena->invalidated) {
                        for (int i = 0; i < arena->count; i++) {
                            void *ptr = arena->allocations[i];
                            int type = arena->alloc_types[i];
                            if (type == 1) {
                                AKSArray *arr = (AKSArray*)ptr;
                                if (!arr->invalidated) {
                                    for (int j = 0; j < arr->count; j++) ValFree(&arr->items[j]);
                                    free(arr->items);
                                    if (!arr->isstack) free(arr);
                                    AtomicDecrement(&gactivearrs);
                                }
                            } else if (type == 2) {
                                AKSStruct *obj = (AKSStruct*)ptr;
                                if (!obj->invalidated) {
                                    for (int j = 0; j < obj->fieldcount; j++) {
                                        if (obj->fieldnames) free(obj->fieldnames[j]);
                                        ValFree(&obj->fieldvals[j]);
                                    }
                                    free(obj->fieldnames);
                                    free(obj->fieldvals);
                                    free(obj->typename);
                                    if (!obj->isstack) free(obj);
                                    AtomicDecrement(&gactivestructs);
                                }
                            }
                        }
                        free(arena->allocations);
                        free(arena->alloc_types);
                        arena->invalidated = 1;
                    }
                    free(arena);
                }
            }
            break;
        }
        default: break;
    }
    *v = ValNull();
}

AKSValue ValCopySlow(AKSValue v) {
    switch (ValKind(v)) {
        case VALSTR:
            AtomicIncrement(&ValToStrPtr(v)->refcount);
            return v;
        case VALARR:
        case VALTUPLE:
            AtomicIncrement(&ValToArr(v)->refcount);
            return v;
        case VALSTRUCT:
            AtomicIncrement(&ValToObj(v)->refcount);
            return v;
        case VALTASK:
            AtomicIncrement(&ValToTask(v)->refcount);
            return v;
        case VALCHANNEL:
            AtomicIncrement(&ValToChan(v)->refcount);
            return v;
        case VALMUTEX:
            AtomicIncrement(&ValToMutex(v)->refcount);
            return v;
        case VALSEM:
            AtomicIncrement(&ValToSem(v)->refcount);
            return v;
        case VALARENA:
            AtomicIncrement(&ValToArena(v)->refcount);
            return v;
        case VALBOUNDMETHOD:
            AtomicIncrement(&ValToBoundMethod(v)->refcount);
            return v;
        default:
            return v;
    }
}

AKSValue ValAdd(AKSValue a, AKSValue b) {
    ValType ta = ValKind(a);
    ValType tb = ValKind(b);
    if (ta == VALSTR && tb == VALSTR) {
        AKSString *sa = ValToStrPtr(a);
        AKSString *sb = ValToStrPtr(b);
        int len = sa->len + sb->len;
        char stackbuf[512];
        char *buf = (len < 512) ? stackbuf : malloc(len + 1);
        if (!buf) return ValNull();
        memcpy(buf, sa->data, sa->len);
        memcpy(buf + sa->len, sb->data, sb->len);
        buf[len] = 0;
        AKSValue r = ValStrLen(buf, len);
        if (buf != stackbuf) free(buf);
        return r;
    }
    if (ta == VALSTR || tb == VALSTR) {
        AKSValue as = (ta == VALSTR) ? ValCopy(a) : ValToStr(a);
        AKSValue bs = (tb == VALSTR) ? ValCopy(b) : ValToStr(b);
        int len = ValToStrPtr(as)->len + ValToStrPtr(bs)->len;
        char stackbuf[512];
        char *buf = (len < 512) ? stackbuf : malloc(len + 1);
        if (!buf) {
            ValFree(&as);
            ValFree(&bs);
            return ValNull();
        }
        memcpy(buf, ValToStrPtr(as)->data, ValToStrPtr(as)->len);
        memcpy(buf + ValToStrPtr(as)->len, ValToStrPtr(bs)->data, ValToStrPtr(bs)->len);
        buf[len] = 0;
        AKSValue r = ValStrLen(buf, len);
        if (buf != stackbuf) free(buf);
        ValFree(&as);
        ValFree(&bs);
        return r;
    }
    if (ta == VALINT && tb == VALINT) {
        return ValAddIntInt(a, b);
    }
    if (ta == VALFLOAT && tb == VALFLOAT) {
        return ValAddFloatFloat(a, b);
    }
    if (ta == VALFLOAT || tb == VALFLOAT || ta == VALINT || tb == VALINT) {
        double fa = ta == VALFLOAT ? ValToFloat(a) : (double)ValToIntUnchecked(a);
        double fb = tb == VALFLOAT ? ValToFloat(b) : (double)ValToIntUnchecked(b);
        return ValFloat(fa + fb);
    }
    return ValNull();
}

AKSValue ValSub(AKSValue a, AKSValue b) {
    ValType ta = ValKind(a);
    ValType tb = ValKind(b);
    if (ta == VALINT && tb == VALINT) {
        return ValSubIntInt(a, b);
    }
    if (ta == VALFLOAT && tb == VALFLOAT) {
        return ValSubFloatFloat(a, b);
    }
    double fa = ta == VALFLOAT ? ValToFloat(a) : (double)ValToIntUnchecked(a);
    double fb = tb == VALFLOAT ? ValToFloat(b) : (double)ValToIntUnchecked(b);
    return ValFloat(fa - fb);
}

AKSValue ValMul(AKSValue a, AKSValue b) {
    ValType ta = ValKind(a);
    ValType tb = ValKind(b);
    if (ta == VALARR && tb == VALINT) {
        AKSArray *base = ValToArr(a);
        int count = (int)ValToIntUnchecked(b);
        if (count <= 0) return ValArr(0);
        int total = base->count * count;
        AKSValue res = ValArr(total);
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < base->count; j++) {
                ValToArr(res)->items[ValToArr(res)->count++] = ValCopy(base->items[j]);
            }
        }
        return res;
    }
    if (ta == VALINT && tb == VALARR) {
        return ValMul(b, a);
    }
    if (ta == VALINT && tb == VALINT) {
        return ValMulIntInt(a, b);
    }
    if (ta == VALFLOAT && tb == VALFLOAT) {
        return ValMulFloatFloat(a, b);
    }
    double fa = ta == VALFLOAT ? ValToFloat(a) : (double)ValToIntUnchecked(a);
    double fb = tb == VALFLOAT ? ValToFloat(b) : (double)ValToIntUnchecked(b);
    return ValFloat(fa * fb);
}

AKSValue ValDiv(AKSValue a, AKSValue b) {
    ValType ta = ValKind(a);
    ValType tb = ValKind(b);
    if (ta == VALINT && tb == VALINT) {
        return ValDivIntInt(a, b);
    }
    if (ta == VALFLOAT && tb == VALFLOAT) {
        double fb = ValToFloat(b);
        if (fb == 0.0) {
            fprintf(stderr, "Runtime error: division by zero\n");
            return ValNull();
        }
        return ValDivFloatFloat(a, b);
    }
    double fa = ta == VALFLOAT ? ValToFloat(a) : (double)ValToIntUnchecked(a);
    double fb = tb == VALFLOAT ? ValToFloat(b) : (double)ValToIntUnchecked(b);
    if (fb == 0.0) {
        fprintf(stderr, "Runtime error: division by zero\n");
        return ValNull();
    }
    return ValFloat(fa / fb);
}

AKSValue ValMod(AKSValue a, AKSValue b) {
    ValType ta = ValKind(a);
    ValType tb = ValKind(b);
    if (ta == VALINT && tb == VALINT) {
        return ValModIntInt(a, b);
    }
    double fa = ta == VALFLOAT ? ValToFloat(a) : (double)ValToIntUnchecked(a);
    double fb = tb == VALFLOAT ? ValToFloat(b) : (double)ValToIntUnchecked(b);
    if (fb == 0.0) {
        fprintf(stderr, "Runtime error: modulo by zero\n");
        return ValNull();
    }
    return ValFloat(fmod(fa, fb));
}

AKSValue ValNeg(AKSValue a) {
    ValType ta = ValKind(a);
    if (ta == VALINT) return ValInt(-ValToInt(a));
    if (ta == VALFLOAT) return ValFloat(-ValToFloat(a));
    return ValNull();
}

#define CMP(name,op) \
AKSValue name(AKSValue a, AKSValue b) { \
    if (ValIsInt(a) & ValIsInt(b)) return ValBool(ValToInt(a) op ValToInt(b)); \
    ValType ta = ValKind(a); \
    ValType tb = ValKind(b); \
    if ((ta == VALINT || ta == VALFLOAT) && (tb == VALINT || tb == VALFLOAT)) { \
        double fa = ta == VALFLOAT ? ValToFloat(a) : (double)ValToInt(a); \
        double fb = tb == VALFLOAT ? ValToFloat(b) : (double)ValToInt(b); \
        return ValBool(fa op fb); \
    } \
    if (ta == VALSTR && tb == VALSTR) { \
        AKSString *sa = ValToStrPtr(a); \
        AKSString *sb = ValToStrPtr(b); \
        if (sa == sb) return ValBool(0 op 0); \
        return ValBool(strcmp(sa->data ? sa->data : "", sb->data ? sb->data : "") op 0); \
    } \
    return ValBool(0); \
}

CMP(ValLt,  <)
CMP(ValGt,  >)
CMP(ValLte, <=)
CMP(ValGte, >=)

AKSValue ValEq(AKSValue a, AKSValue b) {
    if (a == b) return ValBool(1);
    if ((ValIsInt(a) | ValIsBool(a) | ValIsNull(a)) && (ValIsInt(b) | ValIsBool(b) | ValIsNull(b))) {
        return ValBool(0);
    }
    ValType ta = ValKind(a);
    ValType tb = ValKind(b);
    if (ta != tb) return ValBool(0);
    switch (ta) {
        case VALINT:
        case VALBOOL:
        case VALNULL:   return ValBool(0);
        case VALFLOAT:  return ValBool(ValToFloat(a) == ValToFloat(b));
        case VALSTR: {
            AKSString *sa = ValToStrPtr(a);
            AKSString *sb = ValToStrPtr(b);
            if (sa == sb) return ValBool(1);
            if (sa->len != sb->len) return ValBool(0);
            return ValBool(memcmp(sa->data, sb->data, sa->len) == 0);
        }
        case VALSTRUCT: {
            AKSStruct *sa = ValToObj(a);
            AKSStruct *sb = ValToObj(b);
            if (sa == sb) return ValBool(1);
            if (strcmp(sa->typename, sb->typename) != 0) return ValBool(0);
            if (sa->fieldcount != sb->fieldcount) return ValBool(0);
            for (int i = 0; i < sa->fieldcount; i++) {
                int found = -1;
                for (int j = 0; j < sb->fieldcount; j++) {
                    if (strcmp(sa->fieldnames[i], sb->fieldnames[j]) == 0) {
                        found = j;
                        break;
                    }
                }
                if (found < 0) return ValBool(0);
                AKSValue eq = ValEq(sa->fieldvals[i], sb->fieldvals[found]);
                if (!ValToBool(eq)) return ValBool(0);
            }
            return ValBool(1);
        }
        default:        return ValBool(0);
    }
}

AKSValue ValNeq(AKSValue a, AKSValue b) {
    return ValBool(!ValToBool(ValEq(a, b)));
}

AKSValue ValAnd(AKSValue a, AKSValue b) { return ValBool(ValIsTruthy(a) && ValIsTruthy(b)); }
AKSValue ValOr (AKSValue a, AKSValue b) { return ValBool(ValIsTruthy(a) || ValIsTruthy(b)); }
AKSValue ValNot(AKSValue a)             { return ValBool(!ValIsTruthy(a)); }

AKSValue ValToStr(AKSValue a) {
    char buf[64];
    switch (ValKind(a)) {
        case VALINT:    snprintf(buf, sizeof(buf), "%lld", (long long)ValToInt(a)); return ValStr(buf);
        case VALFLOAT:  snprintf(buf, sizeof(buf), "%g", ValToFloat(a)); return ValStr(buf);
        case VALBOOL:   return ValStr(ValToBool(a) ? "true" : "false");
        case VALNULL:   return ValStr("null");
        case VALSTR:    return ValCopy(a);
        case VALFN:     return ValStr("[function]");
        case VALTUPLE: {
            /* Dynamic builder to avoid fixed 4 KB buffer overflow */
            size_t bufsz = 64;
            char *buf = malloc(bufsz);
            if (!buf) return ValStr("()");
            size_t buflen = 0;
            buf[buflen++] = '(';
            for (int i = 0; i < ValToArr(a)->count; i++) {
                if (i > 0) {
                    while (buflen + 2 + 1 > bufsz) { bufsz *= 2; char *nb = realloc(buf, bufsz); if (nb) buf = nb; }
                    buf[buflen++] = ',';
                    buf[buflen++] = ' ';
                }
                AKSValue str_elem = ValToStr(ValToArr(a)->items[i]);
                AKSString *sp = ValToStrPtr(str_elem);
                size_t elen = (size_t)(sp ? sp->len : 0);
                while (buflen + elen + 4 > bufsz) { bufsz *= 2; char *nb = realloc(buf, bufsz); if (nb) buf = nb; }
                if (sp && sp->data) { memcpy(buf + buflen, sp->data, elen); buflen += elen; }
                ValFree(&str_elem);
            }
            if (ValToArr(a)->count == 1) {
                while (buflen + 2 > bufsz) { bufsz *= 2; char *nb = realloc(buf, bufsz); if (nb) buf = nb; }
                buf[buflen++] = ',';
            }
            while (buflen + 2 > bufsz) { bufsz *= 2; char *nb = realloc(buf, bufsz); if (nb) buf = nb; }
            buf[buflen++] = ')';
            buf[buflen] = '\0';
            AKSValue res = ValStrLen(buf, (int)buflen);
            free(buf);
            return res;
        }
        default:        return ValStr("[object]");
    }
}

void ValPrint(AKSValue v) {
    switch (ValKind(v)) {
        case VALINT:    printf("%lld", (long long)ValToInt(v)); break;
        case VALFLOAT:  printf("%g", ValToFloat(v)); break;
        case VALBOOL:   printf("%s", ValToBool(v) ? "true" : "false"); break;
        case VALNULL:   printf("null"); break;
        case VALSTR:    printf("%s", ValToStrPtr(v)->data); break;
        case VALFN:     printf("<function>"); break;
        case VALTASK:   printf("<task>"); break;
        case VALCHANNEL: printf("<channel>"); break;
        case VALMUTEX:  printf("<mutex>"); break;
        case VALSEM:    printf("<semaphore>"); break;
        case VALARR: {
            printf("[");
            for (int i = 0; i < ValToArr(v)->count; i++) {
                if (i) printf(", ");
                ValPrint(ValToArr(v)->items[i]);
            }
            printf("]");
            break;
        }
        case VALTUPLE: {
            printf("(");
            for (int i = 0; i < ValToArr(v)->count; i++) {
                if (i) printf(", ");
                ValPrint(ValToArr(v)->items[i]);
            }
            if (ValToArr(v)->count == 1) printf(",");
            printf(")");
            break;
        }
        default: printf("[object]"); break;
    }
}

void ValPrintln(AKSValue v) { ValPrint(v); printf("\n"); fflush(stdout); }

const char *ValTypeName(ValType t) {
    switch(t){
        case VALINT: return "int"; case VALFLOAT: return "float";
        case VALSTR: return "string"; case VALBOOL: return "bool";
        case VALNULL: return "null"; case VALARR: return "array";
        case VALSTRUCT: return "struct"; case VALFN: return "fn";
        case VALTASK: return "task"; case VALCHANNEL: return "channel";
        case VALMUTEX: return "mutex"; case VALSEM: return "semaphore";
        case VALARENA: return "arena";
        case VALTUPLE: return "tuple";
        default: return "?";
    }
}

AKSValue ValTypeof(AKSValue v) {
    if (ValKind(v) == VALSTRUCT) {
        const char *tname = ValToObj(v)->typename;
        const char *dot = strchr(tname, '.');
        if (dot) {
            size_t len = dot - tname;
            char *buf = malloc(len + 1);
            memcpy(buf, tname, len);
            buf[len] = '\0';
            AKSValue res = ValStr(buf);
            free(buf);
            return res;
        }
        return ValStr(tname);
    }
    return ValStr(ValTypeName(ValKind(v)));
}

AKSValue ValCastInt(AKSValue val, int *ok) {
    if (ok) *ok = 1;
    if (ValKind(val) == VALINT) return ValCopy(val);
    if (ValKind(val) == VALFLOAT) return ValInt((int64_t)ValToFloat(val));
    if (ValKind(val) == VALBOOL) return ValInt(ValToBool(val) ? 1 : 0);
    if (ValKind(val) == VALNULL) return ValInt(0);
    if (ValKind(val) == VALSTR) {
        const char *s = ValToStrPtr(val)->data;
        if (!s || !*s) { if (ok) *ok = 0; return ValInt(0); }
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        const char *p = s;
        if (*p == '-' || *p == '+') p++;
        if (*p < '0' || *p > '9') { if (ok) *ok = 0; return ValInt(0); }
        char *endptr;
        int64_t res = strtoll(s, &endptr, 10);
        return ValInt(res);
    }
    return ValInt(ValToInt(val));
}

AKSValue ValCastFloat(AKSValue val, int *ok) {
    if (ok) *ok = 1;
    if (ValKind(val) == VALFLOAT) return ValCopy(val);
    if (ValKind(val) == VALINT) return ValFloat((double)ValToInt(val));
    if (ValKind(val) == VALBOOL) return ValFloat(ValToBool(val) ? 1.0 : 0.0);
    if (ValKind(val) == VALNULL) return ValFloat(0.0);
    if (ValKind(val) == VALSTR) {
        const char *s = ValToStrPtr(val)->data;
        if (!s || !*s) { if (ok) *ok = 0; return ValFloat(0.0); }
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        char *endptr;
        double res = strtod(s, &endptr);
        if (endptr == s) { if (ok) *ok = 0; return ValFloat(0.0); }
        return ValFloat(res);
    }
    return ValFloat(ValToFloat(val));
}

AKSValue ValCastBool(AKSValue val, int *ok) {
    if (ok) *ok = 1;
    if (ValKind(val) == VALBOOL) return ValCopy(val);
    if (ValKind(val) == VALINT) return ValBool(ValToInt(val) != 0);
    if (ValKind(val) == VALFLOAT) return ValBool(ValToFloat(val) != 0.0);
    if (ValKind(val) == VALNULL) return ValBool(0);
    if (ValKind(val) == VALSTR) {
        const char *s = ValToStrPtr(val)->data;
        if (!s || !*s) { if (ok) *ok = 0; return ValBool(0); }
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        if (strcmp(s, "1") == 0 || strcmp(s, "true") == 0 || strcmp(s, "TRUE") == 0) return ValBool(1);
        if (strcmp(s, "0") == 0 || strcmp(s, "false") == 0 || strcmp(s, "FALSE") == 0) return ValBool(0);
        if (ok) *ok = 0;
        return ValBool(0);
    }
    return ValBool(ValToBool(val));
}

AKSValue ValCastStr(AKSValue val) {
    return ValToStr(val);
}

#ifndef AKS_RELEASE_BUILD
char *AslrStrDup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *res = AslrmAlloc(len + 1);
    if (res) {
        memcpy(res, s, len + 1);
    }
    return res;
}

void *AslrcAlloc(size_t num, size_t size) {
    size_t total = num * size;
    void *ptr = AslrmAlloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}
#endif /* !AKS_RELEASE_BUILD */

static inline void *StackAlloc(VMTask *task, size_t size) {
    if (!task || !task->stackarena) return NULL;
    size = (size + 7) & ~7;
    int limit = task->stackarena_size > 0 ? (int)task->stackarena_size : 65536;
    if (task->stackarenatop + (int)size > limit) {
        return NULL;
    }
    void *ptr = task->stackarena + task->stackarenatop;
    task->stackarenatop += (int)size;
    return ptr;
}

AKSValue ValArrStack(VMTask *task, int cap) {
    if (!task || !task->stackarena) return ValArr(cap);
    size_t hdr_sz = (sizeof(AKSArray) + 7) & ~7;
    size_t items_sz = cap > 0 ? ((cap * sizeof(AKSValue) + 7) & ~7) : 0;
    int limit = task->stackarena_size > 0 ? (int)task->stackarena_size : 65536;
    if (task->stackarenatop + (int)(hdr_sz + items_sz) > limit) {
        return ValArr(cap);
    }
    AKSArray *arr = (AKSArray*)StackAlloc(task, sizeof(AKSArray));
    if (!arr) return ValArr(cap);
    arr->items = cap > 0 ? (AKSValue*)StackAlloc(task, cap * sizeof(AKSValue)) : NULL;
    arr->count = 0;
    arr->capacity = cap;
    arr->refcount = 1;
    arr->invalidated = 0;
    arr->isstack = 1;
    arr->isslice = 0;
    arr->has_slices = 0;
    arr->parent = ValNull();
    arr->arena = NULL;
    arr->detached = NULL;
    AtomicIncrement(&gactivearrs);
    return ((uint64_t)0xFFF5 << 48) | ((uintptr_t)arr & 0x0000FFFFFFFFFFFFULL);
}

AKSValue ValTuple(int cap) {
    AKSValue arr = ValArr(cap);
    return (arr & 0x0000FFFFFFFFFFFFULL) | ((uint64_t)0xFFFE << 48);
}

AKSValue ValTupleStack(VMTask *task, int cap) {
    AKSValue arr = ValArrStack(task, cap);
    return (arr & 0x0000FFFFFFFFFFFFULL) | ((uint64_t)0xFFFE << 48);
}

AKSValue ValStructStack(VMTask *task, const char *typename) {
    if (!task || !task->stackarena) return ValStruct(typename);
    AKSStruct *obj = (AKSStruct*)StackAlloc(task, sizeof(AKSStruct));
    if (!obj) return ValStruct(typename);
    obj->typename = (char*)typename;
    obj->fieldcount = 0;
    obj->fieldnames = NULL;
    obj->fieldvals = NULL;
    obj->refcount = 1;
    obj->invalidated = 0;
    obj->isstack = 1;
    obj->arena = NULL;
    AtomicIncrement(&gactivestructs);
    return ((uint64_t)0xFFF6 << 48) | ((uintptr_t)obj & 0x0000FFFFFFFFFFFFULL);
}

