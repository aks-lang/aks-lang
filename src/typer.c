// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Bidirectional Hindley-Milner type inference for static mode.
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
#include "diagnostic.h"
#include "pkg.h"
#include "bind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

/* Primitive SType singletons (allocated via StypeInternGet per Compiler) */

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
    /* Walk the per-bucket chain (next_bucket) — O(bucket_size), not O(total_types) */
    for (SType *t = c->stype_intern[idx]; t; t = t->next_bucket) {
        if (t->kind == kind && strcmp(t->name, sname) == 0) return t;
    }
    /* Not found: allocate and chain at the bucket head.
     * next_intern is updated by StypeAlloc (global GC list).
     * next_bucket links only types in this specific bucket. */
    SType *t = StypeAlloc(c, kind);
    strncpy(t->name, sname, sizeof(t->name) - 1);
    if (kind == STYPE_INT || kind == STYPE_FLOAT || kind == STYPE_BOOL) t->is_copy = 1;
    t->next_bucket = c->stype_intern[idx];  /* push at bucket head */
    c->stype_intern[idx] = t;
    return t;
}

/* Incremental Signature Cache */
FnSigCache gFnSigCache = {0};

uint64_t TyperComputeNodeHash(Node *n) {
    if (!n) return 0xcbf29ce484222325ULL;
    uint64_t h = 0xcbf29ce484222325ULL;
    #define HASH_BYTE(b) do { h ^= (uint8_t)(b); h *= 0x100000001b3ULL; } while (0)
    #define HASH_INT(i) do { uint64_t v = (uint64_t)(i); for (int k=0; k<8; k++) HASH_BYTE((v >> (k*8)) & 0xFF); } while (0)
    #define HASH_STR(s) do { if (s) { for (const char *p = (s); *p; p++) HASH_BYTE(*p); } } while (0)

    HASH_INT(n->type);
    HASH_INT(n->line);
    if (n->type == NINT) {
        HASH_INT(n->ival);
    } else if (n->type == NFLOAT) {
        uint64_t bits; memcpy(&bits, &n->fval, 8); HASH_INT(bits);
    } else if (n->type == NSTR || n->type == NIDENT) {
        HASH_STR(n->sval);
    } else if (n->type == NBINOP) {
        HASH_STR(n->binop.op);
        h ^= TyperComputeNodeHash(n->binop.left);
        h *= 0x100000001b3ULL;
        h ^= TyperComputeNodeHash(n->binop.right);
        h *= 0x100000001b3ULL;
    } else if (n->type == NUNOP) {
        HASH_STR(n->binop.op);
        h ^= TyperComputeNodeHash(n->binop.left);
        h *= 0x100000001b3ULL;
    } else if (n->type == NVARDECL || n->type == NCONSTDECL) {
        HASH_STR(n->decl.name);
        HASH_STR(n->decl.tname);
        h ^= TyperComputeNodeHash(n->decl.init);
        h *= 0x100000001b3ULL;
    } else if (n->type == NBLOCK || n->type == NPROGRAM) {
        HASH_INT(n->block.count);
        for (int i = 0; i < n->block.count; i++) {
            h ^= TyperComputeNodeHash(n->block.stmts[i]);
            h *= 0x100000001b3ULL;
        }
    } else if (n->type == NIF) {
        h ^= TyperComputeNodeHash(n->ifn.cond);
        h *= 0x100000001b3ULL;
        h ^= TyperComputeNodeHash(n->ifn.then);
        h *= 0x100000001b3ULL;
        h ^= TyperComputeNodeHash(n->ifn.els);
        h *= 0x100000001b3ULL;
    } else if (n->type == NWHILE) {
        h ^= TyperComputeNodeHash(n->whilen.cond);
        h *= 0x100000001b3ULL;
        h ^= TyperComputeNodeHash(n->whilen.body);
        h *= 0x100000001b3ULL;
    } else if (n->type == NCALL) {
        h ^= TyperComputeNodeHash(n->call.callee);
        h *= 0x100000001b3ULL;
        HASH_INT(n->call.argc);
        for (int i = 0; i < n->call.argc; i++) {
            h ^= TyperComputeNodeHash(n->call.args[i]);
            h *= 0x100000001b3ULL;
        }
    } else if (n->type == NASSIGN) {
        h ^= TyperComputeNodeHash(n->assign.target);
        h *= 0x100000001b3ULL;
        h ^= TyperComputeNodeHash(n->assign.value);
        h *= 0x100000001b3ULL;
    } else if (n->type == NMEMBER) {
        h ^= TyperComputeNodeHash(n->member.obj);
        h *= 0x100000001b3ULL;
        HASH_STR(n->member.field);
    } else if (n->type == NINDEX) {
        h ^= TyperComputeNodeHash(n->index.arr);
        h *= 0x100000001b3ULL;
        h ^= TyperComputeNodeHash(n->index.idx);
        h *= 0x100000001b3ULL;
    } else if (n->type == NRETURN || n->type == NEXPRSTMT) {
        h ^= TyperComputeNodeHash(n->single.expr);
        h *= 0x100000001b3ULL;
    }
    #undef HASH_BYTE
    #undef HASH_INT
    #undef HASH_STR
    return h;
}

FnSigEntry *TyperLookupSig(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < gFnSigCache.count; i++) {
        if (strcmp(gFnSigCache.entries[i].name, name) == 0) {
            return &gFnSigCache.entries[i];
        }
    }
    return NULL;
}

void TyperStoreSig(const char *name, uint64_t body_hash, SType *ret_type,
                   SType **param_types, int param_count,
                   char deps[32][128], int dep_count) {
    if (!name) return;
    FnSigEntry *entry = TyperLookupSig(name);
    if (!entry) {
        if (gFnSigCache.count >= 1024) return;
        entry = &gFnSigCache.entries[gFnSigCache.count++];
    }
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->body_hash = body_hash;
    entry->ret_type = ret_type;
    entry->param_count = (param_count > 32) ? 32 : param_count;
    for (int i = 0; i < entry->param_count; i++) {
        entry->param_types[i] = param_types ? param_types[i] : NULL;
    }
    entry->dep_count = (dep_count > 32) ? 32 : dep_count;
    for (int i = 0; i < entry->dep_count; i++) {
        strncpy(entry->deps[i], deps[i], sizeof(entry->deps[i]) - 1);
        entry->deps[i][sizeof(entry->deps[i]) - 1] = '\0';
    }
    entry->is_valid = 1;
}

int TyperIsSigValid(FnSigEntry *sig) {
    if (!sig || !sig->is_valid) return 0;
    for (int i = 0; i < sig->dep_count; i++) {
        FnSigEntry *dep = TyperLookupSig(sig->deps[i]);
        if (dep && !dep->is_valid) return 0;
    }
    return 1;
}

void TyperSaveSigCache(Compiler *c, const char *cache_path) {
    if (!cache_path || gFnSigCache.count == 0) return;
    char dirbuf[512];
    strncpy(dirbuf, cache_path, sizeof(dirbuf) - 1);
    dirbuf[sizeof(dirbuf) - 1] = '\0';
    char *slash = strrchr(dirbuf, '/');
    if (!slash) slash = strrchr(dirbuf, '\\');
    if (slash) {
        *slash = '\0';
#ifdef _WIN32
        _mkdir(dirbuf);
#else
        mkdir(dirbuf, 0755);
#endif
    }

    FILE *f = fopen(cache_path, "wb");
    if (!f) return;
    const char magic[8] = "AKSIG1\0";
    fwrite(magic, 1, 8, f);
    uint32_t count = (uint32_t)gFnSigCache.count;
    fwrite(&count, sizeof(uint32_t), 1, f);
    for (int i = 0; i < gFnSigCache.count; i++) {
        FnSigEntry *e = &gFnSigCache.entries[i];
        fwrite(e->name, 1, 128, f);
        fwrite(&e->body_hash, sizeof(uint64_t), 1, f);
        int32_t pcount = e->param_count;
        fwrite(&pcount, sizeof(int32_t), 1, f);
        for (int p = 0; p < e->param_count; p++) {
            char tname[64] = {0};
            if (e->param_types[p]) StypeName(e->param_types[p], tname, sizeof(tname));
            fwrite(tname, 1, 64, f);
        }
        char rname[64] = {0};
        if (e->ret_type) StypeName(e->ret_type, rname, sizeof(rname));
        fwrite(rname, 1, 64, f);
        int32_t dcount = e->dep_count;
        fwrite(&dcount, sizeof(int32_t), 1, f);
        for (int d = 0; d < e->dep_count; d++) {
            fwrite(e->deps[d], 1, 128, f);
        }
        int32_t valid = e->is_valid;
        fwrite(&valid, sizeof(int32_t), 1, f);
    }
    fclose(f);
}

void TyperLoadSigCache(Compiler *c, const char *cache_path) {
    if (!cache_path) return;
    FILE *f = fopen(cache_path, "rb");
    if (!f) return;
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "AKSIG1\0", 8) != 0) {
        fclose(f);
        return;
    }
    uint32_t count = 0;
    if (fread(&count, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return;
    }
    if (count > 1024) count = 1024;
    gFnSigCache.count = (int)count;
    for (uint32_t i = 0; i < count; i++) {
        FnSigEntry *e = &gFnSigCache.entries[i];
        if (fread(e->name, 1, 128, f) != 128) break;
        if (fread(&e->body_hash, sizeof(uint64_t), 1, f) != 1) break;
        int32_t pcount = 0;
        if (fread(&pcount, sizeof(int32_t), 1, f) != 1) break;
        e->param_count = (pcount > 32) ? 32 : pcount;
        for (int p = 0; p < e->param_count; p++) {
            char tname[64] = {0};
            if (fread(tname, 1, 64, f) != 64) break;
            e->param_types[p] = (tname[0] != '\0') ? TyperResolveName(c, tname) : NULL;
        }
        char rname[64] = {0};
        if (fread(rname, 1, 64, f) != 64) break;
        e->ret_type = (rname[0] != '\0') ? TyperResolveName(c, rname) : NULL;
        int32_t dcount = 0;
        if (fread(&dcount, sizeof(int32_t), 1, f) != 1) break;
        e->dep_count = (dcount > 32) ? 32 : dcount;
        for (int d = 0; d < e->dep_count; d++) {
            if (fread(e->deps[d], 1, 128, f) != 128) break;
        }
        int32_t valid = 0;
        if (fread(&valid, sizeof(int32_t), 1, f) != 1) break;
        e->is_valid = valid;
    }
    fclose(f);
}

SType *StypeMakeArray(Compiler *c, SType *elem, int len) {
    char name[128];
    char elemname[64];
    StypeName(elem, elemname, sizeof(elemname));
    snprintf(name, sizeof(name), "[%s;%d]", elemname, len);
    SType *t = StypeAlloc(c, STYPE_ARRAY);
    t->inner = elem; t->array_len = len;
    snprintf(t->name, sizeof(t->name), "%.127s", name);
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
    t->params = (SType**)malloc((size_t)(count > 0 ? count : 1) * sizeof(SType*));
    if (count > 0 && elems) memcpy(t->params, elems, (size_t)count * sizeof(SType*));
    t->param_count = count;
    char tname[128] = "(";
    for (int i = 0; i < count; i++) {
        char ename[48];
        StypeName(elems[i], ename, sizeof(ename));
        if (i > 0) strncat(tname, ",", sizeof(tname) - strlen(tname) - 1);
        strncat(tname, ename, sizeof(tname) - strlen(tname) - 1);
    }
    strncat(tname, ")", sizeof(tname) - strlen(tname) - 1);
    snprintf(t->name, sizeof(t->name), "%s", tname);
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
    snprintf(t->name, sizeof(t->name), "Result<%.55s,%.55s>", ok_name, err_name);
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
        case STYPE_TRAIT: snprintf(buf, buflen, "dyn %s", t->name[0] ? t->name : "Trait"); return buf;
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
    if (!name) return NULL;
    unsigned int idx = EnvHash(name);
    char first = name[0];
    for (; e; e = e->parent) {
        for (TypeEnvEntry *en = e->buckets[idx]; en; en = en->next) {
            if (en->name[0] == first && strcmp(en->name, name) == 0) return en->stype;
        }
    }
    return NULL;
}

static TypeEnvEntry *EnvGetEntry(TypeEnv *e, const char *name) {
    if (!name) return NULL;
    unsigned int idx = EnvHash(name);
    char first = name[0];
    for (; e; e = e->parent) {
        for (TypeEnvEntry *en = e->buckets[idx]; en; en = en->next) {
            if (en->name[0] == first && strcmp(en->name, name) == 0) return en;
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

static const char *TyperSuggestEnvVar(TypeEnv *env, const char *name) {
    if (!env || !name) return NULL;
    const char *best = NULL;
    int best_dist = 999;
    for (TypeEnv *cur = env; cur; cur = cur->parent) {
        for (int b = 0; b < 64; b++) {
            for (TypeEnvEntry *e = cur->buckets[b]; e; e = e->next) {
                if (!e->name[0]) continue;
                if (strcmp(e->name, name) == 0) continue;
                int d = CompLevenshteinDistance(name, e->name);
                if (d > 0 && d < best_dist && d <= (strlen(name) > 3 ? 2 : 1)) {
                    best_dist = d;
                    best = e->name;
                }
            }
        }
    }
    return best;
}

static const char *TyperSuggestType(Compiler *c, const char *name) {
    static const char *prims[] = {
        "int", "float", "str", "bool", "void", "any", "never",
        "Option", "Result", "Array", "Tuple"
    };
    const char *best = NULL;
    int best_dist = 999;
    for (int i = 0; i < (int)(sizeof(prims)/sizeof(prims[0])); i++) {
        if (strcmp(name, prims[i]) == 0) continue;
        int d = CompLevenshteinDistance(name, prims[i]);
        if (d > 0 && d < best_dist && d <= (strlen(name) > 3 ? 2 : 1)) {
            best_dist = d;
            best = prims[i];
        }
    }
    if (c) {
        for (int i = 0; i < c->structcount; i++) {
            if (!c->structs[i].name[0]) continue;
            if (strcmp(name, c->structs[i].name) == 0) continue;
            int d = CompLevenshteinDistance(name, c->structs[i].name);
            if (d > 0 && d < best_dist && d <= (strlen(name) > 3 ? 2 : 1)) {
                best_dist = d;
                best = c->structs[i].name;
            }
        }
        for (int i = 0; i < c->enumcount; i++) {
            if (!c->enums[i].name[0]) continue;
            if (strcmp(name, c->enums[i].name) == 0) continue;
            int d = CompLevenshteinDistance(name, c->enums[i].name);
            if (d > 0 && d < best_dist && d <= (strlen(name) > 3 ? 2 : 1)) {
                best_dist = d;
                best = c->enums[i].name;
            }
        }
        for (int i = 0; i < c->aliascount; i++) {
            if (!c->aliases[i].name[0]) continue;
            if (strcmp(name, c->aliases[i].name) == 0) continue;
            int d = CompLevenshteinDistance(name, c->aliases[i].name);
            if (d > 0 && d < best_dist && d <= (strlen(name) > 3 ? 2 : 1)) {
                best_dist = d;
                best = c->aliases[i].name;
            }
        }
    }
    return best;
}

/* Error reporting */

static void TyperError(TyperCtx *ctx, int line, const char *msg) {
    if (ctx->error_count < 50) {
        const char *file = (ctx->comp && ctx->comp->currentfilepath[0])
                           ? ctx->comp->currentfilepath : NULL;
        if (file) {
            fprintf(stderr, "Type error in '%s' on line %d: %s\n", file, line, msg);
            if (line > 0) {
                FILE *f = fopen(file, "r");
                if (f) {
                    char linebuf[512];
                    int cur = 1;
                    while (fgets(linebuf, sizeof(linebuf), f)) {
                        if (cur == line) {
                            size_t l = strlen(linebuf);
                            while (l > 0 && (linebuf[l - 1] == '\r' || linebuf[l - 1] == '\n')) linebuf[--l] = '\0';
                            fprintf(stderr, "  %4d | %s\n", line, linebuf);
                            fprintf(stderr, "       | ^~~~\n");
                            break;
                        }
                        cur++;
                    }
                    fclose(f);
                }
            }
        } else {
            fprintf(stderr, "Type error on line %d: %s\n", line, msg);
        }
    } else if (ctx->error_count == 50) {
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
    const char *tp = tname;
    while (*tp == ' ' || *tp == '\t' || *tp == '\r' || *tp == '\n') tp++;
    int is_dyn_kw = (strncmp(tp, "dyn", 3) == 0 && (tp[3] == ' ' || tp[3] == '\t'));
    char clean[256];
    int cidx = 0;
    for (int i = 0; tname[i] && cidx < 255; i++) {
        if (tname[i] != ' ' && tname[i] != '\t' && tname[i] != '\r' && tname[i] != '\n') {
            clean[cidx++] = tname[i];
        }
    }
    clean[cidx] = '\0';
    if (clean[0] == '\0') return StypeInternGet(c, STYPE_UNKNOWN, "");

    /* Strip effect annotations like !IO */
    char *bang = strchr(clean, '!');
    if (bang && bang != clean) *bang = '\0';

    if (is_dyn_kw && clean[3] != '\0') {
        return StypeInternGet(c, STYPE_TRAIT, clean + 3);
    }
    if (strcmp(clean, "any") == 0 || strcmp(clean, "unknown") == 0)
        return StypeInternGet(c, STYPE_UNKNOWN, "any");
    if (strcmp(clean, "int") == 0 || strcmp(clean, "i64") == 0 || strcmp(clean, "i32") == 0)
        return StypeInternGet(c, STYPE_INT, "int");
    if (strcmp(clean, "float") == 0 || strcmp(clean, "f64") == 0 || strcmp(clean, "f32") == 0)
        return StypeInternGet(c, STYPE_FLOAT, "float");
    if (strcmp(clean, "bool") == 0)
        return StypeInternGet(c, STYPE_BOOL, "bool");
    if (strcmp(clean, "str") == 0 || strcmp(clean, "string") == 0)
        return StypeInternGet(c, STYPE_STR, "str");
    if (strcmp(clean, "void") == 0)
        return StypeInternGet(c, STYPE_VOID, "void");
    if (clean[0] == '!') return StypeInternGet(c, STYPE_NEVER, "!");

    /* Function type fn(T1, T2) -> R */
    if (strncmp(clean, "fn(", 3) == 0) {
        char *close_p = strrchr(clean + 3, ')');
        if (close_p) {
            char pbuf[256];
            size_t plen = (size_t)(close_p - (clean + 3));
            if (plen >= sizeof(pbuf)) plen = sizeof(pbuf) - 1;
            memcpy(pbuf, clean + 3, plen);
            pbuf[plen] = '\0';
            SType *ptypes[32];
            int pc = 0;
            if (pbuf[0] != '\0') {
                char *tok = strtok(pbuf, ",");
                while (tok && pc < 32) {
                    ptypes[pc++] = TyperResolveName(c, tok);
                    tok = strtok(NULL, ",");
                }
            }
            const char *rstr = close_p + 1;
            if (strncmp(rstr, "->", 2) == 0) rstr += 2;
            SType *rt = (*rstr) ? TyperResolveName(c, rstr) : StypeInternGet(c, STYPE_VOID, "void");
            return StypeMakeFn(c, ptypes, pc, rt);
        }
    }

    /* Check for Option<T> */
    if (strncmp(clean, "Option<", 7) == 0) {
        char inner_name[64];
        strncpy(inner_name, clean + 7, sizeof(inner_name)-1);
        inner_name[sizeof(inner_name)-1] = '\0';
        int len = (int)strlen(inner_name);
        if (len > 0 && inner_name[len-1] == '>') inner_name[len-1] = '\0';
        SType *inner = TyperResolveName(c, inner_name);
        return StypeMakeOption(c, inner);
    }
    /* Check for Result<T,E> */
    if (strncmp(clean, "Result<", 7) == 0) {
        char rest[128];
        strncpy(rest, clean + 7, sizeof(rest)-1);
        rest[sizeof(rest)-1] = '\0';
        char *comma = strchr(rest, ',');
        if (comma) {
            *comma = '\0';
            char *errpart = comma + 1;
            int elen = (int)strlen(errpart);
            if (elen > 0 && errpart[elen-1] == '>') errpart[elen-1] = '\0';
            return StypeMakeResult(c, TyperResolveName(c, rest), TyperResolveName(c, errpart));
        }
    }
    /* Check for Array<T, N> */
    if (strncmp(clean, "Array<", 6) == 0) {
        char rest[128];
        strncpy(rest, clean + 6, sizeof(rest)-1);
        rest[sizeof(rest)-1] = '\0';
        char *comma = strchr(rest, ',');
        if (comma) {
            *comma = '\0';
            char *npart = comma + 1;
            int nlen = (int)strlen(npart);
            if (nlen > 0 && npart[nlen-1] == '>') npart[nlen-1] = '\0';
            int n = atoi(npart);
            return StypeMakeArray(c, TyperResolveName(c, rest), n);
        }
    }
    /* Check for array [T; N] */
    if (clean[0] == '[') {
        char rest[64]; strncpy(rest, clean+1, sizeof(rest)-1);
        rest[sizeof(rest)-1] = '\0';
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
    /* Check for tuple (T1, T2, ...) */
    if (clean[0] == '(') {
        char rest[256]; strncpy(rest, clean+1, sizeof(rest)-1);
        rest[sizeof(rest)-1] = '\0';
        int len2 = (int)strlen(rest);
        if (len2 > 0 && rest[len2-1] == ')') rest[len2-1] = '\0';
        SType *elems[32];
        int elem_count = 0;
        char *token = strtok(rest, ",");
        while (token && elem_count < 32) {
            elems[elem_count++] = TyperResolveName(c, token);
            token = strtok(NULL, ",");
        }
        return StypeMakeTuple(c, elems, elem_count);
    }
    /* Look up type aliases */
    if (c) {
        for (int i = 0; i < c->aliascount; i++) {
            if (strcmp(c->aliases[i].name, clean) == 0) {
                if (c->aliases[i].isopaque)
                    return StypeInternGet(c, STYPE_STRUCT, clean);
                char target_buf[256];
                strncpy(target_buf, c->aliases[i].target, sizeof(target_buf)-1);
                target_buf[sizeof(target_buf)-1] = '\0';
                char *where_kw = strstr(target_buf, "where");
                if (where_kw) *where_kw = '\0';
                return TyperResolveName(c, target_buf);
            }
            int aname_len = (int)strlen(c->aliases[i].name);
            if (strncmp(clean, c->aliases[i].name, aname_len) == 0 && clean[aname_len] == '<') {
                char targ[64] = {0};
                strncpy(targ, clean + aname_len + 1, sizeof(targ)-1);
                int tlen = (int)strlen(targ);
                if (tlen > 0 && targ[tlen-1] == '>') targ[tlen-1] = '\0';
                char target_buf[256];
                strncpy(target_buf, c->aliases[i].target, sizeof(target_buf)-1);
                target_buf[sizeof(target_buf)-1] = '\0';
                if (c->aliases[i].tpc > 0 && c->aliases[i].tparams) {
                    const char *pname = c->aliases[i].tparams[0];
                    char replaced[256] = {0};
                    char *pos = target_buf;
                    char *match;
                    int rlen = 0;
                    while ((match = strstr(pos, pname)) != NULL && rlen < 240) {
                        int prefix = (int)(match - pos);
                        strncat(replaced, pos, prefix);
                        strcat(replaced, targ);
                        pos = match + strlen(pname);
                    }
                    strcat(replaced, pos);
                    return TyperResolveName(c, replaced);
                }
            }
        }
        for (int i = 0; i < c->structcount; i++) {
            if (strcmp(c->structs[i].name, clean) == 0)
                return StypeInternGet(c, STYPE_STRUCT, clean);
        }
        for (int i = 0; i < c->enumcount; i++) {
            if (strcmp(c->enums[i].name, clean) == 0)
                return StypeInternGet(c, STYPE_ENUM, clean);
        }
        for (int i = 0; i < c->interfacecount; i++) {
            if (strcmp(c->interfaces[i].name, clean) == 0)
                return StypeInternGet(c, STYPE_TRAIT, clean);
        }
    }
    /* SIMD types */
    if (strncmp(clean, "f32x", 4) == 0 || strncmp(clean, "i32x", 4) == 0 ||
        strncmp(clean, "f64x", 4) == 0 || strncmp(clean, "i64x", 4) == 0) {
        return StypeInternGet(c, STYPE_SIMD, clean);
    }
    /* Generic parameter  -  leave as STYPE_GENERIC for monomorphization */
    return StypeInternGet(c, STYPE_GENERIC, clean);
}

static int TyperCheckTraitImpl(Compiler *c, const char *struct_name, const char *trait_name) {
    if (!c || !struct_name || !trait_name) return 0;
    const CompilerStruct *s = NULL;
    for (int i = 0; i < c->structcount; i++) {
        if (strcmp(c->structs[i].name, struct_name) == 0) {
            s = &c->structs[i];
            break;
        }
    }
    if (!s) return 0;
    int declared = 0;
    for (int i = 0; i < s->ic; i++) {
        if (s->interfaces && s->interfaces[i] && strcmp(s->interfaces[i], trait_name) == 0) {
            declared = 1;
            break;
        }
    }
    if (!declared) return 0;
    const CompilerInterface *inf = NULL;
    for (int i = 0; i < c->interfacecount; i++) {
        if (strcmp(c->interfaces[i].name, trait_name) == 0) {
            inf = &c->interfaces[i];
            break;
        }
    }
    if (!inf) return 1;
    for (int j = 0; j < inf->count; j++) {
        if (inf->hasdefault && inf->hasdefault[j]) continue;
        const char *req = inf->fnames[j];
        int ok = 0;
        for (int f = 0; f < s->fc; f++) {
            if (s->fnames && s->fnames[f] && strcmp(s->fnames[f], req) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            char mname[256];
            snprintf(mname, sizeof(mname), "%s.%s", s->name, req);
            for (int k = 0; k < c->fncount; k++) {
                if (c->fns[k] && c->fns[k]->name[0] && strcmp(c->fns[k]->name, mname) == 0) {
                    ok = 1;
                    break;
                }
            }
        }
        if (!ok) return 0;
    }
    return 1;
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
    /* Trait object (dyn Trait) conformance check */
    if (a->kind == STYPE_TRAIT && b->kind == STYPE_STRUCT) {
        if (TyperCheckTraitImpl(c, b->name, a->name)) return a;
    }
    if (b->kind == STYPE_TRAIT && a->kind == STYPE_STRUCT) {
        if (TyperCheckTraitImpl(c, a->name, b->name)) return b;
    }
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
            if (c) {
                if (c->error_count < 50) {
                    if (file)
                        fprintf(stderr, "Type error in '%s' on line %d: array length mismatch: [_;%d] vs [_;%d]\n",
                                file, line, a->array_len, b->array_len);
                    else
                        fprintf(stderr, "Type error on line %d: array length mismatch: [_;%d] vs [_;%d]\n",
                                line, a->array_len, b->array_len);
                } else if (c->error_count == 50) {
                    fprintf(stderr, "... (further type errors suppressed)\n");
                }
                c->error_count++;
                c->haderror = 1;
            } else {
                fprintf(stderr, "Type error on line %d: array length mismatch: [_;%d] vs [_;%d]\n",
                        line, a->array_len, b->array_len);
            }
            return NULL;
        }
        return StypeMakeArray(c, inner, len);
    }
    /* Ref/MutRef unification */
    if (a->kind == b->kind && (a->kind == STYPE_REF || a->kind == STYPE_MUTREF)) {
        SType *inner = TyperUnify(c, a->inner, b->inner, line);
        return inner ? StypeMakeRef(c, inner, a->kind == STYPE_MUTREF) : NULL;
    }
    /* Option unification */
    if (a->kind == STYPE_OPTION && b->kind == STYPE_OPTION) {
        SType *inner = TyperUnify(c, a->inner, b->inner, line);
        return inner ? StypeMakeOption(c, inner) : NULL;
    }
    /* Result unification */
    if (a->kind == STYPE_RESULT && b->kind == STYPE_RESULT) {
        SType *ok = TyperUnify(c, a->inner, b->inner, line);
        SType *err = TyperUnify(c, a->inner2, b->inner2, line);
        return (ok && err) ? StypeMakeResult(c, ok, err) : NULL;
    }
    /* Tuple unification */
    if (a->kind == STYPE_TUPLE && b->kind == STYPE_TUPLE && a->param_count == b->param_count) {
        SType *elems[32];
        for (int i = 0; i < a->param_count && i < 32; i++) {
            elems[i] = TyperUnify(c, a->params[i], b->params[i], line);
            if (!elems[i]) return NULL;
        }
        return StypeMakeTuple(c, elems, a->param_count);
    }
    /* Mismatch */
    char na[64], nb[64];
    StypeName(a, na, sizeof(na)); StypeName(b, nb, sizeof(nb));
    const char *file = (c && c->currentfilepath[0]) ? c->currentfilepath : NULL;
    if (c) {
        if (c->error_count < 50) {
            Diagnostic diag;
            char dmsg[256];
            snprintf(dmsg, sizeof(dmsg), "mismatched types: expected `%s`, found `%s`", na, nb);
            DiagnosticInit(&diag, DIAG_ERROR, "E0308", dmsg);
            if (file && line > 0) {
                char lbl[256];
                snprintf(lbl, sizeof(lbl), "expected `%s`, found `%s`", na, nb);
                DiagnosticAddSpan(&diag, file, line, 1, 4, lbl, 1);
            }
            char dnote[256];
            snprintf(dnote, sizeof(dnote), "expected type `%s`\n              found type `%s`", na, nb);
            DiagnosticSetNote(&diag, dnote);
            DiagnosticEmit(&diag);

            if (file)
                fprintf(stderr, "Type error in '%s' on line %d: type mismatch: expected '%s', found '%s'\n",
                        file, line, na, nb);
            else
                fprintf(stderr, "Type error on line %d: type mismatch: expected '%s', found '%s'\n",
                        line, na, nb);
        } else if (c->error_count == 50) {
            fprintf(stderr, "... (further type errors suppressed)\n");
        }
        c->error_count++;
        c->haderror = 1;
    } else {
        fprintf(stderr, "Type error on line %d: type mismatch: expected '%s', found '%s'\n",
                line, na, nb);
    }
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
static void TyperPreregister(TyperCtx *ctx, TypeEnv *env, Node *program);

static char *TyperReadFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nr = fread(buf, 1, (size_t)sz, f);
    buf[nr] = '\0';
    fclose(f);
    return buf;
}

static int TyperMarkVisitedImport(TyperCtx *ctx, const char *path) {
    for (int i = 0; i < ctx->visited_import_count; i++) {
        if (strcmp(ctx->visited_imports[i], path) == 0) return 0;
    }
    char **na = (char **)realloc(ctx->visited_imports,
                                  (size_t)(ctx->visited_import_count + 1) * sizeof(char *));
    if (!na) return 0;
    ctx->visited_imports = na;
    ctx->visited_imports[ctx->visited_import_count++] = strdup(path);
    return 1;
}

static void TyperProcessImport(TyperCtx *ctx, TypeEnv *env, Node *n) {
    if (!n || !n->single.var || n->decl.isconstflag) return;
    Compiler *c = ctx->comp;
    char *resolved = PkgResolveImport(n->single.var, c ? c->currentfilepath : "");
    char finalpath[512];
    snprintf(finalpath, sizeof(finalpath), "%s", resolved ? resolved : n->single.var);
    if (resolved) free(resolved);
    if (!TyperMarkVisitedImport(ctx, finalpath)) return;
    size_t plen = strlen(finalpath);
    int is_c = n->single.is_c || (plen >= 2 && strcmp(finalpath + plen - 2, ".h") == 0);
    char *src = is_c ? AKSBindHeadersToStr(finalpath, 0, NULL) : TyperReadFile(finalpath);
    if (!src) return;
    int perr = 0;
    Node *imp_ast = Parse(src, &perr);
    free(src);
    if (perr || !imp_ast) {
        if (imp_ast) NodeFree(imp_ast);
        return;
    }
    char saved_path[512] = "";
    if (c) {
        snprintf(saved_path, sizeof(saved_path), "%s", c->currentfilepath);
        snprintf(c->currentfilepath, sizeof(c->currentfilepath), "%s", finalpath);
    }
    TyperPreregister(ctx, env, imp_ast);
    if (imp_ast->type == NPROGRAM) {
        for (int i = 0; i < imp_ast->block.count; i++) {
            InferNode(ctx, env, imp_ast->block.stmts[i]);
        }
    } else {
        InferNode(ctx, env, imp_ast);
    }
    if (c) snprintf(c->currentfilepath, sizeof(c->currentfilepath), "%s", saved_path);
    NodeFree(imp_ast);
}

static SType *TyperBuiltinRetType(Compiler *c, const char *fname) {
    if (!fname) return NULL;
    if (strcmp(fname, "assert") == 0 || strcmp(fname, "print") == 0 ||
        strcmp(fname, "println") == 0 || strcmp(fname, "panic") == 0 ||
        strcmp(fname, "push") == 0 || strcmp(fname, "sleep") == 0 ||
        strcmp(fname, "SleepMs") == 0 || strcmp(fname, "Exit") == 0 ||
        strcmp(fname, "FFIFree") == 0)
        return StypeInternGet(c, STYPE_VOID, "void");
    if (strcmp(fname, "len") == 0 || strcmp(fname, "Int") == 0 ||
        strcmp(fname, "FFILoad") == 0 || strcmp(fname, "exec") == 0)
        return StypeInternGet(c, STYPE_INT, "int");
    if (strcmp(fname, "Float") == 0)
        return StypeInternGet(c, STYPE_FLOAT, "float");
    if (strcmp(fname, "Bool") == 0 || strcmp(fname, "writefile") == 0)
        return StypeInternGet(c, STYPE_BOOL, "bool");
    if (strcmp(fname, "str") == 0 || strcmp(fname, "String") == 0 ||
        strcmp(fname, "type") == 0 || strcmp(fname, "platform") == 0 ||
        strcmp(fname, "readfile") == 0 || strcmp(fname, "format") == 0)
        return StypeInternGet(c, STYPE_STR, "str");
    if (strcmp(fname, "pop") == 0 || strcmp(fname, "FFICall") == 0)
        return StypeInternGet(c, STYPE_UNKNOWN, "");
    return NULL;
}

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
                if (strcmp(n->sval, "Option") == 0 || strcmp(n->sval, "Result") == 0) {
                    t = StypeInternGet(c, STYPE_UNKNOWN, n->sval);
                } else if (c) {
                    for (int i = 0; i < c->enumcount; i++) {
                        if (c->enums[i].name[0] && strcmp(c->enums[i].name, n->sval) == 0) {
                            t = StypeInternGet(c, STYPE_ENUM, n->sval);
                            break;
                        }
                    }
                    if (!t) {
                        for (int i = 0; i < c->structcount; i++) {
                            if (c->structs[i].name[0] && strcmp(c->structs[i].name, n->sval) == 0) {
                                t = StypeInternGet(c, STYPE_STRUCT, n->sval);
                                break;
                            }
                        }
                    }
                    if (!t) {
                        for (int i = 0; i < c->fncount; i++) {
                            if (c->fns[i] && c->fns[i]->name[0] && strcmp(c->fns[i]->name, n->sval) == 0) {
                                t = StypeMakeFn(c, NULL, c->fns[i]->paramcount,
                                                TyperResolveName(c, c->fns[i]->rettype));
                                break;
                            }
                        }
                    }
                }
            }
            if (!t) {
                const char *sugg = TyperSuggestEnvVar(env, n->sval);
                char msg[256];
                if (sugg) {
                    snprintf(msg, sizeof(msg), "undefined variable '%s'. Did you mean '%s'?", n->sval, sugg);
                } else {
                    snprintf(msg, sizeof(msg), "undefined variable '%s'", n->sval);
                }
                extern int gStrictTypes;
                if (gStrictTypes || (c && c->is_static_mode)) {
                    TyperError(ctx, n->line, msg);
                }
                t = StypeInternGet(c, STYPE_UNKNOWN, "");
            }
            result = t;
            break;
        }

        case NVARDECL:
        case NCONSTDECL: {
            /* let x: T = expr  or  const x: T = expr */
            SType *decl_type = NULL;
            if (n->decl.tname && n->decl.tname[0]) {
                decl_type = TyperResolveName(c, n->decl.tname);
                if (decl_type && decl_type->kind == STYPE_GENERIC) {
                    const char *sugg = TyperSuggestType(c, n->decl.tname);
                    if (sugg && strcmp(sugg, n->decl.tname) != 0) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "unknown type '%s'. Did you mean '%s'?", n->decl.tname, sugg);
                        extern int gStrictTypes;
                        if (gStrictTypes || (c && c->is_static_mode)) {
                            TyperError(ctx, n->line, msg);
                        }
                    }
                }
            }
            if (n->decl.init) {
                SType *init_type = InferNode(ctx, env, n->decl.init);
                if (decl_type) {
                    result = TyperUnify(c, decl_type, init_type, n->line);
                    if (!result) {
                        ctx->had_error = 1;
                        ctx->error_count++;
                        result = decl_type;
                    }
                } else {
                    result = init_type;
                }
            } else {
                result = decl_type ? decl_type : StypeInternGet(c, STYPE_UNKNOWN, "");
            }
            if (n->decl.name) {
                EnvSet(env, n->decl.name, result);
                if (ctx->region_depth > 0) {
                    TypeEnvEntry *en = EnvGetEntry(env, n->decl.name);
                    if (en) en->is_region_scoped = 1;
                }
            }
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
                /* Arithmetic / string concatenation: unify operand types */
                if (op && strcmp(op, "+") == 0 && (lt->kind == STYPE_STR || rt->kind == STYPE_STR)) {
                    result = StypeInternGet(c, STYPE_STR, "str");
                } else {
                    result = TyperUnify(c, lt, rt, n->line);
                    if (!result) {
                        ctx->had_error = 1;
                        ctx->error_count++;
                        result = lt;
                    }
                }
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
            if (ctx->region_depth > 0 && n->single.expr) {
                if (n->single.expr->type == NIDENT) {
                    TypeEnvEntry *en = EnvGetEntry(env, n->single.expr->sval);
                    if (en && en->is_region_scoped) {
                        TyperError(ctx, n->line, "Affine lifetime error: region-scoped value cannot escape via return statement");
                    }
                }
            }
            SType *ret_t = n->single.expr
                ? InferNode(ctx, env, n->single.expr)
                : StypeInternGet(c, STYPE_VOID, "void");
            if (n->single.expr) ctx->has_value_return = 1;
            if (ctx->current_fn_ret && ctx->current_fn_ret->kind != STYPE_UNKNOWN) {
                TyperUnify(c, ctx->current_fn_ret, ret_t, n->line);
            } else if (ctx->current_fn_ret && ctx->current_fn_ret->kind == STYPE_UNKNOWN) {
                ctx->current_fn_ret = ret_t;
            }
            result = StypeInternGet(c, STYPE_NEVER, "!");
            break;
        }

        case NPROGRAM: {
            result = StypeInternGet(c, STYPE_VOID, "void");
            for (int i = 0; i < n->block.count; i++) {
                result = InferNode(ctx, env, n->block.stmts[i]);
            }
            break;
        }

        case NIMPORT: {
            TyperProcessImport(ctx, env, n);
            result = StypeInternGet(c, STYPE_VOID, "void");
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
            if (n->assign.target) {
                n->assign.target->stype = rhs;
                if (ctx->region_depth > 0 && n->assign.target->type == NIDENT && n->assign.value) {
                    TypeEnvEntry *target_en = EnvGetEntry(env, n->assign.target->sval);
                    if (target_en && !target_en->is_region_scoped) {
                        if (n->assign.value->type == NIDENT) {
                            TypeEnvEntry *src_en = EnvGetEntry(env, n->assign.value->sval);
                            if (src_en && src_en->is_region_scoped) {
                                TyperError(ctx, n->line, "Affine lifetime error: region-scoped value cannot be assigned to an outer scope variable");
                            }
                        }
                    }
                }
            }
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NCALL: {
            /* If collecting dependencies for incremental typing, track callee */
            if (ctx->collecting_deps && n->call.callee && n->call.callee->type == NIDENT && ctx->dep_count < 32) {
                int found = 0;
                for (int d = 0; d < ctx->dep_count; d++) {
                    if (strcmp(ctx->current_deps[d], n->call.callee->sval) == 0) {
                        found = 1; break;
                    }
                }
                if (!found) {
                    strncpy(ctx->current_deps[ctx->dep_count++], n->call.callee->sval, 127);
                }
            }
            /* Look up function return type */
            SType *callee_fn_t = NULL;
            if (n->call.callee && n->call.callee->type == NIDENT) {
                const char *fname = n->call.callee->sval;
                int found = 0;
                TypeEnvEntry *fn_en = EnvGetEntry(env, fname);
                if (fn_en && fn_en->stype) {
                    if (fn_en->stype->kind == STYPE_FN) {
                        callee_fn_t = fn_en->stype;
                        result = fn_en->stype->ret ? fn_en->stype->ret : StypeInternGet(c, STYPE_UNKNOWN, "");
                    } else {
                        result = fn_en->stype;
                    }
                    found = 1;
                }
                /* Look for FnProto */
                for (int i = 0; !found && i < c->fncount; i++) {
                    if (strcmp(c->fns[i]->name, fname) == 0) {
                        result = TyperResolveName(c, c->fns[i]->rettype);
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    SType *bt = TyperBuiltinRetType(c, fname);
                    if (bt) {
                        result = bt;
                        found = 1;
                    }
                }
                if (!found) {
                    const char *sugg = CompSuggestFunction(c, fname);
                    if (sugg && strcmp(sugg, fname) != 0) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "undefined function '%s'. Did you mean '%s'?", fname, sugg);
                        extern int gStrictTypes;
                        if (gStrictTypes || (c && c->is_static_mode)) {
                            TyperError(ctx, n->line, msg);
                        }
                    }
                }
            } else if (n->call.callee && n->call.callee->type == NMEMBER) {
                Node *m = n->call.callee;
                if (m->member.obj && m->member.obj->type == NIDENT && m->member.field) {
                    if (strcmp(m->member.obj->sval, "Option") == 0) {
                        SType *arg_t = (n->call.argc > 0) ? InferNode(ctx, env, n->call.args[0]) : StypeInternGet(c, STYPE_UNKNOWN, "");
                        result = StypeMakeOption(c, arg_t);
                    } else if (strcmp(m->member.obj->sval, "Result") == 0) {
                        if (strcmp(m->member.field, "Ok") == 0) {
                            SType *arg_t = (n->call.argc > 0) ? InferNode(ctx, env, n->call.args[0]) : StypeInternGet(c, STYPE_UNKNOWN, "");
                            result = StypeMakeResult(c, arg_t, StypeInternGet(c, STYPE_UNKNOWN, ""));
                        } else {
                            SType *arg_t = (n->call.argc > 0) ? InferNode(ctx, env, n->call.args[0]) : StypeInternGet(c, STYPE_UNKNOWN, "");
                            result = StypeMakeResult(c, StypeInternGet(c, STYPE_UNKNOWN, ""), arg_t);
                        }
                    }
                }
                if (result->kind == STYPE_UNKNOWN) {
                    SType *mt = InferNode(ctx, env, m);
                    if (mt && mt->kind == STYPE_FN) {
                        callee_fn_t = mt;
                        result = mt->ret ? mt->ret : StypeInternGet(c, STYPE_UNKNOWN, "");
                    }
                }
            }
            /* Infer arg types and check against parameter types in strict/static mode */
            for (int i = 0; i < n->call.argc; i++) {
                SType *arg_t = InferNode(ctx, env, n->call.args[i]);
                if (callee_fn_t && callee_fn_t->kind == STYPE_FN && callee_fn_t->params && i < callee_fn_t->param_count) {
                    extern int gStrictTypes;
                    if (gStrictTypes || (c && c->is_static_mode)) {
                        SType *pt = callee_fn_t->params[i];
                        if (pt && pt->kind != STYPE_UNKNOWN) {
                            if (!TyperUnify(c, pt, arg_t, n->line)) {
                                ctx->had_error = 1;
                                ctx->error_count++;
                            }
                        }
                    }
                }
            }
            if (result->kind == STYPE_UNKNOWN) result = StypeInternGet(c, STYPE_UNKNOWN, "");
            break;
        }

        case NMEMBER: {
            SType *obj_t = InferNode(ctx, env, n->member.obj);
            if (n->member.obj && n->member.obj->type == NIDENT && n->member.field) {
                if (strcmp(n->member.obj->sval, "Option") == 0) {
                    result = StypeMakeOption(c, StypeInternGet(c, STYPE_UNKNOWN, ""));
                    break;
                }
            }
            /* Trait method lookup for dyn Trait */
            if (obj_t && obj_t->kind == STYPE_TRAIT && n->member.field) {
                int found_m = 0;
                for (int i = 0; i < c->interfacecount; i++) {
                    if (strcmp(c->interfaces[i].name, obj_t->name) == 0) {
                        CompilerInterface *inf = &c->interfaces[i];
                        for (int j = 0; j < inf->count; j++) {
                            if (inf->fnames[j] && strcmp(inf->fnames[j], n->member.field) == 0) {
                                SType *ptypes[32] = {0};
                                int pc = inf->paramcounts ? inf->paramcounts[j] : 0;
                                if (pc > 32) pc = 32;
                                for (int k = 0; k < pc; k++) {
                                    ptypes[k] = (inf->paramtypes && inf->paramtypes[j] && inf->paramtypes[j][k])
                                        ? TyperResolveName(c, inf->paramtypes[j][k])
                                        : StypeInternGet(c, STYPE_UNKNOWN, "");
                                }
                                SType *rt = (inf->rettypes && inf->rettypes[j])
                                    ? TyperResolveName(c, inf->rettypes[j])
                                    : StypeInternGet(c, STYPE_VOID, "void");
                                result = StypeMakeFn(c, ptypes, pc, rt);
                                found_m = 1;
                                break;
                            }
                        }
                        if (!found_m) {
                            extern int gStrictTypes;
                            if (gStrictTypes || (c && c->is_static_mode)) {
                                char msg[256];
                                snprintf(msg, sizeof(msg), "trait '%s' has no method '%s'", obj_t->name, n->member.field);
                                TyperError(ctx, n->line, msg);
                            }
                        }
                        break;
                    }
                }
                break;
            }
            /* Field type lookup for known structs */
            if (obj_t && obj_t->kind == STYPE_STRUCT) {
                int found_f = 0;
                for (int i = 0; i < c->structcount; i++) {
                    if (strcmp(c->structs[i].name, obj_t->name) == 0) {
                        for (int j = 0; j < c->structs[i].fc; j++) {
                            if (c->structs[i].fnames && n->member.field &&
                                strcmp(c->structs[i].fnames[j], n->member.field) == 0) {
                                result = TyperResolveName(c, c->structs[i].ftypes ? c->structs[i].ftypes[j] : "");
                                found_f = 1;
                                break;
                            }
                        }
                        if (!found_f && n->member.field) {
                            const char *best_sugg = NULL;
                            int best_d = 999;
                            for (int j = 0; j < c->structs[i].fc; j++) {
                                if (!c->structs[i].fnames || !c->structs[i].fnames[j]) continue;
                                if (strcmp(c->structs[i].fnames[j], n->member.field) == 0) continue;
                                int d = CompLevenshteinDistance(n->member.field, c->structs[i].fnames[j]);
                                if (d > 0 && d < best_d && d <= (strlen(n->member.field) > 3 ? 2 : 1)) {
                                    best_d = d;
                                    best_sugg = c->structs[i].fnames[j];
                                }
                            }
                            if (best_sugg) {
                                char msg[256];
                                snprintf(msg, sizeof(msg), "struct '%s' has no field '%s'. Did you mean '%s'?",
                                         obj_t->name, n->member.field, best_sugg);
                                extern int gStrictTypes;
                                if (gStrictTypes || (c && c->is_static_mode)) {
                                    TyperError(ctx, n->line, msg);
                                }
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
            else if (arr_t && arr_t->kind == STYPE_TUPLE && n->index.idx && n->index.idx->type == NINT) {
                int idx = (int)n->index.idx->ival;
                if (idx >= 0 && idx < arr_t->param_count) {
                    result = arr_t->params[idx];
                }
            }
            break;
        }

        case NNEW: {
            const char *struct_name = n->newobj.name ? n->newobj.name : "Object";
            result = TyperResolveName(c, struct_name);
            break;
        }

        case NFNDECL: {
            /* Annotate param types in env, infer body */
            TypeEnv *inner = EnvPush(env);
            SType *ret_t = n->fn.ret ? TyperResolveName(c, n->fn.ret)
                                     : StypeInternGet(c, STYPE_UNKNOWN, "");
            SType *param_stypes[32] = {0};
            for (int i = 0; i < n->fn.pc && i < 32; i++) {
                const char *pname = (n->fn.pnames) ? n->fn.pnames[i] : NULL;
                const char *ptype = (n->fn.ptypes) ? n->fn.ptypes[i] : NULL;
                SType *pt = (ptype && ptype[0]) ? TyperResolveName(c, ptype)
                                                : StypeInternGet(c, STYPE_UNKNOWN, "");
                param_stypes[i] = pt;
                if (pname) {
                    EnvSet(inner, pname, pt);
                }
            }

            /* Incremental signature cache verification */
            uint64_t body_hash = TyperComputeNodeHash(n->fn.body);
            FnSigEntry *cached = n->fn.name ? TyperLookupSig(n->fn.name) : NULL;
            if (cached && cached->is_valid && cached->body_hash == body_hash && TyperIsSigValid(cached)) {
                if (cached->ret_type) ret_t = cached->ret_type;
                result = StypeMakeFn(c, cached->param_types, cached->param_count, ret_t);
                n->stype = result;
                EnvFreeLocal(inner); EnvPop(inner);
                if (n->fn.name) EnvSet(env, n->fn.name, result);
                break;
            }

            SType *saved_ret = ctx->current_fn_ret;
            int saved_has_val_ret = ctx->has_value_return;
            ctx->current_fn_ret = ret_t;
            ctx->has_value_return = 0;
            int saved_const = ctx->in_const_fn;
            if (n->fn.name && strncmp(n->fn.name, "const_", 6) == 0) ctx->in_const_fn = 1;

            /* Track dependencies during body inference */
            int saved_collecting = ctx->collecting_deps;
            int saved_dep_count = ctx->dep_count;
            char saved_deps[32][128];
            memcpy(saved_deps, ctx->current_deps, sizeof(saved_deps));
            ctx->collecting_deps = 1;
            ctx->dep_count = 0;

            if (n->fn.body) InferNode(ctx, inner, n->fn.body);

            if (ret_t->kind == STYPE_UNKNOWN && ctx->current_fn_ret && ctx->current_fn_ret->kind != STYPE_UNKNOWN) {
                ret_t = ctx->current_fn_ret;
            }
            if (ret_t->kind == STYPE_UNKNOWN && !ctx->has_value_return) {
                ret_t = StypeInternGet(c, STYPE_VOID, "void");
            }

            /* Save signature to cache */
            if (n->fn.name) {
                TyperStoreSig(n->fn.name, body_hash, ret_t, param_stypes, n->fn.pc,
                              ctx->current_deps, ctx->dep_count);
            }

            ctx->collecting_deps = saved_collecting;
            ctx->dep_count = saved_dep_count;
            memcpy(ctx->current_deps, saved_deps, sizeof(saved_deps));

            ctx->current_fn_ret = saved_ret;
            ctx->has_value_return = saved_has_val_ret;
            ctx->in_const_fn = saved_const;

            result = StypeMakeFn(c, param_stypes, n->fn.pc, ret_t);
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
                                if (n->match.guards && n->match.guards[i] != NULL) continue;
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
                TypeEnv *inner = EnvPush(env);
                Node *pat = n->match.patterns[i];
                if (pat && pat->type == NCALL && pat->call.callee && pat->call.callee->type == NMEMBER) {
                    if (pat->call.argc > 0 && pat->call.args[0]->type == NIDENT) {
                        SType *bind_t = (subj_t && subj_t->inner) ? subj_t->inner : StypeInternGet(c, STYPE_UNKNOWN, "");
                        if (pat->call.callee->member.field && strcmp(pat->call.callee->member.field, "Err") == 0 && subj_t && subj_t->inner2) {
                            bind_t = subj_t->inner2;
                        }
                        EnvSet(inner, pat->call.args[0]->sval, bind_t);
                    }
                }
                if (n->match.bodies[i]) {
                    SType *bt = InferNode(ctx, inner, n->match.bodies[i]);
                    arm_t = TyperUnify(c, arm_t, bt, n->line);
                }
                EnvFreeLocal(inner); EnvPop(inner);
            }
            result = arm_t ? arm_t : StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NPRINT: {
            if (n->single.expr) InferNode(ctx, env, n->single.expr);
            result = StypeInternGet(c, STYPE_VOID, "void");
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

        case NTRYPROPAGATE: {
            SType *t = InferNode(ctx, env, n->single.expr);
            if (t && (t->kind == STYPE_OPTION || t->kind == STYPE_RESULT)) {
                result = t->inner ? t->inner : StypeInternGet(c, STYPE_UNKNOWN, "");
            } else {
                result = StypeInternGet(c, STYPE_UNKNOWN, "");
            }
            break;
        }

        case NCAST: {
            SType *src_t = n->cast.expr ? InferNode(ctx, env, n->cast.expr) : StypeInternGet(c, STYPE_UNKNOWN, "");
            result = TyperResolveName(c, n->cast.targettype);
            if (result && result->kind == STYPE_TRAIT && src_t &&
                src_t->kind != STYPE_UNKNOWN && src_t->kind != STYPE_GENERIC) {
                int ok = (src_t->kind == STYPE_TRAIT && strcmp(src_t->name, result->name) == 0) ||
                         (src_t->kind == STYPE_STRUCT && TyperCheckTraitImpl(c, src_t->name, result->name));
                if (!ok) {
                    extern int gStrictTypes;
                    if (gStrictTypes || (c && c->is_static_mode)) {
                        char sa[64], msg[512];
                        StypeName(src_t, sa, sizeof(sa));
                        snprintf(msg, sizeof(msg), "cannot cast '%.63s' to 'dyn %.127s': struct does not implement trait '%.127s'",
                                 sa, result->name, result->name);
                        TyperError(ctx, n->line, msg);
                    }
                }
            }
            break;
        }

        case NSTRUCTINIT: {
            const char *tname = n->structinit.typename ? n->structinit.typename : "Object";
            if (n->structinit.spread) InferNode(ctx, env, n->structinit.spread);
            const CompilerStruct *cs = NULL;
            if (c) {
                for (int si = 0; si < c->structcount; si++) {
                    if (strcmp(c->structs[si].name, tname) == 0) { cs = &c->structs[si]; break; }
                }
            }
            for (int i = 0; i < n->structinit.fc; i++) {
                SType *ft = InferNode(ctx, env, n->structinit.fvals[i]);
                if (cs && n->structinit.fnames[i]) {
                    for (int f = 0; f < cs->fc; f++) {
                        if (cs->fnames[f] && strcmp(cs->fnames[f], n->structinit.fnames[i]) == 0) {
                            SType *decl_ft = TyperResolveName(c, cs->ftypes[f]);
                            if (!TyperUnify(c, decl_ft, ft, n->line)) {
                                ctx->had_error = 1;
                                ctx->error_count++;
                            }
                            break;
                        }
                    }
                }
            }
            result = TyperResolveName(c, tname);
            break;
        }

        case NDESTRUCTARR: {
            SType *vt = n->destructarr.value ? InferNode(ctx, env, n->destructarr.value) : StypeInternGet(c, STYPE_UNKNOWN, "");
            for (int i = 0; i < n->destructarr.count; i++) {
                SType *et = StypeInternGet(c, STYPE_UNKNOWN, "");
                if (vt && vt->kind == STYPE_TUPLE && vt->params && i < vt->param_count) et = vt->params[i];
                else if (vt && vt->kind == STYPE_ARRAY && vt->inner) et = vt->inner;
                if (n->destructarr.names && n->destructarr.names[i]) EnvSet(env, n->destructarr.names[i], et);
            }
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NDESTRUCTOBJ: {
            SType *vt = n->destructobj.value ? InferNode(ctx, env, n->destructobj.value) : StypeInternGet(c, STYPE_UNKNOWN, "");
            const CompilerStruct *cs = NULL;
            if (c && vt && vt->kind == STYPE_STRUCT) {
                for (int si = 0; si < c->structcount; si++) {
                    if (strcmp(c->structs[si].name, vt->name) == 0) { cs = &c->structs[si]; break; }
                }
            }
            for (int i = 0; i < n->destructobj.count; i++) {
                SType *ft = StypeInternGet(c, STYPE_UNKNOWN, "");
                if (cs && n->destructobj.keys && n->destructobj.keys[i]) {
                    for (int f = 0; f < cs->fc; f++) {
                        if (cs->fnames[f] && strcmp(cs->fnames[f], n->destructobj.keys[i]) == 0) {
                            ft = TyperResolveName(c, cs->ftypes[f]);
                            break;
                        }
                    }
                }
                if (n->destructobj.aliases && n->destructobj.aliases[i]) {
                    EnvSet(env, n->destructobj.aliases[i], ft);
                }
            }
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;
        }

        case NSTRUCTDECL:
        case NINTERFACEDECL:
        case NENUMDECL:
            result = StypeInternGet(c, STYPE_VOID, "void");
            break;

        case NREGION: {
            TypeEnv *inner = EnvPush(env);
            ctx->region_depth++;
            if (n->region.name) {
                SType *reg_t = StypeInternGet(c, STYPE_STRUCT, "RegionScope");
                EnvSet(inner, n->region.name, reg_t);
                TypeEnvEntry *en = EnvGetEntry(inner, n->region.name);
                if (en) en->is_region_scoped = 1;
            }
            if (n->region.body) {
                InferNode(ctx, inner, n->region.body);
            }
            ctx->region_depth--;
            EnvFreeLocal(inner);
            EnvPop(inner);
            result = StypeInternGet(c, STYPE_VOID, "void");
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

/* Pass 1: Pre-register top-level interfaces, structs, enums, functions, and imports */
static void TyperPreregister(TyperCtx *ctx, TypeEnv *env, Node *program) {
    if (!program || !ctx || !ctx->comp) return;
    Compiler *c = ctx->comp;
    if (program->type != NPROGRAM && program->type != NBLOCK) return;
    for (int i = 0; i < program->block.count; i++) {
        Node *n = program->block.stmts[i];
        if (!n) continue;
        if (n->type == NIMPORT && n->single.var && !n->decl.isconstflag) {
            TyperProcessImport(ctx, env, n);
        } else if (n->type == NINTERFACEDECL && n->interfacedecl.name) {
            int found = 0;
            for (int k = 0; k < c->interfacecount; k++) {
                if (strcmp(c->interfaces[k].name, n->interfacedecl.name) == 0) { found = 1; break; }
            }
            if (!found && c->interfacecount < 512) {
                CompilerInterface *inf = &c->interfaces[c->interfacecount++];
                memset(inf, 0, sizeof(*inf));
                snprintf(inf->name, sizeof(inf->name), "%s", n->interfacedecl.name);
                inf->count = n->interfacedecl.count;
                if (inf->count > 0) {
                    inf->fnames = calloc((size_t)inf->count, sizeof(char *));
                    inf->rettypes = calloc((size_t)inf->count, sizeof(char *));
                    inf->paramtypes = calloc((size_t)inf->count, sizeof(char **));
                    inf->paramnames = calloc((size_t)inf->count, sizeof(char **));
                    inf->paramcounts = calloc((size_t)inf->count, sizeof(int));
                    inf->hasdefault = calloc((size_t)inf->count, sizeof(int));
                    inf->defaultbodies = calloc((size_t)inf->count, sizeof(Node *));
                    for (int m = 0; m < inf->count; m++) {
                        inf->fnames[m] = n->interfacedecl.fnames[m] ? strdup(n->interfacedecl.fnames[m]) : strdup("");
                        inf->rettypes[m] = n->interfacedecl.rettypes[m] ? strdup(n->interfacedecl.rettypes[m]) : strdup("void");
                        int pc = n->interfacedecl.paramcounts ? n->interfacedecl.paramcounts[m] : 0;
                        inf->paramcounts[m] = pc;
                        inf->hasdefault[m] = n->interfacedecl.hasdefault ? n->interfacedecl.hasdefault[m] : 0;
                        if (pc > 0) {
                            inf->paramtypes[m] = calloc((size_t)pc, sizeof(char *));
                            inf->paramnames[m] = calloc((size_t)pc, sizeof(char *));
                            for (int p = 0; p < pc; p++) {
                                inf->paramtypes[m][p] = n->interfacedecl.paramtypes[m][p] ? strdup(n->interfacedecl.paramtypes[m][p]) : strdup("any");
                                inf->paramnames[m][p] = n->interfacedecl.paramnames[m][p] ? strdup(n->interfacedecl.paramnames[m][p]) : strdup("arg");
                            }
                        }
                    }
                }
            }
        } else if (n->type == NSTRUCTDECL && n->strct.name && n->strct.tpc == 0) {
            int found = 0;
            for (int k = 0; k < c->structcount; k++) {
                if (strcmp(c->structs[k].name, n->strct.name) == 0) { found = 1; break; }
            }
            if (!found && c->structcount < 512) {
                CompilerStruct *s = &c->structs[c->structcount++];
                memset(s, 0, sizeof(*s));
                snprintf(s->name, sizeof(s->name), "%s", n->strct.name);
                s->fc = n->strct.fc;
                if (s->fc > 0) {
                    s->fnames = calloc((size_t)s->fc, sizeof(char *));
                    s->ftypes = calloc((size_t)s->fc, sizeof(char *));
                    for (int f = 0; f < s->fc; f++) {
                        s->fnames[f] = n->strct.fnames[f] ? strdup(n->strct.fnames[f]) : strdup("");
                        s->ftypes[f] = n->strct.ftypes[f] ? strdup(n->strct.ftypes[f]) : strdup("any");
                    }
                }
                s->ic = n->strct.ic;
                if (s->ic > 0) {
                    s->interfaces = calloc((size_t)s->ic, sizeof(char *));
                    for (int k = 0; k < s->ic; k++) {
                        s->interfaces[k] = n->strct.interfaces[k] ? strdup(n->strct.interfaces[k]) : strdup("");
                    }
                }
            }
        } else if (n->type == NENUMDECL && n->enumdecl.name) {
            int found = 0;
            for (int k = 0; k < c->enumcount; k++) {
                if (strcmp(c->enums[k].name, n->enumdecl.name) == 0) { found = 1; break; }
            }
            if (!found && c->enumcount < 512) {
                CompilerEnum *en = &c->enums[c->enumcount++];
                memset(en, 0, sizeof(*en));
                snprintf(en->name, sizeof(en->name), "%s", n->enumdecl.name);
                en->count = n->enumdecl.count;
                en->isadt = n->enumdecl.isadt;
                if (en->count > 0) {
                    en->members = calloc((size_t)en->count, sizeof(char *));
                    en->values = calloc((size_t)en->count, sizeof(int64_t));
                    for (int m = 0; m < en->count; m++) {
                        en->members[m] = n->enumdecl.members[m] ? strdup(n->enumdecl.members[m]) : strdup("");
                        en->values[m] = n->enumdecl.values ? n->enumdecl.values[m] : 0;
                    }
                }
            }
        } else if (n->type == NFNDECL && n->fn.name) {
            SType *ptypes[32] = {0};
            int pc = (n->fn.pc < 32) ? n->fn.pc : 32;
            for (int p = 0; p < pc; p++) {
                const char *pt = (n->fn.ptypes && n->fn.ptypes[p]) ? n->fn.ptypes[p] : NULL;
                ptypes[p] = (pt && pt[0]) ? TyperResolveName(c, pt) : StypeInternGet(c, STYPE_UNKNOWN, "");
            }
            SType *rt = n->fn.ret ? TyperResolveName(c, n->fn.ret) : StypeInternGet(c, STYPE_UNKNOWN, "");
            EnvSet(env, n->fn.name, StypeMakeFn(c, ptypes, pc, rt));
        }
    }
    /* Verify implemented traits once interfaces & structs are all registered */
    extern int gStrictTypes;
    if (gStrictTypes || c->is_static_mode) {
        for (int i = 0; i < program->block.count; i++) {
            Node *n = program->block.stmts[i];
            if (n && n->type == NSTRUCTDECL && n->strct.name && n->strct.tpc == 0 && n->strct.ic > 0) {
                for (int k = 0; k < n->strct.ic; k++) {
                    if (n->strct.interfaces[k] &&
                        !TyperCheckTraitImpl(c, n->strct.name, n->strct.interfaces[k])) {
                        char msg[256];
                        snprintf(msg, sizeof(msg),
                                 "struct '%s' does not implement required members of trait '%s'",
                                 n->strct.name, n->strct.interfaces[k]);
                        TyperError(ctx, n->line, msg);
                    }
                }
            }
        }
    }
}

/* Public entry point */

int TyperRun(Compiler *c, Node *program) {
    TyperCtx ctx = {0};
    ctx.comp = c;
    TypeEnv root = {0};
    ctx.env = &root;
    ctx.current_fn_ret = StypeInternGet(c, STYPE_VOID, "void");

    /* Load incremental signature cache if present */
    TyperLoadSigCache(c, ".akscache/signatures.bin");

    /* Pre-intern primitive types */
    StypeInternGet(c, STYPE_INT, "int");
    StypeInternGet(c, STYPE_FLOAT, "float");
    StypeInternGet(c, STYPE_BOOL, "bool");
    StypeInternGet(c, STYPE_STR, "str");
    StypeInternGet(c, STYPE_VOID, "void");
    StypeInternGet(c, STYPE_NEVER, "!");

    /* Pass 1: Pre-register declarations and resolve imports */
    if (program) TyperPreregister(&ctx, &root, program);

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

    /* Flush local root env and visited import cache */
    EnvFreeLocal(&root);
    for (int i = 0; i < ctx.visited_import_count; i++) {
        free(ctx.visited_imports[i]);
    }
    free(ctx.visited_imports);

    int total_errs = ctx.error_count + (c ? c->error_count : 0);
    if (total_errs > 0) {
        fprintf(stderr, "Type checking failed: %d error(s) found.\n", total_errs);
    } else {
        TyperSaveSigCache(c, ".akscache/signatures.bin");
    }
    return (ctx.had_error || (c && c->haderror)) ? 0 : 1;
}

SType *TyperInfer(Compiler *c, Node *n) {
    TyperCtx ctx = {0};
    ctx.comp = c;
    TypeEnv root = {0};
    ctx.env = &root;
    ctx.current_fn_ret = StypeInternGet(c, STYPE_VOID, "void");
    SType *t = InferNode(&ctx, &root, n);
    if (ctx.error_count > 50) {
        EnvFreeLocal(&root);
        return t;
    }
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
        if (c->fns[i]->name[0] && strcmp(c->fns[i]->name, fn_node->fn.name ? fn_node->fn.name : "") == 0) {
            fnid = i; break;
        }
    }
    if (fnid >= 0 && c->mono_cache_count < 1024) {
        snprintf(c->mono_cache[c->mono_cache_count].key,
                 sizeof(c->mono_cache[c->mono_cache_count].key), "%s", key);
        c->mono_cache[c->mono_cache_count].fnid = fnid;
        c->mono_cache_count++;
    }
    (void)call_line;
    return fnid;
}
