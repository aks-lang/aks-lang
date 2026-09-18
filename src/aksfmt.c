// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

typedef enum {
    FMT_EOF = 0,
    FMT_KEYWORD,
    FMT_IDENT,
    FMT_INT,
    FMT_FLOAT,
    FMT_STR,
    FMT_COMMENT,
    FMT_SYMBOL,
    FMT_LPAREN, FMT_RPAREN,
    FMT_LBRACE, FMT_RBRACE,
    FMT_LBRACKET, FMT_RBRACKET,
    FMT_SEMI, FMT_COMMA, FMT_COLON,
    FMT_NEWLINE
} FmtTType;

typedef struct {
    FmtTType type;
    char *val;
    int line;
} FmtToken;

static const char *keywords[] = {
    "let", "const", "fn", "return", "if", "else", "while", "for", "in",
    "print", "input", "true", "false", "null", "struct", "new", "import",
    "as", "break", "continue", "try", "catch", "throw", "switch", "case",
    "default", "enum", "yield", "int", "float", "string", "bool", "void",
    "async", "await", "spawn", "match", "implements", "interface", "where",
    "type", "mut", "select", NULL
};

static int IsKeyWord(const char *name) {
    for (int i = 0; keywords[i]; i++) {
        if (strcmp(keywords[i], name) == 0) return 1;
    }
    return 0;
}

static int IsBinopSymbol(const char *val) {
    static const char *binops[] = {
        "=", "+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=",
        "&&", "||", "+=", "-=", "*=", "/=", "%=", "->", "=>", "??", "?.",
        "..", "...", "&", "|", "^", "|>", ">>", "<<", NULL
    };
    for (int i = 0; binops[i]; i++) {
        if (strcmp(binops[i], val) == 0) return 1;
    }
    return 0;
}

// Custom lexer that preserves comments and newlines
static FmtToken *FmtTokenize(const char *src, int *count_out) {
    int cap = 128;
    int count = 0;
    FmtToken *tokens = malloc(cap * sizeof(FmtToken));
    
    size_t pos = 0;
    int line = 1;
    
    while (src[pos]) {
        char c = src[pos];
        
        if (c == '\n' || c == '\r') {
            int nl_count = 0;
            while (src[pos] == '\n' || src[pos] == '\r') {
                if (src[pos] == '\n') {
                    line++;
                    nl_count++;
                }
                pos++;
            }
            if (count >= cap) {
                cap *= 2;
                tokens = realloc(tokens, cap * sizeof(FmtToken));
            }
            tokens[count].type = FMT_NEWLINE;
            tokens[count].val = strdup("\n");
            tokens[count].line = line;
            count++;
            
            // If there were multiple newlines, emit a second one for blank line separation (collapsing excess)
            if (nl_count > 1) {
                if (count >= cap) {
                    cap *= 2;
                    tokens = realloc(tokens, cap * sizeof(FmtToken));
                }
                tokens[count].type = FMT_NEWLINE;
                tokens[count].val = strdup("\n");
                tokens[count].line = line;
                count++;
            }
            continue;
        }
        
        if (isspace((unsigned char)c)) {
            pos++;
            continue;
        }
        
        if (c == '/' && src[pos + 1] == '/') {
            size_t start = pos;
            while (src[pos] && src[pos] != '\n' && src[pos] != '\r') {
                pos++;
            }
            size_t len = pos - start;
            char *comment_val = malloc(len + 1);
            memcpy(comment_val, src + start, len);
            comment_val[len] = '\0';
            
            if (count >= cap) {
                cap *= 2;
                tokens = realloc(tokens, cap * sizeof(FmtToken));
            }
            tokens[count].type = FMT_COMMENT;
            tokens[count].val = comment_val;
            tokens[count].line = line;
            count++;
            continue;
        }
        
        if (c == '/' && src[pos + 1] == '*') {
            size_t start = pos;
            pos += 2;
            while (src[pos]) {
                if (src[pos] == '*' && src[pos + 1] == '/') {
                    pos += 2;
                    break;
                }
                if (src[pos] == '\n') line++;
                pos++;
            }
            size_t len = pos - start;
            char *comment_val = malloc(len + 1);
            memcpy(comment_val, src + start, len);
            comment_val[len] = '\0';
            
            if (count >= cap) {
                cap *= 2;
                tokens = realloc(tokens, cap * sizeof(FmtToken));
            }
            tokens[count].type = FMT_COMMENT;
            tokens[count].val = comment_val;
            tokens[count].line = line;
            count++;
            continue;
        }
        
        if (c == '"') {
            size_t start = pos;
            int is_triple = (src[pos + 1] == '"' && src[pos + 2] == '"');
            if (is_triple) {
                pos += 3;
                while (src[pos]) {
                    if (src[pos] == '"' && src[pos + 1] == '"' && src[pos + 2] == '"') {
                        pos += 3;
                        break;
                    }
                    if (src[pos] == '\\' && src[pos + 1]) {
                        pos += 2;
                    } else {
                        if (src[pos] == '\n') line++;
                        pos++;
                    }
                }
            } else {
                pos++;
                while (src[pos] && src[pos] != '"') {
                    if (src[pos] == '\\' && src[pos + 1]) {
                        pos += 2;
                    } else {
                        if (src[pos] == '\n') line++;
                        pos++;
                    }
                }
                if (src[pos] == '"') pos++;
            }
            size_t len = pos - start;
            char *str_val = malloc(len + 1);
            memcpy(str_val, src + start, len);
            str_val[len] = '\0';
            
            if (count >= cap) {
                cap *= 2;
                tokens = realloc(tokens, cap * sizeof(FmtToken));
            }
            tokens[count].type = FMT_STR;
            tokens[count].val = str_val;
            tokens[count].line = line;
            count++;
            continue;
        }
        
        static const struct { const char *sym; FmtTType type; } sym_map[] = {
            { "++", FMT_SYMBOL }, { "--", FMT_SYMBOL }, { "+=", FMT_SYMBOL },
            { "-=", FMT_SYMBOL }, { "*=", FMT_SYMBOL }, { "/=", FMT_SYMBOL },
            { "%=", FMT_SYMBOL }, { "==", FMT_SYMBOL }, { "!=", FMT_SYMBOL },
            { "<=", FMT_SYMBOL }, { ">=", FMT_SYMBOL }, { "&&", FMT_SYMBOL },
            { "||", FMT_SYMBOL }, { "->", FMT_SYMBOL }, { "=>", FMT_SYMBOL },
            { "??", FMT_SYMBOL }, { "?.", FMT_SYMBOL },
            { "...", FMT_SYMBOL }, { "..", FMT_SYMBOL },
            { "|>", FMT_SYMBOL }, { ">>", FMT_SYMBOL }, { "<<", FMT_SYMBOL },
            { "(", FMT_LPAREN },  { ")", FMT_RPAREN },
            { "{", FMT_LBRACE },  { "}", FMT_RBRACE },
            { "[", FMT_LBRACKET }, { "]", FMT_RBRACKET },
            { ";", FMT_SEMI },    { ",", FMT_COMMA },   { ":", FMT_COLON },
            { NULL, FMT_SYMBOL }
        };
        
        int found_sym = 0;
        for (int i = 0; sym_map[i].sym; i++) {
            size_t sym_len = strlen(sym_map[i].sym);
            if (strncmp(src + pos, sym_map[i].sym, sym_len) == 0) {
                if (count >= cap) {
                    cap *= 2;
                    tokens = realloc(tokens, cap * sizeof(FmtToken));
                }
                tokens[count].type = sym_map[i].type;
                tokens[count].val = strdup(sym_map[i].sym);
                tokens[count].line = line;
                count++;
                pos += sym_len;
                found_sym = 1;
                break;
            }
        }
        if (found_sym) continue;
        
        if (strchr("+-*/%=<>!&|^~@.?", c)) {
            char val_str[2] = { c, '\0' };
            if (count >= cap) {
                cap *= 2;
                tokens = realloc(tokens, cap * sizeof(FmtToken));
            }
            tokens[count].type = FMT_SYMBOL;
            tokens[count].val = strdup(val_str);
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }
        
        if (isdigit((unsigned char)c)) {
            size_t start = pos;
            int is_float = 0;
            while (isdigit((unsigned char)src[pos]) || src[pos] == '.') {
                if (src[pos] == '.') {
                    if (is_float || !isdigit((unsigned char)src[pos + 1])) {
                        break; // ".." range or second dot
                    }
                    is_float = 1;
                }
                pos++;
            }
            size_t len = pos - start;
            char *num_val = malloc(len + 1);
            memcpy(num_val, src + start, len);
            num_val[len] = '\0';
            
            if (count >= cap) {
                cap *= 2;
                tokens = realloc(tokens, cap * sizeof(FmtToken));
            }
            tokens[count].type = is_float ? FMT_FLOAT : FMT_INT;
            tokens[count].val = num_val;
            tokens[count].line = line;
            count++;
            continue;
        }
        
        if (isalpha((unsigned char)c) || c == '_') {
            size_t start = pos;
            while (isalnum((unsigned char)src[pos]) || src[pos] == '_') {
                pos++;
            }
            size_t len = pos - start;
            char *ident_val = malloc(len + 1);
            memcpy(ident_val, src + start, len);
            ident_val[len] = '\0';
            
            if (count >= cap) {
                cap *= 2;
                tokens = realloc(tokens, cap * sizeof(FmtToken));
            }
            tokens[count].type = IsKeyWord(ident_val) ? FMT_KEYWORD : FMT_IDENT;
            tokens[count].val = ident_val;
            tokens[count].line = line;
            count++;
            continue;
        }
        
        pos++;
    }
    
    if (count >= cap) {
        cap *= 2;
        tokens = realloc(tokens, cap * sizeof(FmtToken));
    }
    tokens[count].type = FMT_EOF;
    tokens[count].val = strdup("");
    tokens[count].line = line;
    count++;
    
    *count_out = count;
    return tokens;
}

typedef struct {
    char *path;
    char *basename;
    int token_start;
    int token_end;
    int used;
} ParsedImport;

static int CompareImports(const void *a, const void *b) {
    return strcmp(((ParsedImport*)a)->path, ((ParsedImport*)b)->path);
}

// Extract base name from import path: e.g. "math" -> math, "foo/bar.aks" -> bar
static char *GetImportBasename(const char *path) {
    const char *start = path;
    if (*start == '"') start++;
    const char *end = start;
    while (*end && *end != '"') end++;
    
    // Find last separator or path end
    const char *last_slash = strrchr(start, '/');
    const char *last_bslash = strrchr(start, '\\');
    const char *base = start;
    if (last_slash && last_slash < end && last_slash > base) base = last_slash + 1;
    if (last_bslash && last_bslash < end && last_bslash > base) base = last_bslash + 1;
    
    // Strip trailing .aks
    size_t len = end - base;
    if (len > 4 && strncmp(end - 4, ".aks", 4) == 0) {
        len -= 4;
    }
    char *res = malloc(len + 1);
    memcpy(res, base, len);
    res[len] = '\0';
    return res;
}

static char *FormatTokens(FmtToken *tokens, int count) {
    // Parse imports
    ParsedImport imports[128];
    int import_count = 0;
    int first_non_import_token = -1;
    
    int idx = 0;
    // Skip initial newlines or comments at the top of the file
    while (idx < count && (tokens[idx].type == FMT_NEWLINE || tokens[idx].type == FMT_COMMENT)) {
        idx++;
    }
    
    while (idx < count) {
        if (tokens[idx].type == FMT_KEYWORD && strcmp(tokens[idx].val, "import") == 0) {
            int start_tok = idx;
            idx++;
            // skip space/newline (newlex doesn't have inline spaces, but might have newline/comment)
            while (idx < count && (tokens[idx].type == FMT_NEWLINE || tokens[idx].type == FMT_COMMENT)) idx++;
            
            if (idx < count && tokens[idx].type == FMT_STR) {
                char *path = tokens[idx].val;
                idx++;
                while (idx < count && (tokens[idx].type == FMT_NEWLINE || tokens[idx].type == FMT_COMMENT)) idx++;
                if (idx < count && tokens[idx].type == FMT_SEMI) {
                    int end_tok = idx;
                    idx++;
                    
                    if (import_count < 128) {
                        imports[import_count].path = path;
                        imports[import_count].basename = GetImportBasename(path);
                        imports[import_count].token_start = start_tok;
                        imports[import_count].token_end = end_tok;
                        imports[import_count].used = 0;
                        import_count++;
                    }
                    // Skip trailing newline if immediate
                    if (idx < count && tokens[idx].type == FMT_NEWLINE) idx++;
                    continue;
                }
            }
        }
        break;
    }
    first_non_import_token = idx;
    
    // Scan remainder of tokens for identifier references of imports to remove unused ones
    for (int i = first_non_import_token; i < count; i++) {
        if (tokens[i].type == FMT_IDENT) {
            for (int k = 0; k < import_count; k++) {
                if (strcmp(tokens[i].val, imports[k].basename) == 0) {
                    imports[k].used = 1;
                }
            }
        }
    }
    
    // Also, core system libraries (like "math" or "std") are kept even if unused if user wanted, but style spec says remove unused.
    // Let's filter out unused imports and sort remaining alphabetically
    ParsedImport active_imports[128];
    int active_count = 0;
    for (int k = 0; k < import_count; k++) {
        if (imports[k].used) {
            active_imports[active_count++] = imports[k];
        } else {
            // Free unused import basename
            free(imports[k].basename);
        }
    }
    
    if (active_count > 0) {
        qsort(active_imports, active_count, sizeof(ParsedImport), CompareImports);
    }
    
    // Render formatting
    size_t out_cap = 4096;
    size_t out_len = 0;
    char *out = malloc(out_cap);
    out[0] = '\0';
    
    // Helper macros to write to output
    #define WRITE_STR(s) do { \
        size_t slen = strlen(s); \
        if (out_len + slen + 1 >= out_cap) { \
            out_cap = out_cap * 2 + slen; \
            out = realloc(out, out_cap); \
        } \
        strcpy(out + out_len, s); \
        out_len += slen; \
    } while(0)
    
    #define WRITE_CHAR(c) do { \
        if (out_len + 2 >= out_cap) { \
            out_cap *= 2; \
            out = realloc(out, out_cap); \
        } \
        out[out_len++] = c; \
        out[out_len] = '\0'; \
    } while(0)
    
    // Write sorted active imports at the very top
    for (int k = 0; k < active_count; k++) {
        WRITE_STR("import ");
        WRITE_STR(active_imports[k].path);
        WRITE_STR(";\n");
        free(active_imports[k].basename);
    }
    if (active_count > 0) {
        WRITE_CHAR('\n');
    }
    
    int indent_level = 0;
    int at_line_start = 1;
    int in_for_header = 0;
    int was_newline = 0;
    
    FmtToken *prev = NULL;
    
    for (int i = first_non_import_token; i < count; i++) {
        FmtToken *tok = &tokens[i];
        if (tok->type == FMT_EOF) break;
        
        // Skip consecutive newlines at the very start of file if imports are handled
        if (out_len == 0 && tok->type == FMT_NEWLINE) continue;
        
        // Handle indentations on newlines
        if (tok->type == FMT_NEWLINE) {
            WRITE_CHAR('\n');
            at_line_start = 1;
            was_newline = 1;
            prev = tok;
            continue;
        }
        
        // Adjust indentation level
        if (tok->type == FMT_RBRACE) {
            indent_level--;
            if (indent_level < 0) indent_level = 0;
        }
        if (tok->type == FMT_RBRACKET) {
            // Brackets might be multi-line
        }
        
        // Write indentation
        if (at_line_start) {
            for (int k = 0; k < indent_level * 4; k++) {
                WRITE_CHAR(' ');
            }
            at_line_start = 0;
        }
        
        // Operator / Symbol Spacing Checks
        if (prev && prev->type != FMT_NEWLINE) {
            int needs_space_before = 0;
            
            // General word-to-word spacing rule
            if ((prev->type == FMT_KEYWORD || prev->type == FMT_IDENT || prev->type == FMT_INT || prev->type == FMT_FLOAT || prev->type == FMT_STR) &&
                (tok->type == FMT_KEYWORD || tok->type == FMT_IDENT || tok->type == FMT_INT || tok->type == FMT_FLOAT || tok->type == FMT_STR)) {
                if (tok->type == FMT_STR && prev->type == FMT_IDENT && strcmp(prev->val, "f") == 0) {
                    needs_space_before = 0;
                } else {
                    needs_space_before = 1;
                }
            }
            // Space before opening brace `{`
            else if (tok->type == FMT_LBRACE) {
                needs_space_before = 1;
            }
            // Space after keywords: if, while, for, switch, match, catch
            else if (prev->type == FMT_KEYWORD && (
                strcmp(prev->val, "if") == 0 ||
                strcmp(prev->val, "while") == 0 ||
                strcmp(prev->val, "for") == 0 ||
                strcmp(prev->val, "switch") == 0 ||
                strcmp(prev->val, "match") == 0 ||
                strcmp(prev->val, "catch") == 0 ||
                strcmp(prev->val, "return") == 0 ||
                strcmp(prev->val, "let") == 0 ||
                strcmp(prev->val, "const") == 0
            )) {
                needs_space_before = 1;
            }
            // Space around binary operators
            else if (IsBinopSymbol(tok->val)) {
                // Determine if unary prefix operator (e.g. -x, !y)
                int is_unary = 0;
                if (strcmp(tok->val, "-") == 0 || strcmp(tok->val, "+") == 0) {
                    if (prev->type == FMT_SYMBOL || prev->type == FMT_LPAREN || prev->type == FMT_LBRACKET || prev->type == FMT_LBRACE || prev->type == FMT_SEMI || prev->type == FMT_COMMA || prev->type == FMT_COLON || prev->type == FMT_KEYWORD) {
                        is_unary = 1;
                    }
                }
                // Generator function '*' prefix check (fn*)
                if (strcmp(tok->val, "*") == 0 && prev->type == FMT_KEYWORD && strcmp(prev->val, "fn") == 0) {
                    is_unary = 1;
                }
                if (!is_unary) {
                    needs_space_before = 1;
                }
            }
            // Space after a binary operator
            else if (IsBinopSymbol(prev->val)) {
                int is_unary = 0;
                if (strcmp(prev->val, "-") == 0 || strcmp(prev->val, "+") == 0) {
                    // Check if it was unary
                    // Look back 2 tokens
                    if (i >= 2) {
                        FmtToken *prev2 = &tokens[i - 2];
                        if (prev2->type == FMT_SYMBOL || prev2->type == FMT_LPAREN || prev2->type == FMT_LBRACKET || prev2->type == FMT_LBRACE || prev2->type == FMT_SEMI || prev2->type == FMT_COMMA || prev2->type == FMT_COLON || prev2->type == FMT_KEYWORD) {
                            is_unary = 1;
                        }
                    } else {
                        is_unary = 1;
                    }
                }
                // Generator function '*' suffix check (fn*)
                if (strcmp(prev->val, "*") == 0 && i >= 2 && tokens[i - 2].type == FMT_KEYWORD && strcmp(tokens[i - 2].val, "fn") == 0) {
                    is_unary = 1;
                }
                if (!is_unary) {
                    needs_space_before = 1;
                }
            }
            // Space after commas
            else if (prev->type == FMT_COMMA) {
                needs_space_before = 1;
            }
            // Space after colons (e.g. in maps or types)
            else if (prev->type == FMT_COLON) {
                needs_space_before = 1;
            }
            
            if (needs_space_before) {
                // Check that we don't double space
                if (out_len > 0 && out[out_len - 1] != ' ' && out[out_len - 1] != '\n') {
                    WRITE_CHAR(' ');
                }
            }
        }
        
        // Write the token value
        WRITE_STR(tok->val);
        
        // Adjust indentation levels on opening symbols
        if (tok->type == FMT_LBRACE) {
            indent_level++;
        }
        
        // Manage "for" loop header tracking (to avoid line break on semicolon inside for headers)
        if (tok->type == FMT_KEYWORD && strcmp(tok->val, "for") == 0) {
            in_for_header = 0;
        }
        if (tok->type == FMT_LPAREN && prev && prev->type == FMT_KEYWORD && strcmp(prev->val, "for") == 0) {
            in_for_header = 1;
        }
        if (tok->type == FMT_RPAREN && in_for_header) {
            in_for_header = 0;
        }
        
        // Line break after semicolon (except in for-headers)
        if (tok->type == FMT_SEMI && !in_for_header) {
            // Check that the next token is not already a newline
            if (i + 1 < count && tokens[i+1].type != FMT_NEWLINE) {
                WRITE_CHAR('\n');
                at_line_start = 1;
                was_newline = 1;
            }
        }
        
        was_newline = 0;
        prev = tok;
    }
    
    // Clean up trailing whitespace and collapse consecutive ending newlines to exactly one newline
    while (out_len > 0 && isspace((unsigned char)out[out_len - 1])) {
        out[--out_len] = '\0';
    }
    WRITE_CHAR('\n');
    
    return out;
}

static void FormatFile(const char *filename, int write_mode) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    
    int count = 0;
    FmtToken *tokens = FmtTokenize(buf, &count);
    char *formatted = FormatTokens(tokens, count);
    
    if (write_mode) {
        // If content did not change, skip writing to avoid file modifications
        if (strcmp(buf, formatted) == 0) {
            printf("Unchanged: %s\n", filename);
        } else {
            FILE *wf = fopen(filename, "wb");
            if (!wf) {
                fprintf(stderr, "Error: Could not write to file '%s'\n", filename);
            } else {
                fwrite(formatted, 1, strlen(formatted), wf);
                fclose(wf);
                printf("Formatted: %s\n", filename);
            }
        }
    } else {
        printf("%s", formatted);
    }
    
    for (int i = 0; i < count; i++) {
        free(tokens[i].val);
    }
    free(tokens);
    free(formatted);
    free(buf);
}

// Find files recursively
#ifdef _WIN32
static void ScanDirectoryRec(const char *dir, int write_mode) {
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*", dir);
    
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search_path, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) {
            continue;
        }
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir, ffd.cFileName);
        
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // skip directories like .akscache, mylib, myapp, .git
            if (ffd.cFileName[0] != '.' && strcmp(ffd.cFileName, "mylib") != 0 && strcmp(ffd.cFileName, "myapp") != 0) {
                ScanDirectoryRec(full_path, write_mode);
            }
        } else {
            // check for .aks extension
            const char *dot = strrchr(ffd.cFileName, '.');
            if (dot && strcmp(dot, ".aks") == 0) {
                FormatFile(full_path, write_mode);
            }
        }
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
}
#else
static void ScanDirectoryRec(const char *dir, int write_mode) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (entry->d_name[0] != '.' && strcmp(entry->d_name, "mylib") != 0 && strcmp(entry->d_name, "myapp") != 0) {
                    ScanDirectoryRec(full_path, write_mode);
                }
            } else {
                const char *dot = strrchr(entry->d_name, '.');
                if (dot && strcmp(dot, ".aks") == 0) {
                    FormatFile(full_path, write_mode);
                }
            }
        }
    }
    closedir(d);
}
#endif

char *AksFormatSource(const char *src) {
    if (!src) return NULL;
    int count = 0;
    FmtToken *tokens = FmtTokenize(src, &count);
    if (!tokens) return _strdup(src);
    char *formatted = FormatTokens(tokens, count);
    for (int i = 0; i < count; i++) {
        free(tokens[i].val);
    }
    free(tokens);
    return formatted;
}

#ifndef AKS_NO_FMT_MAIN
int main(int argc, char **argv) {
    int write_mode = 0;
    int file_args_start = 1;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--write") == 0) {
            write_mode = 1;
            if (i == file_args_start) {
                file_args_start++;
            }
        }
    }
    
    int files_count = 0;
    for (int i = file_args_start; i < argc; i++) {
        if (argv[i][0] != '-') {
            files_count++;
            FormatFile(argv[i], write_mode);
        }
    }
    
    if (files_count == 0) {
        // Zero config behavior: recursively scan and format current directory
        printf("Formatting AKS source files in current directory...\n");
        ScanDirectoryRec(".", write_mode);
    }
    
    return 0;
}
#endif
