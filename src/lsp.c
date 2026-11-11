// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "aks_plat.h"
#include <fcntl.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#define _dup dup
#define _dup2 dup2
#define _fileno fileno
#define _close close
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#endif

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "compiler.h"
#include "pkg.h"
#include "diagnostic.h"
#define AKS_NO_FMT_MAIN
#include "aksfmt.c"

const char *gProgramName = "aksls.exe";
int gJitMode = 0;
int gJitCacheEnabled = 0;
int gLto = 0;
const char *gCrossTarget = NULL;
const char *gPgoProfile = NULL;
const char *gPgoUse = NULL;

// JSON parser & representation
typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
    char *key;
    JsonValue *val;
} JsonProperty;

struct JsonValue {
    JsonType type;
    union {
        int boolean;
        double number;
        char *string;
        struct { JsonValue **items; int count; } array;
        struct { JsonProperty *props; int count; } object;
    };
};

static void JSONFree(JsonValue *v) {
    if (!v) return;
    if (v->type == JSON_STRING) {
        free(v->string);
    } else if (v->type == JSON_ARRAY) {
        for (int i = 0; i < v->array.count; i++) JSONFree(v->array.items[i]);
        free(v->array.items);
    } else if (v->type == JSON_OBJECT) {
        for (int i = 0; i < v->object.count; i++) {
            free(v->object.props[i].key);
            JSONFree(v->object.props[i].val);
        }
        free(v->object.props);
    }
    free(v);
}

static const char *SkipWhitespace(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static char *ParseStringLiteral(const char **p) {
    const char *s = *p;
    if (*s != '"') return NULL;
    s++;
    const char *start = s;
    int len = 0;
    while (*s && *s != '"') {
        if (*s == '\\' && *(s + 1)) s++;
        s++; len++;
    }
    char *res = malloc(len + 1);
    int idx = 0;
    s = start;
    while (*s && *s != '"') {
        if (*s == '\\' && *(s + 1)) {
            s++;
            if (*s == 'n') res[idx++] = '\n';
            else if (*s == 't') res[idx++] = '\t';
            else if (*s == 'r') res[idx++] = '\r';
            else res[idx++] = *s;
        } else {
            res[idx++] = *s;
        }
        s++;
    }
    res[idx] = '\0';
    if (*s == '"') s++;
    *p = s;
    return res;
}

static JsonValue *ParseJSONValRPC(const char **p);

static JsonValue *ParseJSONObject(const char **p) {
    const char *s = *p;
    if (*s != '{') return NULL;
    s++;
    JsonValue *obj = calloc(1, sizeof(JsonValue));
    obj->type = JSON_OBJECT;
    int cap = 4;
    obj->object.props = malloc(cap * sizeof(JsonProperty));
    s = SkipWhitespace(s);
    while (*s && *s != '}') {
        if (*s == ',') { s++; s = SkipWhitespace(s); }
        if (*s == '}') break;
        char *key = ParseStringLiteral(&s);
        if (!key) goto Error;
        s = SkipWhitespace(s);
        if (*s != ':') { free(key); goto Error; }
        s++;
        s = SkipWhitespace(s);
        JsonValue *val = ParseJSONValRPC(&s);
        if (!val) { free(key); goto Error; }
        if (obj->object.count >= cap) {
            cap *= 2;
            obj->object.props = realloc(obj->object.props, cap * sizeof(JsonProperty));
        }
        obj->object.props[obj->object.count].key = key;
        obj->object.props[obj->object.count].val = val;
        obj->object.count++;
        s = SkipWhitespace(s);
    }
    if (*s == '}') s++;
    *p = s;
    return obj;
Error:
    JSONFree(obj);
    return NULL;
}

static JsonValue *ParseJSONArray(const char **p) {
    const char *s = *p;
    if (*s != '[') return NULL;
    s++;
    JsonValue *arr = calloc(1, sizeof(JsonValue));
    arr->type = JSON_ARRAY;
    int cap = 4;
    arr->array.items = malloc(cap * sizeof(JsonValue*));
    s = SkipWhitespace(s);
    while (*s && *s != ']') {
        if (*s == ',') { s++; s = SkipWhitespace(s); }
        if (*s == ']') break;
        JsonValue *item = ParseJSONValRPC(&s);
        if (!item) goto Error;
        if (arr->array.count >= cap) {
            cap *= 2;
            arr->array.items = realloc(arr->array.items, cap * sizeof(JsonValue*));
        }
        arr->array.items[arr->array.count++] = item;
        s = SkipWhitespace(s);
    }
    if (*s == ']') s++;
    *p = s;
    return arr;
Error:
    JSONFree(arr);
    return NULL;
}

static JsonValue *ParseJSONValRPC(const char **p) {
    const char *s = SkipWhitespace(*p);
    if (*s == '{') {
        JsonValue *v = ParseJSONObject(&s);
        *p = s;
        return v;
    }
    if (*s == '[') {
        JsonValue *v = ParseJSONArray(&s);
        *p = s;
        return v;
    }
    if (*s == '"') {
        char *str = ParseStringLiteral(&s);
        if (!str) return NULL;
        JsonValue *v = calloc(1, sizeof(JsonValue));
        v->type = JSON_STRING;
        v->string = str;
        *p = s;
        return v;
    }
    if (strncmp(s, "true", 4) == 0) {
        JsonValue *v = calloc(1, sizeof(JsonValue));
        v->type = JSON_BOOL;
        v->boolean = 1;
        *p = s + 4;
        return v;
    }
    if (strncmp(s, "false", 5) == 0) {
        JsonValue *v = calloc(1, sizeof(JsonValue));
        v->type = JSON_BOOL;
        v->boolean = 0;
        *p = s + 5;
        return v;
    }
    if (strncmp(s, "null", 4) == 0) {
        JsonValue *v = calloc(1, sizeof(JsonValue));
        v->type = JSON_NULL;
        *p = s + 4;
        return v;
    }
    if (isdigit(*s) || *s == '-') {
        char *endptr;
        double num = strtod(s, &endptr);
        JsonValue *v = calloc(1, sizeof(JsonValue));
        v->type = JSON_NUMBER;
        v->number = num;
        *p = endptr;
        return v;
    }
    return NULL;
}

static JsonValue *JSONParse(const char *json) {
    const char *p = json;
    return ParseJSONValRPC(&p);
}

static JsonValue *JSONGet(JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < obj->object.count; i++) {
        if (strcmp(obj->object.props[i].key, key) == 0) {
            return obj->object.props[i].val;
        }
    }
    return NULL;
}

static const char *JSONGetStr(JsonValue *obj, const char *key) {
    JsonValue *v = JSONGet(obj, key);
    return (v && v->type == JSON_STRING) ? v->string : NULL;
}

// Document & symbol cache
typedef struct {
    char *uri;
    char *text;
    int len;
} Document;

static Document *g_docs = NULL;
static int g_docs_count = 0;

static void DocUpdate(const char *uri, const char *text) {
    if (!uri || !text) return;
    for (int i = 0; i < g_docs_count; i++) {
        if (g_docs[i].uri && strcmp(g_docs[i].uri, uri) == 0) {
            free(g_docs[i].text);
            g_docs[i].text = strdup(text);
            g_docs[i].len = (int)strlen(text);
            return;
        }
    }
    g_docs = realloc(g_docs, (g_docs_count + 1) * sizeof(Document));
    g_docs[g_docs_count].uri = strdup(uri);
    g_docs[g_docs_count].text = strdup(text);
    g_docs[g_docs_count].len = (int)strlen(text);
    g_docs_count++;
}

static Document *DocGet(const char *uri) {
    if (!uri) return NULL;
    for (int i = 0; i < g_docs_count; i++) {
        if (g_docs[i].uri && strcmp(g_docs[i].uri, uri) == 0) return &g_docs[i];
    }
    return NULL;
}

// Symbol and AST visiting
typedef enum {
    SYM_FN, SYM_STRUCT, SYM_ENUM, SYM_INTERFACE, SYM_VAR
} SymbolKind;

typedef struct {
    char *name;
    char *uri;
    SymbolKind kind;
    int line; // 1-based
    int col;  // 1-based
    char *detail;
    int param_count;
    char **params;
    char *ret_type;
} SymbolDef;

static SymbolDef *g_syms = NULL;
static int g_syms_count = 0;

static int LspUriEqual(const char *a, const char *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

static const char *LspStristr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return p;
    }
    return NULL;
}

static void SymFreeEntry(SymbolDef *s) {
    if (!s) return;
    free(s->name);
    free(s->uri);
    free(s->detail);
    if (s->ret_type) free(s->ret_type);
    if (s->params) {
        for (int p = 0; p < s->param_count; p++) {
            free(s->params[p]);
        }
        free(s->params);
    }
}

static void SymAddExUri(const char *uri, const char *name, SymbolKind kind, int line, int col, const char *detail, int param_count, char **params, const char *ret_type) {
    if (!name || !*name) return;
    SymbolDef *grown = realloc(g_syms, (size_t)(g_syms_count + 1) * sizeof(SymbolDef));
    if (!grown) return;
    g_syms = grown;
    SymbolDef *s = &g_syms[g_syms_count++];
    s->name = strdup(name);
    s->uri = strdup(uri ? uri : "");
    s->kind = kind;
    s->line = line;
    s->col = col;
    s->detail = strdup(detail ? detail : "");
    s->param_count = param_count;
    if (param_count > 0 && params) {
        s->params = malloc((size_t)param_count * sizeof(char *));
        if (s->params) {
            for (int i = 0; i < param_count; i++) {
                s->params[i] = strdup(params[i] ? params[i] : "");
            }
        } else {
            s->param_count = 0;
        }
    } else {
        s->params = NULL;
    }
    s->ret_type = ret_type ? strdup(ret_type) : NULL;
}

static void SymAddUri(const char *uri, const char *name, SymbolKind kind, int line, int col, const char *detail) {
    SymAddExUri(uri, name, kind, line, col, detail, 0, NULL, NULL);
}

static void SymsRemoveForUri(const char *uri) {
    if (!uri) return;
    int write_idx = 0;
    for (int i = 0; i < g_syms_count; i++) {
        if (LspUriEqual(g_syms[i].uri, uri)) {
            SymFreeEntry(&g_syms[i]);
        } else {
            if (write_idx != i) {
                g_syms[write_idx] = g_syms[i];
            }
            write_idx++;
        }
    }
    g_syms_count = write_idx;
}

static void SymsClearAll(void) {
    for (int i = 0; i < g_syms_count; i++) {
        SymFreeEntry(&g_syms[i]);
    }
    free(g_syms);
    g_syms = NULL;
    g_syms_count = 0;
}

static SymbolDef *FindBestSymbol(const char *name, const char *prefer_uri, int fn_only) {
    if (!name || !*name) return NULL;
    SymbolDef *fallback = NULL;
    for (int i = 0; i < g_syms_count; i++) {
        if (fn_only && g_syms[i].kind != SYM_FN) continue;
        if (strcmp(g_syms[i].name, name) != 0) continue;
        if (prefer_uri && LspUriEqual(g_syms[i].uri, prefer_uri)) {
            return &g_syms[i];
        }
        if (!fallback) fallback = &g_syms[i];
    }
    return fallback;
}

static int LspHexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int UriToPath(const char *uri, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    out[0] = '\0';
    if (!uri || !*uri) return 0;

    const char *src = uri;
    if (strncmp(uri, "file://", 7) == 0) {
        src = uri + 7;
        if (strncmp(src, "localhost/", 10) == 0) {
            src += 9;
        }
    } else if (strstr(uri, "://") != NULL) {
        return 0;
    }

    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < out_sz; i++) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            int hi = LspHexVal(src[i + 1]);
            int lo = LspHexVal(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out[j++] = (src[i] == '\\') ? '/' : src[i];
    }
    out[j] = '\0';

#ifdef _WIN32
    if (out[0] == '/' && isalpha((unsigned char)out[1]) && out[2] == ':') {
        memmove(out, out + 1, strlen(out));
    }
#endif
    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/') {
#ifdef _WIN32
        if (len == 3 && isalpha((unsigned char)out[0]) && out[1] == ':') break;
#endif
        out[--len] = '\0';
    }
    return out[0] != '\0';
}

static void PathToUri(const char *path, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!path || !*path) return;
    if (strncmp(path, "file://", 7) == 0) {
        snprintf(out, out_sz, "%s", path);
        return;
    }
    char abs_buf[2048];
#ifdef _WIN32
    if (!_fullpath(abs_buf, path, sizeof(abs_buf))) {
        snprintf(abs_buf, sizeof(abs_buf), "%.2047s", path);
    }
#else
    char *rp = realpath(path, NULL);
    if (rp) {
        snprintf(abs_buf, sizeof(abs_buf), "%.2047s", rp);
        free(rp);
    } else {
        snprintf(abs_buf, sizeof(abs_buf), "%.2047s", path);
    }
#endif
    for (size_t i = 0; abs_buf[i]; i++) {
        if (abs_buf[i] == '\\') abs_buf[i] = '/';
    }
    int max_body = (out_sz > 10) ? (int)(out_sz - 10) : 0;
#ifdef _WIN32
    if (isalpha((unsigned char)abs_buf[0]) && abs_buf[1] == ':') {
        snprintf(out, out_sz, "file:///%.*s", max_body, abs_buf);
    } else if (abs_buf[0] == '/') {
        snprintf(out, out_sz, "file://%.*s", max_body, abs_buf);
    } else {
        snprintf(out, out_sz, "file:///%.*s", max_body, abs_buf);
    }
#else
    if (abs_buf[0] == '/') {
        snprintf(out, out_sz, "file://%.*s", max_body, abs_buf);
    } else {
        snprintf(out, out_sz, "file:///%.*s", max_body, abs_buf);
    }
#endif
}

static char *LspReadFileStr(const char *path) {
    if (!path || !*path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > 4 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t nr = fread(buf, 1, (size_t)sz, f);
    buf[nr] = '\0';
    fclose(f);
    return buf;
}

static void GetColumnForLine(const char *text, int line, const char *name, int *out_col) {
    *out_col = 1;
    int cur_line = 1;
    const char *p = text;
    while (*p && cur_line < line) {
        if (*p == '\n') cur_line++;
        p++;
    }
    if (cur_line == line) {
        const char *line_start = p;
        const char *found = strstr(line_start, name);
        if (found) {
            // make sure we don't bleed into next lines
            const char *nl = strchr(line_start, '\n');
            if (!nl || found < nl) {
                *out_col = (int)(found - line_start) + 1;
            }
        }
    }
}

static void WalkASTForUri(Node *n, const char *text, const char *uri) {
    if (!n) return;
    int col = 1;
    switch (n->type) {
        case NFNDECL:
            if (n->fn.name) {
                GetColumnForLine(text, n->line, n->fn.name, &col);
                char detail[1024];
                char param_buf[1024] = "";
                size_t pb_len = 0;
                char **param_strs = NULL;
                if (n->fn.pc > 0) {
                    param_strs = malloc((size_t)n->fn.pc * sizeof(char *));
                    for (int i = 0; i < n->fn.pc; i++) {
                        char single_p[256];
                        const char *pname = (n->fn.pnames && n->fn.pnames[i]) ? n->fn.pnames[i] : "arg";
                        const char *ptype = (n->fn.ptypes && n->fn.ptypes[i]) ? n->fn.ptypes[i] : NULL;
                        if (ptype && ptype[0] != '\0') {
                            snprintf(single_p, sizeof(single_p), "%s: %s", pname, ptype);
                        } else {
                            snprintf(single_p, sizeof(single_p), "%s", pname);
                        }
                        if (param_strs) param_strs[i] = strdup(single_p);
                        if (pb_len + 4 < sizeof(param_buf)) {
                            int w = snprintf(param_buf + pb_len, sizeof(param_buf) - pb_len,
                                             "%s%s", i > 0 ? ", " : "", single_p);
                            if (w > 0) {
                                size_t adv = (size_t)w;
                                pb_len += (pb_len + adv < sizeof(param_buf)) ? adv : (sizeof(param_buf) - 1 - pb_len);
                            }
                        }
                    }
                }
                if (n->fn.ret && n->fn.ret[0] != '\0') {
                    snprintf(detail, sizeof(detail), "fn %s(%s) -> %s", n->fn.name, param_buf, n->fn.ret);
                } else {
                    snprintf(detail, sizeof(detail), "fn %s(%s)", n->fn.name, param_buf);
                }
                SymAddExUri(uri, n->fn.name, SYM_FN, n->line, col, detail, n->fn.pc, param_strs, n->fn.ret);
                if (param_strs) {
                    for (int i = 0; i < n->fn.pc; i++) free(param_strs[i]);
                    free(param_strs);
                }
            }
            WalkASTForUri(n->fn.body, text, uri);
            break;
        case NSTRUCTDECL:
            if (n->strct.name) {
                GetColumnForLine(text, n->line, n->strct.name, &col);
                char detail[256];
                snprintf(detail, sizeof(detail), "struct %s", n->strct.name);
                SymAddUri(uri, n->strct.name, SYM_STRUCT, n->line, col, detail);
            }
            break;
        case NINTERFACEDECL:
            if (n->interfacedecl.name) {
                GetColumnForLine(text, n->line, n->interfacedecl.name, &col);
                char detail[256];
                snprintf(detail, sizeof(detail), "interface %s", n->interfacedecl.name);
                SymAddUri(uri, n->interfacedecl.name, SYM_INTERFACE, n->line, col, detail);
            }
            break;
        case NVARDECL:
        case NCONSTDECL:
            if (n->decl.name) {
                GetColumnForLine(text, n->line, n->decl.name, &col);
                SymAddUri(uri, n->decl.name, SYM_VAR, n->line, col, "variable");
            }
            WalkASTForUri(n->decl.init, text, uri);
            break;
        case NENUMDECL:
            if (n->enumdecl.name) {
                GetColumnForLine(text, n->line, n->enumdecl.name, &col);
                SymAddUri(uri, n->enumdecl.name, SYM_ENUM, n->line, col, "enum");
            }
            break;
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) WalkASTForUri(n->block.stmts[i], text, uri);
            break;
        case NIF:
            WalkASTForUri(n->ifn.cond, text, uri);
            WalkASTForUri(n->ifn.then, text, uri);
            WalkASTForUri(n->ifn.els, text, uri);
            break;
        case NWHILE:
            WalkASTForUri(n->whilen.cond, text, uri);
            WalkASTForUri(n->whilen.body, text, uri);
            break;
        case NFOR:
            WalkASTForUri(n->forn.init, text, uri);
            WalkASTForUri(n->forn.cond, text, uri);
            WalkASTForUri(n->forn.post, text, uri);
            WalkASTForUri(n->forn.body, text, uri);
            break;
        case NRETURN:
        case NPRINT:
        case NEXPRSTMT:
            WalkASTForUri(n->single.expr, text, uri);
            break;
        case NBINOP:
            WalkASTForUri(n->binop.left, text, uri);
            WalkASTForUri(n->binop.right, text, uri);
            break;
        default:
            break;
    }
}

static int LspIsCHeaderName(const char *path) {
    if (!path) return 0;
    size_t len = strlen(path);
    if (len >= 2 && strcmp(path + len - 2, ".h") == 0) return 1;
    if (len >= 4 && (strcmp(path + len - 4, ".hpp") == 0 || strcmp(path + len - 4, ".hxx") == 0)) return 1;
    return 0;
}

static void CollectImportsFromAST(Node *n, const char **out_imps, int *count, int max_imps) {
    if (!n || *count >= max_imps) return;
    if (n->type == NIMPORT) {
        if (!n->single.is_c && n->single.var && n->single.var[0] != '\0' && !LspIsCHeaderName(n->single.var)) {
            out_imps[(*count)++] = n->single.var;
        }
        return;
    }
    if (n->type == NPROGRAM || n->type == NBLOCK) {
        for (int i = 0; i < n->block.count && *count < max_imps; i++) {
            CollectImportsFromAST(n->block.stmts[i], out_imps, count, max_imps);
        }
    }
}

static char *LspResolveImportFile(const char *imp_path, const char *cur_fs_path) {
    if (!imp_path || !*imp_path || LspIsCHeaderName(imp_path)) return NULL;
    char *resolved = PkgResolveImport(imp_path, cur_fs_path);
    if (resolved) {
        FILE *f = fopen(resolved, "rb");
        if (f) {
            fclose(f);
            return resolved;
        }
        free(resolved);
    }
    // Fallback to sibling `<dir>/<imp>` or `<dir>/<imp>.aks`
    if (cur_fs_path && cur_fs_path[0]) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", cur_fs_path);
        for (size_t i = 0; dir[i]; i++) if (dir[i] == '\\') dir[i] = '/';
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0'; else snprintf(dir, sizeof(dir), ".");

        char *cand = malloc(2048);
        if (!cand) return NULL;
        snprintf(cand, 2048, "%s/%s", dir, imp_path);
        FILE *f = fopen(cand, "rb");
        if (f) { fclose(f); return cand; }
        if (!strstr(imp_path, ".aks")) {
            snprintf(cand, 2048, "%s/%s.aks", dir, imp_path);
            f = fopen(cand, "rb");
            if (f) { fclose(f); return cand; }
        }
        free(cand);
    }
    return NULL;
}

static int LspVisitedContains(char **visited, int count, const char *uri) {
    for (int i = 0; i < count; i++) {
        if (LspUriEqual(visited[i], uri)) return 1;
    }
    return 0;
}

static void IndexFilePathRecursive(const char *fs_path, char **visited, int *vcount, int max_visited, int depth) {
    if (!fs_path || !*fs_path || depth > 6 || *vcount >= max_visited) return;
    char uri[2048];
    PathToUri(fs_path, uri, sizeof(uri));
    if (!uri[0] || LspVisitedContains(visited, *vcount, uri)) return;
    visited[(*vcount)++] = strdup(uri);

    Document *open_doc = DocGet(uri);
    char *disk_src = open_doc ? NULL : LspReadFileStr(fs_path);
    const char *src = open_doc ? open_doc->text : disk_src;
    if (!src) return;

    SymsRemoveForUri(uri);
    int herr = 0;
    Node *prog = Parse(src, &herr);
    if (prog) {
        WalkASTForUri(prog, src, uri);
        const char *imps[64];
        int imp_count = 0;
        CollectImportsFromAST(prog, imps, &imp_count, 64);
        for (int i = 0; i < imp_count; i++) {
            char *next_path = LspResolveImportFile(imps[i], fs_path);
            if (next_path) {
                IndexFilePathRecursive(next_path, visited, vcount, max_visited, depth + 1);
                free(next_path);
            }
        }
        NodeFree(prog);
    }
    free(disk_src);
}

static void IndexImportsForParsedDoc(const char *uri, Node *prog) {
    if (!uri || !prog) return;
    char cur_fs_path[2048];
    if (!UriToPath(uri, cur_fs_path, sizeof(cur_fs_path))) return;

    const char *imps[64];
    int imp_count = 0;
    CollectImportsFromAST(prog, imps, &imp_count, 64);
    if (imp_count == 0) return;

    char *visited[64];
    int vcount = 0;
    visited[vcount++] = strdup(uri);
    for (int i = 0; i < imp_count; i++) {
        char *next_path = LspResolveImportFile(imps[i], cur_fs_path);
        if (next_path) {
            IndexFilePathRecursive(next_path, visited, &vcount, 64, 1);
            free(next_path);
        }
    }
    for (int i = 0; i < vcount; i++) free(visited[i]);
}

static int LspShouldSkipDir(const char *name) {
    if (!name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 1;
    if (strcmp(name, ".git") == 0 || strcmp(name, ".akspackages") == 0 ||
        strcmp(name, ".akscache") == 0 || strcmp(name, ".aks_cache") == 0 ||
        strcmp(name, "obj") == 0 || strcmp(name, "node_modules") == 0) {
        return 1;
    }
    return 0;
}

static int LspIsAksFilename(const char *name) {
    if (!name) return 0;
    size_t len = strlen(name);
    return len > 4 && strcmp(name + len - 4, ".aks") == 0;
}

static void IndexWorkspaceDir(const char *dir_path, int depth, char **visited, int *vcount, int max_visited) {
    if (!dir_path || !*dir_path || depth > 4 || *vcount >= max_visited) return;

#ifdef _WIN32
    char pattern[2048];
    snprintf(pattern, sizeof(pattern), "%s/*", dir_path);
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (LspShouldSkipDir(ffd.cFileName)) continue;
        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ffd.cFileName);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            IndexWorkspaceDir(full, depth + 1, visited, vcount, max_visited);
        } else if (LspIsAksFilename(ffd.cFileName)) {
            IndexFilePathRecursive(full, visited, vcount, max_visited, 0);
        }
    } while (FindNextFileA(hFind, &ffd) != 0 && *vcount < max_visited);
    FindClose(hFind);
#else
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *vcount < max_visited) {
        if (LspShouldSkipDir(ent->d_name)) continue;
        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            IndexWorkspaceDir(full, depth + 1, visited, vcount, max_visited);
        } else if (S_ISREG(st.st_mode) && LspIsAksFilename(ent->d_name)) {
            IndexFilePathRecursive(full, visited, vcount, max_visited, 0);
        }
    }
    closedir(d);
#endif
}

static void IndexWorkspaceRoot(const char *root_path) {
    if (!root_path || !*root_path) return;
    char *visited[256];
    int vcount = 0;
    IndexWorkspaceDir(root_path, 0, visited, &vcount, 256);
    for (int i = 0; i < vcount; i++) free(visited[i]);
}

// Get word under cursor
static void GetWordAtPos(const char *text, int line, int character, char *out_word, int max_len) {
    out_word[0] = '\0';
    int cur_line = 0; // based
    const char *p = text;
    while (*p && cur_line < line) {
        if (*p == '\n') cur_line++;
        p++;
    }
    if (cur_line != line) return;
    const char *line_start = p;
    for (int i = 0; i < character && *p && *p != '\n'; i++) p++;
    
    // go backward to start of word
    while (p > line_start && (isalnum(*(p - 1)) || *(p - 1) == '_')) p--;
    
    int idx = 0;
    while (*p && (isalnum(*p) || *p == '_') && idx < max_len - 1) {
        out_word[idx++] = *p++;
    }
    out_word[idx] = '\0';
}

/* Windows pipe I/O: use WriteFile(GetStdHandle) for output and fgetc for input.
   fgetc correctly reads from the pipe that Python subprocess sets up.
   GetStdHandle(STD_OUTPUT_HANDLE) correctly returns the stdout pipe write handle.  */

static void LSPWrite(const char *data, int len) {
#ifdef _WIN32
    // Must use WriteFile(GetStdHandle) because CRT stdout FILE* does not
    // follow the pipe handle that Python subprocess sets in STARTUPINFO.
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    WriteFile(hOut, data, (DWORD)len, &written, NULL);
#else
    int remaining = len;
    while (remaining > 0) {
        int w = write(1, data + (len - remaining), remaining);
        if (w < 0) break;
        remaining -= w;
    }
#endif
}

static int LSPReadByte(char *out) {
    int c = fgetc(stdin);
    if (c == EOF) return 0;
    *out = (char)c;
    return 1;
}

static int LSPReadLine(char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c;
        if (!LSPReadByte(&c)) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return i;
}

static int LSPReadExact(char *buf, int len) {
    int total = 0;
    while (total < len) {
        char c;
        if (!LSPReadByte(&c)) break;
        buf[total++] = c;
    }
    return total;
}

static void EscapeJson(const char *in, char *out, size_t outsize) {
    if (!out || outsize == 0) return;
    out[0] = '\0';
    if (!in) return;
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < outsize; i++) {
        unsigned char ch = (unsigned char)in[i];
        if (ch == '"') {
            if (j + 2 >= outsize) break;
            out[j++] = '\\'; out[j++] = '"';
        } else if (ch == '\\') {
            if (j + 2 >= outsize) break;
            out[j++] = '\\'; out[j++] = '\\';
        } else if (ch == '\n') {
            if (j + 2 >= outsize) break;
            out[j++] = '\\'; out[j++] = 'n';
        } else if (ch == '\r') {
            if (j + 2 >= outsize) break;
            out[j++] = '\\'; out[j++] = 'r';
        } else if (ch == '\t') {
            if (j + 2 >= outsize) break;
            out[j++] = '\\'; out[j++] = 't';
        } else if (ch < 32) {
            // skip unprintable control chars
        } else {
            out[j++] = ch;
        }
    }
    out[j] = '\0';
}

// Json-rpc messaging output
static void SendNotification(const char *method, const char *params_json) {
    size_t needed = strlen(method) + (params_json ? strlen(params_json) : 4) + 128;
    char *body = malloc(needed);
    if (!body) return;
    int body_len = snprintf(body, needed, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}", method, params_json ? params_json : "null");
    char hdr[64];
    int hdr_len = snprintf(hdr, sizeof(hdr), "Content-Length: %d\r\n\r\n", body_len);
    LSPWrite(hdr, hdr_len);
    LSPWrite(body, body_len);
    free(body);
}

static void SendResponse(JsonValue *id, const char *result_json) {
    char id_str[64] = "null";
    if (id) {
        if (id->type == JSON_NUMBER) snprintf(id_str, sizeof(id_str), "%.0f", id->number);
        else if (id->type == JSON_STRING) snprintf(id_str, sizeof(id_str), "\"%s\"", id->string);
    }
    size_t needed = strlen(id_str) + (result_json ? strlen(result_json) : 4) + 128;
    char *body = malloc(needed);
    if (!body) return;
    int body_len = snprintf(body, needed, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}", id_str, result_json ? result_json : "null");
    char hdr[64];
    int hdr_len = snprintf(hdr, sizeof(hdr), "Content-Length: %d\r\n\r\n", body_len);
    LSPWrite(hdr, hdr_len);
    LSPWrite(body, body_len);
    free(body);
}

typedef struct {
    DiagnosticLevel level;
    char code[32];
    char message[512];
    int line;
    int col;
    int length;
    char source[32];
    DiagnosticSpan related_spans[8];
    int related_span_count;
} LspCollectedDiag;

typedef struct {
    LspCollectedDiag items[64];
    int count;
    const char *uri;
} LspDiagContext;

static void LspDiagHandler(const Diagnostic *d, void *userdata) {
    LspDiagContext *ctx = (LspDiagContext *)userdata;
    if (!ctx || ctx->count >= 64 || !d) return;

    LspCollectedDiag *item = &ctx->items[ctx->count++];
    memset(item, 0, sizeof(LspCollectedDiag));
    item->level = d->level;
    snprintf(item->code, sizeof(item->code), "%s", d->code[0] ? d->code : "E0000");
    snprintf(item->message, sizeof(item->message), "%s", d->message);
    if (d->note[0]) {
        size_t cur_len = strlen(item->message);
        snprintf(item->message + cur_len, sizeof(item->message) - cur_len, "\nNote: %s", d->note);
    }
    if (d->help[0]) {
        size_t cur_len = strlen(item->message);
        snprintf(item->message + cur_len, sizeof(item->message) - cur_len, "\nHelp: %s", d->help);
    }
    item->line = 1;
    item->col = 1;
    item->length = 1;
    snprintf(item->source, sizeof(item->source), "aks");
    item->related_span_count = 0;

    int found_primary = 0;
    for (int i = 0; i < d->span_count; i++) {
        if (d->spans[i].is_primary) {
            item->line = d->spans[i].line > 0 ? d->spans[i].line : 1;
            item->col = d->spans[i].col > 0 ? d->spans[i].col : 1;
            item->length = d->spans[i].length > 0 ? d->spans[i].length : 1;
            found_primary = 1;
        } else {
            if (item->related_span_count < 8) {
                item->related_spans[item->related_span_count++] = d->spans[i];
            }
        }
    }
    if (!found_primary && d->span_count > 0) {
        item->line = d->spans[0].line > 0 ? d->spans[0].line : 1;
        item->col = d->spans[0].col > 0 ? d->spans[0].col : 1;
        item->length = d->spans[0].length > 0 ? d->spans[0].length : 1;
    }
}

// Capture & report syntax and semantic diagnostics
static void CheckDiagnostics(const char *uri, const char *text) {
    SymsRemoveForUri(uri);

    char cap_path[MAX_PATH];
#ifdef _WIN32
    GetTempPathA(sizeof(cap_path), cap_path);
    snprintf(cap_path + strlen(cap_path), sizeof(cap_path) - strlen(cap_path), "aksls_stderr_%lu.txt", GetCurrentProcessId());
#else
    snprintf(cap_path, sizeof(cap_path), "/tmp/aksls_stderr_%lu.txt", (unsigned long)getpid());
#endif

    int orig_stderr = _dup(2);
    FILE *cap_file = fopen(cap_path, "w");
    if (cap_file) {
        _dup2(_fileno(cap_file), 2);
        fclose(cap_file);
    }

    LspDiagContext diag_ctx;
    memset(&diag_ctx, 0, sizeof(diag_ctx));
    diag_ctx.uri = uri;
    DiagnosticSetHandler(LspDiagHandler, &diag_ctx);

    int had_error = 0;
    Node *prog = Parse(text, &had_error);

    if (had_error) {
        fflush(stderr);
        FILE *err_in = fopen(cap_path, "r");
        if (err_in) {
            char line_buf[512];
            int line_num = 1;
            char msg[512] = "Syntax error";
            while (fgets(line_buf, sizeof(line_buf), err_in)) {
                int parsed_line = -1;
                char parsed_msg[512];
                if (sscanf(line_buf, "Syntax error on line %d: %[^\r\n]", &parsed_line, parsed_msg) == 2) {
                    line_num = parsed_line;
                    snprintf(msg, sizeof(msg), "%s", parsed_msg);
                    break;
                }
            }
            fclose(err_in);

            if (diag_ctx.count < 64) {
                LspCollectedDiag *item = &diag_ctx.items[diag_ctx.count++];
                memset(item, 0, sizeof(LspCollectedDiag));
                item->level = DIAG_ERROR;
                snprintf(item->code, sizeof(item->code), "E0001");
                snprintf(item->message, sizeof(item->message), "%s", msg);
                item->line = line_num > 0 ? line_num : 1;
                item->col = 1;
                item->length = 80;
                snprintf(item->source, sizeof(item->source), "aks");
            }
        }
    } else if (prog) {
        WalkASTForUri(prog, text, uri);
        IndexImportsForParsedDoc(uri, prog);

        int prev_borrow = gBorrowCheckerEnabled;
        int prev_type = gTypeCheckingEnabled;
        gBorrowCheckerEnabled = 1;
        gTypeCheckingEnabled = 1;

        Compiler *c = CompNew();
        if (c) {
            char fs_path[2048];
            if (uri && UriToPath(uri, fs_path, sizeof(fs_path))) {
                snprintf(c->currentfilepath, sizeof(c->currentfilepath), "%.511s", fs_path);
            } else if (uri) {
                snprintf(c->currentfilepath, sizeof(c->currentfilepath), "%.511s", uri);
            }
            c->borrow_checker_enabled = 1;
            c->type_checking_enabled = 1;
            CompCompile(c, prog);
            CompFree(c);
        }

        gBorrowCheckerEnabled = prev_borrow;
        gTypeCheckingEnabled = prev_type;

        fflush(stderr);
        FILE *err_in = fopen(cap_path, "r");
        if (err_in) {
            char line_buf[512];
            while (fgets(line_buf, sizeof(line_buf), err_in)) {
                int parsed_line = -1;
                char parsed_msg[512];
                if (sscanf(line_buf, "Compiler error in '%*[^']' on line %d: %[^\r\n]", &parsed_line, parsed_msg) == 2 ||
                    sscanf(line_buf, "Compiler error on line %d: %[^\r\n]", &parsed_line, parsed_msg) == 2) {
                    int already = 0;
                    for (int d = 0; d < diag_ctx.count; d++) {
                        if (diag_ctx.items[d].line == parsed_line) {
                            already = 1;
                            break;
                        }
                    }
                    if (!already && diag_ctx.count < 64) {
                        LspCollectedDiag *item = &diag_ctx.items[diag_ctx.count++];
                        memset(item, 0, sizeof(LspCollectedDiag));
                        item->level = DIAG_ERROR;
                        snprintf(item->code, sizeof(item->code), "E0002");
                        snprintf(item->message, sizeof(item->message), "%s", parsed_msg);
                        item->line = parsed_line > 0 ? parsed_line : 1;
                        item->col = 1;
                        item->length = 1;
                        snprintf(item->source, sizeof(item->source), "aks");
                    }
                }
            }
            fclose(err_in);
        }
    }

    if (prog) NodeFree(prog);

    DiagnosticSetHandler(NULL, NULL);
    fflush(stderr);
    _dup2(orig_stderr, 2);
    _close(orig_stderr);
    remove(cap_path);

    size_t diag_cap = 16384;
    char *diag_json = malloc(diag_cap);
    if (!diag_json) return;
    strcpy(diag_json, "[");
    size_t diag_len = 1;

    for (int i = 0; i < diag_ctx.count; i++) {
        LspCollectedDiag *item = &diag_ctx.items[i];
        char esc_msg[2048];
        EscapeJson(item->message, esc_msg, sizeof(esc_msg));
        char esc_code[64];
        EscapeJson(item->code, esc_code, sizeof(esc_code));

        int start_line = item->line > 0 ? item->line - 1 : 0;
        int start_char = item->col > 0 ? item->col - 1 : 0;
        int end_line = start_line;
        int end_char = start_char + (item->length > 0 ? item->length : 1);

        int severity = 1;
        if (item->level == DIAG_WARNING) severity = 2;
        else if (item->level == DIAG_NOTE) severity = 3;
        else if (item->level == DIAG_HELP) severity = 4;

        char rel_json[4096] = "";
        if (item->related_span_count > 0) {
            strcat(rel_json, ",\"relatedInformation\":[");
            for (int r = 0; r < item->related_span_count; r++) {
                DiagnosticSpan *span = &item->related_spans[r];
                int r_start_line = span->line > 0 ? span->line - 1 : 0;
                int r_start_char = span->col > 0 ? span->col - 1 : 0;
                int r_end_line = r_start_line;
                int r_end_char = r_start_char + (span->length > 0 ? span->length : 1);
                const char *r_uri = (span->file[0] != '\0') ? span->file : uri;
                char esc_label[512];
                EscapeJson(span->label, esc_label, sizeof(esc_label));

                char rel_entry[1024];
                snprintf(rel_entry, sizeof(rel_entry),
                         "%s{\"location\":{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}},\"message\":\"%s\"}",
                         r > 0 ? "," : "",
                         r_uri, r_start_line, r_start_char, r_end_line, r_end_char, esc_label);
                strcat(rel_json, rel_entry);
            }
            strcat(rel_json, "]");
        }

        char item_json[8192];
        snprintf(item_json, sizeof(item_json),
                 "%s{\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}},"
                 "\"severity\":%d,\"code\":\"%s\",\"source\":\"aks\",\"message\":\"%s\"%s}",
                 i > 0 ? "," : "",
                 start_line, start_char, end_line, end_char,
                 severity, esc_code, esc_msg, rel_json);

        size_t item_len = strlen(item_json);
        if (diag_len + item_len + 16 > diag_cap) {
            diag_cap = (diag_len + item_len + 16) * 2;
            char *new_buf = realloc(diag_json, diag_cap);
            if (!new_buf) break;
            diag_json = new_buf;
        }
        strcat(diag_json, item_json);
        diag_len += item_len;
    }
    strcat(diag_json, "]");

    size_t notif_len = strlen(uri) + strlen(diag_json) + 64;
    char *notification_buf = malloc(notif_len);
    if (notification_buf) {
        snprintf(notification_buf, notif_len, "{\"uri\":\"%s\",\"diagnostics\":%s}", uri, diag_json);
        SendNotification("textDocument/publishDiagnostics", notification_buf);
        free(notification_buf);
    }
    free(diag_json);
}

static void LspBufAppend(char **buf, size_t *cap, size_t *len, const char *piece) {
    if (!buf || !*buf || !piece) return;
    size_t plen = strlen(piece);
    if (*len + plen + 16 > *cap) {
        size_t ncap = (*len + plen + 16) * 2;
        char *nb = realloc(*buf, ncap);
        if (!nb) return;
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, piece, plen + 1);
    *len += plen;
}

static int LspSymToDocSymbolKind(SymbolKind k) {
    switch (k) {
        case SYM_FN: return 12;        /* Function */
        case SYM_STRUCT: return 23;    /* Struct */
        case SYM_ENUM: return 10;      /* Enum */
        case SYM_INTERFACE: return 11; /* Interface */
        case SYM_VAR: return 13;       /* Variable */
        default: return 13;
    }
}

static int LspSymToCompletionKind(SymbolKind k) {
    switch (k) {
        case SYM_FN: return 3;        /* Function */
        case SYM_STRUCT: return 22;   /* Struct */
        case SYM_ENUM: return 13;     /* Enum */
        case SYM_INTERFACE: return 8; /* Interface */
        case SYM_VAR: return 6;       /* Variable */
        default: return 6;
    }
}

// LSP handlers
static void HandleInitialize(JsonValue *id, JsonValue *params) {
    if (params) {
        const char *root_uri = JSONGetStr(params, "rootUri");
        if (!root_uri || !*root_uri) {
            JsonValue *folders = JSONGet(params, "workspaceFolders");
            if (folders && folders->type == JSON_ARRAY && folders->array.count > 0) {
                root_uri = JSONGetStr(folders->array.items[0], "uri");
            }
        }
        char root_path_buf[2048] = "";
        if (root_uri && *root_uri) {
            UriToPath(root_uri, root_path_buf, sizeof(root_path_buf));
        } else {
            const char *rp = JSONGetStr(params, "rootPath");
            if (rp && *rp) snprintf(root_path_buf, sizeof(root_path_buf), "%s", rp);
        }
        if (root_path_buf[0] != '\0') {
            IndexWorkspaceRoot(root_path_buf);
        }
    }

    SendResponse(id,
                  "{\"capabilities\":{"
                  "\"textDocumentSync\":1,"
                  "\"hoverProvider\":true,"
                  "\"definitionProvider\":true,"
                  "\"workspaceSymbolProvider\":true,"
                  "\"completionProvider\":{\"triggerCharacters\":[\".\"]},"
                  "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},"
                  "\"referencesProvider\":true,"
                  "\"renameProvider\":true,"
                  "\"codeActionProvider\":true,"
                  "\"documentFormattingProvider\":true,"
                  "\"inlayHintProvider\":true,"
                  "\"documentSymbolProvider\":true,"
                  "\"semanticTokensProvider\":{"
                  "\"legend\":{\"tokenTypes\":[\"keyword\",\"type\",\"string\",\"variable\",\"function\"],\"tokenModifiers\":[]},"
                  "\"full\":true"
                  "}"
                  "}}");
}

static void HandleHover(JsonValue *id, const char *uri, int line, int character) {
    Document *doc = DocGet(uri);
    if (!doc) { SendResponse(id, "null"); return; }

    char word[256];
    GetWordAtPos(doc->text, line, character, word, sizeof(word));
    if (strlen(word) == 0) { SendResponse(id, "null"); return; }

    SymbolDef *found = FindBestSymbol(word, uri, 0);
    if (found) {
        char esc_detail[1024];
        EscapeJson(found->detail, esc_detail, sizeof(esc_detail));
        char hover_markdown[1536];
        snprintf(hover_markdown, sizeof(hover_markdown),
                 "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```aks\\n%s\\n```\"}}",
                 esc_detail);
        SendResponse(id, hover_markdown);
    } else {
        SendResponse(id, "null");
    }
}

static void HandleDefinition(JsonValue *id, const char *uri, int line, int character) {
    Document *doc = DocGet(uri);
    if (!doc) { SendResponse(id, "null"); return; }

    char word[256];
    GetWordAtPos(doc->text, line, character, word, sizeof(word));
    if (strlen(word) == 0) { SendResponse(id, "null"); return; }

    SymbolDef *found = FindBestSymbol(word, uri, 0);
    if (found) {
        const char *target_uri = (found->uri && found->uri[0] != '\0') ? found->uri : uri;
        char esc_uri[2048];
        EscapeJson(target_uri, esc_uri, sizeof(esc_uri));
        char def_json[2560];
        snprintf(def_json, sizeof(def_json),
                 "{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                 esc_uri, found->line - 1, found->col - 1, found->line - 1, found->col - 1 + (int)strlen(word));
        SendResponse(id, def_json);
    } else {
        SendResponse(id, "null");
    }
}

static void HandleCompletion(JsonValue *id, const char *uri) {
    (void)uri;
    size_t cap = 16384;
    char *res = malloc(cap);
    if (!res) { SendResponse(id, "{\"isIncomplete\":false,\"items\":[]}"); return; }
    strcpy(res, "{\"isIncomplete\":false,\"items\":[");
    size_t cur_len = strlen(res);
    int emitted = 0;

    // Add keywords
    const char *kws[] = {"let", "const", "fn", "return", "if", "else", "while", "for", "import", "struct", "enum", "interface", "match", "new"};
    for (size_t i = 0; i < sizeof(kws)/sizeof(kws[0]); i++) {
        char item[128];
        snprintf(item, sizeof(item), "%s{\"label\":\"%s\",\"kind\":14}", emitted++ > 0 ? "," : "", kws[i]);
        LspBufAppend(&res, &cap, &cur_len, item);
    }
    // Add math builtins
    const char *math[] = {"Matrix", "DataFrame", "Plot", "Dual", "BigInt", "Decimal", "SimdAdd", "SimdSub", "SimdMul", "SimdDiv", "FftForward", "FftInverse"};
    for (size_t i = 0; i < sizeof(math)/sizeof(math[0]); i++) {
        char item[128];
        snprintf(item, sizeof(item), "%s{\"label\":\"%s\",\"kind\":3}", emitted++ > 0 ? "," : "", math[i]);
        LspBufAppend(&res, &cap, &cur_len, item);
    }
    // Add scanned workspace & local symbols (deduplicated by name)
    for (int i = 0; i < g_syms_count; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(g_syms[j].name, g_syms[i].name) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;
        char esc_name[256];
        EscapeJson(g_syms[i].name, esc_name, sizeof(esc_name));
        int lsp_kind = LspSymToCompletionKind(g_syms[i].kind);
        char item[384];
        snprintf(item, sizeof(item), "%s{\"label\":\"%s\",\"kind\":%d}", emitted++ > 0 ? "," : "", esc_name, lsp_kind);
        LspBufAppend(&res, &cap, &cur_len, item);
    }
    LspBufAppend(&res, &cap, &cur_len, "]}");

    SendResponse(id, res);
    free(res);
}

static void HandleReferences(JsonValue *id, const char *uri, int line, int character) {
    Document *doc = DocGet(uri);
    if (!doc) { SendResponse(id, "[]"); return; }

    char word[256];
    GetWordAtPos(doc->text, line, character, word, sizeof(word));
    if (strlen(word) == 0) { SendResponse(id, "[]"); return; }

    char res[8192] = "[";
    const char *p = doc->text;
    int cur_line = 0;
    int line_char_offset = 0;
    while (*p) {
        if (*p == '\n') {
            cur_line++;
            line_char_offset = 0;
        } else {
            if (isalnum((unsigned char)*p) || *p == '_') {
                const char *start = p;
                int len = 0;
                while (*p && (isalnum((unsigned char)*p) || *p == '_')) { p++; len++; }
                char sym[256];
                if (len < (int)sizeof(sym)) {
                    memcpy(sym, start, (size_t)len);
                    sym[len] = '\0';
                    if (strcmp(sym, word) == 0) {
                        char loc[512];
                        snprintf(loc, sizeof(loc),
                                 "{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}},",
                                 uri, cur_line, line_char_offset, cur_line, line_char_offset + len);
                        if (strlen(res) + strlen(loc) + 4 < sizeof(res)) strcat(res, loc);
                    }
                }
                line_char_offset += len;
                continue;
            }
            line_char_offset++;
        }
        p++;
    }
    int rlen = (int)strlen(res);
    if (rlen > 1 && res[rlen - 1] == ',') res[rlen - 1] = '\0';
    strcat(res, "]");
    SendResponse(id, res);
}

static void HandleRename(JsonValue *id, const char *uri, int line, int character, const char *new_name) {
    Document *doc = DocGet(uri);
    if (!doc || !new_name) { SendResponse(id, "null"); return; }

    char word[256];
    GetWordAtPos(doc->text, line, character, word, sizeof(word));
    if (strlen(word) == 0) { SendResponse(id, "null"); return; }

    char edits[8192] = "{\"changes\":{";
    char uri_key[512];
    snprintf(uri_key, sizeof(uri_key), "\"%s\":[", uri);
    strcat(edits, uri_key);

    const char *p = doc->text;
    int cur_line = 0;
    int line_char_offset = 0;
    while (*p) {
        if (*p == '\n') {
            cur_line++;
            line_char_offset = 0;
        } else {
            if (isalnum((unsigned char)*p) || *p == '_') {
                const char *start = p;
                int len = 0;
                while (*p && (isalnum((unsigned char)*p) || *p == '_')) { p++; len++; }
                char sym[256];
                if (len < (int)sizeof(sym)) {
                    memcpy(sym, start, (size_t)len);
                    sym[len] = '\0';
                    if (strcmp(sym, word) == 0) {
                        char edit[512];
                        snprintf(edit, sizeof(edit),
                                 "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}},\"newText\":\"%s\"},",
                                 cur_line, line_char_offset, cur_line, line_char_offset + len, new_name);
                        if (strlen(edits) + strlen(edit) + 8 < sizeof(edits)) strcat(edits, edit);
                    }
                }
                line_char_offset += len;
                continue;
            }
            line_char_offset++;
        }
        p++;
    }
    int elen = (int)strlen(edits);
    if (elen > 0 && edits[elen - 1] == ',') edits[elen - 1] = '\0';
    strcat(edits, "]}}");
    SendResponse(id, edits);
}

static void HandleCodeAction(JsonValue *id, const char *uri, int line, JsonValue *diags_arr) {
    char actions[4096] = "[";
    int action_count = 0;

    int num_diags = (diags_arr && diags_arr->type == JSON_ARRAY) ? diags_arr->array.count : 0;
    for (int d = 0; d < num_diags; d++) {
        JsonValue *diag_item = diags_arr->array.items[d];
        const char *msg = JSONGetStr(diag_item, "message");
        const char *code = JSONGetStr(diag_item, "code");

        // 1. Missing semicolon
        if ((code && strcmp(code, "E0001") == 0 && msg && strstr(msg, "expected ';'") != NULL) ||
            (msg && strstr(msg, "expected ';'") != NULL)) {
            if (action_count++ > 0) strcat(actions, ",");
            char act[512];
            snprintf(act, sizeof(act),
                     "{\"title\":\"Add missing semicolon ';'\","
                     "\"kind\":\"quickfix\","
                     "\"isPreferred\":true,"
                     "\"edit\":{\"changes\":{\"%s\":["
                     "{\"range\":{\"start\":{\"line\":%d,\"character\":80},\"end\":{\"line\":%d,\"character\":80}},\"newText\":\";\"}"
                     "]}}}", uri ? uri : "", line, line);
            strcat(actions, act);
        }

        // 2. Use of moved value (E0382)
        if ((code && strcmp(code, "E0382") == 0) || (msg && strstr(msg, "use of moved value") != NULL)) {
            if (action_count++ > 0) strcat(actions, ",");
            char act1[512];
            snprintf(act1, sizeof(act1),
                     "{\"title\":\"Borrow value with '&' to prevent transfer of ownership\","
                     "\"kind\":\"quickfix\","
                     "\"isPreferred\":true,"
                     "\"edit\":{\"changes\":{\"%s\":["
                     "{\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":0}},\"newText\":\"// suggestion: borrow with '&'\\n\"}"
                     "]}}}", uri ? uri : "", line, line);
            strcat(actions, act1);

            if (action_count++ > 0) strcat(actions, ",");
            char act2[512];
            snprintf(act2, sizeof(act2),
                     "{\"title\":\"Clone value with '.clone()'\","
                     "\"kind\":\"quickfix\","
                     "\"isPreferred\":false}");
            strcat(actions, act2);
        }

        // 3. Mutable borrow / immutable assignment error (E0596)
        if ((code && strcmp(code, "E0596") == 0) || (msg && strstr(msg, "cannot borrow") && strstr(msg, "mutable"))) {
            if (action_count++ > 0) strcat(actions, ",");
            char act[512];
            snprintf(act, sizeof(act),
                     "{\"title\":\"Make variable mutable: 'let mut'\","
                     "\"kind\":\"quickfix\","
                     "\"isPreferred\":true}");
            strcat(actions, act);
        }

        // 4. Mismatched types (E0308)
        if ((code && strcmp(code, "E0308") == 0) || (msg && strstr(msg, "mismatched types") != NULL)) {
            if (action_count++ > 0) strcat(actions, ",");
            char act[512];
            snprintf(act, sizeof(act),
                     "{\"title\":\"Add explicit type cast or conversion\","
                     "\"kind\":\"quickfix\","
                     "\"isPreferred\":true}");
            strcat(actions, act);
        }

        // 5. Non-exhaustive match patterns (E0004)
        if ((code && strcmp(code, "E0004") == 0) || (msg && strstr(msg, "non-exhaustive") != NULL)) {
            if (action_count++ > 0) strcat(actions, ",");
            char act[512];
            snprintf(act, sizeof(act),
                     "{\"title\":\"Add wildcard pattern arm: '_ => { ... }'\","
                     "\"kind\":\"quickfix\","
                     "\"isPreferred\":true}");
            strcat(actions, act);
        }
    }

    strcat(actions, "]");
    SendResponse(id, actions);
}

static void HandleSemanticTokens(JsonValue *id, const char *uri) {
    Document *doc = DocGet(uri);
    if (!doc) { SendResponse(id, "null"); return; }
    
    Lexer l = LexNew(doc->text);
    Token t;
    int last_line = 0;
    int last_char = 0;
    
    char tokens_arr[16384] = "{\"data\":[";
    
    while ((t = LexNext(&l)).type != TEOF) {
        if (t.type == TERROR) { TokFree(&t); continue; }
        
        int type_idx = -1;
        // Determine token type
        if (t.type >= TLET && t.type <= TSPAWN) {
            type_idx = 0; // Keyword
        } else if (t.type >= TINT && t.type <= TVOID) {
            type_idx = 1; // Type
        } else if (t.type == TSTRLIT || t.type == TRAWSTRLIT) {
            type_idx = 2; // String
        } else if (t.type == TIDENT) {
            // Check peek token to see if it is a function call
            if (l.queued_count > 0 && l.queued[0].type == TLPAREN) {
                type_idx = 4; // Function
            } else {
                type_idx = 3; // Variable
            }
        }
        
        if (type_idx >= 0) {
            // Convert to relative LSP token offsets
            int tok_line = t.line - 1;
            int tok_col = t.col - 1;
            int delta_line = tok_line - last_line;
            int delta_start = delta_line == 0 ? (tok_col - last_char) : tok_col;
            int len = t.val ? (int)strlen(t.val) : 1;
            
            char tok_entry[128];
            snprintf(tok_entry, sizeof(tok_entry), "%d,%d,%d,%d,0,", delta_line, delta_start, len, type_idx);
            strcat(tokens_arr, tok_entry);
            
            last_line = tok_line;
            last_char = tok_col;
        }
        TokFree(&t);
    }
    int rlen = (int)strlen(tokens_arr);
    if (tokens_arr[rlen - 1] == ',') tokens_arr[rlen - 1] = '\0';
    strcat(tokens_arr, "]}");
    
    SendResponse(id, tokens_arr);
}

static void HandleFormatting(JsonValue *id, const char *uri) {
    Document *doc = DocGet(uri);
    if (!doc || !doc->text) {
        SendResponse(id, "[]");
        return;
    }

    char *formatted = AksFormatSource(doc->text);
    if (!formatted) {
        SendResponse(id, "[]");
        return;
    }

    if (strcmp(doc->text, formatted) == 0) {
        free(formatted);
        SendResponse(id, "[]");
        return;
    }

    int total_lines = 0;
    int last_line_len = 0;
    for (const char *p = doc->text; *p; p++) {
        if (*p == '\n') {
            total_lines++;
            last_line_len = 0;
        } else {
            last_line_len++;
        }
    }

    size_t esc_size = strlen(formatted) * 2 + 1024;
    char *esc_text = malloc(esc_size);
    if (!esc_text) {
        free(formatted);
        SendResponse(id, "[]");
        return;
    }
    EscapeJson(formatted, esc_text, esc_size);
    free(formatted);

    size_t res_size = strlen(esc_text) + 256;
    char *res = malloc(res_size);
    if (!res) {
        free(esc_text);
        SendResponse(id, "[]");
        return;
    }

    snprintf(res, res_size,
             "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":%d,\"character\":%d}},\"newText\":\"%s\"}]",
             total_lines, last_line_len, esc_text);
    free(esc_text);

    SendResponse(id, res);
    free(res);
}

static void HandleDocumentSymbols(JsonValue *id, const char *uri) {
    if (g_syms_count == 0) {
        SendResponse(id, "[]");
        return;
    }

    size_t cap = 16384;
    char *res = malloc(cap);
    if (!res) { SendResponse(id, "[]"); return; }
    strcpy(res, "[");
    size_t cur_len = 1;
    int emitted = 0;

    for (int i = 0; i < g_syms_count; i++) {
        SymbolDef *sym = &g_syms[i];
        if (uri && (!sym->uri || !LspUriEqual(sym->uri, uri))) continue;
        int lsp_kind = LspSymToDocSymbolKind(sym->kind);

        int line_0 = sym->line > 0 ? sym->line - 1 : 0;
        int col_0 = sym->col > 0 ? sym->col - 1 : 0;
        int name_len = (int)strlen(sym->name);

        char esc_name[256];
        EscapeJson(sym->name, esc_name, sizeof(esc_name));
        char esc_detail[256];
        EscapeJson(sym->detail, esc_detail, sizeof(esc_detail));

        char item[1024];
        snprintf(item, sizeof(item),
                 "%s{\"name\":\"%s\",\"detail\":\"%s\",\"kind\":%d,"
                 "\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":%d}},"
                 "\"selectionRange\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                 emitted > 0 ? "," : "",
                 esc_name, esc_detail, lsp_kind,
                 line_0, line_0, col_0 + name_len > 80 ? col_0 + name_len : 80,
                 line_0, col_0, line_0, col_0 + name_len);
        emitted++;
        LspBufAppend(&res, &cap, &cur_len, item);
    }
    LspBufAppend(&res, &cap, &cur_len, "]");

    SendResponse(id, res);
    free(res);
}

static void HandleWorkspaceSymbol(JsonValue *id, JsonValue *params) {
    const char *query = params ? JSONGetStr(params, "query") : "";
    if (!query) query = "";

    size_t cap = 16384;
    char *res = malloc(cap);
    if (!res) { SendResponse(id, "[]"); return; }
    strcpy(res, "[");
    size_t cur_len = 1;
    int emitted = 0;

    for (int i = 0; i < g_syms_count; i++) {
        SymbolDef *sym = &g_syms[i];
        if (query[0] != '\0' && !LspStristr(sym->name, query)) continue;
        if (!sym->uri || sym->uri[0] == '\0') continue;

        int lsp_kind = LspSymToDocSymbolKind(sym->kind);
        int line_0 = sym->line > 0 ? sym->line - 1 : 0;
        int col_0 = sym->col > 0 ? sym->col - 1 : 0;
        int name_len = (int)strlen(sym->name);

        char esc_name[256];
        EscapeJson(sym->name, esc_name, sizeof(esc_name));
        char esc_uri[2048];
        EscapeJson(sym->uri, esc_uri, sizeof(esc_uri));

        char item[2560];
        snprintf(item, sizeof(item),
                 "%s{\"name\":\"%s\",\"kind\":%d,\"location\":{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}}",
                 emitted > 0 ? "," : "",
                 esc_name, lsp_kind, esc_uri,
                 line_0, col_0, line_0, col_0 + name_len);
        emitted++;
        LspBufAppend(&res, &cap, &cur_len, item);
    }
    LspBufAppend(&res, &cap, &cur_len, "]");

    SendResponse(id, res);
    free(res);
}

typedef struct {
    int line;
    int col;
    char label[64];
    int kind;
} LspInlayHint;

typedef struct {
    char name[128];
    char pnames[16][64];
    int param_count;
    char ret[64];
} InlayFuncSig;

typedef struct {
    LspInlayHint hints[512];
    int count;
    const char *text;
    InlayFuncSig funcs[128];
    int func_count;
} InlayContext;

static void CollectFuncSigs(Node *n, InlayContext *ctx) {
    if (!n) return;
    if (n->type == NFNDECL && n->fn.name && ctx->func_count < 128) {
        InlayFuncSig *fs = &ctx->funcs[ctx->func_count++];
        snprintf(fs->name, sizeof(fs->name), "%s", n->fn.name);
        fs->param_count = 0;
        fs->ret[0] = '\0';
        int p_limit = n->fn.pc < 16 ? n->fn.pc : 16;
        for (int p = 0; p < p_limit; p++) {
            if (n->fn.pnames && n->fn.pnames[p]) {
                snprintf(fs->pnames[fs->param_count], sizeof(fs->pnames[fs->param_count]), "%s", n->fn.pnames[p]);
                fs->param_count++;
            }
        }
        if (n->fn.ret) {
            snprintf(fs->ret, sizeof(fs->ret), "%s", n->fn.ret);
        }
    }
    switch (n->type) {
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) CollectFuncSigs(n->block.stmts[i], ctx);
            break;
        case NFNDECL:
            CollectFuncSigs(n->fn.body, ctx);
            break;
        case NIF:
            CollectFuncSigs(n->ifn.then, ctx);
            CollectFuncSigs(n->ifn.els, ctx);
            break;
        case NWHILE:
            CollectFuncSigs(n->whilen.body, ctx);
            break;
        case NFOR:
            CollectFuncSigs(n->forn.body, ctx);
            break;
        default:
            break;
    }
}

static const char *InferExprType(Node *expr, InlayContext *ctx) {
    if (!expr) return NULL;
    switch (expr->type) {
        case NINT: return ": int";
        case NFLOAT: return ": float";
        case NSTR: return ": string";
        case NBOOL: return ": bool";
        case NARRLIT: return ": Array";
        case NMAPLIT: return ": Map";
        case NCALL: {
            if (expr->call.callee && expr->call.callee->type == NIDENT && expr->call.callee->sval) {
                const char *cname = expr->call.callee->sval;
                if (strcmp(cname, "Matrix") == 0) return ": Matrix";
                if (strcmp(cname, "DataFrame") == 0) return ": DataFrame";
                if (strcmp(cname, "Plot") == 0) return ": Plot";
                if (strcmp(cname, "BigInt") == 0) return ": BigInt";
                if (strcmp(cname, "Decimal") == 0) return ": Decimal";
                for (int f = 0; f < ctx->func_count; f++) {
                    if (strcmp(ctx->funcs[f].name, cname) == 0 && ctx->funcs[f].ret[0]) {
                        static char ret_buf[128];
                        snprintf(ret_buf, sizeof(ret_buf), ": %s", ctx->funcs[f].ret);
                        return ret_buf;
                    }
                }
            }
            return NULL;
        }
        case NSTRUCTINIT:
            if (expr->single.var) {
                static char strct_buf[128];
                snprintf(strct_buf, sizeof(strct_buf), ": %s", expr->single.var);
                return strct_buf;
            }
            return NULL;
        default:
            return NULL;
    }
}

static void CollectInlayHints(Node *n, InlayContext *ctx) {
    if (!n) return;

    if ((n->type == NVARDECL || n->type == NCONSTDECL) && n->decl.name && n->decl.init) {
        if (!n->decl.tname) {
            const char *inferred = InferExprType(n->decl.init, ctx);
            if (inferred && ctx->count < 512) {
                int col = 1;
                GetColumnForLine(ctx->text, n->line, n->decl.name, &col);
                LspInlayHint *h = &ctx->hints[ctx->count++];
                h->line = n->line > 0 ? n->line - 1 : 0;
                h->col = col > 0 ? (col - 1 + (int)strlen(n->decl.name)) : 0;
                snprintf(h->label, sizeof(h->label), "%.63s", inferred);
                h->kind = 1; // Type
            }
        }
    }

    if (n->type == NCALL && n->call.callee && n->call.callee->type == NIDENT && n->call.callee->sval) {
        const char *cname = n->call.callee->sval;
        InlayFuncSig *found_fn = NULL;
        for (int f = 0; f < ctx->func_count; f++) {
            if (strcmp(ctx->funcs[f].name, cname) == 0) {
                found_fn = &ctx->funcs[f];
                break;
            }
        }
        if (found_fn && found_fn->param_count > 0) {
            int limit = n->call.argc < found_fn->param_count ? n->call.argc : found_fn->param_count;
            for (int a = 0; a < limit; a++) {
                Node *arg = n->call.args[a];
                if (arg && arg->line > 0 && ctx->count < 512) {
                    int col = 1;
                    const char *arg_word = NULL;
                    if (arg->type == NIDENT && arg->sval) arg_word = arg->sval;
                    else if (arg->type == NSTR && arg->sval) arg_word = arg->sval;
                    if (arg_word) {
                        GetColumnForLine(ctx->text, arg->line, arg_word, &col);
                    }
                    LspInlayHint *h = &ctx->hints[ctx->count++];
                    h->line = arg->line > 0 ? arg->line - 1 : 0;
                    h->col = col > 0 ? col - 1 : 0;
                    snprintf(h->label, sizeof(h->label), "%s:", found_fn->pnames[a]);
                    h->kind = 2; // Parameter
                }
            }
        }
    }

    switch (n->type) {
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) CollectInlayHints(n->block.stmts[i], ctx);
            break;
        case NFNDECL:
            CollectInlayHints(n->fn.body, ctx);
            break;
        case NIF:
            CollectInlayHints(n->ifn.cond, ctx);
            CollectInlayHints(n->ifn.then, ctx);
            CollectInlayHints(n->ifn.els, ctx);
            break;
        case NWHILE:
            CollectInlayHints(n->whilen.cond, ctx);
            CollectInlayHints(n->whilen.body, ctx);
            break;
        case NFOR:
            CollectInlayHints(n->forn.init, ctx);
            CollectInlayHints(n->forn.cond, ctx);
            CollectInlayHints(n->forn.post, ctx);
            CollectInlayHints(n->forn.body, ctx);
            break;
        case NRETURN:
        case NPRINT:
        case NEXPRSTMT:
            CollectInlayHints(n->single.expr, ctx);
            break;
        case NBINOP:
            CollectInlayHints(n->binop.left, ctx);
            CollectInlayHints(n->binop.right, ctx);
            break;
        case NCALL:
            for (int i = 0; i < n->call.argc; i++) CollectInlayHints(n->call.args[i], ctx);
            break;
        case NVARDECL:
        case NCONSTDECL:
            CollectInlayHints(n->decl.init, ctx);
            break;
        default:
            break;
    }
}

static void HandleInlayHints(JsonValue *id, const char *uri) {
    Document *doc = DocGet(uri);
    if (!doc || !doc->text) {
        SendResponse(id, "[]");
        return;
    }

    int had_error = 0;
    Node *prog = Parse(doc->text, &had_error);
    if (!prog || had_error) {
        if (prog) NodeFree(prog);
        SendResponse(id, "[]");
        return;
    }

    InlayContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.text = doc->text;

    CollectFuncSigs(prog, &ctx);
    CollectInlayHints(prog, &ctx);
    NodeFree(prog);

    if (ctx.count == 0) {
        SendResponse(id, "[]");
        return;
    }

    size_t cap = 16384;
    char *res = malloc(cap);
    if (!res) { SendResponse(id, "[]"); return; }
    strcpy(res, "[");
    size_t cur_len = 1;

    for (int i = 0; i < ctx.count; i++) {
        LspInlayHint *h = &ctx.hints[i];
        char esc_label[128];
        EscapeJson(h->label, esc_label, sizeof(esc_label));

        char item[512];
        snprintf(item, sizeof(item),
                 "%s{\"position\":{\"line\":%d,\"character\":%d},"
                 "\"label\":\"%s\",\"kind\":%d,"
                 "\"paddingLeft\":false,\"paddingRight\":%s}",
                 i > 0 ? "," : "",
                 h->line, h->col,
                 esc_label, h->kind,
                 h->kind == 2 ? "true" : "false");

        size_t item_len = strlen(item);
        if (cur_len + item_len + 16 > cap) {
            cap = (cur_len + item_len + 16) * 2;
            char *nb = realloc(res, cap);
            if (!nb) break;
            res = nb;
        }
        strcat(res, item);
        cur_len += item_len;
    }
    strcat(res, "]");

    SendResponse(id, res);
    free(res);
}

typedef struct {
    const char *name;
    const char *label;
    const char *doc;
    int param_count;
    const char *params[4];
} BuiltinSig;

static const BuiltinSig g_builtin_sigs[] = {
    {"println", "println(...values): void", "Prints values followed by a newline.", 1, {"...values"}},
    {"print", "print(...values): void", "Prints values without a trailing newline.", 1, {"...values"}},
    {"len", "len(collection): int", "Returns the number of elements in an array, string, or map.", 1, {"collection"}},
    {"append", "append(arr: []any, elem: any): []any", "Appends an element to an array and returns it.", 2, {"arr: []any", "elem: any"}},
    {"pop", "pop(arr: []any): any", "Removes and returns the last element of an array.", 1, {"arr: []any"}},
    {"assert", "assert(condition: bool, message?: string): void", "Asserts that condition is true, otherwise panics.", 2, {"condition: bool", "message?: string"}},
    {"typeof", "typeof(value: any): string", "Returns the type name of the given value.", 1, {"value: any"}},
    {"panic", "panic(message: string): void", "Terminates execution with an error message.", 1, {"message: string"}},
    {"Matrix", "Matrix(rows: int, cols: int, fill?: float): Matrix", "Creates a 2D dense float64 matrix.", 3, {"rows: int", "cols: int", "fill?: float"}},
    {"DataFrame", "DataFrame(columns: []string): DataFrame", "Creates a columnar tabular DataFrame.", 1, {"columns: []string"}},
    {"Dual", "Dual(real: float, eps: float): Dual", "Creates an automatic differentiation dual number.", 2, {"real: float", "eps: float"}},
    {"SimdAdd", "SimdAdd(a: []float, b: []float): []float", "Vectorized SIMD addition.", 2, {"a: []float", "b: []float"}},
    {"SimdMul", "SimdMul(a: []float, b: []float): []float", "Vectorized SIMD multiplication.", 2, {"a: []float", "b: []float"}},
    {"FftForward", "FftForward(signal: []float): []complex", "Computes the Fast Fourier Transform of a signal.", 1, {"signal: []float"}},
    {"FftInverse", "FftInverse(freq: []complex): []float", "Computes the Inverse Fast Fourier Transform.", 1, {"freq: []complex"}}
};

static void HandleSignatureHelp(JsonValue *id, const char *uri, int line, int character) {
    Document *doc = DocGet(uri);
    if (!doc || !doc->text) {
        SendResponse(id, "null");
        return;
    }

    const char *text = doc->text;
    int cur_line = 0;
    int cur_col = 0;
    int offset = 0;
    while (text[offset]) {
        if (cur_line == line && cur_col == character) break;
        if (text[offset] == '\n') {
            cur_line++;
            cur_col = 0;
        } else {
            cur_col++;
        }
        offset++;
    }

    int depth = 0;
    int active_param = 0;
    int open_paren_idx = -1;

    for (int i = offset - 1; i >= 0; i--) {
        char ch = text[i];
        if (ch == ')' || ch == ']' || ch == '}') {
            depth++;
        } else if (ch == '(') {
            if (depth > 0) {
                depth--;
            } else {
                open_paren_idx = i;
                break;
            }
        } else if (ch == '[' || ch == '{') {
            if (depth > 0) depth--;
        } else if (ch == ',' && depth == 0) {
            active_param++;
        }
    }

    if (open_paren_idx < 0) {
        SendResponse(id, "null");
        return;
    }

    int p = open_paren_idx - 1;
    while (p >= 0 && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r' || text[p] == '\n')) {
        p--;
    }
    int name_end = p;
    while (p >= 0 && (isalnum((unsigned char)text[p]) || text[p] == '_' || text[p] == '.')) {
        p--;
    }
    int name_start = p + 1;
    if (name_end < name_start) {
        SendResponse(id, "null");
        return;
    }

    char callee[128];
    int callee_len = name_end - name_start + 1;
    if (callee_len >= (int)sizeof(callee)) callee_len = (int)sizeof(callee) - 1;
    memcpy(callee, text + name_start, callee_len);
    callee[callee_len] = '\0';

    const char *simple_name = strrchr(callee, '.');
    if (simple_name) simple_name++; else simple_name = callee;

    SymbolDef *user_fn = FindBestSymbol(callee, uri, 1);
    if (!user_fn && simple_name != callee) {
        user_fn = FindBestSymbol(simple_name, uri, 1);
    }

    const BuiltinSig *builtin_fn = NULL;
    if (!user_fn) {
        int n_builtins = (int)(sizeof(g_builtin_sigs) / sizeof(g_builtin_sigs[0]));
        for (int i = 0; i < n_builtins; i++) {
            if (strcmp(g_builtin_sigs[i].name, callee) == 0 || strcmp(g_builtin_sigs[i].name, simple_name) == 0) {
                builtin_fn = &g_builtin_sigs[i];
                break;
            }
        }
    }

    if (!user_fn && !builtin_fn) {
        SendResponse(id, "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}");
        return;
    }

    char res[4096];
    char *p_out = res;
    int remaining = sizeof(res);

    int param_count = user_fn ? user_fn->param_count : builtin_fn->param_count;
    int clamped_param = active_param;
    if (clamped_param >= param_count && param_count > 0) {
        clamped_param = param_count - 1;
    }

    const char *sig_label = user_fn ? user_fn->detail : builtin_fn->label;
    const char *doc_str = user_fn ? "User-defined function" : builtin_fn->doc;
    char esc_sig[1024];
    EscapeJson(sig_label, esc_sig, sizeof(esc_sig));

    int written = snprintf(p_out, remaining,
                           "{\"signatures\":[{"
                           "\"label\":\"%s\","
                           "\"documentation\":{\"kind\":\"markdown\",\"value\":\"%s\"},"
                           "\"parameters\":[",
                           esc_sig, doc_str);
    if (written > 0 && written < remaining) {
        p_out += written;
        remaining -= written;
    }

    for (int i = 0; i < param_count && remaining > 16; i++) {
        const char *plabel = user_fn ? (user_fn->params ? user_fn->params[i] : "arg") : builtin_fn->params[i];
        written = snprintf(p_out, remaining, "%s{\"label\":\"%s\"}", (i > 0 ? "," : ""), plabel);
        if (written > 0 && written < remaining) {
            p_out += written;
            remaining -= written;
        }
    }

    if (remaining > 0) {
        snprintf(p_out, remaining,
                 "],"
                 "\"activeParameter\":%d"
                 "}],"
                 "\"activeSignature\":0,"
                 "\"activeParameter\":%d"
                 "}",
                 clamped_param, clamped_param);
    }

    SendResponse(id, res);
}

// Main json-rpc dispatch loop
int main() {
    char header[256];
    while (LSPReadLine(header, sizeof(header)) > 0) {
        if (strncmp(header, "Content-Length: ", 16) == 0) {
            int length = atoi(header + 16);

            // Skip remaining header lines until blank line
            char empty_line[256];
            while (LSPReadLine(empty_line, sizeof(empty_line)) > 0) {
                if (empty_line[0] == '\r' || empty_line[0] == '\n') break;
            }

            // Read JSON body exactly
            char *json_body = malloc(length + 1);
            int bytes_read = LSPReadExact(json_body, length);
            json_body[bytes_read] = '\0';

            JsonValue *root = JSONParse(json_body);
            free(json_body);
            if (!root) continue;

            JsonValue *id = JSONGet(root, "id");
            const char *method = JSONGetStr(root, "method");
            JsonValue *params = JSONGet(root, "params");

            if (method) {
                if (strcmp(method, "initialize") == 0) {
                    HandleInitialize(id, params);
                } else if (strcmp(method, "textDocument/didOpen") == 0 || strcmp(method, "textDocument/didChange") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    const char *text = JSONGetStr(doc_obj, "text");
                    if (!text) {
                        JsonValue *content_changes = JSONGet(params, "contentChanges");
                        if (content_changes && content_changes->type == JSON_ARRAY && content_changes->array.count > 0) {
                            text = JSONGetStr(content_changes->array.items[0], "text");
                        }
                    }
                    if (uri && text) {
                        DocUpdate(uri, text);
                        CheckDiagnostics(uri, text);
                    }
                } else if (strcmp(method, "textDocument/hover") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    JsonValue *pos_obj = JSONGet(params, "position");
                    int line = (int)JSONGet(pos_obj, "line")->number;
                    int character = (int)JSONGet(pos_obj, "character")->number;
                    HandleHover(id, uri, line, character);
                } else if (strcmp(method, "textDocument/definition") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    JsonValue *pos_obj = JSONGet(params, "position");
                    int line = (int)JSONGet(pos_obj, "line")->number;
                    int character = (int)JSONGet(pos_obj, "character")->number;
                    HandleDefinition(id, uri, line, character);
                } else if (strcmp(method, "textDocument/completion") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = doc_obj ? JSONGetStr(doc_obj, "uri") : NULL;
                    HandleCompletion(id, uri);
                } else if (strcmp(method, "workspace/symbol") == 0) {
                    HandleWorkspaceSymbol(id, params);
                } else if (strcmp(method, "textDocument/references") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    JsonValue *pos_obj = JSONGet(params, "position");
                    int line = (int)JSONGet(pos_obj, "line")->number;
                    int character = (int)JSONGet(pos_obj, "character")->number;
                    HandleReferences(id, uri, line, character);
                } else if (strcmp(method, "textDocument/rename") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    JsonValue *pos_obj = JSONGet(params, "position");
                    int line = (int)JSONGet(pos_obj, "line")->number;
                    int character = (int)JSONGet(pos_obj, "character")->number;
                    const char *new_name = JSONGetStr(params, "newName");
                    HandleRename(id, uri, line, character, new_name);
                } else if (strcmp(method, "textDocument/codeAction") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    JsonValue *range_obj = JSONGet(params, "range");
                    JsonValue *start_obj = JSONGet(range_obj, "start");
                    int line = 0;
                    if (start_obj) {
                        JsonValue *line_v = JSONGet(start_obj, "line");
                        if (line_v && line_v->type == JSON_NUMBER) line = (int)line_v->number;
                    }
                    JsonValue *context_obj = JSONGet(params, "context");
                    JsonValue *diags_arr = JSONGet(context_obj, "diagnostics");
                    HandleCodeAction(id, uri, line, diags_arr);
                } else if (strcmp(method, "textDocument/semanticTokens/full") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    HandleSemanticTokens(id, uri);
                } else if (strcmp(method, "textDocument/formatting") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    HandleFormatting(id, uri);
                } else if (strcmp(method, "textDocument/inlayHint") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    HandleInlayHints(id, uri);
                } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    HandleDocumentSymbols(id, uri);
                } else if (strcmp(method, "textDocument/signatureHelp") == 0) {
                    JsonValue *doc_obj = JSONGet(params, "textDocument");
                    const char *uri = JSONGetStr(doc_obj, "uri");
                    JsonValue *pos_obj = JSONGet(params, "position");
                    int line = 0, character = 0;
                    if (pos_obj) {
                        JsonValue *lv = JSONGet(pos_obj, "line");
                        if (lv && lv->type == JSON_NUMBER) line = (int)lv->number;
                        JsonValue *cv = JSONGet(pos_obj, "character");
                        if (cv && cv->type == JSON_NUMBER) character = (int)cv->number;
                    }
                    HandleSignatureHelp(id, uri, line, character);
                } else if (strcmp(method, "shutdown") == 0) {
                    SendResponse(id, "null");
                } else if (strcmp(method, "exit") == 0) {
                    JSONFree(root);
                    break;
                }
            }
            JSONFree(root);
        }
    }
    
    SymsClearAll();
    for (int i = 0; i < g_docs_count; i++) {
        free(g_docs[i].uri);
        free(g_docs[i].text);
    }
    free(g_docs);
    
    return 0;
}
