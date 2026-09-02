#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "bind.h"

static char GetSigChar(const char *type_str) {
    char clean[256] = "";
    int c_idx = 0;
    for (int i = 0; type_str[i]; i++) {
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

    int len;
    if (is_long_ret) {
        len = snprintf(dest, dest_len, "?%s@@YA_J", name);
    } else {
        len = snprintf(dest, dest_len, "?%s@@YA%c", name, ret_msvc);
    }
    for (int i = 0; i < param_count; i++) {
        char p = param_chars[i];
        if (p == 'i') {
            if (len < dest_len - 1) { dest[len++] = 'H'; dest[len] = '\0'; }
        } else if (p == 'l') {
            if (len < dest_len - 2) { strcat(dest, "_J"); len += 2; }
        } else if (p == 'd') {
            if (len < dest_len - 1) { dest[len++] = 'N'; dest[len] = '\0'; }
        } else if (p == 's') {
            if (len < dest_len - 5) { strcat(dest, "PEBD"); len += 4; }
        } else if (p == 'p') {
            if (len < dest_len - 5) { strcat(dest, "PEAX"); len += 4; }
        }
    }
    if (param_count == 0) {
        if (len < dest_len - 1) { dest[len++] = 'X'; dest[len] = '\0'; }
    }
    if (len < dest_len - 3) {
        strcat(dest, "@Z");
    }
}

static void GetItaniumMangled(char *dest, size_t dest_len, const char *name, const char *param_chars, int param_count) {
    int len = snprintf(dest, dest_len, "_Z%d%s", (int)strlen(name), name);
    for (int i = 0; i < param_count; i++) {
        char p = param_chars[i];
        if (p == 'i') {
            if (len < dest_len - 1) { dest[len++] = 'i'; dest[len] = '\0'; }
        } else if (p == 'l') {
            if (len < dest_len - 1) { dest[len++] = 'l'; dest[len] = '\0'; }
        } else if (p == 'd') {
            if (len < dest_len - 1) { dest[len++] = 'd'; dest[len] = '\0'; }
        } else if (p == 's') {
            if (len < dest_len - 4) { strcat(dest, "PKc"); len += 3; }
        } else if (p == 'p') {
            if (len < dest_len - 3) { strcat(dest, "Pv"); len += 2; }
        }
    }
    if (param_count == 0) {
        if (len < dest_len - 1) { dest[len++] = 'v'; dest[len] = '\0'; }
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

int AKSBindHeaders(const char *header_path, const char *output_path, int is_cpp, const char *libname) {
    FILE *in = fopen(header_path, "rb");
    if (!in) {
        fprintf(stderr, "Error: cannot open header file %s\n", header_path);
        return -1;
    }
    fseek(in, 0, SEEK_END);
    long fsize = ftell(in);
    fseek(in, 0, SEEK_SET);

    char *buf = malloc(fsize + 1);
    if (!buf) {
        fclose(in);
        return -1;
    }
    fread(buf, 1, fsize, in);
    buf[fsize] = '\0';
    fclose(in);

    // Comment stripping (single-line // and block /* ... */)
    char *clean = malloc(fsize + 1);
    int c_idx = 0;
    int in_scomment = 0, in_bcomment = 0;
    for (int i = 0; i < fsize; i++) {
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
            while (i < fsize && buf[i] != '\n') i++;
            continue;
        }
        clean[c_idx++] = (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') ? ' ' : buf[i];
    }
    clean[c_idx] = '\0';
    free(buf);

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Error: cannot open output file %s\n", output_path);
        free(clean);
        return -1;
    }

    fprintf(out, "// Automatically generated AKS FFI bindings for '%s'\n\n", libname);
    fprintf(out, "let _ffi_lib = FFILoad(\"%s\");\n\n", libname);

    char *p = clean;
    while (*p) {
        char *semi = strchr(p, ';');
        if (!semi) break;

        size_t decl_len = semi - p;
        char *decl = malloc(decl_len + 1);
        memcpy(decl, p, decl_len);
        decl[decl_len] = '\0';
        p = semi + 1;

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
                    if (p_star && (!p_space || p_star > p_space)) {
                        *p_star = '\0';
                        strcat(param_decl, "*");
                    } else if (p_space) {
                        *p_space = '\0';
                    }
                    TrimWhitespace(param_decl);
                    param_types[param_count++] = GetSigChar(param_decl);
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

            if (is_cpp) {
                char msvc_name[512] = "";
                char it_name[512] = "";
                GetMsvcMangled(msvc_name, sizeof(msvc_name), func_name, ret_char, param_types, param_count);
                GetItaniumMangled(it_name, sizeof(it_name), func_name, param_types, param_count);

                fprintf(out, "let _sym_%s = \"\";\n", func_name);
                fprintf(out, "if (platform() == \"windows\") {\n");
                fprintf(out, "    _sym_%s = \"%s\";\n", func_name, msvc_name);
                fprintf(out, "} else {\n");
                fprintf(out, "    _sym_%s = \"%s\";\n", func_name, it_name);
                fprintf(out, "}\n");
            } else {
                fprintf(out, "let _sym_%s = \"%s\";\n", func_name, func_name);
            }

            fprintf(out, "fn %s(", func_name);
            for (int i = 0; i < param_count; i++) {
                fprintf(out, "p%d", i);
                if (i + 1 < param_count) fprintf(out, ", ");
            }
            fprintf(out, ") {\n");
            fprintf(out, "    return FFICall(_ffi_lib, _sym_%s, \"%s\", [", func_name, sig_str);
            for (int i = 0; i < param_count; i++) {
                fprintf(out, "p%d", i);
                if (i + 1 < param_count) fprintf(out, ", ");
            }
            fprintf(out, "]);\n");
            fprintf(out, "}\n\n");
        }
        free(decl);
    }

    free(clean);
    fclose(out);
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
