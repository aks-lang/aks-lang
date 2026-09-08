// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef COMPH
#define COMPH

#include <stdint.h>

/* Static Type System: Dual-tier architecture.
 * Static mode uses concrete native types (i64, double, i1)
 * instead of the NaN-boxed AKSValue tag-dispatch system. */

typedef enum {
    STYPE_UNKNOWN = 0,
    STYPE_VOID,
    STYPE_BOOL,
    STYPE_INT,       /* i64  -  auto-copy */
    STYPE_FLOAT,     /* f64  -  auto-copy */
    STYPE_STR,       /* owned *AKSString */
    STYPE_PTR,       /* raw *T (only inside unsafe{}) */
    STYPE_ARRAY,     /* [T; N] (len>=0 fixed) or []T (len=-1 dynamic) */
    STYPE_STRUCT,    /* named struct */
    STYPE_FN,        /* fn(T...) -> R */
    STYPE_ENUM,      /* tagged-union ADT */
    STYPE_GENERIC,   /* unresolved T before monomorphization */
    STYPE_REF,       /* &T   -  immutable reference */
    STYPE_MUTREF,    /* &mut T  -  mutable reference */
    STYPE_TUPLE,     /* (T1, T2, ...) */
    STYPE_RESULT,    /* Result<T, E> */
    STYPE_OPTION,    /* Option<T> */
    STYPE_SIMD,      /* f32x4 / i32x8 etc */
    STYPE_NEVER,     /* !  -  diverging (return from panic/loop-forever) */
} STypeKind;

typedef struct SType SType;
struct SType {
    STypeKind   kind;
    char        name[128];    /* canonical name for struct/enum/generic */
    SType      *inner;        /* element type: array, ref, Option<T>, Result<T,?> */
    SType      *inner2;       /* error type for Result<?,E> */
    int         array_len;    /* -1 = dynamic, N >= 0 = fixed */
    SType     **params;       /* fn param types / tuple elems */
    int         param_count;
    SType      *ret;          /* fn return type */
    int         is_copy;      /* 1 = auto-copy (int, float, bool, Copy structs) */
    int         is_const;     /* 1 = const fn (foldable at compile time) */
    int         is_unsafe;    /* 1 = fn declared unsafe */
    /* Intern identity: two SType* with same kind+name are pointer-equal after interning */
    struct SType *next_intern;  /* GC list: links ALL allocated SType objects for cleanup */
    struct SType *next_bucket;  /* bucket chain: links types in the SAME hash bucket only */
};

/* Static-mode local variable (parallel to dynamic-mode Local) */
typedef struct {
    char    name[128];
    int     slot;
    SType  *stype;
    int     movestate;    /* reuse MoveState enum values */
    int     is_mutable;
    int     scope_depth;
    int     is_borrowed;      /* 1 = currently immutably borrowed */
    int     is_mut_borrowed;  /* 1 = currently mutably borrowed */
    int     lifetime_id;      /* -1 = no explicit lifetime */
} StaticLocal;

/* Lifetime region for static borrow analysis */
typedef struct {
    int     id;
    int     scope_depth;     /* depth at which this lifetime begins */
    int     is_outlives[32]; /* id-indexed: this lifetime outlives [id] */
    int     outlives_count;
} Lifetime;

/* Type intern table  -  canonical SType* allocation */
#define STYPE_INTERN_BUCKETS 256


#define MAXLOCALS 2048
#define MAXFNS 65536
#define AKSMAGIC 0x534B4100   // "aks\0" in hex (big-endian/little-endian matching)
#define AKSVERSION 3

typedef struct {
    uint8_t *code;
    int *lines;
    int count;
    int cap;
    int *globalcache;
    int *loopcounters;
    int *fieldcache;
    uint8_t *executed;
} Chunk;

typedef struct {
    char name[128];
    int is_reference;      /* 1 = &T or &mut T */
    int is_mutable_ref;    /* 1 = &mut T, 0 = &T */
    char element_type[128];
} TypeInfo;

typedef struct {
    char borrower[128];
    char target[128];
    int scope_depth;
    int is_mutable;
    int line;
} BorrowTracker;

typedef enum {
    MOVE_STATE_VALID = 0,
    MOVE_STATE_MOVED = 1,
    MOVE_STATE_MAYBE_MOVED = 2
} MoveState;

#define MOVED MOVE_STATE_MOVED
#define MAYBE_MOVED MOVE_STATE_MAYBE_MOVED

typedef struct {
    char name[128];
    int slot;
    int depth;
    int isconst;
    char typename[64];
    int ismoved;
    int is_mutable_binding; /* 1 = let mut x, 0 = let x */
    MoveState movestate;
} Local;

typedef struct {
    char name[128];
    int id;
    int paramcount;
    int localcount;
    int scopedepth;
    Local locals[MAXLOCALS];
    Chunk chunk;
    int isasync;
    char rettype[64];
    char paramtypes[32][64];
    void *regcode;
    int regcount;
    int maxregisters;
    int *offsettomap;
    struct Node *ast;
    char file_name[256];
    int is_lazy_stub;   /* 1 = fn declared but body not yet compiled */
} FnProto;

typedef struct {
    char name[128];
    char **fnames;
    char **ftypes;
    int fc;
    char **interfaces;
    int ic;
} CompilerStruct;

typedef struct {
    char name[128];
    char **fnames;
    char **rettypes;
    char ***paramtypes;
    char ***paramnames;
    int *paramcounts;
    int count;
    int *hasdefault;
    struct Node **defaultbodies;
    char **assoctypes;
    int assoccount;
} CompilerInterface;

typedef struct {
    char name[128];
    char **members;
    int64_t *values;
    int count;
    int isadt;
} CompilerEnum;

typedef struct {
    char name[128];
    struct Node *body;
    char **pnames;
    int pc;
} MacroDecl;

typedef struct {
    char name[128];
    struct Node *ast;
    char **constraintparams;
    char **constrainttypes;
    int constraintcount;
} TemplateDecl;

typedef struct {
    char name[128];
    char target[256];
    int isopaque;
    char **tparams;
    int tpc;
} CompilerTypeAlias;

typedef struct {
    char name[256];
    char target[256];   /* e.g. "math::add" */
    int  visibility;    /* VIS_* constant */
    char source_file[512];
} UseAlias;

typedef struct {
    char name[256];
    int  visibility;    /* VIS_* constant */
    char source_file[512];
    char namespace_[256]; /* namespace where declared */
} CompilerSymbol;

typedef struct Module {
    char name[256];
    char path[512];
    struct Node *ast;
    CompilerSymbol symbols[256];
    int symbol_count;
    char dependencies[32][256];
    int dep_count;
    int is_loaded;
} Module;

#define STRPOOL_BUCKETS 16384

typedef struct StrPoolEntry {
    char *str;
    int   idx;
    struct StrPoolEntry *next;
} StrPoolEntry;

typedef struct {
    char **strpool;
    int strcount;
    int strcap;
    StrPoolEntry *str_hash[STRPOOL_BUCKETS];
    Module modules[256];
    int module_count;
    FnProto *fns[MAXFNS];
    int fncount;
    FnProto *current;
    int haderror;
    int error_count;
    int isrepl;
    int iseval;
    MacroDecl macros[512];
    int macrocount;
    TemplateDecl templates[512];
    int templatecount;
    CompilerStruct structs[512];
    int structcount;
    CompilerInterface interfaces[512];
    int interfacecount;
    CompilerEnum enums[512];
    int enumcount;
    CompilerTypeAlias aliases[512];
    int aliascount;
    
    int loopstarts[128];
    int loopbreaks[128][128];
    int loopbreakcounts[128];
    int loopcontinues[128][128];
    int loopcontinuecounts[128];
    int loopscopes[128];
    int loopdepth;
    int trydepth;
    int is_type_checking_pass;
    char currentfilepath[512];

    /* Module system fields */
    char current_namespace[512];          /* active namespace prefix, e.g. "math" */
    char active_imports[64][512];         /* import stack for circular detection */
    int  import_depth;                    /* depth of active_imports stack */
    char compiled_files[256][512];        /* files already compiled (deduplication) */
    int  compiled_file_count;             /* count of compiled_files */
    CompilerSymbol mod_symbols[4096];     /* visibility symbol table */
    int mod_symbol_count;
    UseAlias use_aliases[512];            /* use/re-export alias table */
    int use_alias_count;
    struct Node *imported_asts[1024];     /* ASTs kept alive for analysis */
    int imported_ast_count;

    /* Lazy module loading */
    struct {
        char path[512];         /* resolved file path */
        struct Node *ast;       /* parsed AST (kept until first use) */
        char savedpath[512];    /* compiler filepath at registration time */
        int loaded;             /* 0 = stub only, 1 = fully compiled */
    } lazy_mods[64];
    int lazy_mod_count;

    /* Static mode  */
    int is_static_mode;             /* 1 = compiling in static tier */
    int is_unsafe_block;            /* 1 = inside unsafe{} block */
    int is_const_fn;                /* 1 = inside const fn (compile-time eval) */
    /* Static local variable table */
    StaticLocal static_locals[MAXLOCALS];
    int static_local_count;
    /* Lifetime regions */
    Lifetime lifetimes[64];
    int lifetime_count;
    int next_lifetime_id;
    /* SType intern table & all-types allocation list */
    SType *stype_intern[STYPE_INTERN_BUCKETS];
    SType *all_stypes;             /* linked list of all SType objects for cleanup */
    /* Monomorphization cache: template_name<T1,T2,...> -> FnProto* */
    struct {
        char key[256];        /* "fn_name<concrete_types>" */
        int  fnid;            /* compiled FnProto index */
    } mono_cache[1024];
    int mono_cache_count;
    /* Current function's return SType for static codegen */
    SType *current_ret_stype;
} Compiler;

struct Node; // forward declare Node from ast.h

Compiler *CompNew(void);
void CompFree(Compiler *c);
int AddStr(Compiler *c, const char *s);
int CompCompile(Compiler *c, struct Node *program);
int CompAot(Compiler *c, const char *outputpath);
int CompWrt(Compiler *c, const char *path);

void ChunkWrite(Chunk *ch, uint8_t byte, int line);
void ChunkWriteU16(Chunk *ch, uint16_t v, int line);
void ChunkWriteI32(Chunk *ch, int32_t v, int line);
void ChunkWriteI64(Chunk *ch, int64_t v, int line);
void ChunkWriteF64(Chunk *ch, double v, int line);

// Memory safety configuration variables
extern int gNoBoundsCheck;
extern int gStrictOvf;
extern int gTypeCheckingEnabled;
extern int gBorrowCheckerEnabled;
extern int gImmutableByDefault;
extern int gStrictTypes;
extern int gIncrementalTyping;
extern int gParallelCompilation;

/* Static mode global  -  when 1, borrow/type enforcement is always-on */
extern int gStaticMode;
/* Debug build flag  -  when 0, bounds/overflow checks elided in static mode */
extern int gDebugBuild;

void NodeFlowTypeCheck(Compiler *c, struct Node *program);

/* SType API */
SType *StypeInternGet(Compiler *c, STypeKind kind, const char *name);
SType *StypeMakeArray(Compiler *c, SType *elem, int len);
SType *StypeMakeRef(Compiler *c, SType *inner, int is_mut);
SType *StypeMakeFn(Compiler *c, SType **params, int pc, SType *ret);
SType *StypeMakeTuple(Compiler *c, SType **elems, int count);
SType *StypeMakeOption(Compiler *c, SType *inner);
SType *StypeMakeResult(Compiler *c, SType *ok, SType *err);
int    StypeIsCopy(SType *t);
int    StypeEquals(SType *a, SType *b);
const char *StypeName(SType *t, char *buf, int buflen);
void   StypeFreeAll(Compiler *c);

/* Typer (HM type inference) */
int  TyperRun(Compiler *c, struct Node *program);
SType *TyperInfer(Compiler *c, struct Node *n);
SType *TyperLookupVar(Compiler *c, const char *name);

/* Static Codegen  */
int StaticCodeGenEmit(Compiler *c, struct Node *program, const char *outpath);

/* Enforced static borrow check  */
void StaticBorrowCheck(Compiler *c, struct Node *fn_node);
void StaticLifetimeInfer(Compiler *c, struct Node *fn_node);

/* Module topological sort for dependency resolution */
int CompilerSortModulesTopological(Compiler *c, int *out_order, int *out_count);
int HasCircularDependency(Module *m, const char *dep_name);

/* Parallel module compilation across worker threads */
int CompilerParallelCompileModules(Compiler *c, const char **module_paths, int count);
void CompilerParallelOptimizeBytecode(Compiler *c);

/* Diagnostic string distance and suggestions */
int CompLevenshteinDistance(const char *s1, const char *s2);
const char *CompSuggestFunction(Compiler *c, const char *name);
const char *CompSuggestLocal(Compiler *c, const char *name);

/* Incremental Type Checking & Function Dependency Graph */
typedef struct FunctionDependency {
    char from[128]; /* caller function name */
    char to[128];   /* callee function name */
} FunctionDependency;

typedef struct FileInfo {
    char path[512];
    uint64_t mtime;
    uint32_t hash;
} FileInfo;

typedef struct FunctionDeps {
    char name[128];
    char callers[32][128];
    int caller_count;
    char callees[32][128];
    int callee_count;
} FunctionDeps;

typedef struct CachedSignature {
    char function_name[128];
    SType *return_type;
    SType *param_types[32];
    int param_count;
    uint64_t body_hash;
    uint64_t file_mtime;
    int is_valid;
} CachedSignature;

int CompilerCheckIncremental(Compiler *c, const char *filepath);
void CompilerTrackFunctionDependency(Compiler *c, const char *caller, const char *callee);

#endif // Comph
