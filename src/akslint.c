// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
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
    int col;
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
        "..", "...", "&", "|", "^", "|>", NULL
    };
    for (int i = 0; binops[i]; i++) {
        if (strcmp(binops[i], val) == 0) return 1;
    }
    return 0;
}

static FmtToken *LintTokenize(const char *src, int *count_out) {
    int cap = 128;
    int count = 0;
    FmtToken *tokens = malloc(cap * sizeof(FmtToken));
    
    size_t pos = 0;
    int line = 1;
    int col = 1;
    
    while (src[pos]) {
        char c = src[pos];
        int start_col = col;
        
        if (c == '\n') {
            if (count >= cap) {
                cap *= 2;
                tokens = realloc(tokens, cap * sizeof(FmtToken));
            }
            tokens[count].type = FMT_NEWLINE;
            tokens[count].val = strdup("\n");
            tokens[count].line = line;
            tokens[count].col = col;
            count++;
            
            line++;
            col = 1;
            pos++;
            continue;
        }
        if (c == '\r') {
            pos++;
            continue;
        }
        
        if (isspace((unsigned char)c)) {
            pos++;
            col++;
            continue;
        }
        
        if (c == '/' && src[pos + 1] == '/') {
            size_t start = pos;
            while (src[pos] && src[pos] != '\n' && src[pos] != '\r') {
                pos++;
                col++;
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
            tokens[count].col = start_col;
            count++;
            continue;
        }
        
        if (c == '/' && src[pos + 1] == '*') {
            size_t start = pos;
            pos += 2;
            col += 2;
            while (src[pos]) {
                if (src[pos] == '*' && src[pos + 1] == '/') {
                    pos += 2;
                    col += 2;
                    break;
                }
                if (src[pos] == '\n') {
                    line++;
                    col = 1;
                } else {
                    col++;
                }
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
            tokens[count].col = start_col;
            count++;
            continue;
        }
        
        if (c == '"') {
            size_t start = pos;
            pos++;
            col++;
            while (src[pos] && src[pos] != '"') {
                if (src[pos] == '\\' && src[pos + 1]) {
                    pos += 2;
                    col += 2;
                } else {
                    if (src[pos] == '\n') {
                        line++;
                        col = 1;
                    } else {
                        col++;
                    }
                    pos++;
                }
            }
            if (src[pos] == '"') { pos++; col++; }
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
            tokens[count].col = start_col;
            count++;
            continue;
        }
        
        // Symbols
        static const struct { const char *sym; FmtTType type; } sym_map[] = {
            { "++", FMT_SYMBOL }, { "--", FMT_SYMBOL }, { "+=", FMT_SYMBOL },
            { "-=", FMT_SYMBOL }, { "*=", FMT_SYMBOL }, { "/=", FMT_SYMBOL },
            { "%=", FMT_SYMBOL }, { "==", FMT_SYMBOL }, { "!=", FMT_SYMBOL },
            { "<=", FMT_SYMBOL }, { ">=", FMT_SYMBOL }, { "&&", FMT_SYMBOL },
            { "||", FMT_SYMBOL }, { "->", FMT_SYMBOL }, { "=>", FMT_SYMBOL },
            { "??", FMT_SYMBOL }, { "?.", FMT_SYMBOL }, { "..", FMT_SYMBOL },
            { "...", FMT_SYMBOL }, { "|>", FMT_SYMBOL },
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
                tokens[count].col = start_col;
                count++;
                pos += sym_len;
                col += (int)sym_len;
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
            tokens[count].col = start_col;
            count++;
            pos++;
            col++;
            continue;
        }
        
        // Numbers
        if (isdigit((unsigned char)c)) {
            size_t start = pos;
            int is_float = 0;
            while (isdigit((unsigned char)src[pos]) || src[pos] == '.') {
                if (src[pos] == '.') {
                    if (is_float || !isdigit((unsigned char)src[pos + 1])) {
                        break;
                    }
                    is_float = 1;
                }
                pos++;
                col++;
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
            tokens[count].col = start_col;
            count++;
            continue;
        }
        
        // Identifiers
        if (isalpha((unsigned char)c) || c == '_') {
            size_t start = pos;
            while (isalnum((unsigned char)src[pos]) || src[pos] == '_') {
                pos++;
                col++;
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
            tokens[count].col = start_col;
            count++;
            continue;
        }
        
        pos++;
        col++;
    }
    
    if (count >= cap) {
        cap *= 2;
        tokens = realloc(tokens, cap * sizeof(FmtToken));
    }
    tokens[count].type = FMT_EOF;
    tokens[count].val = strdup("");
    tokens[count].line = line;
    tokens[count].col = col;
    count++;
    
    *count_out = count;
    return tokens;
}

// Extract base name from import path: e.g. "math" -> math, "foo/bar.aks" -> bar
static char *GetImportBasenameLint(const char *path) {
    const char *start = path;
    if (*start == '"') start++;
    const char *end = start;
    while (*end && *end != '"') end++;
    
    const char *last_slash = strrchr(start, '/');
    const char *last_bslash = strrchr(start, '\\');
    const char *base = start;
    if (last_slash && last_slash < end && last_slash > base) base = last_slash + 1;
    if (last_bslash && last_bslash < end && last_bslash > base) base = last_bslash + 1;
    
    size_t len = end - base;
    if (len > 4 && strncmp(end - 4, ".aks", 4) == 0) {
        len -= 4;
    }
    char *res = malloc(len + 1);
    memcpy(res, base, len);
    res[len] = '\0';
    return res;
}

// Linter rules framework
typedef struct {
    const char *name;
    const char *desc;
    int enabled;
    int (*check)(const char *filename, const char *source, FmtToken *tokens, int count, int *warns);
} LintRule;

// Rule: indent-style (no tabs for indentation)
static int RuleIndentStyle(const char *filename, const char *source, FmtToken *tokens, int count, int *warns) {
    int idx = 0;
    int line = 1;
    int start_of_line = 1;
    while (source[idx]) {
        if (source[idx] == '\n') {
            line++;
            start_of_line = 1;
        } else if (source[idx] == '\r') {
            // skip
        } else {
            if (start_of_line) {
                if (source[idx] == '\t') {
                    printf("%s:%d:1: Warning: [indent-style] Use of tab character for indentation is discouraged. Use spaces instead.\n", filename, line);
                    (*warns)++;
                    start_of_line = 0; // only report once per line
                } else if (source[idx] != ' ') {
                    start_of_line = 0;
                }
            }
        }
        idx++;
    }
    return 0;
}

// Rule: unused-import
static int RuleUnusedImport(const char *filename, const char *source, FmtToken *tokens, int count, int *warns) {
    typedef struct {
        char *basename;
        char *path;
        int line;
        int col;
        int used;
    } ImportInfo;
    
    ImportInfo imports[128];
    int imp_count = 0;
    
    int first_non_import = -1;
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == FMT_NEWLINE || tokens[i].type == FMT_COMMENT) continue;
        if (tokens[i].type == FMT_KEYWORD && strcmp(tokens[i].val, "import") == 0) {
            int line = tokens[i].line;
            int col = tokens[i].col;
            i++;
            while (i < count && (tokens[i].type == FMT_NEWLINE || tokens[i].type == FMT_COMMENT)) i++;
            if (i < count && tokens[i].type == FMT_STR) {
                char *path = tokens[i].val;
                i++;
                while (i < count && (tokens[i].type == FMT_NEWLINE || tokens[i].type == FMT_COMMENT)) i++;
                if (i < count && tokens[i].type == FMT_SEMI) {
                    if (imp_count < 128) {
                        imports[imp_count].path = path;
                        imports[imp_count].basename = GetImportBasenameLint(path);
                        imports[imp_count].line = line;
                        imports[imp_count].col = col;
                        imports[imp_count].used = 0;
                        imp_count++;
                    }
                    continue;
                }
            }
        }
        first_non_import = i;
        break;
    }
    
    if (first_non_import < 0) first_non_import = 0;
    
    for (int i = first_non_import; i < count; i++) {
        if (tokens[i].type == FMT_IDENT) {
            for (int k = 0; k < imp_count; k++) {
                if (strcmp(tokens[i].val, imports[k].basename) == 0) {
                    imports[k].used = 1;
                }
            }
        }
    }
    
    for (int k = 0; k < imp_count; k++) {
        if (!imports[k].used) {
            printf("%s:%d:%d: Warning: [unused-import] Imported package '%s' is never used.\n", filename, imports[k].line, imports[k].col, imports[k].basename);
            (*warns)++;
        }
        free(imports[k].basename);
    }
    return 0;
}

// Rule: naming-convention
static int RuleNamingConvention(const char *filename, const char *source, FmtToken *tokens, int count, int *warns) {
    for (int i = 0; i < count - 1; i++) {
        // let x or const y
        if (tokens[i].type == FMT_KEYWORD && (strcmp(tokens[i].val, "let") == 0 || strcmp(tokens[i].val, "const") == 0)) {
            int next = i + 1;
            while (next < count && (tokens[next].type == FMT_NEWLINE || tokens[next].type == FMT_COMMENT)) next++;
            if (next < count && tokens[next].type == FMT_IDENT) {
                char *name = tokens[next].val;
                // variable naming: camelCase or snake_case (starts with lowercase)
                if (name[0] && isupper((unsigned char)name[0])) {
                    printf("%s:%d:%d: Warning: [naming-convention] Variable '%s' should start with a lowercase letter.\n", filename, tokens[next].line, tokens[next].col, name);
                    (*warns)++;
                }
            }
        }
        // fn foo
        if (tokens[i].type == FMT_KEYWORD && strcmp(tokens[i].val, "fn") == 0) {
            int next = i + 1;
            while (next < count && (tokens[next].type == FMT_NEWLINE || tokens[next].type == FMT_COMMENT)) next++;
            if (next < count && tokens[next].type == FMT_IDENT) {
                char *name = tokens[next].val;
                if (name[0] && isupper((unsigned char)name[0])) {
                    printf("%s:%d:%d: Warning: [naming-convention] Function '%s' should start with a lowercase letter.\n", filename, tokens[next].line, tokens[next].col, name);
                    (*warns)++;
                }
            }
        }
        // struct, enum, interface PascalCase
        if (tokens[i].type == FMT_KEYWORD && (strcmp(tokens[i].val, "struct") == 0 || strcmp(tokens[i].val, "enum") == 0 || strcmp(tokens[i].val, "interface") == 0)) {
            int next = i + 1;
            while (next < count && (tokens[next].type == FMT_NEWLINE || tokens[next].type == FMT_COMMENT)) next++;
            if (next < count && tokens[next].type == FMT_IDENT) {
                char *name = tokens[next].val;
                if (name[0] && islower((unsigned char)name[0])) {
                    printf("%s:%d:%d: Warning: [naming-convention] Struct/Enum/Interface '%s' should start with an uppercase letter (PascalCase).\n", filename, tokens[next].line, tokens[next].col, name);
                    (*warns)++;
                }
            }
        }
    }
    return 0;
}

// Rule: operator-spacing
static int RuleOperatorSpacing(const char *filename, const char *source, FmtToken *tokens, int count, int *warns) {
    for (int i = 1; i < count - 1; i++) {
        if (IsBinopSymbol(tokens[i].val)) {
            // check if unary first
            int is_unary = 0;
            if (strcmp(tokens[i].val, "-") == 0 || strcmp(tokens[i].val, "+") == 0) {
                FmtToken *prev = &tokens[i - 1];
                if (prev->type == FMT_SYMBOL || prev->type == FMT_LPAREN || prev->type == FMT_LBRACKET || prev->type == FMT_LBRACE || prev->type == FMT_SEMI || prev->type == FMT_COMMA || prev->type == FMT_COLON || prev->type == FMT_KEYWORD) {
                    is_unary = 1;
                }
            }
            if (is_unary) continue;
            
            // For binary operators, check spacing around them
            // If they are not followed or preceded by space/newline, warn
            // Since we skipped space tokens, we need to inspect the original source text around token.
            // But we can approximate using columns if they are on the same line!
            FmtToken *prev = &tokens[i - 1];
            FmtToken *next = &tokens[i + 1];
            
            if (prev->line == tokens[i].line) {
                int expected_col = prev->col + (int)strlen(prev->val) + 1;
                if (tokens[i].col < expected_col) {
                    printf("%s:%d:%d: Warning: [operator-spacing] Missing space before operator '%s'.\n", filename, tokens[i].line, tokens[i].col, tokens[i].val);
                    (*warns)++;
                }
            }
            if (tokens[i].line == next->line) {
                int expected_col = tokens[i].col + (int)strlen(tokens[i].val) + 1;
                if (next->col < expected_col) {
                    printf("%s:%d:%d: Warning: [operator-spacing] Missing space after operator '%s'.\n", filename, tokens[i].line, tokens[i].col, tokens[i].val);
                    (*warns)++;
                }
            }
        }
    }
    return 0;
}

// Rule: brace-style (Egyptian braces)
static int RuleBraceStyle(const char *filename, const char *source, FmtToken *tokens, int count, int *warns) {
    for (int i = 1; i < count; i++) {
        if (tokens[i].type == FMT_LBRACE) {
            FmtToken *prev = &tokens[i - 1];
            if (prev->type == FMT_NEWLINE) {
                printf("%s:%d:%d: Warning: [brace-style] Opening brace '{' should be on the same line as the statement (Egyptian brace style).\n", filename, tokens[i].line, tokens[i].col);
                (*warns)++;
            } else if (prev->line == tokens[i].line) {
                int expected_col = prev->col + (int)strlen(prev->val) + 1;
                if (tokens[i].col < expected_col) {
                    printf("%s:%d:%d: Warning: [brace-style] Missing space before opening brace '{'.\n", filename, tokens[i].line, tokens[i].col);
                    (*warns)++;
                }
            }
        }
    }
    return 0;
}

// Rule: style-semicolon (require semicolons at the end of statements)
static int RuleStyleSemicolon(const char *filename, const char *source, FmtToken *tokens, int count, int *warns) {
    for (int i = 0; i < count - 1; i++) {
        if (tokens[i].type == FMT_KEYWORD && (
            strcmp(tokens[i].val, "let") == 0 ||
            strcmp(tokens[i].val, "const") == 0 ||
            strcmp(tokens[i].val, "return") == 0 ||
            strcmp(tokens[i].val, "throw") == 0 ||
            strcmp(tokens[i].val, "yield") == 0
        )) {
            // Scan forward to the end of the line or block
            int next = i + 1;
            int found_semi = 0;
            int found_newline = 0;
            while (next < count) {
                if (tokens[next].type == FMT_SEMI) {
                    found_semi = 1;
                    break;
                }
                if (tokens[next].type == FMT_NEWLINE) {
                    found_newline = 1;
                    break;
                }
                if (tokens[next].type == FMT_RBRACE) {
                    break;
                }
                next++;
            }
            if (found_newline && !found_semi) {
                printf("%s:%d:%d: Warning: [style-semicolon] Statement should end with a semicolon ';'.\n", filename, tokens[i].line, tokens[i].col);
                (*warns)++;
            }
        }
    }
    return 0;
}

// Rule: unused-variable
static int RuleUnusedVariable(const char *filename, const char *source, FmtToken *tokens, int count, int *warns) {
    typedef struct {
        char *name;
        int line;
        int col;
        int used;
    } VarInfo;
    
    VarInfo vars[256];
    int var_count = 0;
    
    for (int i = 0; i < count - 1; i++) {
        if (tokens[i].type == FMT_KEYWORD && (strcmp(tokens[i].val, "let") == 0 || strcmp(tokens[i].val, "const") == 0)) {
            int next = i + 1;
            while (next < count && (tokens[next].type == FMT_NEWLINE || tokens[next].type == FMT_COMMENT)) next++;
            if (next < count && tokens[next].type == FMT_IDENT) {
                if (var_count < 256) {
                    vars[var_count].name = tokens[next].val;
                    vars[var_count].line = tokens[next].line;
                    vars[var_count].col = tokens[next].col;
                    vars[var_count].used = 0;
                    var_count++;
                }
            }
        }
    }
    
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == FMT_IDENT) {
            // Check if it is a reference rather than a declaration
            // If the preceding token was let/const, it's a declaration.
            int is_decl = 0;
            if (i > 0) {
                int prev = i - 1;
                while (prev >= 0 && (tokens[prev].type == FMT_NEWLINE || tokens[prev].type == FMT_COMMENT)) prev--;
                if (prev >= 0 && tokens[prev].type == FMT_KEYWORD && (strcmp(tokens[prev].val, "let") == 0 || strcmp(tokens[prev].val, "const") == 0)) {
                    is_decl = 1;
                }
            }
            if (!is_decl) {
                for (int k = 0; k < var_count; k++) {
                    if (strcmp(tokens[i].val, vars[k].name) == 0) {
                        vars[k].used = 1;
                    }
                }
            }
        }
    }
    
    for (int k = 0; k < var_count; k++) {
        if (!vars[k].used) {
            printf("%s:%d:%d: Warning: [unused-variable] Variable '%s' is declared but never used.\n", filename, vars[k].line, vars[k].col, vars[k].name);
            (*warns)++;
        }
    }
    return 0;
}

static LintRule rules_registry[] = {
    { "indent-style", "Discourages use of tab characters for indentation", 1, RuleIndentStyle },
    { "unused-import", "Warns on unused package imports", 1, RuleUnusedImport },
    { "naming-convention", "Enforces style conventions on identifiers", 1, RuleNamingConvention },
    { "operator-spacing", "Enforces space formatting around binary operators", 1, RuleOperatorSpacing },
    { "brace-style", "Enforces Egyptian opening brace styling", 1, RuleBraceStyle },
    { "style-semicolon", "Requires semicolons at end of statements", 1, RuleStyleSemicolon },
    { "unused-variable", "Warns on variables declared but never used", 1, RuleUnusedVariable }
};

static const int rules_count = sizeof(rules_registry) / sizeof(rules_registry[0]);

static void RunLinterOnFile(const char *filename, int *warns) {
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
    FmtToken *tokens = LintTokenize(buf, &count);
    
    for (int i = 0; i < rules_count; i++) {
        if (rules_registry[i].enabled) {
            rules_registry[i].check(filename, buf, tokens, count, warns);
        }
    }
    
    for (int i = 0; i < count; i++) {
        free(tokens[i].val);
    }
    free(tokens);
    free(buf);
}

// Find files recursively to lint
#ifdef _WIN32
static void ScanAndLintRec(const char *dir, int *warns) {
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
            if (ffd.cFileName[0] != '.' &&
                strcmp(ffd.cFileName, "mylib") != 0 &&
                strcmp(ffd.cFileName, "myapp") != 0 &&
                strcmp(ffd.cFileName, "tests") != 0 &&
                strcmp(ffd.cFileName, "scratch") != 0 &&
                strcmp(ffd.cFileName, "templibrarydir") != 0) {
                ScanAndLintRec(full_path, warns);
            }
        } else {
            const char *dot = strrchr(ffd.cFileName, '.');
            if (dot && strcmp(dot, ".aks") == 0) {
                RunLinterOnFile(full_path, warns);
            }
        }
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
}
#else
static void ScanAndLintRec(const char *dir, int *warns) {
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
                if (entry->d_name[0] != '.' &&
                    strcmp(entry->d_name, "mylib") != 0 &&
                    strcmp(entry->d_name, "myapp") != 0 &&
                    strcmp(entry->d_name, "tests") != 0 &&
                    strcmp(entry->d_name, "scratch") != 0 &&
                    strcmp(entry->d_name, "templibrarydir") != 0) {
                    ScanAndLintRec(full_path, warns);
                }
            } else {
                const char *dot = strrchr(entry->d_name, '.');
                if (dot && strcmp(dot, ".aks") == 0) {
                    RunLinterOnFile(full_path, warns);
                }
            }
        }
    }
    closedir(d);
}
#endif

// Run formatter formatting function natively on a file for auto-fix mode
static void AutofixFile(const char *filename) {
    // We launch aksfmt with --write option on this file
    char cmd[512];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), ".\\aksfmt.exe --write \"%s\"", filename);
#else
    snprintf(cmd, sizeof(cmd), "./aksfmt --write \"%s\"", filename);
#endif
    int res = system(cmd);
    if (res == 0) {
        printf("Auto-fixed style violations in: %s\n", filename);
    } else {
        fprintf(stderr, "Error: Auto-fix failed on '%s'\n", filename);
    }
}

#ifdef _WIN32
static void ScanAndAutofixRec(const char *dir) {
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
            if (ffd.cFileName[0] != '.' &&
                strcmp(ffd.cFileName, "mylib") != 0 &&
                strcmp(ffd.cFileName, "myapp") != 0 &&
                strcmp(ffd.cFileName, "tests") != 0 &&
                strcmp(ffd.cFileName, "scratch") != 0 &&
                strcmp(ffd.cFileName, "templibrarydir") != 0) {
                ScanAndAutofixRec(full_path);
            }
        } else {
            const char *dot = strrchr(ffd.cFileName, '.');
            if (dot && strcmp(dot, ".aks") == 0) {
                AutofixFile(full_path);
            }
        }
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
}
#else
static void ScanAndAutofixRec(const char *dir) {
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
                if (entry->d_name[0] != '.' &&
                    strcmp(entry->d_name, "mylib") != 0 &&
                    strcmp(entry->d_name, "myapp") != 0 &&
                    strcmp(entry->d_name, "tests") != 0 &&
                    strcmp(entry->d_name, "scratch") != 0 &&
                    strcmp(entry->d_name, "templibrarydir") != 0) {
                    ScanAndAutofixRec(full_path);
                }
            } else {
                const char *dot = strrchr(entry->d_name, '.');
                if (dot && strcmp(dot, ".aks") == 0) {
                    AutofixFile(full_path);
                }
            }
        }
    }
    closedir(d);
}
#endif

int main(int argc, char **argv) {
    int fix_mode = 0;
    int files_start = 1;
    
    // Parse rules configuration, e.g. --rules=indent-style,naming-convention
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fix") == 0) {
            fix_mode = 1;
            if (i == files_start) files_start++;
        } else if (strncmp(argv[i], "--rules=", 8) == 0) {
            const char *rules_list = argv[i] + 8;
            // Disable all rules first
            for (int k = 0; k < rules_count; k++) {
                rules_registry[k].enabled = 0;
            }
            // Parse comma-separated list
            char *list_copy = strdup(rules_list);
            char *tok = strtok(list_copy, ",");
            while (tok) {
                int matched = 0;
                for (int k = 0; k < rules_count; k++) {
                    if (strcmp(rules_registry[k].name, tok) == 0) {
                        rules_registry[k].enabled = 1;
                        matched = 1;
                    }
                }
                if (!matched) {
                    fprintf(stderr, "Unknown rule: '%s'\n", tok);
                }
                tok = strtok(NULL, ",");
            }
            free(list_copy);
            if (i == files_start) files_start++;
        }
    }
    
    int files_count = 0;
    int warns = 0;
    
    for (int i = files_start; i < argc; i++) {
        if (argv[i][0] != '-') {
            files_count++;
            if (fix_mode) {
                AutofixFile(argv[i]);
            } else {
                RunLinterOnFile(argv[i], &warns);
            }
        }
    }
    
    if (files_count == 0) {
        // Zero config behavior: lint recursively in current directory
        printf("Scanning current directory for AKS source files...\n");
        if (fix_mode) {
            ScanAndAutofixRec(".");
        } else {
            ScanAndLintRec(".", &warns);
        }
    }
    
    if (!fix_mode) {
        printf("\nLinting complete: %d warning(s) found.\n", warns);
        if (warns > 0) {
            return 1; // Exit with error if any warning is found
        }
    }
    
    return 0;
}
