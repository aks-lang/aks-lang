// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef JITX64H
#define JITX64H

#include "value.h"
#include "vm.h"

typedef struct {
    uint8_t *code;
    int size;
    int cap;
} JITBuffer;

void JITBufferInit(JITBuffer *buf);
void JITBufferFree(JITBuffer *buf);
void JITEmitByte(JITBuffer *buf, uint8_t b);
void JITEmitBytes(JITBuffer *buf, const uint8_t *bytes, int count);
void JITEmitWord(JITBuffer *buf, uint16_t w);
void JITEmitDword(JITBuffer *buf, uint32_t dw);
void JITEmitQword(JITBuffer *buf, uint64_t qw);

// Direct compile trace to native memory block
void *CompileX64Trace(VM *vm, FnProto *fn, int startoffset, TraceStep *trace, int stepcount, int startdepth);

// Direct compile function method to native memory block
void *CompileX64Method(VM *vm, FnProto *fn);

// Clean up function stack frame slots
void JITCleanupFrame(AKSValue *stack, AKSValue *locals, int64_t vtop);

// Get already JIT compiled function pointer if it exists
void *GetJITCompiledFn(VM *vm, FnProto *fn, int targetoffset);

// Load and relocate trace from cache
void *LoadX64TraceFromCache(const char *cachefile, int *outsize);

#endif
