// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Bidirectional Hindley-Milner type inference for static mode.
//
// The typer walks the AST *before* bytecode emission. It annotates every Node
// with a SType* (n->stype) representing its concrete, compile-time type.
// Generic functions are monomorphized per concrete call-site type.
#ifndef TYPERH
#define TYPERH

#include "compiler.h"
#include "ast.h"

/* TypeEnv  -  lexically scoped variable type environment */
#define TYPEENV_MAX_VARS 512

typedef struct TypeEnvEntry {
    char name[128];
    SType *stype;
    int is_region_scoped;
    struct TypeEnvEntry *next; /* hash chain */
} TypeEnvEntry;

typedef struct TypeEnv {
    TypeEnvEntry *buckets[64]; /* small hash table */
    struct TypeEnv *parent;    /* enclosing scope */
} TypeEnv;

/* TypeVar  -  unification variable for HM inference */
typedef struct TypeVar {
    int id;
    SType *resolved; /* NULL = unbound, non-NULL = resolved to this type */
    struct TypeVar *next; /* linked list of all active typevars */
} TypeVar;

/* Maximum recursion depth allowed during type inference */
#define TYPER_MAX_RECURSION_DEPTH 512

/* TyperCtx  -  per-compilation type inference context */
typedef struct {
    Compiler   *comp;
    TypeEnv    *env;           /* current scope environment */
    TypeVar    *typevars;      /* all unification variables */
    int         next_tv_id;
    int         had_error;
    int         error_count;
    int         recursion_depth;
    /* Current function return type (for checking return stmts) */
    SType      *current_fn_ret;
    /* Const-folding: non-NULL if inside a const fn */
    int         in_const_fn;
    /* Unsafe context: non-NULL if inside unsafe{} */
    int         in_unsafe;
    /* Region depth for affine lifetime escape checks */
    int         region_depth;
    /* Monomorphization stack to detect recursion */
    char        mono_stack[32][256];
    int         mono_depth;
    /* Incremental dependency tracking */
    int         collecting_deps;
    char        current_deps[32][128];
    int         dep_count;
} TyperCtx;

/* Public API (implemented in typer.c) */

/* Run full type inference over a program AST.
   Annotates all nodes with n->stype.
   Returns 1 on success, 0 if type errors found. */
int TyperRun(Compiler *c, Node *program);

/* Infer the type of a single expression node.
   Sets n->stype and returns it. */
SType *TyperInfer(Compiler *c, Node *n);

/* Check that node n has type 'expected'.
   Reports error if mismatch. */
void TyperCheck(Compiler *c, Node *n, SType *expected);

/* Look up variable type in the current scope chain */
SType *TyperLookupVar(Compiler *c, const char *name);

/* Unify two types. Returns the unified type or NULL on mismatch. */
SType *TyperUnify(Compiler *c, SType *a, SType *b, int line);

/* Monomorphize a generic FnProto with concrete argument types.
   Returns the id of the new specialized FnProto, or -1 on error. */
int TyperMono(Compiler *c, Node *fn_node, SType **arg_types, int argc, int call_line);

/* Resolve a type name string ("int","float","bool","str", etc.) to SType* */
SType *TyperResolveName(Compiler *c, const char *typename_str);

/* Const-fold a node if it is a compile-time constant.
   Returns 1 if folded (n->type changed to NINT/NFLOAT/NBOOL), 0 otherwise. */
int TyperConstFold(Compiler *c, Node *n);

/* Incremental Type Checking & Signature Caching */
typedef struct FnSigEntry {
    char        name[128];
    uint64_t    body_hash;
    SType      *ret_type;
    SType      *param_types[32];
    int         param_count;
    char        deps[32][128];
    int         dep_count;
    int         is_valid;
} FnSigEntry;

typedef struct FnSigCache {
    FnSigEntry  entries[1024];
    int         count;
} FnSigCache;

extern FnSigCache gFnSigCache;

/* Compute FNV-1a hash of an AST node tree */
uint64_t TyperComputeNodeHash(Node *n);

/* Lookup cached signature for function */
FnSigEntry *TyperLookupSig(const char *name);

/* Cache or update signature for function */
void TyperStoreSig(const char *name, uint64_t body_hash, SType *ret_type,
                   SType **param_types, int param_count,
                   char deps[32][128], int dep_count);

/* Check if cached signature is still valid (its dependencies have not changed) */
int TyperIsSigValid(FnSigEntry *sig);

/* Persist signature cache to disk */
void TyperSaveSigCache(Compiler *c, const char *cache_path);

/* Load signature cache from disk */
void TyperLoadSigCache(Compiler *c, const char *cache_path);

#endif // TYPERH
