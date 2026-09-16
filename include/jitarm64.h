// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef JITARM64_H
#define JITARM64_H

#include "value.h"
#include "vm.h"

typedef struct {
    uint8_t *code;
    int size;
    int cap;
} JITArm64Buffer;

void JITArm64BufferInit(JITArm64Buffer *buf);
void JITArm64BufferFree(JITArm64Buffer *buf);
void JITArm64Emit32(JITArm64Buffer *buf, uint32_t inst);

// Compile trace into native ARM64 / AArch64 machine code
void *CompileArm64Trace(VM *vm, FnProto *fn, int startoffset, TraceStep *trace, int stepcount, int startdepth);

// Compile method into native ARM64 / AArch64 machine code
void *CompileArm64Method(VM *vm, FnProto *fn);

#endif // JITARM64_H
