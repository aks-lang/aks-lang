#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "bind.h"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} BindBuf;

static int BindBufAppendf(BindBuf *b, const char *fmt, ...) {
    va_list ap1, ap2;
    va_start(ap1, fmt);
    va_copy(ap2, ap1);
    int needed = vsnprintf(NULL, 0, fmt, ap1);
    va_end(ap1);
    if (needed < 0) {
        va_end(ap2);
        return -1;
    }
    size_t req = b->len + (size_t)needed + 1;
    if (req > b->cap) {
        size_t new_cap = b->cap == 0 ? 1024 : b->cap;
        while (new_cap < req) new_cap *= 2;
        char *nb = (char *)realloc(b->data, new_cap);
        if (!nb) {
            va_end(ap2);
            return -1;
        }
        b->data = nb;
        b->cap = new_cap;
    }
    vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)needed;
    return 0;
}

static char GetSigChar(const char *type_str) {
    if (!type_str) return 'i';
    char clean[256] = "";
    int c_idx = 0;
    for (int i = 0; type_str[i] && c_idx < 255; i++) {
        if (!isspace((unsigned char)type_str[i])) {
            clean[c_idx++] = type_str[i];
        }
    }
    clean[c_idx] = '\0';

    if (strstr(clean, "(*") || strstr(clean, "function")) {
        return 'p'; // function pointer mapped to pointer
    }
    if (strstr(clean, "char") && strchr(clean, '*')) {
        return 's'; // string pointer
    }
    if (strchr(clean, '*')) {
        return 'p'; // generic pointer
    }
    if (strcmp(clean, "double") == 0 || strcmp(clean, "float") == 0) {
        return 'd'; // double/float
    }
    if (strcmp(clean, "void") == 0) {
        return 'v'; // void
    }
    if (strstr(clean, "long") || strstr(clean, "int64")) {
        return 'l'; // bit int
    }
    return 'i'; // default to int
}

static void GetMsvcMangled(char *dest, size_t dest_len, const char *name, char ret_char, const char *param_chars, int param_count) {
    int is_long_ret = (ret_char == 'l');
    char ret_msvc = 'X';
    if (ret_char == 'i') ret_msvc = 'H';
    else if (ret_char == 'l') ret_msvc = '_';
    else if (ret_char == 'd') ret_msvc = 'N';
    else if (ret_char == 's') ret_msvc = 'P';
    else if (ret_char == 'p') ret_msvc = 'P';
    else if (ret_char == 'v') ret_msvc = 'X';

    int raw_len;
    if (is_long_ret) {
        raw_len = snprintf(dest, dest_len, "?%s@@YA_J", name);
    } else {
        raw_len = snprintf(dest, dest_len, "?%s@@YA%c", name, ret_msvc);
    }
    size_t len = (raw_len > 0) ? (size_t)raw_len : 0;
    for (int i = 0; i < param_count; i++) {
        char p = param_chars[i];
        if (p == 'i') {
            if (len + 1 < dest_len) { dest[len++] = 'H'; dest[len] = '\0'; }
        } else if (p == 'l') {
            if (len + 2 < dest_len) { strcat(dest, "_J"); len += 2; }
        } else if (p == 'd') {
            if (len + 1 < dest_len) { dest[len++] = 'N'; dest[len] = '\0'; }
        } else if (p == 's') {
            if (len + 4 < dest_len) { strcat(dest, "PEBD"); len += 4; }
        } else if (p == 'p') {
            if (len + 4 < dest_len) { strcat(dest, "PEAX"); len += 4; }
        }
    }
    if (param_count == 0) {
        if (len + 1 < dest_len) { dest[len++] = 'X'; dest[len] = '\0'; }
    }
    if (len + 2 < dest_len) {
        strcat(dest, "@Z");
    }
}

static void GetItaniumMangled(char *dest, size_t dest_len, const char *name, const char *param_chars, int param_count) {
    int raw_len = snprintf(dest, dest_len, "_Z%d%s", (int)strlen(name), name);
    size_t len = (raw_len > 0) ? (size_t)raw_len : 0;
    for (int i = 0; i < param_count; i++) {
        char p = param_chars[i];
        if (p == 'i') {
            if (len + 1 < dest_len) { dest[len++] = 'i'; dest[len] = '\0'; }
        } else if (p == 'l') {
            if (len + 1 < dest_len) { dest[len++] = 'l'; dest[len] = '\0'; }
        } else if (p == 'd') {
            if (len + 1 < dest_len) { dest[len++] = 'd'; dest[len] = '\0'; }
        } else if (p == 's') {
            if (len + 3 < dest_len) { strcat(dest, "PKc"); len += 3; }
        } else if (p == 'p') {
            if (len + 2 < dest_len) { strcat(dest, "Pv"); len += 2; }
        }
    }
    if (param_count == 0) {
        if (len + 1 < dest_len) { dest[len++] = 'v'; dest[len] = '\0'; }
    }
}

static void TrimWhitespace(char *str) {
    // Trim leading
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    // Trim trailing
    int len = (int)strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

char *AKSBindHeadersToStr(const char *header_path, int is_cpp, const char *libname) {
    if (!header_path) return NULL;
    FILE *in = fopen(header_path, "rb");
    if (!in) {
        fprintf(stderr, "Error: cannot open header file %s\n", header_path);
        return NULL;
    }
    fseek(in, 0, SEEK_END);
    long fsize = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (fsize < 0) {
        fclose(in);
        return NULL;
    }

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(in);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, in);
    buf[nread] = '\0';
    fclose(in);

    // Comment stripping (single-line // and block /* ... */)
    char *clean = malloc(nread + 1);
    if (!clean) {
        free(buf);
        return NULL;
    }
    size_t c_idx = 0;
    int in_scomment = 0, in_bcomment = 0;
    for (size_t i = 0; i < nread; i++) {
        if (in_scomment) {
            if (buf[i] == '\n') in_scomment = 0;
            continue;
        }
        if (in_bcomment) {
            if (buf[i] == '*' && buf[i+1] == '/') { in_bcomment = 0; i++; }
            continue;
        }
        if (buf[i] == '/' && buf[i+1] == '/') { in_scomment = 1; i++; continue; }
        if (buf[i] == '/' && buf[i+1] == '*') { in_bcomment = 1; i++; continue; }
        if (buf[i] == '#') {
            while (i < nread && buf[i] != '\n') i++;
            continue;
        }
        clean[c_idx++] = (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') ? ' ' : buf[i];
    }
    clean[c_idx] = '\0';
    free(buf);

    BindBuf out = {NULL, 0, 0};
    const char *hdr_label = (libname && libname[0]) ? libname : header_path;
    BindBufAppendf(&out, "// Automatically generated AKS FFI bindings for '%s'\n\n", hdr_label);
    if (libname && libname[0]) {
        BindBufAppendf(&out, "let _ffi_lib = FFILoad(\"%s\");\n\n", libname);
    } else {
        BindBufAppendf(&out, "let _ffi_lib = 0;\n\n");
    }

    char *p = clean;
    while (*p) {
        char *semi = strchr(p, ';');
        char *lbrace = strchr(p, '{');
        char *rbrace = NULL;
        int has_body = 0;
        char *decl = NULL;
        char *body_str = NULL;

        if (lbrace && (!semi || lbrace < semi)) {
            // Function definition with inline body { ... }
            rbrace = strchr(lbrace, '}');
            if (rbrace) {
                has_body = 1;
                size_t dlen = lbrace - p;
                decl = malloc(dlen + 1);
                memcpy(decl, p, dlen);
                decl[dlen] = '\0';

                size_t blen = rbrace - lbrace - 1;
                body_str = malloc(blen + 1);
                memcpy(body_str, lbrace + 1, blen);
                body_str[blen] = '\0';

                p = rbrace + 1;
            }
        }
        if (!has_body) {
            if (!semi) break;
            size_t decl_len = semi - p;
            decl = malloc(decl_len + 1);
            if (!decl) {
                p = semi + 1;
                continue;
            }
            memcpy(decl, p, decl_len);
            decl[decl_len] = '\0';
            p = semi + 1;
        }

        TrimWhitespace(decl);
        char *lparen = strchr(decl, '(');
        char *rparen = strrchr(decl, ')');
        if (lparen && rparen && lparen < rparen) {
            *rparen = '\0';
            *lparen = '\0';

            char ret_and_name[512];
            strncpy(ret_and_name, decl, sizeof(ret_and_name) - 1);
            ret_and_name[sizeof(ret_and_name) - 1] = '\0';
            TrimWhitespace(ret_and_name);

            char *last_space = strrchr(ret_and_name, ' ');
            char *last_star = strrchr(ret_and_name, '*');
            char *name_start = NULL;
            if (last_star && (!last_space || last_star > last_space)) {
                name_start = last_star + 1;
            } else if (last_space) {
                name_start = last_space + 1;
            } else {
                name_start = ret_and_name;
            }

            char func_name[256];
            strncpy(func_name, name_start, sizeof(func_name) - 1);
            func_name[sizeof(func_name) - 1] = '\0';
            TrimWhitespace(func_name);

            if (name_start > ret_and_name) {
                *(name_start - 1) = '\0';
            } else {
                ret_and_name[0] = '\0';
            }
            char ret_type[256];
            strncpy(ret_type, ret_and_name, sizeof(ret_type) - 1);
            ret_type[sizeof(ret_type) - 1] = '\0';
            TrimWhitespace(ret_type);
            if (ret_type[0] == '\0') {
                strcpy(ret_type, "int");
            }

            char ret_char = GetSigChar(ret_type);

            char *params_str = lparen + 1;
            char param_names[16][64];
            char param_types[16];
            int param_count = 0;

            char *token = strtok(params_str, ",");
            while (token && param_count < 16) {
                TrimWhitespace(token);
                if (strcmp(token, "void") != 0 && token[0] != '\0') {
                    char param_decl[256];
                    strncpy(param_decl, token, sizeof(param_decl) - 1);
                    param_decl[sizeof(param_decl) - 1] = '\0';
                    TrimWhitespace(param_decl);

                    char *p_space = strrchr(param_decl, ' ');
                    char *p_star = strrchr(param_decl, '*');
                    char *pname = NULL;
                    if (p_star && (!p_space || p_star > p_space)) {
                        pname = p_star + 1;
                        *p_star = '\0';
                        strcat(param_decl, "*");
                    } else if (p_space) {
                        pname = p_space + 1;
                        *p_space = '\0';
                    }
                    if (pname && pname[0] != '\0') {
                        TrimWhitespace(pname);
                        strncpy(param_names[param_count], pname, 63);
                        param_names[param_count][63] = '\0';
                    } else {
                        snprintf(param_names[param_count], 64, "p%d", param_count);
                    }
                    TrimWhitespace(param_decl);
                    param_types[param_count] = GetSigChar(param_decl);
                    param_count++;
                }
                token = strtok(NULL, ",");
            }

            char sig_str[64];
            int s_idx = snprintf(sig_str, sizeof(sig_str), "%c(", ret_char == 'v' ? 'v' : ret_char);
            for (int i = 0; i < param_count; i++) {
                sig_str[s_idx++] = param_types[i];
            }
            sig_str[s_idx++] = ')';
            sig_str[s_idx] = '\0';

            if (has_body && body_str) {
                BindBufAppendf(&out, "fn %s(", func_name);
                for (int i = 0; i < param_count; i++) {
                    BindBufAppendf(&out, "%s", param_names[i]);
                    if (i + 1 < param_count) BindBufAppendf(&out, ", ");
                }
                TrimWhitespace(body_str);
                BindBufAppendf(&out, ") {\n    %s\n}\n\n", body_str);
                free(body_str);
            } else {
                if (is_cpp) {
                    char msvc_name[512] = "";
                    char it_name[512] = "";
                    GetMsvcMangled(msvc_name, sizeof(msvc_name), func_name, ret_char, param_types, param_count);
                    GetItaniumMangled(it_name, sizeof(it_name), func_name, param_types, param_count);

                    BindBufAppendf(&out, "let _sym_%s = \"\";\n", func_name);
                    BindBufAppendf(&out, "if (platform() == \"windows\") {\n");
                    BindBufAppendf(&out, "    _sym_%s = \"%s\";\n", func_name, msvc_name);
                    BindBufAppendf(&out, "} else {\n");
                    BindBufAppendf(&out, "    _sym_%s = \"%s\";\n", func_name, it_name);
                    BindBufAppendf(&out, "}\n");
                } else {
                    BindBufAppendf(&out, "let _sym_%s = \"%s\";\n", func_name, func_name);
                }

                BindBufAppendf(&out, "fn %s(", func_name);
                for (int i = 0; i < param_count; i++) {
                    BindBufAppendf(&out, "%s", param_names[i]);
                    if (i + 1 < param_count) BindBufAppendf(&out, ", ");
                }
                BindBufAppendf(&out, ") {\n");
                BindBufAppendf(&out, "    return FFICall(_ffi_lib, _sym_%s, \"%s\", [", func_name, sig_str);
                for (int i = 0; i < param_count; i++) {
                    BindBufAppendf(&out, "%s", param_names[i]);
                    if (i + 1 < param_count) BindBufAppendf(&out, ", ");
                }
                BindBufAppendf(&out, "]);\n");
                BindBufAppendf(&out, "}\n\n");
            }
        }
        free(decl);
    }

    free(clean);
    if (!out.data) return strdup("");
    return out.data;
}

int AKSBindHeaders(const char *header_path, const char *output_path, int is_cpp, const char *libname) {
    char *generated = AKSBindHeadersToStr(header_path, is_cpp, libname);
    if (!generated) return -1;
    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Error: cannot open output file %s\n", output_path);
        free(generated);
        return -1;
    }
    fputs(generated, out);
    fclose(out);
    free(generated);
    return 0;
}

int AKSGenerateWasmGlue(const char *wit_path, const char *js_path, const char *ts_path) {
    FILE *in = fopen(wit_path, "r");
    if (!in) {
        fprintf(stderr, "Error: cannot open WIT file %s\n", wit_path);
        return -1;
    }

    FILE *js = fopen(js_path, "w");
    if (!js) {
        fprintf(stderr, "Error: cannot open JS output file %s\n", js_path);
        fclose(in);
        return -1;
    }

    FILE *ts = fopen(ts_path, "w");
    if (!ts) {
        fprintf(stderr, "Error: cannot open TS output file %s\n", ts_path);
        fclose(in);
        fclose(js);
        return -1;
    }

    // Write js header
    fprintf(js, "// Automatically generated JS glue by aks wasm-bind\n\n");
    fprintf(js, "class AKSRuntime {\n");
    fprintf(js, "    constructor(wasmInstance) {\n");
    fprintf(js, "        this.instance = wasmInstance;\n");
    fprintf(js, "        this.exports = wasmInstance.exports;\n");
    fprintf(js, "        this.memory = this.exports.memory;\n");
    fprintf(js, "    }\n\n");
    fprintf(js, "    writeString(str) {\n");
    fprintf(js, "        const bytes = new TextEncoder().encode(str);\n");
    fprintf(js, "        const ptr = this.exports.wasm_alloc(bytes.length + 1);\n");
    fprintf(js, "        const mem = new Uint8Array(this.memory.buffer);\n");
    fprintf(js, "        mem.set(bytes, ptr);\n");
    fprintf(js, "        mem[ptr + bytes.length] = 0;\n");
    fprintf(js, "        return ptr;\n");
    fprintf(js, "    }\n\n");
    fprintf(js, "    readString(ptr) {\n");
    fprintf(js, "        const mem = new Uint8Array(this.memory.buffer);\n");
    fprintf(js, "        let end = ptr;\n");
    fprintf(js, "        while (mem[end] !== 0) end++;\n");
    fprintf(js, "        return new TextDecoder().decode(mem.subarray(ptr, end));\n");
    fprintf(js, "    }\n\n");

    // Write ts header
    fprintf(ts, "// Automatically generated TypeScript declarations by aks wasm-bind\n\n");
    fprintf(ts, "export class AKSRuntime {\n");
    fprintf(ts, "    constructor(wasmInstance: any);\n");

    char line[1024];
    while (fgets(line, sizeof(line), in)) {
        TrimWhitespace(line);
        if (line[0] == '\0' || strncmp(line, "//", 2) == 0) {
            continue;
        }

        if (strstr(line, "export ") && strstr(line, "func(")) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *func_name_raw = line + 7;
                TrimWhitespace(func_name_raw);
                
                char func_name[256];
                int fn_idx = 0;
                for (int i = 0; func_name_raw[i]; i++) {
                    if (func_name_raw[i] == '-') {
                        i++;
                        if (func_name_raw[i]) {
                            func_name[fn_idx++] = toupper((unsigned char)func_name_raw[i]);
                        }
                    } else {
                        func_name[fn_idx++] = func_name_raw[i];
                    }
                }
                func_name[fn_idx] = '\0';

                char wasm_export_name[256];
                fn_idx = 0;
                for (int i = 0; func_name_raw[i]; i++) {
                    if (func_name_raw[i] == '-') {
                        func_name_raw[i] = '_';
                    }
                    wasm_export_name[fn_idx++] = func_name_raw[i];
                }
                wasm_export_name[fn_idx] = '\0';

                fprintf(js, "    %s(source) {\n", func_name);
                fprintf(js, "        const ptr = this.writeString(source);\n");
                fprintf(js, "        const res = this.exports.wasm_%s(ptr);\n", wasm_export_name);
                fprintf(js, "        this.exports.wasm_free(ptr);\n");
                fprintf(js, "        return res;\n");
                fprintf(js, "    }\n\n");

                fprintf(ts, "    %s(source: string): number;\n", func_name);
            }
        }
    }

    fprintf(js, "}\n\n");
    fprintf(js, "async function instantiate(wasmModuleOrBuffer, imports = {}) {\n");
    fprintf(js, "    const wasiImports = {\n");
    fprintf(js, "        fd_write: (fd, iovs, iovs_len, nwritten) => {\n");
    fprintf(js, "            return 0;\n");
    fprintf(js, "        }\n");
    fprintf(js, "    };\n");
    fprintf(js, "    let instanceObj = null;\n");
    fprintf(js, "    const wasmImports = {\n");
    fprintf(js, "        wasi_snapshot_preview1: wasiImports,\n");
    fprintf(js, "        env: {\n");
    fprintf(js, "            print: (ptr) => {\n");
    fprintf(js, "                if (instanceObj) {\n");
    fprintf(js, "                    const msg = instanceObj.readString(ptr);\n");
    fprintf(js, "                    if (imports.print) imports.print(msg);\n");
    fprintf(js, "                    else console.log(msg);\n");
    fprintf(js, "                }\n");
    fprintf(js, "            }\n");
    fprintf(js, "        }\n");
    fprintf(js, "    };\n");
    fprintf(js, "    const { instance } = await WebAssembly.instantiate(wasmModuleOrBuffer, wasmImports);\n");
    fprintf(js, "    instanceObj = new AKSRuntime(instance);\n");
    fprintf(js, "    return instanceObj;\n");
    fprintf(js, "}\n\n");
    fprintf(js, "module.exports = { instantiate };\n");

    fprintf(ts, "}\n\n");
    fprintf(ts, "export function instantiate(wasmModuleOrBuffer: any, imports?: {\n");
    fprintf(ts, "    print?: (msg: string) => void;\n");
    fprintf(ts, "}): Promise<AKSRuntime>;\n");

    fclose(in);
    fclose(js);
    fclose(ts);
    return 0;
}
