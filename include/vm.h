// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef VMH
#define VMH

#include "value.h"
#include "compiler.h"
#include "aks_plat.h"

#if defined(__linux__) && !defined(AKS_DISABLE_IO_URING)
#define AKS_USE_IO_URING 1
#include <liburing.h>
#endif

#define MAXSTACK 65536
#define MAXFRAMES 2048
#define MAXGLOBALS 8192
#define MAXFNS 65536

typedef struct {
    uint8_t op;
    int ipoffset;
    int32_t jumpoffset;
    int guardoutcome;
    int64_t pushival;
    double pushfval;
    uint16_t slot;
} TraceStep;


typedef struct {
    FnProto *fn;
    uint8_t *ip;
    int slotsbase;
    int argc;
} CallFrame;

typedef struct {
    char *name;
    AKSValue val;
} GlobalVar;

typedef struct FlameNode {
    char name[128];
    uint64_t samples;
    uint64_t self_samples;
    int parent_idx;
    int depth;
} FlameNode;

typedef struct VMTask VMTask;
typedef struct VMChannel VMChannel;
typedef struct VMMutex VMMutex;
typedef struct VMSemaphore VMSemaphore;

#define MINSTACK 1024
#define STACKGROW 2048
#define MINFRAMES 16
#define FRAMESGROW 32

struct VMTask {
    int refcount;
    int id;
    int state; // =ready, 1=running, 2=blocked, 3=completed
    AKSValue *stack;
    int stacktop;
    int stackcap;
    int stack_peak;  /* peak stack slot index ever reached (watermark) */
    CallFrame *frames;
    int framecount;
    int framecap;
    struct {
        int frameindex;
        int stacktopbase;
        uint8_t *catchip;
    } tryhandlers[256];
    int tryhandlercount;
    AKSValue result;
    
    VMTask **awaiters;
    int awaitercount;
    int awaitercap;
    
    void *blockedon; // channel, mutex, or semaphore
    int blockedontype; // =channel, 2=mutex, 3=semaphore, 4=task, 5=select
    int isyielded;
    int blockedonstdlib;
    char stdlibname[64];
    int stdlibargc;

    int tracingmode; // =none, 1=recording
    int tracestartoffset;
    void *recordedtrace; // pointer to TraceStep array
    int tracestepcount;
    int totalinstructionsrun;
    int haserror;

    int timedout;
    int cancelled;
    int was_cancelled;
    int rendezvous_done;
    int is_in_select;
    int select_woken_index;
    int select_op_count;
    struct {
        VMChannel *chan;
        int is_send;
        AKSValue send_val;
    } select_ops[32];

    char *stackarena;
    int stackarenatop;
    size_t stackarena_size;
    int arenatops[MAXFRAMES];

    AKSValue effect_handlers[32];
    int effect_handler_count;
};

struct VMChannel {
    int refcount;
    AKSValue *buffer;
    int capacity;
    int head;
    int tail;
    int count;
    int isclosed;
    void *lock;
    VMTask *blockedreaders[256];
    int readercount;
    VMTask *blockedwriters[256];
    int writercount;
};

struct VMMutex {
    int refcount;
    int islocked;
    VMTask *owner;
    void *lock;
    VMTask *blockedtasks[256];
    int blockedcount;
};

struct VMSemaphore {
    int refcount;
    int count;
    void *lock;
    VMTask *blockedtasks[256];
    int blockedcount;
};

typedef struct {
    VMTask *task;
    VMTask *awaiting_on;
    uint64_t target_ms;
    int active;
} VMTimer;

typedef struct {
    void *threadhandle;
    int id;
    VMTask *deque[1024];
    int head;
    int tail;
    void *lock; // opaque pointer to CRITICAL_SECTION
} VMWorker;

typedef struct {
    FnProto *fns[MAXFNS];
    int fncount;

    char **strpool;
    int strcount;

    GlobalVar globals[MAXGLOBALS];
    int globalcount;

    int isrepl;

    // JIT compilation fields
    int jitmode; // =c, 1=x64, 2=llvm, 3=qbe
    int jitcacheenabled;
    const char *pgoprofile;
    const char *pgouse;
    struct {
        int fnid;
        int targetoffset;
        void *libhandle;
        void (*jitfn)(AKSValue *stack, int *stacktop, AKSValue *locals, void *vm);
    } jitloops[128];
    int jitloopcount;
    int fncallcounts[4096];
    int hot_loop_threshold;
    int hot_func_threshold;

    // concurrency scheduling fields
    VMTask *maintask;
    int nexttaskid;
    VMWorker workers[16];
    int workercount;
    void *globallock; // opaque pointer to CRITICAL_SECTION
    int shouldterminate;
    
#define PERMREAD   (1 << 0)
#define PERMWRITE  (1 << 1)
#define PERMEXEC   (1 << 2)
#define PERMENV    (1 << 3)
#define PERMFFI    (1 << 4)
#define PERMALL    (PERMREAD | PERMWRITE | PERMEXEC | PERMENV | PERMFFI)

    // tracking list of all tasks for cleanup
    VMTask *alltasks[4096];
    int alltaskscount;

    // security fields
    uint32_t permissions;
    int sandboxenabled;
    int capmode;
    int inrestrictedeval;
    uint64_t stackcanary;
    int64_t instructionlimit;
    int64_t instructioncount;

    // profiling & flamegraph fields
    int profilingmode;
    uint64_t instructioncounters[256];
    FlameNode flame_nodes[4096];
    int flame_node_count;
    int flame_profiling_enabled;
    char flame_output_path[512];
    int flame_current_node;

    VMTimer timers[1024];
    int timercount;
    void *timerthread;

#ifdef _WIN32
    void *iocp;
#elif defined(AKS_USE_IO_URING)
    struct io_uring ring;
#endif

    struct {
        char *name;
        void *func;
    } registered_natives[128];
    int registered_natives_count;
    AKSValue panic_hook;
    CompilerStruct structs[512];
    int structcount;
    int is_ctfe;
} VM;

typedef enum {
    regophalt = 0,
    regoppushconst,
    regopload,
    regopstore,
    regoploadglobal,
    regopstoreglobal,
    regopadd,
    regopsub,
    regopmul,
    regopdiv,
    regopmod,
    regopneg,
    regoplt,
    regopgt,
    regoplte,
    regopgte,
    regopeq,
    regopneq,
    regopand,
    regopor,
    regopnot,
    regopjmp,
    regopjmpiffalse,
    regopjmpifnull,
    regopcall,
    regopcalldynamic,
    regopret,
    regopretval,
    regopnewarr,
    regopnewtuple,
    regoparrget,
    regoparrset,
    regopnewstruct,
    regopgetfield,
    regopsetfield,
    regopinc,
    regopdec,
    regoppushfn,
    regoptrypush,
    regoptrypop,
    regopthrow,
    regopcoalesce,
    regoprange,
    regoptailcall,
    regopspawn,
    regopawait,
    regopshl,
    regopshr,
    regopbitand,
    regopbitor,
    regopbitxor,
    regopbitnot,
    regopapply,
    regoprestargs,
    regopaddreg,
    regopaddint,
    regopcopy,
    regopwriteln,
    regopinputstr,
    regopprintrepl,
    regoptypeof,
    regopclonestruct,
    regopcastint,
    regopcastfloat,
    regopcastbool,
    regopcaststr
} RegOpCode;

typedef struct {
    int op;
    int dst;
    int src1;
    int src2;
    AKSValue constval;
    char *fieldname;
    char *cachedtypename;
    int cachedindex;
    int targetip;
    int bytecodeoffset;
} RegInst;

void TranslateToRegister(VM *vm, FnProto *fn);

VM *VMNew(void);
void VMFree(VM *vm);
int VMLoad(VM *vm, const char *path);
int VMLoadMem(VM *vm, Compiler *c);
int VMRun(VM *vm);
int VMRepl(VM *vm, int startipoffset);
void VMSync(VM *vm, Compiler *c, int syncmain);

// VM task and resource lifecycle helpers (called by value freeing logic)
VMTask *TaskNew(VM *vm, FnProto *fn, int startipoffset);
void TaskFree(VMTask *t);
void TaskReLease(VM *vm, VMTask *t);
void *AllocStackArenaWithGuard(size_t size, size_t *allocated_size);
void FreeStackArenaWithGuard(void *ptr, size_t size);
void ChanFree(VMChannel *c);
void MutexFree(VMMutex *m);
void SemFree(VMSemaphore *s);

// DAP debugger support declarations
extern int GDapActive;
extern int GDapPaused;
extern int gTestMode;
extern int gCoverageEnabled;
extern char gStdlibErrorMessage[2048];

void DAPInit(int port);
void DAPCheckDebugger(VM *vm, VMTask *task, CallFrame *frame, int offset);
int RunTaskSlice(VM *vm, VMTask *task, int maxinstructions);
int VMGenerateFlamegraphSVG(VM *vm, const char *filepath);

/* JIT Hot Path Profiling and Detection Infrastructure */
typedef struct VMProfilingStats {
    uint64_t total_instructions;
    uint64_t hot_loop_triggers;
    uint64_t hot_function_triggers;
    uint64_t jit_tierings;
    uint64_t native_executions;
} VMProfilingStats;

extern VMProfilingStats gVMProfiling;

int DetectHotFunction(VM *vm, FnProto *fn);
int DetectHotLoop(VM *vm, FnProto *fn, int offset);

#endif // Vmh
