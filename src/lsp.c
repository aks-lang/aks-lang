// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "aks_plat.h"
#include <fcntl.h>

#ifndef _WIN32
#include <unistd.h>
#define _dup dup
#define _dup2 dup2
#define _fileno fileno
#define _close close
#define MAX_PATH 260
#endif

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "compiler.h"
#include "diagnostic.h"

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
    SYM_FN, SYM_STRUCT, SYM_ENUM, SYM_VAR
} SymbolKind;

typedef struct {
    char *name;
    SymbolKind kind;
    int line; // based
    int col;  // based
    char *detail;
} SymbolDef;

static SymbolDef *g_syms = NULL;
static int g_syms_count = 0;

static void SymAdd(const char *name, SymbolKind kind, int line, int col, const char *detail) {
    g_syms = realloc(g_syms, (g_syms_count + 1) * sizeof(SymbolDef));
    g_syms[g_syms_count].name = strdup(name);
    g_syms[g_syms_count].kind = kind;
    g_syms[g_syms_count].line = line;
    g_syms[g_syms_count].col = col;
    g_syms[g_syms_count].detail = strdup(detail ? detail : "");
    g_syms_count++;
}

static void SymsClear() {
    for (int i = 0; i < g_syms_count; i++) {
        free(g_syms[i].name);
        free(g_syms[i].detail);
    }
    free(g_syms);
    g_syms = NULL;
    g_syms_count = 0;
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

static void WalkAST(Node *n, const char *text) {
    if (!n) return;
    int col = 1;
    switch (n->type) {
        case NFNDECL:
            if (n->fn.name) {
                GetColumnForLine(text, n->line, n->fn.name, &col);
                char detail[256];
                snprintf(detail, sizeof(detail), "fn %s()", n->fn.name);
                SymAdd(n->fn.name, SYM_FN, n->line, col, detail);
            }
            WalkAST(n->fn.body, text);
            break;
        case NSTRUCTDECL:
            if (n->strct.name) {
                GetColumnForLine(text, n->line, n->strct.name, &col);
                char detail[256];
                snprintf(detail, sizeof(detail), "struct %s", n->strct.name);
                SymAdd(n->strct.name, SYM_STRUCT, n->line, col, detail);
            }
            break;
        case NVARDECL:
        case NCONSTDECL:
            if (n->decl.name) {
                GetColumnForLine(text, n->line, n->decl.name, &col);
                SymAdd(n->decl.name, SYM_VAR, n->line, col, "variable");
            }
            WalkAST(n->decl.init, text);
            break;
        case NENUMDECL:
            if (n->enumdecl.name) {
                GetColumnForLine(text, n->line, n->enumdecl.name, &col);
                SymAdd(n->enumdecl.name, SYM_ENUM, n->line, col, "enum");
            }
            break;
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) WalkAST(n->block.stmts[i], text);
            break;
        case NIF:
            WalkAST(n->ifn.cond, text);
            WalkAST(n->ifn.then, text);
            WalkAST(n->ifn.els, text);
            break;
        case NWHILE:
            WalkAST(n->whilen.cond, text);
            WalkAST(n->whilen.body, text);
            break;
        case NFOR:
            WalkAST(n->forn.init, text);
            WalkAST(n->forn.cond, text);
            WalkAST(n->forn.post, text);
            WalkAST(n->forn.body, text);
            break;
        case NRETURN:
        case NPRINT:
        case NEXPRSTMT:
            WalkAST(n->single.expr, text);
            break;
        case NBINOP:
            WalkAST(n->binop.left, text);
            WalkAST(n->binop.right, text);
            break;
        default:
            break;
    }
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
    strncpy(item->code, d->code[0] ? d->code : "E0000", sizeof(item->code) - 1);
    strncpy(item->message, d->message, sizeof(item->message) - 1);
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
    strncpy(item->source, "aks", sizeof(item->source) - 1);
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
    SymsClear();

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
                    strncpy(msg, parsed_msg, sizeof(msg) - 1);
                    break;
                }
            }
            fclose(err_in);

            if (diag_ctx.count < 64) {
                LspCollectedDiag *item = &diag_ctx.items[diag_ctx.count++];
                memset(item, 0, sizeof(LspCollectedDiag));
                item->level = DIAG_ERROR;
                strncpy(item->code, "E0001", sizeof(item->code) - 1);
                strncpy(item->message, msg, sizeof(item->message) - 1);
                item->line = line_num > 0 ? line_num : 1;
                item->col = 1;
                item->length = 80;
                strncpy(item->source, "aks", sizeof(item->source) - 1);
            }
        }
    } else if (prog) {
        WalkAST(prog, text);

        int prev_borrow = gBorrowCheckerEnabled;
        int prev_type = gTypeCheckingEnabled;
        gBorrowCheckerEnabled = 1;
        gTypeCheckingEnabled = 1;

        Compiler *c = CompNew();
        if (c) {
            if (uri) strncpy(c->currentfilepath, uri, sizeof(c->currentfilepath) - 1);
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
                        strncpy(item->code, "E0002", sizeof(item->code) - 1);
                        strncpy(item->message, parsed_msg, sizeof(item->message) - 1);
                        item->line = parsed_line > 0 ? parsed_line : 1;
                        item->col = 1;
                        item->length = 1;
                        strncpy(item->source, "aks", sizeof(item->source) - 1);
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

// LSP handlers
static void HandleInitialize(JsonValue *id) {
    SendResponse(id,
                  "{\"capabilities\":{"
                  "\"textDocumentSync\":1,"
                  "\"hoverProvider\":true,"
                  "\"definitionProvider\":true,"
                  "\"completionProvider\":{\"triggerCharacters\":[\".\"]},"
                  "\"referencesProvider\":true,"
                  "\"renameProvider\":true,"
                  "\"codeActionProvider\":true,"
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
    
    SymbolDef *found = NULL;
    for (int i = 0; i < g_syms_count; i++) {
        if (strcmp(g_syms[i].name, word) == 0) { found = &g_syms[i]; break; }
    }
    if (found) {
        char hover_markdown[1024];
        snprintf(hover_markdown, sizeof(hover_markdown),
                 "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```aks\\n%s\\n```\"}}",
                 found->detail);
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
    
    SymbolDef *found = NULL;
    for (int i = 0; i < g_syms_count; i++) {
        if (strcmp(g_syms[i].name, word) == 0) { found = &g_syms[i]; break; }
    }
    if (found) {
        char def_json[1024];
        snprintf(def_json, sizeof(def_json),
                 "{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                 uri, found->line - 1, found->col - 1, found->line - 1, found->col - 1 + (int)strlen(word));
        SendResponse(id, def_json);
    } else {
        SendResponse(id, "null");
    }
}

static void HandleCompletion(JsonValue *id) {
    // Standard completions: keywords, standard math functions/prelude classes, and cached symbols
    char res[8192] = "{\"isIncomplete\":false,\"items\":[";
    
    // Add keywords
    const char *kws[] = {"let", "const", "fn", "return", "if", "else", "while", "for", "import", "struct", "enum", "match", "new"};
    for (size_t i = 0; i < sizeof(kws)/sizeof(kws[0]); i++) {
        char item[128];
        snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":14},", kws[i]);
        strcat(res, item);
    }
    // Add math builtins
    const char *math[] = {"Matrix", "DataFrame", "Plot", "Dual", "BigInt", "Decimal", "SimdAdd", "SimdSub", "SimdMul", "SimdDiv", "FftForward", "FftInverse"};
    for (size_t i = 0; i < sizeof(math)/sizeof(math[0]); i++) {
        char item[128];
        snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":3},", math[i]);
        strcat(res, item);
    }
    // Add scanned symbols
    for (int i = 0; i < g_syms_count; i++) {
        int lsp_kind = 6; // Variable
        if (g_syms[i].kind == SYM_FN) lsp_kind = 3;
        else if (g_syms[i].kind == SYM_STRUCT) lsp_kind = 22;
        char item[256];
        snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":%d},", g_syms[i].name, lsp_kind);
        strcat(res, item);
    }
    // Remove trailing comma if any
    int rlen = (int)strlen(res);
    if (res[rlen - 1] == ',') res[rlen - 1] = '\0';
    strcat(res, "]}");
    
    SendResponse(id, res);
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
            if (isalnum(*p) || *p == '_') {
                const char *start = p;
                int len = 0;
                while (*p && (isalnum(*p) || *p == '_')) { p++; len++; }
                char sym[256];
                if (len < sizeof(sym)) {
                    memcpy(sym, start, len);
                    sym[len] = '\0';
                    if (strcmp(sym, word) == 0) {
                        char loc[512];
                        snprintf(loc, sizeof(loc),
                                 "{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}},",
                                 uri, cur_line, line_char_offset, cur_line, line_char_offset + len);
                        strcat(res, loc);
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
    if (res[rlen - 1] == ',') res[rlen - 1] = '\0';
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
            if (isalnum(*p) || *p == '_') {
                const char *start = p;
                int len = 0;
                while (*p && (isalnum(*p) || *p == '_')) { p++; len++; }
                char sym[256];
                if (len < sizeof(sym)) {
                    memcpy(sym, start, len);
                    sym[len] = '\0';
                    if (strcmp(sym, word) == 0) {
                        char edit[512];
                        snprintf(edit, sizeof(edit),
                                 "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}},\"newText\":\"%s\"},",
                                 cur_line, line_char_offset, cur_line, line_char_offset + len, new_name);
                        strcat(edits, edit);
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
    if (edits[elen - 1] == ',') edits[elen - 1] = '\0';
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
        } else if (t.type == TSTRLIT) {
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
                    HandleInitialize(id);
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
                    HandleCompletion(id);
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
    
    SymsClear();
    for (int i = 0; i < g_docs_count; i++) {
        free(g_docs[i].uri);
        free(g_docs[i].text);
    }
    free(g_docs);
    
    return 0;
}
