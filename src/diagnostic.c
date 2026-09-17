// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define isatty_portable _isatty
#define fileno_portable _fileno
#else
#include <unistd.h>
#define isatty_portable isatty
#define fileno_portable fileno
#endif

static int SupportsAnsiColor(void) {
    static int cached = -1;
    if (cached != -1) return cached;
    if (getenv("NO_COLOR")) {
        cached = 0;
        return 0;
    }
    cached = isatty_portable(fileno_portable(stderr));
    return cached;
}

void DiagnosticInit(Diagnostic *d, DiagnosticLevel level, const char *code, const char *message) {
    memset(d, 0, sizeof(Diagnostic));
    d->level = level;
    if (code) strncpy(d->code, code, sizeof(d->code) - 1);
    if (message) strncpy(d->message, message, sizeof(d->message) - 1);
}

void DiagnosticAddSpan(Diagnostic *d, const char *file, int line, int col, int length, const char *label, int is_primary) {
    if (d->span_count >= (int)(sizeof(d->spans) / sizeof(d->spans[0]))) return;
    DiagnosticSpan *s = &d->spans[d->span_count++];
    if (file) strncpy(s->file, file, sizeof(s->file) - 1);
    s->line = line;
    s->col = col > 0 ? col : 1;
    s->length = length > 0 ? length : 1;
    if (label) strncpy(s->label, label, sizeof(s->label) - 1);
    s->is_primary = is_primary;
}

void DiagnosticSetNote(Diagnostic *d, const char *note) {
    if (note) strncpy(d->note, note, sizeof(d->note) - 1);
}

void DiagnosticSetHelp(Diagnostic *d, const char *help) {
    if (help) strncpy(d->help, help, sizeof(d->help) - 1);
}

static char *GetLineFromFile(const char *path, int target_line) {
    if (!path || path[0] == '\0' || target_line <= 0) return NULL;
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char buf[1024];
    int cur = 1;
    char *result = NULL;
    while (fgets(buf, sizeof(buf), f)) {
        if (cur == target_line) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
                buf[--len] = '\0';
            }
            result = (char*)malloc(len * 4 + 1);
            if (result) {
                // Expand tabs to 4 spaces for aligned column carets
                size_t out_idx = 0;
                for (size_t i = 0; i < len; i++) {
                    if (buf[i] == '\t') {
                        result[out_idx++] = ' ';
                        result[out_idx++] = ' ';
                        result[out_idx++] = ' ';
                        result[out_idx++] = ' ';
                    } else {
                        result[out_idx++] = buf[i];
                    }
                }
                result[out_idx] = '\0';
            }
            break;
        }
        cur++;
    }
    fclose(f);
    return result;
}

void DiagnosticEmit(const Diagnostic *d) {
    if (!d) return;
    int color = SupportsAnsiColor();

    const char *c_reset   = color ? "\033[0m" : "";
    const char *c_bold    = color ? "\033[1m" : "";
    const char *c_red     = color ? "\033[1;31m" : "";
    const char *c_yellow  = color ? "\033[1;33m" : "";
    const char *c_cyan    = color ? "\033[1;36m" : "";
    const char *c_blue    = color ? "\033[1;34m" : "";
    const char *c_green   = color ? "\033[1;32m" : "";
    const char *c_dim     = color ? "\033[90m" : "";

    // 1. Header: level[code]: message
    const char *level_color = c_red;
    const char *level_name = "error";
    if (d->level == DIAG_WARNING) {
        level_color = c_yellow;
        level_name = "warning";
    } else if (d->level == DIAG_NOTE) {
        level_color = c_cyan;
        level_name = "note";
    } else if (d->level == DIAG_HELP) {
        level_color = c_green;
        level_name = "help";
    }

    if (d->code[0] != '\0') {
        fprintf(stderr, "%s%s[%s]%s: %s%s%s\n", level_color, level_name, d->code, c_reset, c_bold, d->message, c_reset);
    } else {
        fprintf(stderr, "%s%s%s: %s%s%s\n", level_color, level_name, c_reset, c_bold, d->message, c_reset);
    }

    // 2. Primary span origin link: --> file:line:col
    int primary_idx = 0;
    for (int i = 0; i < d->span_count; i++) {
        if (d->spans[i].is_primary) {
            primary_idx = i;
            break;
        }
    }

    if (d->span_count > 0) {
        const DiagnosticSpan *prim = &d->spans[primary_idx];
        if (prim->file[0] != '\0') {
            fprintf(stderr, "  %s-->%s %s:%d:%d\n", c_blue, c_reset, prim->file, prim->line, prim->col);
        } else {
            fprintf(stderr, "  %s-->%s line %d:%d\n", c_blue, c_reset, prim->line, prim->col);
        }
        fprintf(stderr, "   %s|%s\n", c_blue, c_reset);
    }

    // 3. Spans with source lines and underlines
    for (int i = 0; i < d->span_count; i++) {
        const DiagnosticSpan *span = &d->spans[i];
        char *srcline = GetLineFromFile(span->file, span->line);
        if (srcline) {
            fprintf(stderr, "%s%4d |%s %s\n", c_blue, span->line, c_reset, srcline);
            fprintf(stderr, "   %s|%s ", c_blue, c_reset);

            int col = span->col > 0 ? span->col - 1 : 0;
            for (int k = 0; k < col; k++) {
                fputc(' ', stderr);
            }

            const char *span_color = span->is_primary ? c_red : c_blue;
            char marker = span->is_primary ? '^' : '-';
            int len = span->length > 0 ? span->length : 1;

            fprintf(stderr, "%s", span_color);
            for (int k = 0; k < len; k++) {
                fputc(marker, stderr);
            }
            if (span->label[0] != '\0') {
                fprintf(stderr, " %s", span->label);
            }
            fprintf(stderr, "%s\n", c_reset);
            free(srcline);
        }
    }

    if (d->span_count > 0) {
        fprintf(stderr, "   %s|%s\n", c_blue, c_reset);
    }

    // 4. Notes and Helps
    if (d->note[0] != '\0') {
        fprintf(stderr, "   %s=%s %snote%s: %s\n", c_blue, c_reset, c_bold, c_reset, d->note);
    }
    if (d->help[0] != '\0') {
        fprintf(stderr, "   %s=%s %s%shelp%s: %s\n", c_blue, c_reset, c_green, c_bold, c_reset, d->help);
    }
}

void DiagnosticEmitSimple(const char *file, int line, const char *code, const char *message) {
    Diagnostic d;
    DiagnosticInit(&d, DIAG_ERROR, code, message);
    if (line > 0) {
        DiagnosticAddSpan(&d, file, line, 1, 4, "", 1);
    }
    DiagnosticEmit(&d);
}
