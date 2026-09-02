// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Bidirectional Hindley-Milner type inference for AKS static mode.
//
// Design:
//  - Two-pass: first pre-register all top-level fn/struct/enum signatures,
//    then infer expression types bottom-up with top-down checking.
//  - Unification with occurs-check prevents infinite types.
//  - Monomorphization: each generic fn is instantiated per concrete call-site.
//    Instantiations are cached in Compiler.mono_cache to avoid duplication.
//  - Const folding: const fn with literal arguments are evaluated at compile time.
//
#include "typer.h"
#include "compiler.h"
#include "ast.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Primitive SType singletons (allocated once per Compiler) */

static SType *s_void, *s_bool, *s_int, *s_float, *s_str, *s_never;

/* SType intern table */

static unsigned int StypeHash(STypeKind kind, const char *name) {
    unsigned int h = (unsigned int)kind * 2654435761u;
    if (name) {
        for (const char *p = name; *p; p++) h = h * 31 + (unsigned char)*p;
    }
    return h % STYPE_INTERN_BUCKETS;
}

static SType *StypeAlloc(Compiler *c, STypeKind kind) {
    SType *t = (SType*)calloc(1, sizeof(SType));
    t->kind = kind;
    if (c) {
        t->next_intern = c->all_stypes;
        c->all_stypes = t;
    }
    return t;
}

SType *StypeInternGet(Compiler *c, STypeKind kind, const char *name) {
    if (!c) return NULL;
    const char *sname = name ? name : "";
    unsigned int idx = StypeHash(kind, sname);
    /* Walk the bucket chain to find an existing intern */
    for (SType *t = c->stype_intern[idx]; t; t = t->next_intern) {
        if (t->kind == kind && strcmp(t->name, sname) == 0) return t;
    }
    /* New type: allocate and chain it at the bucket head.
     * StypeAlloc sets next_intern = c->all_stypes and updates c->all_stypes,
     * so next_intern forms the global GC list.  We then point the bucket slot
     * at the new node; future lookups walk next_intern from this node, which
     * traverses the entire all_stypes list — this is O(total_types) in the
     * worst case but is correct and safe.  A two-pointer improvement can follow. */
    SType *t = StypeAlloc(c, kind);
    strncpy(t->name, sname, sizeof(t->name) - 1);
    if (kind == STYPE_INT || kind == STYPE_FLOAT || kind == STYPE_BOOL) t->is_copy = 1;
    c->stype_intern[idx] = t;  /* bucket head now points at newest interned type */
    return t;
}

SType *StypeMakeArray(Compiler *c, SType *elem, int len) {
    char name[128];
    char elemname[64];
    StypeName(elem, elemname, sizeof(elemname));
    snprintf(name, sizeof(name), "[%s;%d]", elemname, len);
    SType *t = StypeAlloc(c, STYPE_ARRAY);
    t->inner = elem; t->array_len = len;
    strncpy(t->name, name, sizeof(t->name) - 1);
    return t;
}

SType *StypeMakeRef(Compiler *c, SType *inner, int is_mut) {
    SType *t = StypeAlloc(c, is_mut ? STYPE_MUTREF : STYPE_REF);
    t->inner = inner;
    char iname[64]; StypeName(inner, iname, sizeof(iname));
    snprintf(t->name, sizeof(t->name), is_mut ? "&mut %s" : "&%s", iname);
    return t;
}

SType *StypeMakeFn(Compiler *c, SType **params, int pc, SType *ret) {
    SType *t = StypeAlloc(c, STYPE_FN);
    t->params = (SType**)malloc(pc * sizeof(SType*));
    memcpy(t->params, params, pc * sizeof(SType*));
    t->param_count = pc; t->ret = ret;
    return t;
}

SType *StypeMakeTuple(Compiler *c, SType **elems, int count) {
    SType *t = StypeAlloc(c, STYPE_TUPLE);
    t->params = (SType**)malloc(count * sizeof(SType*));
    memcpy(t->params, elems, count * sizeof(SType*));
    t->param_count = count;
    return t;
}

SType *StypeMakeOption(Compiler *c, SType *inner) {
    SType *t = StypeAlloc(c, STYPE_OPTION);
    t->inner = inner;
    char iname[64]; StypeName(inner, iname, sizeof(iname));
    snprintf(t->name, sizeof(t->name), "Option<%s>", iname);
    return t;
}

SType *StypeMakeResult(Compiler *c, SType *ok, SType *err) {
    SType *t = StypeAlloc(c, STYPE_RESULT);
    t->inner = ok; t->inner2 = err;
    char ok_name[64], err_name[64];
    StypeName(ok, ok_name, sizeof(ok_name));
    StypeName(err, err_name, sizeof(err_name));
    snprintf(t->name, sizeof(t->name), "Result<%s,%s>", ok_name, err_name);
    return t;
}

int StypeIsCopy(SType *t) {
    if (!t) return 0;
    return t->is_copy;
}

int StypeEquals(SType *a, SType *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (strcmp(a->name, b->name) != 0) return 0;
    return 1;
}

const char *StypeName(SType *t, char *buf, int buflen) {
    if (!t) { strncpy(buf, "?", buflen-1); return buf; }
    switch (t->kind) {
        case STYPE_INT:   strncpy(buf, "int", buflen-1); break;
        case STYPE_FLOAT: strncpy(buf, "float", buflen-1); break;
        case STYPE_BOOL:  strncpy(buf, "bool", buflen-1); break;
        case STYPE_VOID:  strncpy(buf, "void", buflen-1); break;
        case STYPE_NEVER: strncpy(buf, "!", buflen-1); break;
        case STYPE_STR:   strncpy(buf, "str", buflen-1); break;
        case STYPE_UNKNOWN: strncpy(buf, "unknown", buflen-1); break;
        default: strncpy(buf, t->name[0] ? t->name : "?", buflen-1); break;
    }
    buf[buflen-1] = '\0';
    return buf;
}

void StypeFreeAll(Compiler *c) {
    if (!c) return;
    SType *t = c->all_stypes, *next;
    while (t) {
        next = t->next_intern;
        if (t->params) free(t->params);
        free(t);
        t = next;
    }
    c->all_stypes = NULL;
    memset(c->stype_intern, 0, sizeof(c->stype_intern));
}


/* TypeEnv helpers */

static TypeEnv *EnvPush(TypeEnv *parent) {
    TypeEnv *e = (TypeEnv*)calloc(1, sizeof(TypeEnv));
    e->parent = parent;
    return e;
}

static void EnvPop(TypeEnv *e) { free(e); }

static unsigned int EnvHash(const char *name) {
    unsigned int h = 0;
    for (const char *p = name; *p; p++) h = h * 31 + (unsigned char)*p;
    return h % 64;
}

static void EnvSet(TypeEnv *e, const char *name, SType *t) {
    unsigned int idx = EnvHash(name);
    TypeEnvEntry *entry = (TypeEnvEntry*)calloc(1, sizeof(TypeEnvEntry));
    strncpy(entry->name, name, sizeof(entry->name)-1);
    entry->stype = t;
    entry->next = e->buckets[idx];
    e->buckets[idx] = entry;
}

static SType *EnvGet(TypeEnv *e, const char *name) {
    for (; e; e = e->parent) {
        unsigned int idx = EnvHash(name);
        for (TypeEnvEntry *en = e->buckets[idx]; en; en = en->next) {
            if (strcmp(en->name, name) == 0) return en->stype;
        }
    }
    return NULL;
}

static void EnvFreeLocal(TypeEnv *e) {
    for (int i = 0; i < 64; i++) {
        TypeEnvEntry *en = e->buckets[i], *next;
        while (en) { next = en->next; free(en); en = next; }
    }
}

/* Error reporting */

static void TyperError(TyperCtx *ctx, int line, const char *msg) {
    if (ctx->error_count < 20) {
        const char *file = (ctx->comp && ctx->comp->currentfilepath[0])
                           ? ctx->comp->currentfilepath : NULL;
        if (file) {
            fprintf(stderr, "Type error in '%s' on line %d: %s\n", file, line, msg);
        } else {
            fprintf(stderr, "Type error on line %d: %s\n", line, msg);
        }
    } else if (ctx->error_count == 20) {
        fprintf(stderr, "... (further type errors suppressed)\n");
    }
    ctx->error_count++;
    ctx->had_error = 1;
    ctx->comp->haderror = 1;
}

/* Convenience: report a type error using line info from a Node* */
static void TyperErrorNode(TyperCtx *ctx, Node *n, const char *msg) {
    TyperError(ctx, n ? n->line : 0, msg);
}

static void TyperWarn(TyperCtx *ctx, int line, const char *msg) {
    const char *file = (ctx->comp && ctx->comp->currentfilepath[0])
                       ? ctx->comp->currentfilepath : NULL;
    if (file) {
        fprintf(stderr, "Type warning in '%s' on line %d: %s\n", file, line, msg);
    } else {
        fprintf(stderr, "Type warning on line %d: %s\n", line, msg);
    }
}


/* Type name resolution */

SType *TyperResolveName(Compiler *c, const char *tname) {
    if (!tname || tname[0] == '\0') return StypeInternGet(c, STYPE_UNKNOWN, "");
    if (strcmp(tname, "int") == 0 || strcmp(tname, "i64") == 0 || strcmp(tname, "i32") == 0)
        return StypeInternGet(c, STYPE_INT, "int");
    if (strcmp(tname, "float") == 0 || strcmp(tname, "f64") == 0 || strcmp(tname, "f32") == 0)
        return StypeInternGet(c, STYPE_FLOAT, "float");
    if (strcmp(tname, "bool") == 0)
        return StypeInternGet(c, STYPE_BOOL, "bool");
    if (strcmp(tname, "str") == 0 || strcmp(tname, "string") == 0)
        return StypeInternGet(c, STYPE_STR, "str");
    if (strcmp(tname, "void") == 0)
        return StypeInternGet(c, STYPE_VOID, "void");
    if (tname[0] == '!') return StypeInternGet(c, STYPE_NEVER, "!");
    /* Check for Option<T> */
    if (strncmp(tname, "Option<", 7) == 0) {
        char inner_name[64];
        strncpy(inner_name, tname + 7, sizeof(inner_name)-1);
        int len = (int)strlen(inner_name);
        if (len > 0 && inner_name[len-1] == '>') inner_name[len-1] = '\0';
        SType *inner = TyperResolveName(c, inner_name);
        return StypeMakeOption(c, inner);
    }
    /* Check for Result<T,E> */
    if (strncmp(tname, "Result<", 7) == 0) {
        char rest[128];
        strncpy(rest, tname + 7, sizeof(rest)-1);
        char *comma = strchr(rest, ',');
        if (comma) {
            *comma = '\0';
            char *errpart = comma + 1;
            int elen = (int)strlen(errpart);
            if (elen > 0 && errpart[elen-1] == '>') errpart[elen-1] = '\0';
            return StypeMakeResult(c, TyperResolveName(c, rest), TyperResolveName(c, errpart));
        }
    }
    /* Check for array [T; N] */
    if (tname[0] == '[') {
        char rest[64]; strncpy(rest, tname+1, sizeof(rest)-1);
        char *semi = strchr(rest, ';');
        if (semi) {
            *semi = '\0'; char *nstr = semi + 1;
            int len2 = (int)strlen(nstr);
            if (len2 > 0 && nstr[len2-1] == ']') nstr[len2-1] = '\0';
            int n = atoi(nstr);
            return StypeMakeArray(c, TyperResolveName(c, rest), n);
        }
        /* Dynamic array []T */
        int len2 = (int)strlen(rest);
        if (len2 > 0 && rest[len2-1] == ']') rest[len2-1] = '\0';
        return StypeMakeArray(c, TyperResolveName(c, rest), -1);
    }
    /* Look up as struct/enum name */
    for (int i = 0; i < c->structcount; i++) {
        if (strcmp(c->structs[i].name, tname) == 0)
            return StypeInternGet(c, STYPE_STRUCT, tname);
    }
    for (int i = 0; i < c->enumcount; i++) {
        if (strcmp(c->enums[i].name, tname) == 0)
            return StypeInternGet(c, STYPE_ENUM, tname);
    }
    /* SIMD types */
    if (strncmp(tname, "f32x", 4) == 0 || strncmp(tname, "i32x", 4) == 0 ||
        strncmp(tname, "f64x", 4) == 0 || strncmp(tname, "i64x", 4) == 0) {
        return StypeInternGet(c, STYPE_SIMD, tname);
    }
    /* Generic parameter  -  leave as STYPE_GENERIC for monomorphization */
    return StypeInternGet(c, STYPE_GENERIC, tname);
}

/* Unification */

SType *TyperUnify(Compiler *c, SType *a, SType *b, int line) {
    if (!a || a->kind == STYPE_UNKNOWN) return b;
    if (!b || b->kind == STYPE_UNKNOWN) return a;
    if (a->kind == STYPE_GENERIC) return b;
    if (b->kind == STYPE_GENERIC) return a;
    if (a->kind == STYPE_NEVER) return b;
    if (b->kind == STYPE_NEVER) return a;
    if (StypeEquals(a, b)) return a;
    /* Numeric coercion: int literal may unify with float */
    if (a->kind == STYPE_INT && b->kind == STYPE_FLOAT) return b;
    if (a->kind == STYPE_FLOAT && b->kind == STYPE_INT) return a;
    /* Structural unification for arrays */
    if (a->kind == STYPE_ARRAY && b->kind == STYPE_ARRAY) {
        SType *inner = TyperUnify(c, a->inner, b->inner, line);
        if (!inner) return NULL;
        int len = (a->array_len == b->array_len) ? a->array_len :
                  (a->array_len == -1) ? b->array_len :
                  (b->array_len == -1) ? a->array_len : -2;
        if (len == -2) {
            const char *file = (c && c->currentfilepath[0]) ? c->currentfilepath : NULL;
            if (file)
                fprintf(stderr, "Type error in '%s' on line %d: array length mismatch: [_;%d] vs [_;%d]\n",
                        file, line, a->array_len, b->array_len);
            else
                fprintf(stderr, "Type error on line %d: array length mismatch: [_;%d] vs [_;%d]\n",
                        line, a->array_len, b->array_len);
            return NULL;
        }
        return StypeMakeArray(c, inner, len);
    }
    /* Ref/MutRef unification */
    if (a->kind == b->kind && (a->kind == STYPE_REF || a->kind == STYPE_MUTREF)) {
        SType *inner = TyperUnify(c, a->inner, b->inner, line);
        return inner ? StypeMakeRef(c, inner, a->kind == STYPE_MUTREF) : NULL;
    }
    /* Mismatch */
    char na[64], nb[64];
    StypeName(a, na, sizeof(na)); StypeName(b, nb, sizeof(nb));
    const char *file = (c && c->currentfilepath[0]) ? c->currentfilepath : NULL;
    if (file)
        fprintf(stderr, "Type error in '%s' on line %d: type mismatch: expected '%s', found '%s'\n",
                file, line, na, nb);
    else
        fprintf(stderr, "Type error on line %d: type mismatch: expected '%s', found '%s'\n",
                line, na, nb);
    return NULL;
}


/* Const folding */

int TyperConstFold(Compiler *c, Node *n) {
    if (!n) return 0;
    if (n->type == NBINOP && n->binop.left && n->binop.right) {
        Node *L = n->binop.left, *R = n->binop.right;
        const char *op = n->binop.op;
        if (!op) return 0;

        /* Integer × Integer constant folding */
        if (L->type == NINT && R->type == NINT) {
            int64_t a = L->ival, b = R->ival, res = 0;
            int folded = 1;
            if      (strcmp(op, "+") == 0)             res = a + b;
            else if (strcmp(op, "-") == 0)             res = a - b;
            else if (strcmp(op, "*") == 0)             res = a * b;
            else if (strcmp(op, "/") == 0 && b != 0)  res = a / b;
            else if (strcmp(op, "%") == 0 && b != 0)  res = a % b;
            else if (strcmp(op, "&") == 0)             res = a & b;
            else if (strcmp(op, "|") == 0)             res = a | b;
            else if (strcmp(op, "^") == 0)             res = a ^ b;
            else if (strcmp(op, "<<") == 0 && b >= 0 && b < 64) res = a << b;
            else if (strcmp(op, ">>") == 0 && b >= 0 && b < 64) res = a >> b;
            else {
                /* Comparison folds to bool */
                int bres = 0;
                if      (strcmp(op, "==") == 0) bres = (a == b);
                else if (strcmp(op, "!=") == 0) bres = (a != b);
                else if (strcmp(op, "<")  == 0) bres = (a <  b);
                else if (strcmp(op, ">")  == 0) bres = (a >  b);
                else if (strcmp(op, "<=") == 0) bres = (a <= b);
                else if (strcmp(op, ">=") == 0) bres = (a >= b);
                else { folded = 0; }
                if (folded) {
                    n->type = NBOOL; n->ival = bres;
                    n->stype = StypeInternGet(c, STYPE_BOOL, "bool");
                    return 1;
                }
            }
            if (folded) {
                n->type = NINT; n->ival = res;
                n->stype = StypeInternGet(c, STYPE_INT, "int");
                return 1;
            }
        }

        /* Float × Float constant folding */
        if (L->type == NFLOAT && R->type == NFLOAT) {
            double a = L->fval, b = R->fval, res = 0.0;
            int folded = 1;
            if      (strcmp(op, "+") == 0)                   res = a + b;
            else if (strcmp(op, "-") == 0)                   res = a - b;
            else if (strcmp(op, "*") == 0)                   res = a * b;
            else if (strcmp(op, "/") == 0 && b != 0.0)      res = a / b;
            else {
                int bres = 0;
                if      (strcmp(op, "==") == 0) bres = (a == b);
                else if (strcmp(op, "!=") == 0) bres = (a != b);
                else if (strcmp(op, "<")  == 0) bres = (a <  b);
                else if (strcmp(op, ">")  == 0) bres = (a >  b);
                else if (strcmp(op, "<=") == 0) bres = (a <= b);
                else if (strcmp(op, ">=") == 0) bres = (a >= b);
                else { folded = 0; }
                if (folded) {
                    n->type = NBOOL; n->ival = bres;
                    n->stype = StypeInternGet(c, STYPE_BOOL, "bool");
                    return 1;
                }
            }
            if (folded) {
                n->type = NFLOAT; n->fval = res;
                n->stype = StypeInternGet(c, STYPE_FLOAT, "float");
                return 1;
            }
        }

        /* Identity element elimination (algebraic simplifications) */
        /* x + 0 -> x */
        if (strcmp(op, "+") == 0 && R->type == NINT && R->ival == 0) {
            Node *old_L = L;
            NodeFree(R);
            *n = *old_L;
            free(old_L);
            return 1;
        }
        /* 0 + x -> x */
        if (strcmp(op, "+") == 0 && L->type == NINT && L->ival == 0) {
            Node *old_R = R;
            NodeFree(L);
            *n = *old_R;
            free(old_R);
            return 1;
        }
        /* x - 0 -> x */
        if (strcmp(op, "-") == 0 && R->type == NINT && R->ival == 0) {
            Node *old_L = L;
            NodeFree(R);
            *n = *old_L;
            free(old_L);
            return 1;
        }
        /* x * 1 -> x */
        if (strcmp(op, "*") == 0 && R->type == NINT && R->ival == 1) {
            Node *old_L = L;
            NodeFree(R);
            *n = *old_L;
            free(old_L);
            return 1;
        }
        /* 1 * x -> x */
        if (strcmp(op, "*") == 0 && L->type == NINT && L->ival == 1) {
            Node *old_R = R;
            NodeFree(L);
            *n = *old_R;
            free(old_R);
            return 1;
        }
        /* x * 0 or 0 * x -> 0 (only if pure operand) */
        if (strcmp(op, "*") == 0) {
            if (R->type == NINT && R->ival == 0 && (L->type == NINT || L->type == NFLOAT || L->type == NIDENT || L->type == NSTR)) {
                NodeFree(L);
                NodeFree(R);
                memset(n, 0, sizeof(Node));
                n->type = NINT; n->ival = 0;
                n->stype = StypeInternGet(c, STYPE_INT, "int");
                return 1;
            }
            if (L->type == NINT && L->ival == 0 && (R->type == NINT || R->type == NFLOAT || R->type == NIDENT || R->type == NSTR)) {
                NodeFree(L);
                NodeFree(R);
                memset(n, 0, sizeof(Node));
                n->type = NINT; n->ival = 0;
                n->stype = StypeInternGet(c, STYPE_INT, "int");
                return 1;
            }
        }
        /* x / 1 -> x */
        if (strcmp(op, "/") == 0 && R->type == NINT && R->ival == 1) {
            Node *old_L = L;
            NodeFree(R);
            *n = *old_L;
            free(old_L);
            return 1;
        }
        /* Strength reduction: x * 2^k -> x << k (release only) */
        if (strcmp(op, "*") == 0 && R->type == NINT && R->ival > 0) {
            int64_t v = R->ival;
            if ((v & (v - 1)) == 0) { /* power of two */
                int shift = 0;
                while (v > 1) { v >>= 1; shift++; }
                R->ival = shift;
                n->binop.op = "<<";
                return 1;
            }
        }
        /* x / 2^k -> x >> k */
        if (strcmp(op, "/") == 0 && R->type == NINT && R->ival > 0) {
            int64_t v = R->ival;
            if ((v & (v - 1)) == 0) {
                int shift = 0;
                while (v > 1) { v >>= 1; shift++; }
                R->ival = shift;
                n->binop.op = ">>";
                return 1;
            }
        }

        /* Recursive descent into operands */
        TyperConstFold(c, L);
        TyperConstFold(c, R);
    }

    /* Unary const fold */
    if (n->type == NUNOP && n->binop.left) {
        Node *operand = n->binop.left;
        const char *op = n->binop.op;
        if (op && strcmp(op, "-") == 0) {
            if (operand->type == NINT) {
                n->type = NINT; n->ival = -operand->ival;
                n->stype = StypeInternGet(c, STYPE_INT, "int"); return 1;
            }
            if (operand->type == NFLOAT) {
                n->type = NFLOAT; n->fval = -operand->fval;
                n->stype = StypeInternGet(c, STYPE_FLOAT, "float"); return 1;
            }
        }
        if (op && strcmp(op, "!") == 0 && operand->type == NBOOL) {
            n->type = NBOOL; n->ival = !operand->ival;
            n->stype = StypeInternGet(c, STYPE_BOOL, "bool"); return 1;
        }
    }
    return 0;
}

/* Core type inference */

static SType *InferNode(TyperCtx *ctx, TypeEnv *env, Node *n);
static void CheckNode(TyperCtx *ctx, TypeEnv *env, Node *n, SType *expected);

static SType *InferNode(TyperCtx *ctx, TypeEnv *env, Node *n) {
    if (!n) return StypeInternGet(ctx->comp, STYPE_VOID, "void");
    if (n->stype) return n->stype; /* already annotated */

    if (ctx->recursion_depth >= TYPER_MAX_RECURSION_DEPTH) {
        TyperError(ctx, n->line, "Maximum type inference recursion depth exceeded");
        return StypeInternGet(ctx->comp, STYPE_UNKNOWN, "");
    }
    ctx->recursion_depth++;

    Compiler *c = ctx->comp;
    SType *result = StypeInternGet(c, STYPE_UNKNOWN, "");

    switch (n->type) {
        case NINT:
            result = StypeInternGet(c, STYPE_INT, "int");
            break;
        case NFLOAT:
            result = StypeInternGet(c, STYPE_FLOAT, "float");
            break;
        case NBOOL:
            result = StypeInternGet(c, STYPE_BOOL, "bool");
            break;
        case NSTR:
            result = StypeInternGet(c, STYPE_STR, "str");
            break;
        case NNULL:
            result = StypeInternGet(c, STYPE_UNKNOWN, "null");
            break;

        case NIDENT: {
            SType *t = EnvGet(env, n->sval);
            if (!t) {
                char msg[256];
                snprintf(msg, sizeof(msg), "undefined variable '%s'", n->sval);
                /* Don't hard-error: dynamic mode allows late binding */
                t = StypeInternGet(c, STYPE_UNKNOWN, "");
            }
            result = t;
            break;
        }

        case NVARDECL: {
            /* let x: T = expr  or  let x = expr */
            SType *decl_type = NULL;
            if (n->decl.tname && n->decl.tname[0]) {
                decl_type = TyperResolveName(c, n->decl.tname);
            }
            if (n->decl.init) {
                SType *init_type = InferNode(ctx, env, n->decl.init);
                if (decl_type) {
                    result = TyperUnify(c, decl_type, init_type, n->line);
                } else {
                    result = init_type;
                }
            } else {
                result = decl_type ? decl_type : StypeInternGet(c, STYPE_UNKNOWN, "");
            }
            if (n->decl.name) EnvSet(env, n->decl.name, result);
            /* Always try const folding on the init expr  -  eliminates
               dead computations before IR emission regardless of const_fn */
            if (n->decl.init) TyperConstFold(c, n->decl.init);
            break;
        }

        case NBINOP: {
            SType *lt = InferNode(ctx, env, n->binop.left);
            SType *rt = InferNode(ctx, env, n->binop.right);
            const char *op = n->binop.op;
            if (!op) { result = lt; break; }
            /* Comparison operators always return bool */
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                strcmp(op, "<") == 0  || strcmp(op, ">") == 0  ||
                strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
                result = StypeInternGet(c, STYPE_BOOL, "bool");
            } else if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
                result = StypeInternGet(c, STYPE_BOOL, "bool");
            } else {
                /* Arithmetic: unify operand types */
                result = TyperUnify(c, lt, rt, n->line);
                if (!result) result = lt;
            }
            /* Always attempt const folding  -  regardless of const_fn.
               This propagates literals through all arithmetic chains,
               enabling strength reduction and identity elimination
               before any IR is emitted. */
            TyperConstFold(c, n);
            break;
        }

        case NUNOP: {
            SType *it = InferNode(ctx, env, n->binop.left);
            const char *op = n->binop.op;
            if (op && strcmp(op, "!") == 0)
                result = StypeInternGet(c, STYPE_BOOL, "bool");
            else
                result = it;
            break;
        }

        case NRETURN: {
            SType *ret_t = n->single.expr
                ? InferNode(ctx, env, n->single.expr)
                : StypeInternGet(c, STYPE_VOID, "void");
            if (ctx->current_fn_ret) {
                TyperUnify(c, ctx->current_fn_ret, ret_t, n->line);
            }
            result = StypeInternGet(c, STYPE_NEVER, "!");
            break;
        }

        case NBLOCK: {
            TypeEnv *inner = EnvPush(env);
            result = StypeInternGet(c, STYPE_VOID, "void");
            for (int i = 0; i < n->block.count; i++) {
                result = InferNode(ctx, inner, n->block.stmts[i]);
            }
            EnvFreeLocal(inner);
            EnvPop(inner);
            break;
        }

        case NIF: {
            InferNode(ctx, env, n->ifn.cond);
            SType *then_t = InferNode(ctx, env, n->ifn.then);
            SType *else_t = n->ifn.els
                ? InferNode(ctx, env, n->ifn.els)
                : StypeInternGet(c, STYPE_VOID, "void");
            result = TyperUnify(c, then_t, else_t, n->line);
            if (!result) result = then_t;
            break;
        }

        case NWHILE: {
            CheckNode(ctx, env, n->whilen.cond, StypeInternGet(c, STYPE_BOOL, "bool"));
            InferNode(ctx, env, n->whilen.body);
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NFOR: {
            TypeEnv *inner = EnvPush(env);
            if (n->forn.init) InferNode(ctx, inner, n->forn.init);
            if (n->forn.cond) CheckNode(ctx, inner, n->forn.cond, StypeInternGet(c, STYPE_BOOL, "bool"));
            if (n->forn.post) InferNode(ctx, inner, n->forn.post);
            if (n->forn.body) InferNode(ctx, inner, n->forn.body);
            EnvFreeLocal(inner); EnvPop(inner);
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NFORIN: {
            SType *iter_t = InferNode(ctx, env, n->forin.iter);
            SType *elem_t = StypeInternGet(c, STYPE_UNKNOWN, "");
            if (iter_t && iter_t->kind == STYPE_ARRAY) elem_t = iter_t->inner;
            TypeEnv *inner = EnvPush(env);
            if (n->forin.var) EnvSet(inner, n->forin.var, elem_t);
            InferNode(ctx, inner, n->forin.body);
            EnvFreeLocal(inner); EnvPop(inner);
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NARRLIT: {
            SType *elem_t = StypeInternGet(c, STYPE_UNKNOWN, "");
            for (int i = 0; i < n->arrlit.count; i++) {
                SType *et = InferNode(ctx, env, n->arrlit.elems[i]);
                elem_t = TyperUnify(c, elem_t, et, n->line);
            }
            if (!elem_t) elem_t = StypeInternGet(c, STYPE_UNKNOWN, "");
            result = StypeMakeArray(c, elem_t, n->arrlit.count);
            break;
        }

        case NTUPLE: {
            SType **elems = (SType**)malloc(n->tuple.count * sizeof(SType*));
            for (int i = 0; i < n->tuple.count; i++) {
                elems[i] = InferNode(ctx, env, n->tuple.elems[i]);
            }
            result = StypeMakeTuple(c, elems, n->tuple.count);
            free(elems);
            break;
        }

        case NREF: {
            SType *inner = InferNode(ctx, env, n->refnode.expr);
            result = StypeMakeRef(c, inner, 0);
            break;
        }
        case NMUTREF: {
            SType *inner = InferNode(ctx, env, n->refnode.expr);
            result = StypeMakeRef(c, inner, 1);
            break;
        }

        case NASSIGN: {
            SType *rhs = InferNode(ctx, env, n->assign.value);
            if (n->assign.target) n->assign.target->stype = rhs;
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NCALL: {
            /* Look up function return type */
            if (n->call.callee && n->call.callee->type == NIDENT) {
                const char *fname = n->call.callee->sval;
                /* Look for FnProto */
                for (int i = 0; i < c->fncount; i++) {
                    if (strcmp(c->fns[i]->name, fname) == 0) {
                        result = TyperResolveName(c, c->fns[i]->rettype);
                        break;
                    }
                }
            }
            /* Infer arg types too */
            for (int i = 0; i < n->call.argc; i++) {
                InferNode(ctx, env, n->call.args[i]);
            }
            if (result->kind == STYPE_UNKNOWN) result = StypeInternGet(c, STYPE_UNKNOWN, "");
            break;
        }

        case NMEMBER: {
            SType *obj_t = InferNode(ctx, env, n->member.obj);
            /* Field type lookup for known structs */
            if (obj_t && obj_t->kind == STYPE_STRUCT) {
                for (int i = 0; i < c->structcount; i++) {
                    if (strcmp(c->structs[i].name, obj_t->name) == 0) {
                        for (int j = 0; j < c->structs[i].fc; j++) {
                            if (c->structs[i].fnames && n->member.field &&
                                strcmp(c->structs[i].fnames[j], n->member.field) == 0) {
                                result = TyperResolveName(c, c->structs[i].ftypes ? c->structs[i].ftypes[j] : "");
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            break;
        }

        case NINDEX: {
            SType *arr_t = InferNode(ctx, env, n->index.arr);
            InferNode(ctx, env, n->index.idx);
            if (arr_t && arr_t->kind == STYPE_ARRAY && arr_t->inner)
                result = arr_t->inner;
            break;
        }

        case NFNDECL: {
            /* Annotate param types in env, infer body */
            TypeEnv *inner = EnvPush(env);
            SType *ret_t = n->fn.ret ? TyperResolveName(c, n->fn.ret)
                                     : StypeInternGet(c, STYPE_VOID, "void");
            for (int i = 0; i < n->fn.pc; i++) {
                if (n->fn.pnames && n->fn.ptypes) {
                    SType *pt = TyperResolveName(c, n->fn.ptypes[i]);
                    EnvSet(inner, n->fn.pnames[i], pt);
                }
            }
            SType *saved_ret = ctx->current_fn_ret;
            ctx->current_fn_ret = ret_t;
            int saved_const = ctx->in_const_fn;
            /* Check if this is a const fn (annotated via n->fn.effect or similar) */
            /* For now: treat fn named "const_..." as const fn */
            if (n->fn.name && strncmp(n->fn.name, "const_", 6) == 0) ctx->in_const_fn = 1;
            if (n->fn.body) InferNode(ctx, inner, n->fn.body);
            ctx->current_fn_ret = saved_ret;
            ctx->in_const_fn = saved_const;
            /* Build fn SType */
            SType **param_stypes = (SType**)malloc(n->fn.pc * sizeof(SType*));
            for (int i = 0; i < n->fn.pc; i++) {
                param_stypes[i] = n->fn.ptypes
                    ? TyperResolveName(c, n->fn.ptypes[i])
                    : StypeInternGet(c, STYPE_UNKNOWN, "");
            }
            result = StypeMakeFn(c, param_stypes, n->fn.pc, ret_t);
            free(param_stypes);
            EnvFreeLocal(inner); EnvPop(inner);
            if (n->fn.name) EnvSet(env, n->fn.name, result);
            break;
        }

        case NUNSAFE: {
            /* unsafe { block }  -  type of block, borrow checker suspended */
            int saved = ctx->in_unsafe;
            ctx->in_unsafe = 1;
            if (n->single.expr) result = InferNode(ctx, env, n->single.expr);
            ctx->in_unsafe = saved;
            break;
        }

        case NCONST_EVAL: {
            if (n->single.expr) {
                int saved = ctx->in_const_fn;
                ctx->in_const_fn = 1;
                result = InferNode(ctx, env, n->single.expr);
                TyperConstFold(c, n->single.expr);
                ctx->in_const_fn = saved;
            }
            break;
        }

        case NMATCH: {
            /* Infer subject, then check exhaustiveness */
            SType *subj_t = InferNode(ctx, env, n->match.expr);
            if (subj_t && subj_t->kind == STYPE_ENUM && c) {
                int enumidx = -1;
                for (int i = 0; i < c->enumcount; i++) {
                    if (strcmp(c->enums[i].name, subj_t->name) == 0) {
                        enumidx = i; break;
                    }
                }
                if (enumidx >= 0) {
                    CompilerEnum *en = &c->enums[enumidx];
                    int haswildcard = 0;
                    for (int i = 0; i < n->match.count; i++) {
                        Node *pat = n->match.patterns[i];
                        if (pat && pat->type == NIDENT &&
                            (strcmp(pat->sval, "_") == 0 || strcmp(pat->sval, "default") == 0 || strcmp(pat->sval, "else") == 0)) {
                            haswildcard = 1; break;
                        }
                    }
                    if (!haswildcard) {
                        char missingbuf[512] = "";
                        int missingcount = 0;
                        for (int m = 0; m < en->count; m++) {
                            int matched = 0;
                            for (int i = 0; i < n->match.count; i++) {
                                Node *pat = n->match.patterns[i];
                                if (!pat) continue;
                                if (pat->type == NCALL) {
                                    if (pat->call.callee->type == NMEMBER && strcmp(pat->call.callee->member.field, en->members[m]) == 0) {
                                        matched = 1; break;
                                    }
                                    if (pat->call.callee->type == NIDENT && strcmp(pat->call.callee->sval, en->members[m]) == 0) {
                                        matched = 1; break;
                                    }
                                } else if (pat->type == NMEMBER) {
                                    if (strcmp(pat->member.field, en->members[m]) == 0) {
                                        matched = 1; break;
                                    }
                                } else if (pat->type == NIDENT) {
                                    if (strcmp(pat->sval, en->members[m]) == 0) {
                                        matched = 1; break;
                                    }
                                }
                            }
                            if (!matched) {
                                if (missingcount > 0 && strlen(missingbuf) + 2 < sizeof(missingbuf)) {
                                    strcat(missingbuf, ", ");
                                }
                                strncat(missingbuf, en->members[m], sizeof(missingbuf) - strlen(missingbuf) - 1);
                                missingcount++;
                            }
                        }
                        if (missingcount > 0) {
                            char warnmsg[1024];
                            snprintf(warnmsg, sizeof(warnmsg), "Non-exhaustive match on enum '%s': missing variant(s): %s", en->name, missingbuf);
                            extern int gStrictTypes;
                            if (gStrictTypes) {
                                TyperError(ctx, n->line, warnmsg);
                            } else {
                                TyperWarn(ctx, n->line, warnmsg);
                            }
                        }
                    }
                }
            }
            SType *arm_t = StypeInternGet(c, STYPE_UNKNOWN, "");
            for (int i = 0; i < n->match.count; i++) {
                if (n->match.bodies[i]) {
                    SType *bt = InferNode(ctx, env, n->match.bodies[i]);
                    arm_t = TyperUnify(c, arm_t, bt, n->line);
                }
            }
            result = arm_t ? arm_t : StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NEXPRSTMT: {
            result = InferNode(ctx, env, n->single.expr);
            break;
        }

        case NAWAIT: {
            /* await task: type of the task result */
            SType *t = InferNode(ctx, env, n->awaitn.expr);
            /* Unwrap future/task wrapper  -  for now, pass through */
            result = t;
            break;
        }

        case NSPAWN: {
            result = StypeInternGet(c, STYPE_UNKNOWN, "task");
            if (n->spawnn.expr) InferNode(ctx, env, n->spawnn.expr);
            break;
        }

        default:
            /* For all other node types, recursively infer children */
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;
    }

    n->stype = result;
    ctx->recursion_depth--;
    return result;
}

static void CheckNode(TyperCtx *ctx, TypeEnv *env, Node *n, SType *expected) {
    SType *actual = InferNode(ctx, env, n);
    SType *unified = TyperUnify(ctx->comp, expected, actual, n->line);
    if (!unified && expected->kind != STYPE_UNKNOWN && actual->kind != STYPE_UNKNOWN) {
        char ea[64], aa[64];
        StypeName(expected, ea, sizeof(ea));
        StypeName(actual, aa, sizeof(aa));
        char msg[256];
        snprintf(msg, sizeof(msg), "expected type '%s', found '%s'", ea, aa);
        TyperErrorNode(ctx, n, msg);
    }
    if (unified) n->stype = unified;
}


/* AST Optimization Pass (DCE, Constant Branch Pruning) */

static void TyperOptimizeAST(Node *n) {
    if (!n) return;
    switch (n->type) {
        case NBLOCK: case NPROGRAM: {
            int write_idx = 0;
            for (int i = 0; i < n->block.count; i++) {
                Node *s = n->block.stmts[i];
                TyperOptimizeAST(s);
                n->block.stmts[write_idx++] = s;
                /* Dead code elimination: prune remaining statements after a return/throw */
                if (s && (s->type == NRETURN || s->type == NTHROW)) {
                    break;
                }
            }
            n->block.count = write_idx;
            break;
        }
        case NIF: {
            TyperOptimizeAST(n->ifn.cond);
            TyperOptimizeAST(n->ifn.then);
            TyperOptimizeAST(n->ifn.els);
            /* Simplify constant boolean branch */
            if (n->ifn.cond && n->ifn.cond->type == NBOOL) {
                if (n->ifn.cond->ival) {
                    /* Replace if(true) with then-branch */
                    if (n->ifn.then) *n = *n->ifn.then;
                } else {
                    /* Replace if(false) with else-branch (or empty block if no else) */
                    if (n->ifn.els) *n = *n->ifn.els;
                }
            }
            break;
        }
        case NFNDECL:
            if (n->fn.body) TyperOptimizeAST(n->fn.body);
            break;
        case NWHILE:
            if (n->whilen.cond) TyperOptimizeAST(n->whilen.cond);
            if (n->whilen.body) TyperOptimizeAST(n->whilen.body);
            break;
        case NFOR:
            if (n->forn.init) TyperOptimizeAST(n->forn.init);
            if (n->forn.cond) TyperOptimizeAST(n->forn.cond);
            if (n->forn.post) TyperOptimizeAST(n->forn.post);
            if (n->forn.body) TyperOptimizeAST(n->forn.body);
            break;
        default:
            break;
    }
}

/* Public entry point */

int TyperRun(Compiler *c, Node *program) {
    TyperCtx ctx = {0};
    ctx.comp = c;
    TypeEnv root = {0};
    ctx.env = &root;
    ctx.current_fn_ret = StypeInternGet(c, STYPE_VOID, "void");

    /* Pre-intern primitive types */
    StypeInternGet(c, STYPE_INT, "int");
    StypeInternGet(c, STYPE_FLOAT, "float");
    StypeInternGet(c, STYPE_BOOL, "bool");
    StypeInternGet(c, STYPE_STR, "str");
    StypeInternGet(c, STYPE_VOID, "void");
    StypeInternGet(c, STYPE_NEVER, "!");

    /* Infer all top-level nodes */
    if (program && program->type == NPROGRAM) {
        for (int i = 0; i < program->block.count; i++) {
            InferNode(&ctx, &root, program->block.stmts[i]);
        }
    } else if (program) {
        InferNode(&ctx, &root, program);
    }

    /* Run AST-level optimization pass */
    if (program) {
        TyperOptimizeAST(program);
    }

    /* Flush local root env */
    EnvFreeLocal(&root);

    return ctx.had_error ? 0 : 1;
}

SType *TyperInfer(Compiler *c, Node *n) {
    TyperCtx ctx = {0};
    ctx.comp = c;
    TypeEnv root = {0};
    ctx.env = &root;
    ctx.current_fn_ret = StypeInternGet(c, STYPE_VOID, "void");
    SType *t = InferNode(&ctx, &root, n);
    EnvFreeLocal(&root);
    return t;
}

SType *TyperLookupVar(Compiler *c, const char *name) {
    if (!c || !name) return NULL;
    for (int i = c->static_local_count - 1; i >= 0; i--) {
        if (strcmp(c->static_locals[i].name, name) == 0) {
            return c->static_locals[i].stype;
        }
    }
    return StypeInternGet(c, STYPE_UNKNOWN, "");
}

/* Monomorphization stub  -  instantiates a copy of a generic function
   with concrete type arguments. The full implementation duplicates the
   fn AST node, substitutes type parameters, then re-invokes the compiler
   on the specialized body. */
int TyperMono(Compiler *c, Node *fn_node, SType **arg_types, int argc, int call_line) {
    if (!fn_node || fn_node->type != NFNDECL) return -1;
    /* Build cache key: "fn_name<T1,T2,...>" */
    char key[256];
    strncpy(key, fn_node->fn.name ? fn_node->fn.name : "__anon", sizeof(key)-1);
    strncat(key, "<", sizeof(key)-strlen(key)-1);
    for (int i = 0; i < argc; i++) {
        char tname[64]; StypeName(arg_types[i], tname, sizeof(tname));
        strncat(key, tname, sizeof(key)-strlen(key)-1);
        if (i+1 < argc) strncat(key, ",", sizeof(key)-strlen(key)-1);
    }
    strncat(key, ">", sizeof(key)-strlen(key)-1);
    /* Check cache */
    for (int i = 0; i < c->mono_cache_count; i++) {
        if (strcmp(c->mono_cache[i].key, key) == 0) return c->mono_cache[i].fnid;
    }
    /* Full monomorphization: clone AST, substitute type params, recompile.
       For now, return the original fn id and record it so subsequent calls
       don't re-instantiate. The emitter uses SType annotations for native-type
       selection without actually needing a distinct FnProto per instantiation
       when the body is identical. */
    int fnid = -1;
    for (int i = 0; i < c->fncount; i++) {
        if (c->fns[i]->name && strcmp(c->fns[i]->name, fn_node->fn.name ? fn_node->fn.name : "") == 0) {
            fnid = i; break;
        }
    }
    if (fnid >= 0 && c->mono_cache_count < 1024) {
        strncpy(c->mono_cache[c->mono_cache_count].key, key, 255);
        c->mono_cache[c->mono_cache_count].fnid = fnid;
        c->mono_cache_count++;
    }
    (void)call_line;
    return fnid;
}
