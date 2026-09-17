// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef AKS_DIAGNOSTIC_H
#define AKS_DIAGNOSTIC_H

#include <stdio.h>
#include <stdint.h>

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE,
    DIAG_HELP
} DiagnosticLevel;

typedef struct {
    char file[260];
    int line;
    int col;
    int length;
    char label[128];
    int is_primary;
} DiagnosticSpan;

typedef struct {
    DiagnosticLevel level;
    char code[16];
    char message[256];
    DiagnosticSpan spans[8];
    int span_count;
    char note[256];
    char help[256];
} Diagnostic;

typedef void (*DiagnosticHandler)(const Diagnostic *d, void *userdata);
void DiagnosticSetHandler(DiagnosticHandler handler, void *userdata);

void DiagnosticInit(Diagnostic *d, DiagnosticLevel level, const char *code, const char *message);
void DiagnosticAddSpan(Diagnostic *d, const char *file, int line, int col, int length, const char *label, int is_primary);
void DiagnosticSetNote(Diagnostic *d, const char *note);
void DiagnosticSetHelp(Diagnostic *d, const char *help);
void DiagnosticEmit(const Diagnostic *d);
void DiagnosticEmitSimple(const char *file, int line, const char *code, const char *message);

#endif // AKS_DIAGNOSTIC_H
