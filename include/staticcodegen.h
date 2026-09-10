// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Static LLVM IR code generator context definitions.
#ifndef STATICCODEGENH
#define STATICCODEGENH

#include <stdio.h>
#include <stdint.h>
#include "compiler.h"
#include "ast.h"

/* Global static mode flags (defined in staticcodegen.c) */
extern int gStaticMode;
extern int gDebugBuild;

/* Static codegen context */
typedef struct SCGCtx {
    Compiler  *comp;
    FILE      *out;          /* output .ll file */
    int        tmp;          /* SSA value counter */
    int        label;        /* basic block label counter */
    int        had_error;
    int        debug_mode;   /* 1 = emit debug checks */
    char       cur_fn[128];
    /* Local SSA slot map: variable name -> slot index */
    char       slot_names[MAXLOCALS][64];
    int        slot_types[MAXLOCALS]; /* STypeKind */
    int        slot_count;
    /* Async state machine */
    int        resume_count;
    int        in_async;
    
    /* Loop vectorization metadata */
    int        loop_meta_id;           /* Counter for loop !meta IDs */
    char       loop_metas[128][256];   /* Buffered metadata source strings */
    int        loop_meta_count;
    
    /* Pure-function detection for readnone/readonly attributes */
    int        fn_has_ptr_param;       /* 1 if function takes any pointer, reference, or array argument */
    int        fn_has_global_write;    /* 1 if function writes to a global variable */
    
    /* Dead-branch elimination */
    int        suppress_dead_branch;   /* 1 if constant-folded condition is known false */
    
    /* Scalar parameter register promotion */
    int        is_direct[MAXLOCALS];      /* 1 if slot is a scalar argument kept purely in register */
    int        direct_argidx[MAXLOCALS];  /* Argument index %argN for direct slot loading */

    /* Multi-architecture target configuration */
    const char *target_triple;
    int        lto;
    const char *pgo_profile;
    const char *pgo_use;
} SCGCtx;

/* Public API declarations */
int StaticCodeGenEmit(Compiler *c, Node *program, const char *outpath);
int StaticCodeGenEmitTarget(Compiler *c, Node *program, const char *outpath, const char *target_triple, int lto, const char *pgo_profile, const char *pgo_use);
void StaticBorrowCheck(Compiler *c, Node *fn_node);
void StaticLifetimeInfer(Compiler *c, Node *fn_node);

#endif // Staticcodegenh
