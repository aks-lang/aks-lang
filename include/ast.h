// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef ASTH
#define ASTH

#include <stdint.h>

/* Visibility modifier constants */
#define VIS_DEFAULT  0  /* unspecified: pub at top-level, private inside module */
#define VIS_PUB      1  /* visible everywhere */
#define VIS_PRIVATE  2  /* visible only within declaring module */
#define VIS_INTERNAL 3  /* visible only within declaring file */

typedef enum {
    NSTR, NIDENT, NBOOL, NINT, NFLOAT, NNULL,
    NBINOP, NUNOP,
    NVARDECL, NCONSTDECL,
    NFNDECL,
    NBLOCK, NPROGRAM,
    NIF, NTERNARY,
    NWHILE, NFOR, NFORIN,
    NRETURN, NPRINT, NEXPRSTMT,
    NASSIGN,
    NCALL, NINDEX, NMEMBER,
    NARRLIT, NMAPLIT,
    NSTRUCTDECL,
    NINC, NDEC,
    NNEW,
    NINPUT, NBREAK, NCONTINUE,
    NIMPORT,
    NTRYCATCH, NTHROW,
    NCOALESCE, NOPTCHAIN, NRANGE,
    NSPREAD,
    NDESTRUCTARR, NDESTRUCTOBJ,
    NSWITCH,
    NENUMDECL,
    NYIELD,
    NMULTIRETURN,
    NAWAIT,
    NSPAWN,
    NMACRODECL,
    NMACROCALL,
    NTEMPLATECALL,
    NMATCH,
    NINTERFACEDECL,
    NTUPLE,
    NTUPLEPAT,
    NSTRUCTPAT,
    NORPAT,
    NTYPEALIAS,
    NTRYPROPAGATE,
    NSELECT,
    NSTRUCTINIT,
    NCHAINEDCMP,
    NMODULE,     /* namespaced module declaration: module name { ... } */
    NUSE,        /* use/re-export declaration: [pub] use path::item; */
    NREF,        /* immutable reference &expr */
    NMUTREF,     /* mutable reference &mut expr */
    NASM,        /* inline assembly statement */
    NCAST,       /* type casting expression: expr as type */
    NNURSERY,    /* structured concurrency nursery: nursery n { ... } */
    NREGION,     /* scoped memory arena region: region name { ... } */
    NARRPAT,     /* array destructuring pattern: [a, b, ..rest] */
    NACTORDECL,  /* actor declaration: actor Name { ... } */
    NEFFECTDECL, /* effect declaration: effect Name { fn Op(args); } */
    NPERFORM,    /* perform effect operation: perform Name.Op(args) */
    NHANDLE,     /* handle expression: handle (expr) with { ... } */
    NRESUME,     /* resume continuation: resume(val) */
    NGPUFN,      /* gpu compute kernel: gpu fn Name(args) { ... } */
    NATOMIC,     /* atomic transactional block: atomic { ... } */
    NDEFER,      /* defer statement: defer stmt */
    NERRDEFER,   /* errdefer statement: errdefer stmt */
    /* Static mode node types  */
    NADT_VARIANT,   /* ADT tagged-union variant access (compile-time discriminant) */
    NUNSAFE,        /* unsafe { ... } block  -  suspends borrow checker */
    NCONST_EVAL,    /* const expression  -  evaluated at compile time */
    NSTATIC_ATTR    /* #![static] file-level attribute */
} NType;

typedef struct Node Node;

struct Node {
    NType type;
    int line;
    int isstack;
    int is_static_safe; /* 1 = proven exclusive non-escaping static ownership, bypass refcount & valisinvalidated checks */
    /* static type annotation  -  set by typer_infer(), NULL until then */
    struct SType *stype;
    union {
        char    *sval;
        int64_t  ival;
        double   fval;
        struct { char *code; } asmstmt;
        struct { Node *expr; int is_mutable; } refnode;
        struct { char *op; Node *left; Node *right; } binop;
        struct { char *name; char *tname; Node *init; Node *isconst; int isconstflag; int visibility; } decl;
        struct {
            char  *name; char *ret;
            char **pnames; char **ptypes; Node **pdefaults;
            int    pc; int isvariadic;
            int    isasync;
            int    isgen;
            int    visibility;  /* VIS_* constant */
            Node  *body;
            char **tparams; int tpc; // template/generic parameters
            char  *effect;
            Node **requires; int req_count;
            Node **ensures; int ens_count;
            char **lifetimes; int lifetime_count;
        } fn;
        struct { Node **stmts; int count; } block;
        struct { Node *cond; Node *then; Node *els; } ifn;
        struct { Node *cond; Node *tval; Node *fval; } ternary;
        struct { Node *cond; Node *body; Node **invariants; int inv_count; } whilen;
        struct { Node *init; Node *cond; Node *post; Node *body; Node **invariants; int inv_count; } forn;
        struct { char *var; Node *iter; Node *body; int isasync; } forin;
        struct { Node *expr; char *var; } single;
        struct { Node *target; Node *value; } assign;
        struct {
            Node *callee; Node **args; int argc; int *isspread;
            char **targs; int tc; // template/generic arguments
        } call;
        struct { Node *arr; Node *idx; } index;
        struct { Node *obj; char *field; } member;
        struct { Node **elems; int count; int *isspread; } arrlit;
        struct { char **keys; Node **vals; int count; } maplit;
        struct {
            char *name; char **fnames; char **ftypes; int fc;
            char **interfaces; int ic;
            char **tparams; int tpc;
            char **constraintparams;
            char **constrainttypes;
            int constraintcount;
            int visibility;  /* VIS_* constant */
        } strct;
        struct { Node *expr; } incdec;
        struct { Node *tryblock; char *errvar; Node *catchblock; Node *finallyblock; } trycatch;
        struct { Node *expr; } throwstmt;
        struct { Node *left; Node *right; } coalesce;
        struct { Node *obj; char *field; Node *idx; } optchain;
        struct { Node *start; Node *end; } range;
        struct { Node *expr; } spread;
        struct { char **names; int count; Node *value; } destructarr;
        struct { char **keys; char **aliases; int count; Node *value; } destructobj;
        struct {
            Node *expr;
            Node **casevals; Node **casebodies; int casecount;
            Node *defaultbody;
        } sw;
        struct {
            char *name; char **members; int64_t *values; int count;
            char ***membertypes; int *membertypecounts; int isadt;
            int visibility;  /* VIS_* constant */
        } enumdecl;
        struct { Node *expr; } yieldstmt;
        struct { Node **exprs; int count; } multiret;
        struct { Node *expr; Node *timeout; } awaitn;
        struct { Node *expr; } spawnn;
        struct { Node *expr; Node **patterns; Node **bodies; Node **guards; int count; } match;
        struct { Node **elems; int count; } tuple;
        struct { Node **patterns; int count; } tuplepat;
        struct { char *name; char **fields; Node **patterns; int count; } structpat;
        struct { Node *left; Node *right; } orpat;
        struct {
            char *name; char **fnames; char **rettypes;
            char ***paramtypes; char ***paramnames; int *paramcounts;
            int count;
            int *hasdefault;
            struct Node **defaultbodies;
            char **assoctypes;
            int assoccount;
        } interfacedecl;
        struct { char *name; char **targs; int tc; } newobj;
        struct {
            char *name;
            char *target;
            int isopaque;
            char **tparams;
            int tpc;
        } typealias;
        struct {
            Node **cases;
            Node **bodies;
            int count;
            Node *defaultbody;
        } selectn;
        struct {
            char *typename;
            Node *spread;
            char **fnames;
            Node **fvals;
            int fc;
        } structinit;
        struct {
            Node **exprs;
            char **ops;
            int count;
        } chainedcmp;
        struct {
            Node *expr;
            char *targettype;
        } cast;
        struct {
            char *name;
            Node *body;
        } nursery;
        struct {
            char *name;
            Node *body;
        } region;
        struct {
            Node **patterns;
            int count;
            int hasrest;
            int restidx;
        } arrpat;
        struct {
            char *name;
            char **fnames;
            char **ftypes;
            Node **fdefaults;
            int fc;
            Node **methods;
            int mc;
            int visibility;
        } actordecl;
        struct {
            char *name;
            char **opnames;
            char ***opparamnames;
            char ***opparamtypes;
            int *opparamcounts;
            int opcount;
        } effectdecl;
        struct {
            char *effectname;
            char *opname;
            Node **args;
            int argcount;
        } performn;
        struct {
            Node *body;
            char **opnames;
            Node **handlers;
            int count;
        } handlen;
        struct {
            Node *expr;
        } resumen;
        struct {
            Node *body;
        } atomicn;
    };
};

Node *NodeNew(NType t, int line);
void  NodeFree(Node *n);
Node *NodeOpt(Node *n);
Node *NodeCseOpt(Node *n);
Node *NodeInlineOpt(Node *n);
Node *NodeDceOpt(Node *n);
void  NodeEscapeAnalysis(Node *n);
Node *NodeCopy(Node *n);
void  ASTSubstitute(Node **nodeptr, char **pnames, Node **args, int pc);
Node *ASTMacroExpand(Node *body, char **pnames, Node **args, int pc);

#endif // Asth
